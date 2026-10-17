#include "aol.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace memopt {

std::string AOLCalculator::validateLatency(const std::optional<double>& ns) {
    if (!ns) return "latency missing";
    if (!std::isfinite(*ns)) return "latency is not a finite number";
    if (*ns < kMinLatencyNs) return "latency below " + formatDouble(kMinLatencyNs, 0) + " ns";
    if (*ns > kMaxLatencyNs) return "latency above " + formatDouble(kMaxLatencyNs, 0) + " ns";
    return "";
}

std::string AOLCalculator::validateMlp(const std::optional<double>& mlp) {
    if (!mlp) return "MLP missing";
    if (!std::isfinite(*mlp)) return "MLP is not a finite number";
    if (*mlp == 0.0) return "MLP is zero (division by zero)";
    if (*mlp < kMinMlp) return "MLP below 1 is physically impossible";
    if (*mlp > kMaxMlp) return "MLP above " + formatDouble(kMaxMlp, 0);
    return "";
}

AolResult AOLCalculator::compute(std::optional<double> latency_ns, std::optional<double> mlp, MetricKind kind) {
    AolResult r;
    std::string e = validateLatency(latency_ns);
    if (e.empty()) e = validateMlp(mlp);
    if (!e.empty()) {
        r.kind = MetricKind::Unavailable;
        r.status = "AOL unavailable: " + e;
        return r;
    }
    r.value = *latency_ns / *mlp;
    r.kind = kind;
    r.status = "AOL = latency / MLP (" + metricKindName(kind) + " inputs)";
    return r;
}

AolResult AOLCalculator::compute(const Metric& latency, const Metric& mlp) {
    if (latency.kind == MetricKind::Unavailable || mlp.kind == MetricKind::Unavailable) {
        AolResult r;
        r.status = "AOL unavailable: " +
                   (latency.kind == MetricKind::Unavailable ? "latency " + latency.status : "MLP " + mlp.status);
        return r;
    }
    return compute(latency.value, mlp.value, weakerKind(latency.kind, mlp.kind));
}

void AOLCalculator::apply(std::vector<DataUnit>& units) {
    for (auto& u : units) {
        AolResult r = compute(u.latency, u.mlp);
        u.aol = r.value;
        u.aol_kind = r.kind;
        u.aol_status = r.status;
    }
}

std::vector<size_t> AOLCalculator::rankByAol(const std::vector<DataUnit>& units) {
    std::vector<size_t> idx(units.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const auto& ua = units[a];
        const auto& ub = units[b];
        if (ua.aol.has_value() != ub.aol.has_value()) return ua.aol.has_value();
        if (ua.aol && ub.aol && *ua.aol != *ub.aol) return *ua.aol > *ub.aol;
        return ua.id < ub.id;
    });
    return idx;
}

}  // namespace memopt
