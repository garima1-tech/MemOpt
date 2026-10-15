// hotness.h - Traditional hotness (access-frequency) baseline.
//
// Ranks data units ONLY by how often they were accessed and puts the
// top-N into the FAST tier. This is what classic tiering systems do and
// is the baseline that MemOpt is compared against.
#pragma once

#include <vector>

#include "common.h"
#include "policy_types.h"

namespace memopt {

class HotnessBaseline {
public:
    explicit HotnessBaseline(int fast_capacity) : capacity_(fast_capacity < 0 ? 0 : fast_capacity) {}

    // Indices sorted by access count (highest first, ties by id).
    static std::vector<size_t> rankByAccess(const std::vector<DataUnit>& units);

    // One decision per unit. Units with zero accesses always stay SLOW.
    std::vector<PlacementDecision> allocate(const std::vector<DataUnit>& units,
                                            const Placement& current) const;

    // Re-rank with new counts; moves units whose tier changes and returns
    // one MigrationRecord per move. No hysteresis (that is the point of
    // the baseline: it reacts to every change in hotness).
    std::vector<MigrationRecord> rebalance(const std::vector<DataUnit>& units, Placement& placement,
                                           int epoch) const;

    int capacity() const { return capacity_; }

private:
    int capacity_;
};

}  // namespace memopt
