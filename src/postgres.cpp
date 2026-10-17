#include "dataset.h"

#include <algorithm>
#include <cstdio>

namespace memopt {

const std::vector<TableLayout>& tableLayouts() {
    static const std::vector<TableLayout> layouts = {
        {"users", "user_id", 5000, 500},
        {"products", "product_id", 2000, 500},
        {"orders", "order_id", 20000, 2000},
        {"transactions", "txn_id", 20000, 2000},
    };
    return layouts;
}

static std::string makeId(const std::string& table, long long seg) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s_s%02lld", table.c_str(), seg);
    return buf;
}

std::vector<DataUnit> buildUnitCatalog() {
    std::vector<DataUnit> units;
    for (const auto& l : tableLayouts()) {
        long long segs = (l.rows + l.segment_size - 1) / l.segment_size;
        for (long long s = 0; s < segs; ++s) {
            DataUnit u;
            u.id = makeId(l.table, s);
            u.table = l.table;
            u.first_key = s * l.segment_size + 1;
            u.last_key = std::min(l.rows, (s + 1) * l.segment_size);
            units.push_back(u);
        }
    }
    return units;
}

std::string unitIdFor(const std::string& table, long long key) {
    for (const auto& l : tableLayouts()) {
        if (l.table != table) continue;
        if (key < 1 || key > l.rows) return "";
        return makeId(table, (key - 1) / l.segment_size);
    }
    return "";
}

long long expectedRows(const std::string& table) {
    for (const auto& l : tableLayouts())
        if (l.table == table) return l.rows;
    return -1;
}

}  // namespace memopt

#include "postgres.h"

#include <libpq-fe.h>

#include <string>

#include "common.h"

namespace memopt {

// ------------------------------------------------------------ PgResult ---
PgResult::PgResult(PGresult* r, std::string error) : res_(r), error_(std::move(error)) {
    if (res_) {
        ExecStatusType st = PQresultStatus(res_);
        ok_ = (st == PGRES_TUPLES_OK || st == PGRES_COMMAND_OK);
        if (!ok_ && error_.empty()) error_ = trim(PQresultErrorMessage(res_));
    } else if (error_.empty()) {
        error_ = "no result from server";
    }
}

PgResult::~PgResult() {
    if (res_) PQclear(res_);
}

PgResult::PgResult(PgResult&& o) noexcept : res_(o.res_), ok_(o.ok_), error_(std::move(o.error_)) {
    o.res_ = nullptr;
}

PgResult& PgResult::operator=(PgResult&& o) noexcept {
    if (this != &o) {
        if (res_) PQclear(res_);
        res_ = o.res_;
        ok_ = o.ok_;
        error_ = std::move(o.error_);
        o.res_ = nullptr;
    }
    return *this;
}

int PgResult::rows() const { return res_ ? PQntuples(res_) : 0; }
int PgResult::cols() const { return res_ ? PQnfields(res_) : 0; }
std::string PgResult::value(int row, int col) const {
    if (!res_ || row < 0 || col < 0 || row >= rows() || col >= cols()) return "";
    return PQgetvalue(res_, row, col);
}
bool PgResult::isNull(int row, int col) const {
    if (!res_ || row < 0 || col < 0 || row >= rows() || col >= cols()) return true;
    return PQgetisnull(res_, row, col) != 0;
}
int PgResult::columnIndex(const std::string& name) const {
    return res_ ? PQfnumber(res_, name.c_str()) : -1;
}
std::string PgResult::columnName(int col) const {
    if (!res_ || col < 0 || col >= cols()) return "";
    return PQfname(res_, col);
}
long long PgResult::affectedRows() const {
    if (!res_) return 0;
    const char* s = PQcmdTuples(res_);
    if (!s || !*s) return 0;
    try { return std::stoll(s); } catch (...) { return 0; }
}

// -------------------------------------------------------- PgConnection ---
PgConnection::~PgConnection() { disconnect(); }

void PgConnection::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool PgConnection::isConnected() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

bool PgConnection::connect(const AppConfig& cfg) {
    disconnect();
    std::string port = std::to_string(cfg.db_port);
    std::string timeout = std::to_string(cfg.db_connect_timeout_s);
    const char* keys[] = {"host", "port", "dbname", "user", "password",
                          "connect_timeout", "application_name", nullptr};
    const char* vals[] = {cfg.db_host.c_str(), port.c_str(), cfg.db_name.c_str(),
                          cfg.db_user.c_str(), cfg.db_password.c_str(),
                          timeout.c_str(), "memopt", nullptr};
    // An empty password lets libpq fall back to PGPASSWORD / pgpass.conf.

    conn_ = PQconnectdbParams(keys, vals, 0);
    if (!conn_) {
        last_error_ = "Out of memory while creating the PostgreSQL connection.";
        return false;
    }
    if (PQstatus(conn_) != CONNECTION_OK) {
        last_error_ = friendlyPgError(PQerrorMessage(conn_), cfg);
        disconnect();
        return false;
    }
    read_only_ = !cfg.db_allow_writes;
    if (read_only_) {
        PgResult r = exec("SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY");
        if (!r.ok()) {
            last_error_ = "Connected, but could not switch to read-only mode: " + r.error();
            disconnect();
            return false;
        }
    }
    last_error_.clear();
    return true;
}

bool PgConnection::ensureConnected(const AppConfig& cfg) {
    if (isConnected()) return true;
    if (conn_) {
        PQreset(conn_);
        if (isConnected()) return true;
    }
    return connect(cfg);
}

PgResult PgConnection::exec(const std::string& sql) {
    if (!isConnected()) return PgResult(nullptr, "not connected to PostgreSQL");
    return PgResult(PQexec(conn_, sql.c_str()), "");
}

PgResult PgConnection::execParams(const std::string& sql, const std::vector<Param>& params) {
    if (!isConnected()) return PgResult(nullptr, "not connected to PostgreSQL");
    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& p : params) values.push_back(p ? p->c_str() : nullptr);
    PGresult* r = PQexecParams(conn_, sql.c_str(), static_cast<int>(params.size()), nullptr,
                               values.empty() ? nullptr : values.data(), nullptr, nullptr, 0);
    return PgResult(r, "");
}

