// main.cpp - MemOpt command-line entry point.
//
//   memopt serve      [--port N] [--host 127.0.0.1]   start API + dashboard
//   memopt check                                      test DB connection + tables
//   memopt workload   <sequential|random|popular|mixed> [--reps N] [--warmup N]
//   memopt profile    [workload] [--reps N]
//   memopt simulate   [--policy both|memopt|hotness] [--mode auto|allocate|migrate] [--dry-run]
//   memopt benchmark  [--epochs N] [--reps N] [--warmup N]
//   memopt reset-tiers
//   memopt help
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "api.h"
#include "app.h"
#include "dataset.h"
#include "hotness.h"
#include "http_server.h"

using namespace memopt;

namespace {

void usage() {
    std::cout <<
        "MemOpt - tiered memory placement beyond hotness (college prototype)\n\n"
        "Usage: memopt <command> [options]\n\n"
        "Commands:\n"
        "  serve [--port 8080] [--host 127.0.0.1]   Start the API server and dashboard\n"
        "  check                                    Test the PostgreSQL connection and tables\n"
        "  workload <type> [--reps 3] [--warmup 1]  Run sequential | random | popular | mixed\n"
        "  profile [type] [--reps 2]                Run a workload under the profiler\n"
        "  simulate [--policy both] [--mode auto] [--dry-run]\n"
        "                                           Run 'mixed', then compute placements\n"
        "  benchmark [--epochs 4] [--reps 3] [--warmup 1]\n"
        "                                           Hotness vs MemOpt comparison (CSV in results/)\n"
        "  reset-tiers                              Clear fast_memory/ and slow_memory/\n"
        "  help                                     Show this text\n\n"
        "Settings come from .env in the project folder and MEMOPT_* environment variables.\n";
}

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> opts;
    bool has(const std::string& k) const { return opts.count(k) > 0; }
    int integer(const std::string& k, int def) const {
        auto it = opts.find(k);
        if (it == opts.end()) return def;
        try {
            return std::stoi(it->second);
        } catch (...) {
            std::cerr << "Invalid number for --" << k << ": " << it->second << "\n";
            std::exit(2);
        }
    }
    std::string str(const std::string& k, const std::string& def) const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
};

