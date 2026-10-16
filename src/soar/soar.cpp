#include "soar.h"

#include <algorithm>
#include <numeric>

namespace memopt {

std::optional<double> AOLRankingPolicy::priority(const DataUnit& u) {
    if (!u.aol || u.accessCount() == 0) return std::nullopt;
    return static_cast<double>(u.accessCount()) * *u.aol;
}

std::vector<size_t> AOLRankingPolicy::rank(const std::vector<DataUnit>& units) {
    std::vector<size_t> idx(units.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        auto pa = priority(units[a]);
        auto pb = priority(units[b]);
        if (pa.has_value() != pb.has_value()) return pa.has_value();
        if (pa && pb && *pa != *pb) return *pa > *pb;
        return units[a].id < units[b].id;
    });
    return idx;
}

AllocationResult SoarInspiredAllocator::allocate(const std::vector<DataUnit>& units,
                                                 const Placement& current) const {
    AllocationResult res;
    int with_aol = 0;
    for (const auto& u : units)
        if (u.aol) ++with_aol;
    if (with_aol == 0) {
        res.ok = false;
        res.message = "MemOpt placement refused: no data unit has a usable AOL value. "
                      "Run a workload first, or set MEMOPT_METRIC_SOURCE=fallback to use the "
                      "labelled synthetic/estimated model.";
        return res;
    }

    int fast_used = 0;
    int rank = 0;
    for (size_t i : AOLRankingPolicy::rank(units)) {
        const auto& u = units[i];
        PlacementDecision d;
        d.policy = "memopt";
        d.unit_id = u.id;
        d.access_count = u.accessCount();
        d.latency_ns = u.latency.value;
        d.mlp = u.mlp.value;
        d.aol = u.aol;
        d.basis = u.aol_kind;
        auto p = AOLRankingPolicy::priority(u);
        d.score = p.value_or(0.0);
        d.rank = ++rank;
        auto it = current.find(u.id);
        d.current = it == current.end() ? Tier::NONE : it->second;
        if (!p) {
            d.proposed = Tier::SLOW;
            d.reason = u.accessCount() == 0 ? "cold: no accesses observed" : ("no AOL: " + u.aol_status);
        } else if (fast_used < capacity_) {
            d.proposed = Tier::FAST;
            ++fast_used;
            d.reason = "rank #" + std::to_string(d.rank) + " by stall priority (accesses " +
                       std::to_string(u.accessCount()) + " x AOL " + formatDouble(*u.aol, 2) + ")";
        } else {
            d.proposed = Tier::SLOW;
            d.reason = "FAST tier full; stall priority rank #" + std::to_string(d.rank) +
                       " (AOL " + formatDouble(*u.aol, 2) + ")";
        }
        res.decisions.push_back(d);
    }
    MetricKind basis = MetricKind::Measured;
    for (const auto& u : units)
        if (u.aol) basis = weakerKind(basis, u.aol_kind);
    res.ok = true;
    res.message = "SOAR-inspired allocation: " + std::to_string(fast_used) + " unit(s) in FAST (capacity " +
                  std::to_string(capacity_) + "), AOL basis: " + metricKindName(basis);
    return res;
}

}  // namespace memopt
