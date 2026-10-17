// unit_metrics.cpp - Per-data-unit latency and MLP inputs for the AOL model.
#include <cstdint>

#include "profiler.h"

namespace memopt {

// FNV-1a: tiny portable hash so every platform gets the same jitter.
static uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

double UnitMetricEstimator::syntheticLatencyFor(const std::string& unit_id) const {
    // +/-10 % deterministic jitter around the assumed base latency so that
    // units are not all identical. This is an ASSUMPTION, not a measurement.
    double jitter = (static_cast<double>(fnv1a(unit_id) % 2001u) - 1000.0) / 10000.0;
    return cfg_.base_latency_ns * (1.0 + jitter);
}

std::optional<double> UnitMetricEstimator::estimatedMlpFor(const DataUnit& u) const {
    double total = static_cast<double>(u.accessCount());
    if (total <= 0) return std::nullopt;
    if (cfg_.mlp_sequential <= 0 || cfg_.mlp_random <= 0 || cfg_.mlp_repeated <= 0) return std::nullopt;
    // Average stall per access = sum(fraction_p * latency / MLP_p).
    // So the effective MLP is the weighted harmonic mean of per-pattern MLP.
    double inv = (u.seq_touches / total) / cfg_.mlp_sequential +
                 (u.random_touches / total) / cfg_.mlp_random +
                 (u.repeated_touches / total) / cfg_.mlp_repeated;
    if (inv <= 0) return std::nullopt;
    return 1.0 / inv;
}

void UnitMetricEstimator::estimate(std::vector<DataUnit>& units, const IProfiler& profiler) const {
    bool measured_mode = toLower(cfg_.metric_source) == "measured";
    for (auto& u : units) {
        u.latency = Metric{};
        u.mlp = Metric{};
        u.latency.name = "latency";
        u.latency.unit = "ns";
        u.mlp.name = "mlp";
        u.mlp.unit = "ratio";

        if (measured_mode) {
            const char* why = profiler.providesPerUnitLatency()
                                  ? "profiler did not return a sample for this unit"
                                  : "MEMOPT_METRIC_SOURCE=measured, but no available profiler can measure "
                                    "per-unit latency/MLP (needs PEBS/IBS). Not substituted.";
            u.latency = Metric::unavailable("latency", "ns", profiler.name(), why);
            u.mlp = Metric::unavailable("mlp", "ratio", profiler.name(), why);
            continue;
        }

        u.latency.value = syntheticLatencyFor(u.id);
        u.latency.source = "fallback-model (assumed base latency)";
        u.latency.kind = MetricKind::Synthetic;
        u.latency.status = "synthetic: MEMOPT_BASE_LATENCY_NS +/-10% per unit";

        auto mlp = estimatedMlpFor(u);
        if (mlp) {
            u.mlp.value = *mlp;
            u.mlp.source = "fallback-model (access-pattern heuristic)";
            u.mlp.kind = MetricKind::Estimated;
            u.mlp.status = "estimated from measured access-pattern mix";
        } else {
            u.mlp = Metric::unavailable("mlp", "ratio", "fallback-model", "no accesses observed for this unit");
        }
    }
}

}  // namespace memopt
