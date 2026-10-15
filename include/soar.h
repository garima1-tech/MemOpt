// soar.h - SOAR-inspired initial allocation (simplified, educational).
//
// The research paper's SOAR policy places data by its estimated
// performance impact instead of by hotness. This prototype approximates
// "performance impact" as
//
//     priority = access_count x AOL      (estimated stall contribution)
//
// i.e. how many accesses a unit gets, times how much each access stalls.
// A very hot unit with a tiny AOL (prefetch-friendly scan) can rank BELOW
// a less hot unit with a large AOL (pointer-chasing lookups).
// This is NOT the paper's full algorithm.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common.h"
#include "policy_types.h"

namespace memopt {

class AOLRankingPolicy {
public:
    // Empty when the unit has no AOL (unavailable inputs) or no accesses.
    static std::optional<double> priority(const DataUnit& u);
    // Indices sorted by priority (highest first). Units without a priority
    // come last (ordered by id).
    static std::vector<size_t> rank(const std::vector<DataUnit>& units);
};

struct AllocationResult {
    bool ok = false;
    std::string message;                       // error or summary
    std::vector<PlacementDecision> decisions;  // empty when !ok
};

class SoarInspiredAllocator {
public:
    explicit SoarInspiredAllocator(int fast_capacity) : capacity_(fast_capacity < 0 ? 0 : fast_capacity) {}

    // Fails (ok=false) when NO unit has a usable AOL, e.g. in "measured"
    // mode without a capable profiler: we refuse to guess.
    AllocationResult allocate(const std::vector<DataUnit>& units, const Placement& current) const;

    int capacity() const { return capacity_; }

private:
    int capacity_;
};

}  // namespace memopt
