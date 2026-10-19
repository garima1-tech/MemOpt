// workload.h - Runs REAL SQL workloads against PostgreSQL and records
// client-observed query timings and data-unit access counts.
//
// NOTE: a query duration measured here is the full client-side round trip
// (network/IPC, parsing, planning, execution, buffer cache, result
// transfer). It is NOT memory latency and is never used as memory latency.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "common.h"
#include "postgres.h"

namespace memopt {

enum class WorkloadType { Sequential, Random, Popular, Mixed };

std::string workloadName(WorkloadType t);  // "sequential", "random", "popular", "mixed"
std::optional<WorkloadType> parseWorkload(const std::string& s);
std::vector<WorkloadType> allWorkloads();

// ---------------------------------------------------------- query library --
// Parses "-- name: x" blocks from workloads/workload.sql.
std::map<std::string, std::string> parseQueryLibrary(const std::string& text);

class QueryLibrary {
public:
    bool load(const std::string& path, std::string& error);
    void setQueries(std::map<std::string, std::string> q) { queries_ = std::move(q); }
    const std::string* get(const std::string& name) const;
    const std::map<std::string, std::string>& all() const { return queries_; }
    // Names required by the workload runner.
    static const std::vector<std::string>& requiredNames();
    std::vector<std::string> missingNames() const;

private:
    std::map<std::string, std::string> queries_;
};

// ----------------------------------------------------------- statistics ---
struct QueryStats {
    std::string name;
    int executions = 0;
    int success = 0;
    int failure = 0;
    double total_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    long long rows = 0;
    double avgMs() const { return executions > 0 ? total_ms / executions : 0.0; }
    void add(double ms, bool ok, long long r);
};

struct WorkloadResult {
    WorkloadType type = WorkloadType::Mixed;
    int repetitions = 0;
    int warmup_runs = 0;
    int epoch = 0;
    std::string started_at;
    double total_ms = 0.0;  // wall time of the measured repetitions only
    int success = 0;
    int failure = 0;
    std::vector<QueryStats> queries;   // per query name
    std::vector<std::string> errors;   // first few error messages
    std::vector<DataUnit> units;       // access counts (measured repetitions only)
    long long run_id = -1;             // workload_runs.run_id if saved

    int totalQueries() const { return success + failure; }
    double avgQueryMs() const;
};

// Attributes row touches to data units.
class AccessTracker {
public:
    explicit AccessTracker(std::vector<DataUnit>& units);
    void touchRange(const std::string& table, long long lo, long long hi, AccessPattern p);
    void touchKey(const std::string& table, long long key, AccessPattern p);

private:
    std::vector<DataUnit>& units_;
    std::map<std::string, size_t> index_;
};

using ProgressFn = std::function<void(const std::string&)>;

class WorkloadRunner {
public:
    WorkloadRunner(PgConnection& db, const QueryLibrary& lib, unsigned seed);

    // Runs `warmup` unrecorded repetitions, then `repetitions` measured ones.
    // `epoch` shifts the random-access region (used by the benchmark to
    // create workload phases for the migration controller).
    WorkloadResult run(WorkloadType type, int repetitions, int warmup, int epoch = 0,
                       const ProgressFn& progress = {});


};

}  // namespace memopt