int PgConnection::backendPid() const { return isConnected() ? PQbackendPID(conn_) : 0; }

std::string PgConnection::serverVersion() const {
    if (!isConnected()) return "";
    int v = PQserverVersion(conn_);
    return std::to_string(v / 10000) + "." + std::to_string(v % 10000);
}

std::string PgConnection::databaseName() const {
    if (!conn_) return "";
    const char* d = PQdb(conn_);
    return d ? d : "";
}

// -------------------------------------------------------------- helpers ---
const std::vector<std::string>& requiredTables() {
    static const std::vector<std::string> t = {
        "users", "products", "orders", "transactions", "workload_runs", "query_metrics",
        "placement_decisions", "migration_history", "benchmark_results"};
    return t;
}

const std::vector<std::string>& dataTables() {
    static const std::vector<std::string> t = {"users", "products", "orders", "transactions"};
    return t;
}

std::vector<TableInfo> inspectTables(PgConnection& db, std::string& error) {
    std::vector<TableInfo> out;
    error.clear();
    for (const auto& name : requiredTables()) {
        TableInfo ti;
        ti.name = name;
        PgResult r = db.execParams(
            "SELECT 1 FROM information_schema.tables WHERE table_schema = 'public' AND table_name = $1",
            {name});
        if (!r.ok()) {
            error = r.error();
            return out;
        }
        ti.exists = r.rows() > 0;
        if (ti.exists) {
            // Table names come from our fixed whitelist above, never from
            // user input, so building the identifier here is safe.
            PgResult c = db.exec("SELECT COUNT(*) FROM \"" + name + "\"");
            if (c.ok() && c.rows() == 1) ti.rows = std::stoll(c.value(0, 0));
        }
        out.push_back(ti);
    }
    return out;
}

