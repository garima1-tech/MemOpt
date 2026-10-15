// policy_types.h - Records produced by the placement and migration policies.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "common.h"

namespace memopt {

// unit id -> logical tier
using Placement = std::map<std::string, Tier>;

struct PlacementDecision {
    std::string policy;            // "hotness" or "memopt"
    std::string unit_id;
    uint64_t access_count = 0;
    std::optional<double> latency_ns;
    std::optional<double> mlp;
    std::optional<double> aol;
    MetricKind basis = MetricKind::Unavailable;  // weakest input used
    double score = 0.0;            // ranking score used by the policy
    int rank = 0;                  // 1 = best
    Tier current = Tier::NONE;
    Tier proposed = Tier::SLOW;
    std::string reason;
};

struct MigrationRecord {
    std::string policy;
    std::string unit_id;
    int epoch = 0;
    Tier old_tier = Tier::NONE;
    Tier new_tier = Tier::NONE;
    std::string trigger_metric;    // "aol" or "access_count"
    std::optional<double> trigger_value;
    std::string reason;
    bool dry_run = false;
    bool applied = false;          // true if the tier folders were changed
    std::string timestamp;
};

// Helpers (implemented in src/hotness/hotness.cpp).
Placement placementFrom(const std::vector<PlacementDecision>& decisions);
int countTier(const Placement& p, Tier t);

}  // namespace memopt
