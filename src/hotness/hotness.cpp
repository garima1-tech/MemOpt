#include "hotness.h"

#include <algorithm>
#include <numeric>

namespace memopt {

std::vector<size_t> HotnessBaseline::rankByAccess(const std::vector<DataUnit>& units) {
    std::vector<size_t> idx(units.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        if (units[a].accessCount() != units[b].accessCount())
            return units[a].accessCount() > units[b].accessCount();
        return units[a].id < units[b].id;
    });
    return idx;
}

std::vector<PlacementDecision> HotnessBaseline::allocate(const std::vector<DataUnit>& units,
                                                         const Placement& current) const {
    std::vector<PlacementDecision> out;
    auto order = rankByAccess(units);
    int fast_used = 0;
    int rank = 0;
    for (size_t i : order) {
        const auto& u = units[i];
        PlacementDecision d;
        d.policy = "hotness";
        d.unit_id = u.id;
        d.access_count = u.accessCount();
        d.latency_ns = u.latency.value;
        d.mlp = u.mlp.value;
        d.aol = u.aol;
        d.basis = MetricKind::Measured;  // access counts come from real SQL runs
        d.score = static_cast<double>(u.accessCount());
        d.rank = ++rank;
        auto it = current.find(u.id);
        d.current = it == current.end() ? Tier::NONE : it->second;
        if (u.accessCount() == 0) {
            d.proposed = Tier::SLOW;
            d.reason = "cold: no accesses observed";
        } else if (fast_used < capacity_) {
            d.proposed = Tier::FAST;
            ++fast_used;
            d.reason = "hot: rank #" + std::to_string(d.rank) + " by access count (" +
                       std::to_string(u.accessCount()) + " row touches)";
        } else {
            d.proposed = Tier::SLOW;
            d.reason = "FAST tier full (" + std::to_string(capacity_) + " units); rank #" +
                       std::to_string(d.rank) + " by access count";
        }
        out.push_back(d);
    }
    return out;
}

std::vector<MigrationRecord> HotnessBaseline::rebalance(const std::vector<DataUnit>& units,
                                                        Placement& placement, int epoch) const {
    std::vector<MigrationRecord> moves;
    auto decisions = allocate(units, placement);
    std::string ts = nowIso8601();
    // Demotions first, then promotions, so capacity is never exceeded.
    for (int pass = 0; pass < 2; ++pass) {
        Tier target = pass == 0 ? Tier::SLOW : Tier::FAST;
        for (const auto& d : decisions) {
            Tier cur = placement.count(d.unit_id) ? placement[d.unit_id] : Tier::SLOW;
            if (d.proposed != target || cur == target) continue;
            MigrationRecord m;
            m.policy = "hotness";
            m.unit_id = d.unit_id;
            m.epoch = epoch;
            m.old_tier = cur;
            m.new_tier = target;
            m.trigger_metric = "access_count";
            m.trigger_value = static_cast<double>(d.access_count);
            m.reason = d.reason;
            m.timestamp = ts;
            placement[d.unit_id] = target;
            moves.push_back(m);
        }
    }
    return moves;
}

Placement placementFrom(const std::vector<PlacementDecision>& decisions) {
    Placement p;
    for (const auto& d : decisions) p[d.unit_id] = d.proposed;
    return p;
}

int countTier(const Placement& p, Tier t) {
    int n = 0;
    for (const auto& kv : p)
        if (kv.second == t) ++n;
    return n;
}

}  // namespace memopt
