#include "api.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "aol.h"
#include "dataset.h"
#include "hotness.h"
#include "soar.h"

namespace fs = std::filesystem;

namespace memopt {

static const char* kVersion = "1.0.0";

HttpResponse jsonResponse(int status, const JsonValue& v) {
    HttpResponse r;
    r.status = status;
    r.body = v.dump();
    return r;
}

HttpResponse jsonError(int status, const std::string& message) {
    JsonValue v = JsonValue::object();
    v.set("ok", false);
    v.set("error", message);
    return jsonResponse(status, v);
}

// ------------------------------------------------------- input validation --
namespace {
struct BadInput {
    std::string msg;
};

int intField(const JsonValue& body, const char* key, int def, int lo, int hi) {
    const JsonValue* v = body.get(key);
    if (!v || v->isNull()) return def;
    if (!v->isNumber() || v->asNumber() != static_cast<double>(static_cast<long long>(v->asNumber())))
        throw BadInput{std::string("'") + key + "' must be a whole number"};
    double d = v->asNumber();
    if (d < lo || d > hi)
        throw BadInput{std::string("'") + key + "' must be between " + std::to_string(lo) + " and " + std::to_string(hi)};
    return static_cast<int>(d);
}

std::string strField(const JsonValue& body, const char* key, const std::string& def) {
    const JsonValue* v = body.get(key);
    if (!v || v->isNull()) return def;
    if (!v->isString()) throw BadInput{std::string("'") + key + "' must be a string"};
    if (v->asString().size() > 40) throw BadInput{std::string("'") + key + "' is too long"};
    return v->asString();
}

bool boolField(const JsonValue& body, const char* key, bool def) {
    const JsonValue* v = body.get(key);
    if (!v || v->isNull()) return def;
    if (!v->isBool()) throw BadInput{std::string("'") + key + "' must be true or false"};
    return v->asBool();
}

WorkloadType workloadField(const JsonValue& body) {
    std::string w = strField(body, "workload", "mixed");
    auto t = parseWorkload(w);
    if (!t) throw BadInput{"unknown workload '" + w + "' (use sequential, random, popular or mixed)"};
    return *t;
}
}  // namespace

// ------------------------------------------------------------ JSON views --
JsonValue Api::metricJson(const Metric& m) {
    JsonValue v = JsonValue::object();
    v.set("name", m.name);
    v.set("value", m.value);
    v.set("unit", m.unit);
    v.set("source", m.source);
    v.set("kind", metricKindName(m.kind));
    v.set("status", m.status);
    return v;
}

JsonValue Api::workloadJson(const WorkloadResult& r) {
    JsonValue v = JsonValue::object();
    v.set("workload", workloadName(r.type));
    v.set("repetitions", r.repetitions);
    v.set("warmup_runs", r.warmup_runs);
    v.set("epoch", r.epoch);
    v.set("started_at", r.started_at);
    v.set("total_ms", r.total_ms);
    v.set("avg_query_ms", r.avgQueryMs());
    v.set("success", r.success);
    v.set("failure", r.failure);
    v.set("run_id", r.run_id);
    v.set("measurement", "measured client-side round-trip time (steady_clock); includes parsing, planning, "
                         "execution and result transfer - NOT memory latency");
    JsonValue qs = JsonValue::array();
    for (const auto& q : r.queries) {
        JsonValue o = JsonValue::object();
        o.set("name", q.name);
        o.set("executions", q.executions);
        o.set("success", q.success);
        o.set("failure", q.failure);
        o.set("total_ms", q.total_ms);
        o.set("avg_ms", q.avgMs());
        o.set("min_ms", q.min_ms);
        o.set("max_ms", q.max_ms);
        o.set("rows", q.rows);
        qs.push(o);
    }
    v.set("queries", qs);
    JsonValue errs = JsonValue::array();
    for (const auto& e : r.errors) errs.push(e);
    v.set("errors", errs);
    return v;
}

JsonValue Api::decisionJson(const PlacementDecision& d) {
    JsonValue v = JsonValue::object();
    v.set("policy", d.policy);
    v.set("unit_id", d.unit_id);
    v.set("rank", d.rank);
    v.set("access_count", d.access_count);
    v.set("latency_ns", d.latency_ns);
    v.set("mlp", d.mlp);
    v.set("aol", d.aol);
    v.set("basis", metricKindName(d.basis));
    v.set("score", d.score);
    v.set("current_tier", tierName(d.current));
    v.set("proposed_tier", tierName(d.proposed));
    v.set("reason", d.reason);
    return v;
}

JsonValue Api::migrationJson(const MigrationRecord& m) {
    JsonValue v = JsonValue::object();
    v.set("timestamp", m.timestamp);
    v.set("policy", m.policy);
    v.set("epoch", m.epoch);
    v.set("unit_id", m.unit_id);
    v.set("old_tier", tierName(m.old_tier));
    v.set("new_tier", tierName(m.new_tier));
    v.set("trigger_metric", m.trigger_metric);
    v.set("trigger_value", m.trigger_value);
    v.set("reason", m.reason);
    v.set("dry_run", m.dry_run);
    v.set("applied", m.applied);
    return v;
}

JsonValue Api::profileJson(const ProfileReport& p) {
    JsonValue v = JsonValue::object();
    v.set("profiler", p.profiler);
    v.set("scope", p.scope);
    v.set("hardware_counters", p.hardware_counters);
    v.set("status", p.status);
    JsonValue c = JsonValue::array();
    for (const auto& m : p.counters) c.push(metricJson(m));
    v.set("counters", c);
    return v;
}

static JsonValue policyRunJson(const PolicyRun& p) {
    JsonValue v = JsonValue::object();
    v.set("policy", p.policy);
    v.set("total_workload_ms", p.total_workload_ms);
    v.set("avg_query_ms", p.avg_query_ms);
    v.set("success", p.success);
    v.set("failure", p.failure);
    v.set("fast_units", p.fast_units);
    v.set("slow_units", p.slow_units);
    v.set("migration_count", p.migration_count);
    v.set("simulated_stall_cost", p.simulated_stall_cost);
    v.set("metric_basis", p.metric_basis);
    v.set("error", p.error);
    JsonValue em = JsonValue::array();
    for (double d : p.epoch_ms) em.push(d);
    v.set("epoch_ms", em);
    JsonValue ec = JsonValue::array();
    for (const auto& c : p.epoch_cost) ec.push(JsonValue(c));
    v.set("epoch_cost", ec);
    JsonValue fast = JsonValue::array();
    for (const auto& kv : p.final_placement)
        if (kv.second == Tier::FAST) fast.push(kv.first);
    v.set("final_fast_units", fast);
    JsonValue mig = JsonValue::array();
    for (const auto& m : p.migrations) mig.push(Api::migrationJson(m));
    v.set("migrations", mig);
    v.set("profile", Api::profileJson(p.profile));
    return v;
}

JsonValue Api::benchmarkJson(const BenchmarkResult& b) {
    JsonValue v = JsonValue::object();
    v.set("ok", b.ok);
    v.set("error", b.error);
    v.set("benchmark_id", b.benchmark_id);
    v.set("created_at", b.created_at);
    v.set("epochs", b.epochs);
    v.set("repetitions", b.repetitions);
    v.set("warmup_runs", b.warmup_runs);
    v.set("fast_capacity", b.fast_capacity);
    v.set("slow_tier_penalty", b.slow_tier_penalty);
    v.set("hotness", policyRunJson(b.hotness));
    v.set("memopt", policyRunJson(b.memopt));
    v.set("time_diff_pct", b.time_diff_pct);
    v.set("cost_diff_pct", b.cost_diff_pct);
    v.set("csv_path", b.csv_path);
    JsonValue n = JsonValue::array();
    for (const auto& x : b.notes) n.push(x);
    v.set("notes", n);
    return v;
}

// ------------------------------------------------------------- routing ----
HttpResponse Api::handle(const HttpRequest& req) {
    const std::string& p = req.path;
    if (p.rfind("/api/", 0) != 0) {
        if (req.method != "GET") return jsonError(405, "method not allowed");
        return serveStatic(req);
    }

    struct Route {
        const char* method;
        const char* path;
        int id;
    };
    static const Route routes[] = {
        {"GET", "/api/health", 1},          {"GET", "/api/database/status", 2},
        {"POST", "/api/database/test", 3},  {"GET", "/api/database/tables", 4},
        {"POST", "/api/workload/run", 5},   {"GET", "/api/workload/latest", 6},
        {"POST", "/api/profiling/run", 7},  {"GET", "/api/profiling/latest", 8},
        {"GET", "/api/placement", 9},       {"POST", "/api/placement/simulate", 10},
        {"GET", "/api/migrations", 11},     {"POST", "/api/benchmark/run", 12},
        {"GET", "/api/benchmark/latest", 13}, {"GET", "/api/results/export", 14},
        {"POST", "/api/tiers/reset", 15},   {"GET", "/api/config", 16},
    };
    const Route* match = nullptr;
    bool path_known = false;
    for (const auto& r : routes) {
        if (p != r.path) continue;
        path_known = true;
        if (req.method == r.method) match = &r;
    }
    if (!path_known) return jsonError(404, "unknown endpoint " + p);
    if (!match) return jsonError(405, "method " + req.method + " not allowed for " + p);

    JsonValue body = JsonValue::object();
    if (std::string(match->method) == "POST" && !trim(req.body).empty()) {
        try {
            body = parseJson(req.body);
        } catch (const JsonParseError& e) {
            return jsonError(400, e.what());
        }
        if (!body.isObject()) return jsonError(400, "request body must be a JSON object");
    }

    try {
        switch (match->id) {
            case 1: return health();
            case 2: return databaseStatus();
            case 3: return databaseTest();
            case 4: return databaseTables();
            case 5: return workloadRun(body);
            case 6: return workloadLatest();
            case 7: return profilingRun(body);
            case 8: return profilingLatest();
            case 9: return placementGet();
            case 10: return placementSimulate(body);
            case 11: return migrations();
            case 12: return benchmarkRun(body);
            case 13: return benchmarkLatest();
            case 14: return resultsExport(req);
            case 15: return tiersReset();
            case 16: return config();
        }
    } catch (const BadInput& b) {
        return jsonError(400, b.msg);
    }
    return jsonError(501, "not implemented");
}

// ----------------------------------------------------------- endpoints ----
HttpResponse Api::health() {
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("service", "memopt");
    v.set("version", kVersion);
    v.set("time", nowIso8601());
    v.set("query_library", s_.lib_error.empty() ? "loaded" : s_.lib_error);
    v.set("profiler", s_.profiler->name());
    v.set("profiler_note", s_.profiler_note);
    return jsonResponse(200, v);
}

HttpResponse Api::databaseStatus() {
    std::string err;
    bool ok = s_.connectDb(err);
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("connected", ok);
    v.set("host", s_.cfg.db_host);
    v.set("port", s_.cfg.db_port);
    v.set("database", s_.cfg.db_name);
    v.set("user", s_.cfg.db_user);
    v.set("password_set", s_.cfg.hasPassword());  // never the password itself
    v.set("read_only", !s_.cfg.db_allow_writes);
    v.set("error", ok ? std::string() : err);
    if (ok) {
        v.set("server_version", s_.db.serverVersion());
        std::string terr;
        auto tables = inspectTables(s_.db, terr);
        long long total = 0;
        int missing = 0;
        for (const auto& t : tables) {
            if (!t.exists) ++missing;
            else if (t.rows > 0 && expectedRows(t.name) > 0) total += t.rows;
        }
        v.set("data_rows", total);
        v.set("missing_tables", missing);
        if (!terr.empty()) v.set("error", terr);
    }
    JsonValue lw = JsonValue::object();
    if (s_.latest_workload) {
        lw.set("workload", workloadName(s_.latest_workload->type));
        lw.set("started_at", s_.latest_workload->started_at);
        lw.set("success", s_.latest_workload->success);
        lw.set("failure", s_.latest_workload->failure);
    }
    v.set("latest_workload", s_.latest_workload ? lw : JsonValue());
    JsonValue lb = JsonValue::object();
    if (s_.latest_benchmark) {
        lb.set("benchmark_id", s_.latest_benchmark->benchmark_id);
        lb.set("ok", s_.latest_benchmark->ok);
        lb.set("created_at", s_.latest_benchmark->created_at);
    }
    v.set("latest_benchmark", s_.latest_benchmark ? lb : JsonValue());
    JsonValue w = JsonValue::array();
    for (const auto& x : s_.warnings) w.push(x);
    v.set("warnings", w);
    return jsonResponse(200, v);
}

HttpResponse Api::databaseTest() {
    std::string err;
    if (!s_.connectDb(err, true)) return jsonError(503, err);
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("message", "Connected to " + s_.cfg.describeConnection());
    v.set("server_version", s_.db.serverVersion());
    v.set("backend_pid", s_.db.backendPid());
    return jsonResponse(200, v);
}

HttpResponse Api::databaseTables() {
    std::string err;
    if (!s_.connectDb(err)) return jsonError(503, err);
    auto tables = inspectTables(s_.db, err);
    if (!err.empty()) return jsonError(503, err);
    JsonValue arr = JsonValue::array();
    bool all = true;
    for (const auto& t : tables) {
        JsonValue o = JsonValue::object();
        o.set("name", t.name);
        o.set("exists", t.exists);
        o.set("rows", t.rows);
        long long exp = expectedRows(t.name);
        o.set("expected_rows", exp > 0 ? JsonValue(exp) : JsonValue());
        o.set("kind", exp > 0 ? "demo data" : "MemOpt results");
        if (!t.exists) all = false;
        arr.push(o);
    }
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("all_present", all);
    v.set("tables", arr);
    if (!all) v.set("hint", "Run scripts/setup_windows.ps1 (Windows) or scripts/setup_linux.sh to create the schema.");
    return jsonResponse(200, v);
}

HttpResponse Api::workloadRun(const JsonValue& body) {
    WorkloadType t = workloadField(body);
    int reps = intField(body, "repetitions", 3, 1, 20);
    int warm = intField(body, "warmup", 1, 0, 5);
    if (auto e = s_.runWorkload(t, reps, warm)) return jsonError(e->http_status, e->message);
    JsonValue v = workloadJson(*s_.latest_workload);
    v.set("ok", true);
    return jsonResponse(200, v);
}

HttpResponse Api::workloadLatest() {
    if (!s_.latest_workload) return jsonError(404, "no workload has been run yet in this session");
    JsonValue v = workloadJson(*s_.latest_workload);
    v.set("ok", true);
    return jsonResponse(200, v);
}

static JsonValue profileRunJson(const ProfileRunResult& r) {
    JsonValue v = Api::profileJson(r.report);
    v.set("ok", true);
    v.set("workload", workloadName(r.workload));
    v.set("repetitions", r.repetitions);
    v.set("started_at", r.started_at);
    JsonValue qd = JsonValue::object();
    qd.set("total_ms", r.total_query_ms);
    qd.set("avg_ms", r.avg_query_ms);
    qd.set("queries", r.queries);
    qd.set("kind", "measured");
    qd.set("source", "steady_clock around PQexecParams (client round-trip)");
    v.set("query_duration", qd);
    return v;
}

HttpResponse Api::profilingRun(const JsonValue& body) {
    WorkloadType t = workloadField(body);
    int reps = intField(body, "repetitions", 2, 1, 20);
    if (auto e = s_.runProfile(t, reps)) return jsonError(e->http_status, e->message);
    return jsonResponse(200, profileRunJson(*s_.latest_profile));
}

HttpResponse Api::profilingLatest() {
    if (!s_.latest_profile) return jsonError(404, "no profiling run yet");
    return jsonResponse(200, profileRunJson(*s_.latest_profile));
}

HttpResponse Api::placementGet() {
    Placement folders = s_.tiers.current();
    std::map<std::string, const PlacementDecision*> hot, mem;
    for (const auto& d : s_.hotness_decisions) hot[d.unit_id] = &d;
    for (const auto& d : s_.memopt_decisions) mem[d.unit_id] = &d;

    // Ranks computed live from current metrics, so the UI can show the
    // difference between the two orderings even before /simulate.
    std::map<std::string, int> hot_rank, aol_rank, prio_rank;
    int i = 0;
    for (size_t k : HotnessBaseline::rankByAccess(s_.units)) hot_rank[s_.units[k].id] = ++i;
    i = 0;
    for (size_t k : AOLCalculator::rankByAol(s_.units)) aol_rank[s_.units[k].id] = ++i;
    i = 0;
    for (size_t k : AOLRankingPolicy::rank(s_.units)) prio_rank[s_.units[k].id] = ++i;

    JsonValue arr = JsonValue::array();
    for (const auto& u : s_.units) {
        JsonValue o = JsonValue::object();
        o.set("unit_id", u.id);
        o.set("table", u.table);
        o.set("key_range", std::to_string(u.first_key) + "-" + std::to_string(u.last_key));
        o.set("access_count", u.accessCount());
        o.set("seq_touches", u.seq_touches);
        o.set("random_touches", u.random_touches);
        o.set("repeated_touches", u.repeated_touches);
        o.set("latency", metricJson(u.latency));
        o.set("mlp", metricJson(u.mlp));
        o.set("aol", u.aol);
        o.set("aol_kind", metricKindName(u.aol_kind));
        o.set("aol_status", u.aol_status);
        auto pr = AOLRankingPolicy::priority(u);
        o.set("stall_priority", pr);
        o.set("hotness_rank", u.accessCount() ? JsonValue(hot_rank[u.id]) : JsonValue());
        o.set("aol_rank", u.aol ? JsonValue(aol_rank[u.id]) : JsonValue());
        o.set("memopt_rank", pr ? JsonValue(prio_rank[u.id]) : JsonValue());
        auto f = folders.find(u.id);
        o.set("current_tier", tierName(f == folders.end() ? Tier::NONE : f->second));
        o.set("hotness_proposed", hot.count(u.id) ? tierName(hot[u.id]->proposed) : std::string("-"));
        o.set("hotness_reason", hot.count(u.id) ? hot[u.id]->reason : std::string());
        o.set("memopt_proposed", mem.count(u.id) ? tierName(mem[u.id]->proposed) : std::string("-"));
        o.set("memopt_reason", mem.count(u.id) ? mem[u.id]->reason : std::string());
        arr.push(o);
    }
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("has_workload_data", s_.latest_workload.has_value());
    v.set("fast_capacity", s_.cfg.fast_capacity_units);
    v.set("fast_in_folders", countTier(folders, Tier::FAST));
    v.set("slow_in_folders", countTier(folders, Tier::SLOW));
    v.set("metric_source", s_.cfg.metric_source);
    v.set("simulation_note", "FAST/SLOW are logical tiers simulated with two folders on one disk. "
                             "Placement does not move PostgreSQL pages or physical memory.");
    v.set("units", arr);
    return jsonResponse(200, v);
}

HttpResponse Api::placementSimulate(const JsonValue& body) {
    std::string policy = strField(body, "policy", "both");
    std::string mode = strField(body, "mode", "auto");
    bool dry = boolField(body, "dry_run", s_.cfg.dry_run);
    std::string summary;
    if (auto e = s_.simulatePlacement(policy, mode, dry, summary)) return jsonError(e->http_status, e->message);
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("summary", summary);
    v.set("dry_run", dry);
    JsonValue h = JsonValue::array(), m = JsonValue::array();
    for (const auto& d : s_.hotness_decisions) h.push(decisionJson(d));
    for (const auto& d : s_.memopt_decisions) m.push(decisionJson(d));
    v.set("hotness_decisions", h);
    v.set("memopt_decisions", m);
    return jsonResponse(200, v);
}

HttpResponse Api::migrations() {
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    JsonValue live = JsonValue::array();
    for (const auto& m : s_.live_migrations) live.push(migrationJson(m));
    v.set("live", live);
    JsonValue bh = JsonValue::array(), bm = JsonValue::array();
    if (s_.latest_benchmark) {
        for (const auto& m : s_.latest_benchmark->hotness.migrations) bh.push(migrationJson(m));
        for (const auto& m : s_.latest_benchmark->memopt.migrations) bm.push(migrationJson(m));
    }
    v.set("benchmark_hotness", bh);
    v.set("benchmark_memopt", bm);
    return jsonResponse(200, v);
}

HttpResponse Api::benchmarkRun(const JsonValue& body) {
    int epochs = intField(body, "epochs", s_.cfg.benchmark_epochs, 2, 10);
    int reps = intField(body, "repetitions", s_.cfg.benchmark_repetitions, 1, 10);
    int warm = intField(body, "warmup", s_.cfg.warmup_runs, 0, 5);
    if (auto e = s_.runBenchmark(epochs, reps, warm)) return jsonError(e->http_status, e->message);
    return jsonResponse(200, benchmarkJson(*s_.latest_benchmark));
}

HttpResponse Api::benchmarkLatest() {
    if (!s_.latest_benchmark) return jsonError(404, "no benchmark has been run yet in this session");
    return jsonResponse(200, benchmarkJson(*s_.latest_benchmark));
}

HttpResponse Api::resultsExport(const HttpRequest& req) {
    auto it = req.params.find("type");
    std::string type = it == req.params.end() ? "benchmark" : it->second;
    static const std::map<std::string, std::string> prefixes = {
        {"benchmark", "benchmark_"}, {"workload", "workload_"}, {"placement", "placement_"},
        {"migrations", "migrations_"}};
    auto p = prefixes.find(type);
    if (p == prefixes.end()) return jsonError(400, "type must be benchmark, workload, placement or migrations");
    std::string path = s_.logger.latestFile(p->second);
    if (path.empty()) return jsonError(404, "no " + type + " CSV exported yet - run it first");
    std::ifstream f(path, std::ios::binary);
    if (!f) return jsonError(500, "cannot read " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    HttpResponse r;
    r.content_type = "text/csv; charset=utf-8";
    r.body = ss.str();
    r.headers["Content-Disposition"] = "attachment; filename=\"" + fs::path(path).filename().string() + "\"";
    return r;
}

HttpResponse Api::tiersReset() {
    if (auto e = s_.resetTiers()) return jsonError(e->http_status, e->message);
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("message", "Tier folders cleared; next simulate will do a fresh SOAR-inspired allocation.");
    return jsonResponse(200, v);
}

HttpResponse Api::config() {
    const AppConfig& c = s_.cfg;
    JsonValue v = JsonValue::object();
    v.set("ok", true);
    v.set("connection", c.describeConnection());
    v.set("metric_source", c.metric_source);
    v.set("profiler", s_.profiler->name());
    v.set("profiler_note", s_.profiler_note);
    v.set("fast_capacity_units", c.fast_capacity_units);
    v.set("aol_high", c.aol_high);
    v.set("aol_low", c.aol_low);
    v.set("cooldown_epochs", c.cooldown_epochs);
    v.set("confirm_epochs", c.confirm_epochs);
    v.set("swap_margin", c.swap_margin);
    v.set("dry_run", c.dry_run);
    v.set("base_latency_ns", c.base_latency_ns);
    v.set("mlp_sequential", c.mlp_sequential);
    v.set("mlp_random", c.mlp_random);
    v.set("mlp_repeated", c.mlp_repeated);
    v.set("slow_tier_penalty", c.slow_tier_penalty);
    v.set("benchmark_epochs", c.benchmark_epochs);
    v.set("benchmark_repetitions", c.benchmark_repetitions);
    v.set("warmup_runs", c.warmup_runs);
    v.set("model_note", "Latency, MLP constants and thresholds are illustrative prototype assumptions, "
                        "not values from the research paper.");
    return jsonResponse(200, v);
}

HttpResponse Api::serveStatic(const HttpRequest& req) {
    std::string rel = req.path == "/" ? "index.html" : req.path.substr(1);
    // Only simple file names inside frontend/ - no "..", no sub-folders.
    if (rel.empty() || rel.find("..") != std::string::npos || rel.find('/') != std::string::npos ||
        rel.find('\\') != std::string::npos)
        return jsonError(404, "not found");
    for (unsigned char c : rel)
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_')) return jsonError(404, "not found");
    fs::path file = fs::path(s_.cfg.path("frontend")) / rel;
    std::ifstream f(file, std::ios::binary);
    if (!f) return jsonError(404, "not found");
    std::stringstream ss;
    ss << f.rdbuf();
    HttpResponse r;
    std::string ext = file.extension().string();
    if (ext == ".html") r.content_type = "text/html; charset=utf-8";
    else if (ext == ".css") r.content_type = "text/css; charset=utf-8";
    else if (ext == ".js") r.content_type = "application/javascript; charset=utf-8";
    else if (ext == ".svg") r.content_type = "image/svg+xml";
    else if (ext == ".png") r.content_type = "image/png";
    else r.content_type = "application/octet-stream";
    r.body = ss.str();
    return r;
}

}  // namespace memopt
