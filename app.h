// app.h - Application state + operations shared by the CLI and the HTTP API.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "alto.h"
#include "benchmark.h"
#include "config.h"
#include "policy_types.h"
#include "postgres.h"
#include "profiler.h"
#include "result_logger.h"
#include "tier_manager.h"
#include "workload.h"

namespace memopt {

struct OpError {
    int http_status = 500;
    std::string message;
};

struct ProfileRunResult {
    ProfileReport report;
    WorkloadType workload = WorkloadType::Mixed;
    int repetitions = 0;
    double total_query_ms = 0.0;
    double avg_query_ms = 0.0;
    int queries = 0;
    std::string started_at;
};

class AppState {
public:
    explicit AppState(AppConfig cfg);

    AppConfig cfg;
    PgConnection db;
    QueryLibrary lib;
    std::string lib_error;  // non-empty if workload.sql failed to load
    std::unique_ptr<IProfiler> profiler;
    std::string profiler_note;
    TierManager tiers;
    ResultLogger logger;
    AltoInspiredMigrationController alto;
    int live_epoch = 0;

    std::optional<WorkloadResult> latest_workload;
    std::vector<DataUnit> units;  // latest per-unit metrics
    std::vector<PlacementDecision> hotness_decisions;
    std::vector<PlacementDecision> memopt_decisions;
    std::vector<MigrationRecord> live_migrations;  // from /placement/simulate
    std::optional<BenchmarkResult> latest_benchmark;
    std::optional<ProfileRunResult> latest_profile;
    std::vector<std::string> warnings;  // e.g. failed DB/CSV saves

    // Connects (or reconnects). On failure returns the friendly error.
    bool connectDb(std::string& error, bool force = false);

    std::optional<OpError> runWorkload(WorkloadType type, int reps, int warmup, const ProgressFn& progress = {});
    std::optional<OpError> runProfile(WorkloadType type, int reps, const ProgressFn& progress = {});
    // policy: "both" | "memopt" | "hotness"; mode: "auto" | "allocate" | "migrate"
    std::optional<OpError> simulatePlacement(const std::string& policy, const std::string& mode, bool dry_run,
                                             std::string& summary);
    std::optional<OpError> runBenchmark(int epochs, int reps, int warmup, const ProgressFn& progress = {});
    std::optional<OpError> resetTiers();

private:
    void refreshUnitMetrics(std::vector<DataUnit>& units);
    void warn(const std::string& w);
    std::optional<OpError> requireDb();
};

}  // namespace memopt
