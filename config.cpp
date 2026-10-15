#include "config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common.h"

namespace fs = std::filesystem;

namespace memopt {

std::map<std::string, std::string> parseEnvFile(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        if (!key.empty()) out[key] = val;
    }
    return out;
}

static bool looksLikeRoot(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p / "workloads" / "workload.sql", ec);
}

std::string findProjectRoot(const std::string& hint) {
    std::error_code ec;
    if (!hint.empty() && looksLikeRoot(hint)) return fs::absolute(hint, ec).string();
    if (const char* env = std::getenv("MEMOPT_ROOT")) {
        if (looksLikeRoot(env)) return fs::absolute(env, ec).string();
    }
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 5 && !p.empty(); ++i) {
        if (looksLikeRoot(p)) return p.string();
        if (p == p.parent_path()) break;
        p = p.parent_path();
    }
    return fs::current_path(ec).string();  // best effort
}

static void apply(AppConfig& c, const std::map<std::string, std::string>& kv,
                  bool& writes_explicit) {
    auto str = [&](const char* k, std::string& dst) {
        auto it = kv.find(k);
        if (it != kv.end()) dst = it->second;
    };
    auto num = [&](const char* k, auto& dst) {
        auto it = kv.find(k);
        if (it == kv.end() || it->second.empty()) return;
        try {
            using T = std::decay_t<decltype(dst)>;
            if constexpr (std::is_same_v<T, int>) dst = std::stoi(it->second);
            else if constexpr (std::is_same_v<T, unsigned>) dst = static_cast<unsigned>(std::stoul(it->second));
            else dst = std::stod(it->second);
        } catch (...) {
            // ignore malformed numbers, keep default
        }
    };
    auto boolean = [&](const char* k, bool& dst) -> bool {
        auto it = kv.find(k);
        if (it == kv.end()) return false;
        std::string v = toLower(it->second);
        dst = (v == "1" || v == "true" || v == "yes" || v == "on");
        return true;
    };

    str("MEMOPT_DB_HOST", c.db_host);
    num("MEMOPT_DB_PORT", c.db_port);
    str("MEMOPT_DB_NAME", c.db_name);
    str("MEMOPT_DB_USER", c.db_user);
    str("MEMOPT_DB_PASSWORD", c.db_password);
    num("MEMOPT_DB_CONNECT_TIMEOUT", c.db_connect_timeout_s);
    bool ro = false;
    if (boolean("MEMOPT_DB_READONLY", ro)) {
        c.db_allow_writes = !ro;
        writes_explicit = true;
    }
    str("MEMOPT_SERVER_HOST", c.server_host);
    num("MEMOPT_SERVER_PORT", c.server_port);
    str("MEMOPT_METRIC_SOURCE", c.metric_source);
    str("MEMOPT_PROFILER", c.profiler);
    num("MEMOPT_BASE_LATENCY_NS", c.base_latency_ns);
    num("MEMOPT_MLP_SEQUENTIAL", c.mlp_sequential);
    num("MEMOPT_MLP_RANDOM", c.mlp_random);
    num("MEMOPT_MLP_REPEATED", c.mlp_repeated);
    num("MEMOPT_SLOW_TIER_PENALTY", c.slow_tier_penalty);
    num("MEMOPT_SEED", c.seed);
    num("MEMOPT_FAST_CAPACITY", c.fast_capacity_units);
    num("MEMOPT_AOL_HIGH", c.aol_high);
    num("MEMOPT_AOL_LOW", c.aol_low);
    num("MEMOPT_COOLDOWN_EPOCHS", c.cooldown_epochs);
    num("MEMOPT_CONFIRM_EPOCHS", c.confirm_epochs);
    num("MEMOPT_SWAP_MARGIN", c.swap_margin);
    boolean("MEMOPT_DRY_RUN", c.dry_run);
    num("MEMOPT_BENCH_EPOCHS", c.benchmark_epochs);
    num("MEMOPT_BENCH_REPETITIONS", c.benchmark_repetitions);
    num("MEMOPT_WARMUP_RUNS", c.warmup_runs);
}

AppConfig AppConfig::load(const std::string& root_hint) {
    AppConfig c;
    c.project_root = findProjectRoot(root_hint);
    bool writes_explicit = false;

    // .env file (optional)
    std::ifstream f(fs::path(c.project_root) / ".env");
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        apply(c, parseEnvFile(ss.str()), writes_explicit);
    }

    // Environment variables override the file.
    static const char* keys[] = {
        "MEMOPT_DB_HOST", "MEMOPT_DB_PORT", "MEMOPT_DB_NAME", "MEMOPT_DB_USER",
        "MEMOPT_DB_PASSWORD", "MEMOPT_DB_CONNECT_TIMEOUT", "MEMOPT_DB_READONLY",
        "MEMOPT_SERVER_HOST", "MEMOPT_SERVER_PORT", "MEMOPT_METRIC_SOURCE",
        "MEMOPT_PROFILER", "MEMOPT_BASE_LATENCY_NS", "MEMOPT_MLP_SEQUENTIAL",
        "MEMOPT_MLP_RANDOM", "MEMOPT_MLP_REPEATED", "MEMOPT_SLOW_TIER_PENALTY",
        "MEMOPT_SEED", "MEMOPT_FAST_CAPACITY", "MEMOPT_AOL_HIGH", "MEMOPT_AOL_LOW",
        "MEMOPT_COOLDOWN_EPOCHS", "MEMOPT_CONFIRM_EPOCHS", "MEMOPT_SWAP_MARGIN",
        "MEMOPT_DRY_RUN", "MEMOPT_BENCH_EPOCHS", "MEMOPT_BENCH_REPETITIONS",
        "MEMOPT_WARMUP_RUNS"};
    std::map<std::string, std::string> env;
    for (const char* k : keys) {
        if (const char* v = std::getenv(k)) env[k] = v;
    }
    apply(c, env, writes_explicit);

    // Safety: a database other than the demo DB is read-only unless the
    // user explicitly sets MEMOPT_DB_READONLY=0.
    if (!writes_explicit) c.db_allow_writes = (c.db_name == "memopt_demo");
    return c;
}

std::string AppConfig::describeConnection() const {
    std::ostringstream os;
    os << db_user << "@" << db_host << ":" << db_port << "/" << db_name
       << " (password " << (db_password.empty() ? "not set" : "set, hidden") << ", "
       << (db_allow_writes ? "read-write" : "read-only") << ")";
    return os.str();
}

std::string AppConfig::path(const std::string& relative) const {
    return (fs::path(project_root) / relative).string();
}

}  // namespace memopt