std::string friendlyPgError(const std::string& raw, const AppConfig& cfg) {
    std::string msg = trim(raw);
    std::string low = toLower(msg);
    std::string hint;
    if (low.find("password authentication failed") != std::string::npos) {
        hint = "Wrong user name or password. Check MEMOPT_DB_USER / MEMOPT_DB_PASSWORD in your .env file.";
    } else if (low.find("no password supplied") != std::string::npos ||
               low.find("fe_sendauth") != std::string::npos) {
        hint = "The server needs a password. Set MEMOPT_DB_PASSWORD in your .env file.";
    } else if (low.find("does not exist") != std::string::npos && low.find("database") != std::string::npos) {
        hint = "Database '" + cfg.db_name + "' does not exist yet. Run scripts/setup_windows.ps1 "
               "(or scripts/setup_linux.sh) to create it.";
    } else if (low.find("connection refused") != std::string::npos ||
               low.find("could not connect") != std::string::npos ||
               low.find("no such file") != std::string::npos) {
        hint = "PostgreSQL is not running or not listening on " + cfg.db_host + ":" +
               std::to_string(cfg.db_port) + ". On Windows open services.msc and start the "
               "'postgresql-x64-16' service; on Linux run: sudo service postgresql start";
    } else if (low.find("timeout") != std::string::npos) {
        hint = "The connection timed out. Check the host name and firewall.";
    } else if (low.find("could not translate host name") != std::string::npos) {
        hint = "Unknown host name '" + cfg.db_host + "'. Try MEMOPT_DB_HOST=localhost.";
    }
    // Never echo the password, even if libpq ever included it.
    if (!cfg.db_password.empty()) {
        size_t p;
        while ((p = msg.find(cfg.db_password)) != std::string::npos) msg.replace(p, cfg.db_password.size(), "***");
    }
    return hint.empty() ? msg : (hint + " (PostgreSQL said: " + msg + ")");
}

}  // namespace memopt

#include "workload.h"

#include <cctype>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "dataset.h"

