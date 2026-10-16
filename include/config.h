// config.h - Application settings.
//
// Load order (later wins):
//   1. Built-in defaults (safe demo values, NO real password)
//   2. <project root>/.env file   (excluded from Git)
//   3. Process environment variables (MEMOPT_*)
//
// The database password is never printed, logged or returned by the API.
#pragma once

#include <map>
#include <string>

namespace memopt {

struct AppConfig {
    // --- database -----------------------------------------------------
    std::string db_host = "localhost";
    int db_port = 5432;
    std::string db_name = "memopt_demo";
    std::string db_user = "postgres";
    std::string db_password;          // empty by default; set in .env
    int db_connect_timeout_s = 5;
    // When false MemOpt never writes to the database (read-only session).
    // Defaults to true only for the demo database "memopt_demo".
    bool db_allow_writes = true;

    // --- web server -----------------------------------------------------
    std::string server_host = "127.0.0.1";  // localhost only by default
    int server_port = 8080;

    // --- paths ----------------------------------------------------------
    std::string project_root;  // folder that contains workloads/ frontend/ ...

    // --- metrics --------------------------------------------------------
    // "fallback": use labelled synthetic latency + estimated MLP.
    // "measured": require per-unit latency/MLP from a real profiler; if no
    //             profiler can provide them, AOL is reported as unavailable.
    std::string metric_source = "fallback";
    // "auto" | "fallback" | "linux-perf"
    std::string profiler = "auto";

    // --- fallback model assumptions (illustrative, NOT from the paper) ----
    double base_latency_ns = 100.0;  // assumed FAST-tier load latency
    double mlp_sequential = 48.0;    // assumed MLP for sequential scans
    double mlp_random = 9.0;         // assumed MLP for random lookups
    double mlp_repeated = 20.0;      // assumed MLP for repeated hot lookups
    double slow_tier_penalty = 2.0;  // assumed SLOW/FAST latency ratio
    unsigned seed = 42;              // deterministic workload parameters

    // --- placement policy ----------------------------------------------
    int fast_capacity_units = 6;  // how many data units fit in FAST
    double aol_high = 7.0;        // AOL above this -> prefer FAST
    double aol_low = 3.0;         // AOL below this -> prefer SLOW
    int cooldown_epochs = 1;      // epochs a unit must stay after moving
    int confirm_epochs = 1;       // epochs a signal must persist before moving
    double swap_margin = 0.25;    // +25% stall priority needed to evict a FAST unit
    bool dry_run = false;         // log migrations but do not move files

    // --- benchmark ------------------------------------------------------
    int benchmark_epochs = 4;
    int benchmark_repetitions = 3;
    int warmup_runs = 1;

    // Load defaults + .env + environment. `root_hint` may be empty.
    static AppConfig load(const std::string& root_hint = "");

    // Safe description for logs/UI (password masked).
    std::string describeConnection() const;
    bool hasPassword() const { return !db_password.empty(); }

    // Paths inside the project root.
    std::string path(const std::string& relative) const;
};

// Parse KEY=VALUE lines (comments with #). Exposed for tests.
std::map<std::string, std::string> parseEnvFile(const std::string& text);

// Find the project root: hint, MEMOPT_ROOT, cwd and its parents
// (a folder that contains workloads/workload.sql).
std::string findProjectRoot(const std::string& hint);

}  // namespace memopt
