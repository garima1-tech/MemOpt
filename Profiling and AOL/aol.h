// aol.h - Simplified AOL model (educational approximation).
//
//     AOL = latency / MLP
//
// Intuition: a load that misses the cache costs `latency` ns, but if the
// CPU keeps MLP misses in flight at the same time, each one only "costs"
// latency / MLP ns of stall on average. A data unit with a HIGH AOL makes
// the program wait a lot per access -> it benefits from the FAST tier.
// A unit with a LOW AOL (e.g. a sequential scan with many parallel,
// prefetched misses) hides latency well -> it can live in the SLOW tier
// even if it is accessed very often ("beyond hotness").
//
// The research paper computes its metric from hardware events; this
// prototype only implements the simple ratio above and labels every
// result with the weakest kind of its inputs (measured/estimated/synthetic).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common.h"

namespace memopt {

struct AolResult {
    std::optional<double> value;
    MetricKind kind = MetricKind::Unavailable;
    std::string status;
};

class AOLCalculator {
public:
    // Valid ranges (sanity limits for the prototype).
    static constexpr double kMinLatencyNs = 1.0;
    static constexpr double kMaxLatencyNs = 100000.0;
    static constexpr double kMinMlp = 1.0;     // at least one miss in flight
    static constexpr double kMaxMlp = 1000.0;

    // Returns an error message, or empty string when valid.
    static std::string validateLatency(const std::optional<double>& ns);
    static std::string validateMlp(const std::optional<double>& mlp);

    // Safe computation: missing / invalid / zero inputs give an
    // Unavailable result with a reason (never a crash or a fake value).
    static AolResult compute(const Metric& latency, const Metric& mlp);
    static AolResult compute(std::optional<double> latency_ns, std::optional<double> mlp,
                             MetricKind kind = MetricKind::Measured);

    // Fills DataUnit::aol / aol_kind / aol_status for every unit.
    static void apply(std::vector<DataUnit>& units);

    // Indices of units sorted by AOL (highest first). Units without AOL
    // come last, ordered by id for stable output.
    static std::vector<size_t> rankByAol(const std::vector<DataUnit>& units);
};

}  // namespace memopt