Args parseArgs(int argc, char** argv, int start) {
    Args a;
    for (int i = start; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string key = s.substr(2);
            if (key == "dry-run") {
                a.opts[key] = "1";
            } else if (i + 1 < argc) {
                a.opts[key] = argv[++i];
            } else {
                std::cerr << "Missing value for " << s << "\n";
                std::exit(2);
            }
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

ProgressFn printer() {
    return [](const std::string& m) { std::cout << "  ... " << m << std::endl; };
}

int fail(const OpError& e) {
    std::cerr << "ERROR (" << e.http_status << "): " << e.message << std::endl;
    return 1;
}

void printWorkload(const WorkloadResult& r) {
    std::cout << "\nWorkload '" << workloadName(r.type) << "': " << r.repetitions << " measured repetition(s), "
              << r.warmup_runs << " warm-up\n";
    std::cout << std::left << std::setw(30) << "query" << std::right << std::setw(7) << "execs" << std::setw(11)
              << "avg ms" << std::setw(11) << "min ms" << std::setw(11) << "max ms" << std::setw(9) << "rows"
              << std::setw(6) << "fail" << "\n";
    for (const auto& q : r.queries) {
        std::cout << std::left << std::setw(30) << q.name << std::right << std::setw(7) << q.executions
                  << std::setw(11) << formatDouble(q.avgMs(), 4) << std::setw(11) << formatDouble(q.min_ms, 4)
                  << std::setw(11) << formatDouble(q.max_ms, 4) << std::setw(9) << q.rows << std::setw(6)
                  << q.failure << "\n";
    }
    std::cout << "Total: " << r.totalQueries() << " queries, " << r.success << " ok, " << r.failure
              << " failed, " << formatDouble(r.total_ms, 1) << " ms measured\n"
              << "(Client-side round-trip times - they include much more than memory latency.)\n";
    for (const auto& e : r.errors) std::cout << "  error: " << e << "\n";
}

void printUnits(const std::vector<DataUnit>& units) {
    std::cout << "\n" << std::left << std::setw(18) << "unit" << std::right << std::setw(8) << "access"
              << std::setw(7) << "seq" << std::setw(7) << "rand" << std::setw(7) << "rep" << std::setw(10)
              << "lat(ns)" << std::setw(8) << "MLP" << std::setw(8) << "AOL" << "  basis\n";
    for (const auto& u : units) {
        if (u.accessCount() == 0) continue;
        std::cout << std::left << std::setw(18) << u.id << std::right << std::setw(8) << u.accessCount()
                  << std::setw(7) << u.seq_touches << std::setw(7) << u.random_touches << std::setw(7)
                  << u.repeated_touches << std::setw(10)
                  << (u.latency.value ? formatDouble(*u.latency.value, 1) : "n/a") << std::setw(8)
                  << (u.mlp.value ? formatDouble(*u.mlp.value, 1) : "n/a") << std::setw(8)
                  << (u.aol ? formatDouble(*u.aol, 2) : "n/a") << "  " << metricKindName(u.aol_kind) << "\n";
    }
    std::cout << "(units with zero accesses are hidden; latency = synthetic, MLP = estimated in fallback mode)\n";
}

int cmdCheck(AppState& s) {
    std::cout << "Connecting to " << s.cfg.describeConnection() << " ...\n";
    std::string err;
    if (!s.connectDb(err, true)) {
        std::cerr << "FAILED: " << err << "\n";
        return 1;
    }
    std::cout << "Connected. PostgreSQL " << s.db.serverVersion() << ", backend pid " << s.db.backendPid() << "\n";
    auto tables = inspectTables(s.db, err);
    if (!err.empty()) {
        std::cerr << "Cannot inspect tables: " << err << "\n";
        return 1;
    }
    bool ok = true;
    for (const auto& t : tables) {
        long long exp = expectedRows(t.name);
        std::cout << "  " << std::left << std::setw(22) << t.name
                  << (t.exists ? "present  rows=" + std::to_string(t.rows) : std::string("MISSING"));
        if (t.exists && exp > 0 && t.rows != exp) std::cout << "  (expected " << exp << ")";
        std::cout << "\n";
        if (!t.exists) ok = false;
    }
    if (!s.lib_error.empty()) {
        std::cerr << "Query library problem: " << s.lib_error << "\n";
        ok = false;
    } else {
        std::cout << "Query library: " << s.lib.all().size() << " queries loaded from workloads/workload.sql\n";
    }
    std::cout << "Profiler: " << s.profiler_note << "\n";
    if (!ok) std::cerr << "Some checks failed. Run the setup script to create the database.\n";
    return ok ? 0 : 1;
}

int cmdServe(AppState& s, const Args& a) {
    std::string host = a.str("host", s.cfg.server_host);
    int port = a.integer("port", s.cfg.server_port);
    if (host != "127.0.0.1" && host != "localhost") {
        std::cout << "WARNING: binding to " << host << " exposes the demo API to your network.\n";
    }
    Api api(s);
    HttpServer server(host, port, [&api](const HttpRequest& r) { return api.handle(r); });
    std::string err;
    if (!server.start(err)) {
        std::cerr << "ERROR: " << err << "\n";
        return 1;
    }
    std::string dberr;
    bool db = s.connectDb(dberr);
    std::cout << "MemOpt " << "server running at http://" << (host == "localhost" ? "127.0.0.1" : host) << ":" << port
              << "/\n"
              << "Database: " << s.cfg.describeConnection() << " -> "
              << (db ? "connected" : "NOT connected: " + dberr) << "\n"
              << "Profiler: " << s.profiler_note << "\n"
              << "Press Ctrl+C to stop.\n";
    server.serveForever();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        usage();
        return 0;
    }
    Args a = parseArgs(argc, argv, 2);
    AppState s(AppConfig::load());

    if (cmd == "check") return cmdCheck(s);
    if (cmd == "serve") return cmdServe(s, a);

    if (cmd == "workload") {
        std::string w = a.positional.empty() ? "mixed" : a.positional[0];
        auto t = parseWorkload(w);
        if (!t) {
            std::cerr << "Unknown workload '" << w << "'. Use sequential, random, popular or mixed.\n";
            return 2;
        }
        if (auto e = s.runWorkload(*t, a.integer("reps", 3), a.integer("warmup", 1), printer())) return fail(*e);
        printWorkload(*s.latest_workload);
        printUnits(s.units);
        for (const auto& w2 : s.warnings) std::cout << "warning: " << w2 << "\n";
        std::cout << "CSV written to " << s.logger.latestFile("workload_") << "\n";
        return 0;
    }

    if (cmd == "profile") {
        std::string w = a.positional.empty() ? "mixed" : a.positional[0];
        auto t = parseWorkload(w);
        if (!t) {
            std::cerr << "Unknown workload '" << w << "'.\n";
            return 2;
        }
        if (auto e = s.runProfile(*t, a.integer("reps", 2), printer())) return fail(*e);
        const auto& p = *s.latest_profile;
        std::cout << "\nProfiler: " << p.report.profiler << "\nScope:    " << p.report.scope << "\nStatus:   "
                  << p.report.status << "\n\n";
        for (const auto& m : p.report.counters) {
            std::cout << "  " << std::left << std::setw(18) << m.name << std::setw(14)
                      << (m.value ? formatDouble(*m.value, 2) : "n/a") << std::setw(12) << m.unit << std::setw(12)
                      << metricKindName(m.kind) << m.status << "\n";
        }
        std::cout << "\nQuery duration (measured): " << p.queries << " queries, avg "
                  << formatDouble(p.avg_query_ms, 4) << " ms\n";
        return 0;
    }

    if (cmd == "simulate") {
        if (auto e = s.runWorkload(WorkloadType::Mixed, a.integer("reps", 3), 1, printer())) return fail(*e);
        std::string summary;
        if (auto e = s.simulatePlacement(a.str("policy", "both"), a.str("mode", "auto"), a.has("dry-run"), summary))
            return fail(*e);
        printUnits(s.units);
        std::cout << "\n" << summary << "\n\n";
        std::cout << std::left << std::setw(18) << "unit" << std::setw(10) << "hotness" << std::setw(10)
                  << "memopt" << "memopt reason\n";
        std::map<std::string, std::string> hot;
        for (const auto& d : s.hotness_decisions) hot[d.unit_id] = tierName(d.proposed);
        for (const auto& d : s.memopt_decisions) {
            if (d.access_count == 0) continue;
            std::cout << std::left << std::setw(18) << d.unit_id << std::setw(10)
                      << (hot.count(d.unit_id) ? hot[d.unit_id] : "-") << std::setw(10) << tierName(d.proposed)
                      << d.reason << "\n";
        }
        std::cout << "\nTier folders now: FAST=" << countTier(s.tiers.current(), Tier::FAST)
                  << " SLOW=" << countTier(s.tiers.current(), Tier::SLOW) << " (manifest: " << s.tiers.manifestPath()
                  << ")\n";
        return 0;
    }

    if (cmd == "benchmark") {
        if (auto e = s.runBenchmark(a.integer("epochs", s.cfg.benchmark_epochs), a.integer("reps", s.cfg.benchmark_repetitions),
                                    a.integer("warmup", s.cfg.warmup_runs), printer()))
            return fail(*e);
        const auto& b = *s.latest_benchmark;
        std::cout << "\nBenchmark " << b.benchmark_id << " (" << b.epochs << " epochs x " << b.repetitions
                  << " reps, FAST capacity " << b.fast_capacity << ")\n\n";
        std::cout << std::left << std::setw(10) << "policy" << std::right << std::setw(14) << "total ms"
                  << std::setw(12) << "avg q ms" << std::setw(9) << "ok" << std::setw(7) << "fail" << std::setw(7)
                  << "FAST" << std::setw(7) << "SLOW" << std::setw(11) << "migrations" << std::setw(16)
                  << "sim. stall cost" << "\n";
        for (const PolicyRun* p : {&b.hotness, &b.memopt}) {
            std::cout << std::left << std::setw(10) << p->policy << std::right << std::setw(14)
                      << formatDouble(p->total_workload_ms, 1) << std::setw(12) << formatDouble(p->avg_query_ms, 4)
                      << std::setw(9) << p->success << std::setw(7) << p->failure << std::setw(7) << p->fast_units
                      << std::setw(7) << p->slow_units << std::setw(11) << p->migration_count << std::setw(16)
                      << (p->simulated_stall_cost ? formatDouble(*p->simulated_stall_cost, 0) : "n/a") << "\n";
        }
        std::cout << "\nReal query time difference (MemOpt vs hotness): "
                  << (b.time_diff_pct ? formatDouble(*b.time_diff_pct, 2) + " %" : "n/a")
                  << "  <- same SQL for both: this is noise\n";
        std::cout << "Simulated stall cost difference:                "
                  << (b.cost_diff_pct ? formatDouble(*b.cost_diff_pct, 2) + " %" : "n/a") << "  <- model output\n\n";
        for (const auto& n : b.notes) std::cout << "note: " << n << "\n";
        std::cout << "Profiler: " << b.memopt.profile.profiler << " - " << b.memopt.profile.status << "\n";
        if (!b.error.empty()) std::cout << "error: " << b.error << "\n";
        for (const auto& w : s.warnings) std::cout << "warning: " << w << "\n";
        std::cout << "CSV: " << b.csv_path << "\n";
        return b.ok ? 0 : 1;
    }

    if (cmd == "reset-tiers") {
        if (auto e = s.resetTiers()) return fail(*e);
        std::cout << "Tier folders cleared.\n";
        return 0;
    }

    std::cerr << "Unknown command '" << cmd << "'.\n\n";
    usage();
    return 2;
}
