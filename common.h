// common.h - Shared types used by every MemOpt module.
//
// IMPORTANT (research integrity): every number MemOpt shows carries a
// MetricKind so the dashboard can tell the difference between
//   * Measured    - really collected (SQL timings, hardware counters)
//   * Estimated   - computed by a heuristic from real observations
//   * Synthetic   - an assumed/model value, not observed at all
//   * Unavailable - could not be collected; value is empty
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace memopt {

// ---------------------------------------------------------------- tiers ---
// FAST and SLOW are LOGICAL tiers. They are simulated with two folders
// (fast_memory/ and slow_memory/). They are not real DRAM / CXL memory.
enum class Tier { FAST, SLOW, NONE };

std::string tierName(Tier t);
Tier parseTier(const std::string& s);  // "FAST"/"SLOW", anything else -> NONE

// -------------------------------------------------------------- metrics ---
enum class MetricKind { Measured, Estimated, Synthetic, Unavailable };

std::string metricKindName(MetricKind k);

struct Metric {
    std::string name;
    std::optional<double> value;  // empty when Unavailable
    std::string unit;
    std::string source;           // e.g. "perf_event_open", "fallback-model"
    MetricKind kind = MetricKind::Unavailable;
    std::string status;           // human readable collection status

    static Metric unavailable(const std::string& name, const std::string& unit,
                              const std::string& source, const std::string& why);
};

// "Weakest" kind of two inputs: Measured > Estimated > Synthetic > Unavailable.
MetricKind weakerKind(MetricKind a, MetricKind b);

// ----------------------------------------------------------- data units ---
// A data unit is a fixed range of primary keys of one table, for example
// "orders_s03" = orders.order_id 6001..8000. It is the "page/object" that
// the placement policies move between the logical tiers. It is NOT a real
// PostgreSQL buffer page.
enum class AccessPattern { Sequential, Random, Repeated };

std::string patternName(AccessPattern p);

struct DataUnit {
    std::string id;       // e.g. "orders_s03"
    std::string table;    // e.g. "orders"
    long long first_key = 0;
    long long last_key = 0;

    // Row touches observed while running real SQL (measured counts).
    uint64_t seq_touches = 0;
    uint64_t random_touches = 0;
    uint64_t repeated_touches = 0;

    uint64_t accessCount() const { return seq_touches + random_touches + repeated_touches; }

    // Per-unit memory metrics (see profiling/unit_metrics). These are only
    // Measured if a profiler really sampled them; otherwise they are labelled.
    Metric latency;  // ns
    Metric mlp;      // memory-level parallelism (dimensionless)

    // AOL result (filled by AOLCalculator).
    std::optional<double> aol;
    MetricKind aol_kind = MetricKind::Unavailable;
    std::string aol_status;

    void resetCounts() { seq_touches = random_touches = repeated_touches = 0; }
};

// ------------------------------------------------------------- helpers ----
std::string nowIso8601();          // local time, e.g. 2026-10-13T11:25:01
std::string timestampForFile();    // e.g. 20261013_112501
std::string formatDouble(double v, int precision = 3);
std::string toUpper(std::string s);
std::string toLower(std::string s);
std::string trim(const std::string& s);

// Percentage difference of `value` relative to `baseline`.
// Returns empty optional when baseline is 0 (division by zero).
std::optional<double> percentDifference(double baseline, double value);

}  // namespace memopt
