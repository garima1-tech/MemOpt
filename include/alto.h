// alto.h - ALTO-inspired migration controller (simplified, educational).
//
// After every epoch it looks at each unit's new AOL and decides whether
// to migrate it:
//
//     AOL > aol_high  -> prefer FAST   (default 7, illustrative)
//     AOL < aol_low   -> prefer SLOW   (default 3, illustrative)
//     otherwise       -> keep current tier (dead band = hysteresis)
//
// Extra protections against FAST->SLOW->FAST oscillation ("ping-pong"):
//   * confirm_epochs : the preference must hold this many epochs in a row
//   * cooldown_epochs: a unit that just moved cannot move again for N epochs
//   * swap_margin    : when FAST is full, a promotion may evict the FAST
//                      unit with the lowest stall priority (accesses x AOL)
//                      only if the candidate's priority is at least
//                      (1 + swap_margin) times larger (default 0.25 = +25%)
//   * idle FAST units (no accesses this epoch) are demoted
//
// Thresholds are prototype settings, NOT values from the paper.
#pragma once

#include <map>
#include <vector>

#include "common.h"
#include "policy_types.h"

namespace memopt {

struct AltoConfig {
    double aol_high = 7.0;
    double aol_low = 3.0;
    int cooldown_epochs = 1;
    int confirm_epochs = 1;
    double swap_margin = 0.25;
    int fast_capacity = 6;
    bool dry_run = false;
};

class AltoInspiredMigrationController {
public:
    explicit AltoInspiredMigrationController(AltoConfig cfg) : cfg_(cfg) {}

    // Reviews units after `epoch`. Updates `placement` (unless dry_run) and
    // returns one record per (proposed) migration.
    std::vector<MigrationRecord> review(const std::vector<DataUnit>& units, Placement& placement, int epoch);

    void reset();
    const AltoConfig& config() const { return cfg_; }
    void setDryRun(bool dry) { cfg_.dry_run = dry; }

    // Preference for one AOL value (FAST, SLOW, or NONE = keep).
    Tier preferenceFor(double aol) const;

private:
    struct UnitState {
        Tier last_pref = Tier::NONE;
        int streak = 0;
        int last_move_epoch = -1000000;
    };
    AltoConfig cfg_;
    std::map<std::string, UnitState> state_;

    bool inCooldown(const UnitState& s, int epoch) const;
};

}  // namespace memopt