namespace memopt {

// ------------------------------------------------------------ names -----
std::string workloadName(WorkloadType t) {
    switch (t) {
        case WorkloadType::Sequential: return "sequential";
        case WorkloadType::Random: return "random";
        case WorkloadType::Popular: return "popular";
        default: return "mixed";
    }
}

std::optional<WorkloadType> parseWorkload(const std::string& s) {
    std::string v = toLower(trim(s));
    if (v == "sequential" || v == "seq") return WorkloadType::Sequential;
    if (v == "random" || v == "rand") return WorkloadType::Random;
    if (v == "popular" || v == "repeated") return WorkloadType::Popular;
    if (v == "mixed") return WorkloadType::Mixed;
    return std::nullopt;
}

std::vector<WorkloadType> allWorkloads() {
    return {WorkloadType::Sequential, WorkloadType::Random, WorkloadType::Popular, WorkloadType::Mixed};
}

// --------------------------------------------------------- query library --
std::map<std::string, std::string> parseQueryLibrary(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line, current, body;
    auto flush = [&]() {
        if (current.empty()) return;
        std::string q = trim(body);
        while (!q.empty() && (q.back() == ';' || std::isspace(static_cast<unsigned char>(q.back())))) q.pop_back();
        if (!q.empty()) out[current] = q;
    };
    while (std::getline(in, line)) {
        std::string t = trim(line);
        const std::string tag = "-- name:";
        if (t.rfind(tag, 0) == 0) {
            flush();
            current = trim(t.substr(tag.size()));
            body.clear();
            continue;
        }
        if (current.empty()) continue;           // header comments
        if (t.rfind("--", 0) == 0) continue;     // comment line inside block
        body += line;
        body += '\n';
    }
    flush();
    return out;
}

bool QueryLibrary::load(const std::string& path, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "Cannot open query library " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    queries_ = parseQueryLibrary(ss.str());
    auto missing = missingNames();
    if (!missing.empty()) {
        error = "workload.sql is missing queries:";
        for (auto& m : missing) error += " " + m;
        return false;
    }
    return true;
}

const std::string* QueryLibrary::get(const std::string& name) const {
    auto it = queries_.find(name);
    return it == queries_.end() ? nullptr : &it->second;
}

const std::vector<std::string>& QueryLibrary::requiredNames() {
    static const std::vector<std::string> names = {
        "seq_scan_orders_range", "seq_scan_transactions_range", "seq_aggregate_orders_status",
        "lookup_user_by_id", "lookup_order_by_id", "popular_product_lookup",
        "popular_user_lookup", "join_order_details_range", "category_revenue_range"};
    return names;
}

std::vector<std::string> QueryLibrary::missingNames() const {
    std::vector<std::string> m;
    for (const auto& n : requiredNames())
        if (!get(n)) m.push_back(n);
    return m;
}

// ------------------------------------------------------------ stats -----
void QueryStats::add(double ms, bool ok, long long r) {
    if (executions == 0) {
        min_ms = max_ms = ms;
    } else {
        min_ms = std::min(min_ms, ms);
        max_ms = std::max(max_ms, ms);
    }
    ++executions;
    total_ms += ms;
    if (ok) {
        ++success;
        rows += r;
    } else {
        ++failure;
    }
}

double WorkloadResult::avgQueryMs() const {
    double t = 0;
    int n = 0;
    for (const auto& q : queries) {
        t += q.total_ms;
        n += q.executions;
    }
    return n > 0 ? t / n : 0.0;
}

// ------------------------------------------------------- AccessTracker ---
AccessTracker::AccessTracker(std::vector<DataUnit>& units) : units_(units) {
    for (size_t i = 0; i < units_.size(); ++i) index_[units_[i].id] = i;
}

static void bump(DataUnit& u, AccessPattern p, uint64_t n) {
    switch (p) {
        case AccessPattern::Sequential: u.seq_touches += n; break;
        case AccessPattern::Random: u.random_touches += n; break;
        case AccessPattern::Repeated: u.repeated_touches += n; break;
    }
}

void AccessTracker::touchRange(const std::string& table, long long lo, long long hi, AccessPattern p) {
    if (hi < lo) std::swap(lo, hi);
    for (auto& u : units_) {
        if (u.table != table) continue;
        long long a = std::max(lo, u.first_key);
        long long b = std::min(hi, u.last_key);
        if (a <= b) bump(u, p, static_cast<uint64_t>(b - a + 1));
    }
}

void AccessTracker::touchKey(const std::string& table, long long key, AccessPattern p) {
    auto it = index_.find(unitIdFor(table, key));
    if (it != index_.end()) bump(units_[it->second], p, 1);
}

// ------------------------------------------------------- WorkloadRunner ---
struct WorkloadRunner::Ctx {
    bool record = false;
    std::map<std::string, QueryStats> stats;
    std::vector<std::string> order;
    std::vector<std::string> errors;
    int success = 0;
    int failure = 0;
    std::vector<DataUnit> scratch_units;  // used during warm-up (discarded)
    AccessTracker* tracker = nullptr;
};

WorkloadRunner::WorkloadRunner(PgConnection& db, const QueryLibrary& lib, unsigned seed)
    : db_(db), lib_(lib), seed_(seed) {}

// Portable random helpers: std::mt19937 output is identical on every
// compiler, but std::uniform_int_distribution is not. We map raw output
// ourselves so Windows and Linux produce the same workload parameters.
static long long uniformInt(std::mt19937& rng, long long lo, long long hi) {
    unsigned long long span = static_cast<unsigned long long>(hi - lo + 1);
    return lo + static_cast<long long>(rng() % span);
}
static double unit01(std::mt19937& rng) { return rng() / 4294967296.0; }

// Key inside a wrapping window [start, start+len) over 1..rows.
static long long wrapKey(long long start, long long offset, long long rows) {
    return ((start - 1 + offset) % rows + rows) % rows + 1;
}

void WorkloadRunner::execute(Ctx& ctx, const std::string& name, const std::vector<long long>& params,
                             const std::function<void(const PgResult&, AccessTracker&)>& attribute) {
    const std::string* sql = lib_.get(name);
    std::vector<Param> p;
    for (long long v : params) p.emplace_back(std::to_string(v));

    auto t0 = std::chrono::steady_clock::now();
    PgResult r = sql ? db_.execParams(*sql, p) : PgResult(nullptr, "query '" + name + "' not found");
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    bool ok = r.ok();
    if (ok) {
        ++ctx.success;
        if (ctx.tracker) attribute(r, *ctx.tracker);
    } else {
        ++ctx.failure;
        if (ctx.errors.size() < 5) ctx.errors.push_back(name + ": " + r.error());
    }
    if (!ctx.record) return;
    auto it = ctx.stats.find(name);
    if (it == ctx.stats.end()) {
        ctx.order.push_back(name);
        it = ctx.stats.emplace(name, QueryStats{}).first;
        it->second.name = name;
    }
    it->second.add(ms, ok, ok ? r.rows() : 0);
}

// Attribute rows returned in key columns (user_id, product_id ...) to units.
static void attributeColumn(const PgResult& r, AccessTracker& t, const std::string& col,
                            const std::string& table, AccessPattern p) {
    int c = r.columnIndex(col);
    if (c < 0) return;
    for (int i = 0; i < r.rows(); ++i) {
        if (r.isNull(i, c)) continue;
        try {
            t.touchKey(table, std::stoll(r.value(i, c)), p);
        } catch (...) {
        }
    }
}

// Sequential part: range scans over a stable "report" region.
void WorkloadRunner::seqPart(Ctx& ctx, int /*epoch*/) {
    for (long long lo = 1; lo <= 600; lo += 100) {
        long long hi = lo + 99;
        execute(ctx, "seq_scan_orders_range", {lo, hi}, [lo, hi](const PgResult&, AccessTracker& t) {
            t.touchRange("orders", lo, hi, AccessPattern::Sequential);
        });
        execute(ctx, "seq_scan_transactions_range", {lo, hi}, [lo, hi](const PgResult&, AccessTracker& t) {
            t.touchRange("transactions", lo, hi, AccessPattern::Sequential);
        });
    }
    execute(ctx, "seq_aggregate_orders_status", {2001, 2400}, [](const PgResult&, AccessTracker& t) {
        t.touchRange("orders", 2001, 2400, AccessPattern::Sequential);
    });
}

// Random part: point lookups in a region that moves with the epoch.
void WorkloadRunner::randomPart(Ctx& ctx, int epoch, std::mt19937& rng) {
    long long user_start = 1 + (500LL * epoch) % 5000;
    long long order_start = wrapKey(16001, -2000LL * epoch, 20000);
    for (int i = 0; i < 300; ++i) {
        long long uid = wrapKey(user_start, uniformInt(rng, 0, 1499), 5000);
        execute(ctx, "lookup_user_by_id", {uid}, [uid](const PgResult&, AccessTracker& t) {
            t.touchKey("users", uid, AccessPattern::Random);
        });
        long long oid = wrapKey(order_start, uniformInt(rng, 0, 3999), 20000);
        execute(ctx, "lookup_order_by_id", {oid}, [oid](const PgResult& r, AccessTracker& t) {
            t.touchKey("orders", oid, AccessPattern::Random);
            attributeColumn(r, t, "product_id", "products", AccessPattern::Random);
        });
    }
}

// Popular part: a small set of hot rows accessed again and again (skewed).
void WorkloadRunner::popularPart(Ctx& ctx, std::mt19937& rng) {
    for (int i = 0; i < 200; ++i) {
        double u = unit01(rng);
        long long pid = 1 + 23LL * static_cast<long long>(20 * u * u);  // 20 hot products in 1..458
        execute(ctx, "popular_product_lookup", {pid}, [pid](const PgResult&, AccessTracker& t) {
            t.touchKey("products", pid, AccessPattern::Repeated);
        });
    }
    for (int i = 0; i < 150; ++i) {
        double u = unit01(rng);
        long long uid = 2001 + 31LL * static_cast<long long>(15 * u * u);  // 15 hot users
        execute(ctx, "popular_user_lookup", {uid}, [uid](const PgResult&, AccessTracker& t) {
            t.touchKey("users", uid, AccessPattern::Repeated);
        });
    }
}

// Join + aggregate part used by the mixed workload.
void WorkloadRunner::joinPart(Ctx& ctx, int epoch) {
    long long lo = 4001 + 200LL * (epoch % 5);
    long long hi = lo + 199;
    execute(ctx, "join_order_details_range", {lo, hi}, [lo, hi](const PgResult& r, AccessTracker& t) {
        t.touchRange("orders", lo, hi, AccessPattern::Sequential);
        attributeColumn(r, t, "user_id", "users", AccessPattern::Random);
        attributeColumn(r, t, "product_id", "products", AccessPattern::Random);
        attributeColumn(r, t, "txn_id", "transactions", AccessPattern::Sequential);
    });
    execute(ctx, "category_revenue_range", {8001, 8400}, [](const PgResult&, AccessTracker& t) {
        // Only the declared order range is attributed; the products touched
        // by the join are not returned, so they are not counted.
        t.touchRange("orders", 8001, 8400, AccessPattern::Sequential);
    });
}

void WorkloadRunner::runOnce(WorkloadType type, int epoch, std::mt19937& rng, Ctx& ctx) {
    switch (type) {
        case WorkloadType::Sequential: seqPart(ctx, epoch); break;
        case WorkloadType::Random: randomPart(ctx, epoch, rng); break;
        case WorkloadType::Popular: popularPart(ctx, rng); break;
        case WorkloadType::Mixed:
            seqPart(ctx, epoch);
            randomPart(ctx, epoch, rng);
            popularPart(ctx, rng);
            joinPart(ctx, epoch);
            break;
    }
}

WorkloadResult WorkloadRunner::run(WorkloadType type, int repetitions, int warmup, int epoch,
                                   const ProgressFn& progress) {
    WorkloadResult res;
    res.type = type;
    res.repetitions = std::max(1, repetitions);
    res.warmup_runs = std::max(0, warmup);
    res.epoch = epoch;
    res.started_at = nowIso8601();
    res.units = buildUnitCatalog();

    // Warm-up: fills PostgreSQL's buffer cache; nothing is recorded.
    for (int w = 0; w < res.warmup_runs; ++w) {
        Ctx ctx;
        ctx.scratch_units = buildUnitCatalog();
        AccessTracker scratch(ctx.scratch_units);
        ctx.tracker = &scratch;
        std::mt19937 rng(seed_ + 7919u * static_cast<unsigned>(epoch) + 104729u * static_cast<unsigned>(1000 + w));
        if (progress) progress("warm-up " + std::to_string(w + 1) + "/" + std::to_string(res.warmup_runs));
        runOnce(type, epoch, rng, ctx);
        for (auto& e : ctx.errors)
            if (res.errors.size() < 5) res.errors.push_back("[warm-up] " + e);
    }

    Ctx ctx;
    ctx.record = true;
    AccessTracker tracker(res.units);
    ctx.tracker = &tracker;
    for (int rep = 0; rep < res.repetitions; ++rep) {
        std::mt19937 rng(seed_ + 7919u * static_cast<unsigned>(epoch) + 104729u * static_cast<unsigned>(rep));
        if (progress) progress("repetition " + std::to_string(rep + 1) + "/" + std::to_string(res.repetitions));
        auto t0 = std::chrono::steady_clock::now();
        runOnce(type, epoch, rng, ctx);
        auto t1 = std::chrono::steady_clock::now();
        res.total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    res.success = ctx.success;
    res.failure = ctx.failure;
    for (const auto& name : ctx.order) res.queries.push_back(ctx.stats[name]);
    for (auto& e : ctx.errors)
        if (res.errors.size() < 10) res.errors.push_back(e);
    return res;
}

}  // namespace memopt
