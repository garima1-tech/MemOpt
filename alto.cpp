#include "alto.h"

#include <algorithm>

namespace memopt {

Tier AltoInspiredMigrationController::preferenceFor(double aol) const {
    if (aol > cfg_.aol_high) return Tier::FAST;
    if (aol < cfg_.aol_low) return Tier::SLOW;
    return Tier::NONE;
}

void AltoInspiredMigrationController::reset() { state_.clear(); }

bool AltoInspiredMigrationController::inCooldown(const UnitState& s, int epoch) const {
    return epoch - s.last_move_epoch <= cfg_.cooldown_epochs;
}

std::vector<MigrationRecord> AltoInspiredMigrationController::review(const std::vector<DataUnit>& units,
                                                                     Placement& placement, int epoch) {
    std::vector<MigrationRecord> out;
    std::string ts = nowIso8601();
    // Work on a copy so dry-run never changes the real placement.
    Placement work = placement;
    auto tierOf = [&](const std::string& id) {
        auto it = work.find(id);
        return it == work.end() ? Tier::SLOW : it->second;
    };

    struct Candidate {
        const DataUnit* u;
        Tier want;
        std::string why;
    };
    std::vector<Candidate> demote, promote;

    // 1. Update streaks and collect eligible candidates.
    for (const auto& u : units) {
        UnitState& s = state_[u.id];
        Tier cur = tierOf(u.id);
        Tier pref = Tier::NONE;
        std::string why;
        if (u.aol) {
            pref = preferenceFor(*u.aol);
            if (pref == Tier::FAST) why = "AOL " + formatDouble(*u.aol, 2) + " > high threshold " + formatDouble(cfg_.aol_high, 1);
            if (pref == Tier::SLOW) why = "AOL " + formatDouble(*u.aol, 2) + " < low threshold " + formatDouble(cfg_.aol_low, 1);
        } else if (cur == Tier::FAST && u.accessCount() == 0) {
            pref = Tier::SLOW;
            why = "idle: no accesses this epoch";
        }
        if (pref != Tier::NONE && pref == s.last_pref) ++s.streak;
        else s.streak = (pref == Tier::NONE) ? 0 : 1;
        s.last_pref = pref;

        if (pref == Tier::NONE || pref == cur) continue;
        if (s.streak < cfg_.confirm_epochs) continue;  // not confirmed yet
        if (inCooldown(s, epoch)) continue;            // moved too recently
        if (pref == Tier::SLOW) demote.push_back({&u, pref, why});
        else promote.push_back({&u, pref, why});
    }

    auto record = [&](const DataUnit& u, Tier from, Tier to, const std::string& reason) {
        MigrationRecord m;
        m.policy = "memopt";
        m.unit_id = u.id;
        m.epoch = epoch;
        m.old_tier = from;
        m.new_tier = to;
        m.trigger_metric = u.aol ? "aol" : "access_count";
        m.trigger_value = u.aol ? u.aol : std::optional<double>(static_cast<double>(u.accessCount()));
        m.reason = reason;
        m.dry_run = cfg_.dry_run;
        m.timestamp = ts;
        work[u.id] = to;
        if (!cfg_.dry_run) state_[u.id].last_move_epoch = epoch;
        out.push_back(m);
    };

    // 2. Demotions first: they free FAST capacity.
    for (const auto& c : demote) record(*c.u, Tier::FAST, Tier::SLOW, c.why);

    // 3. Promotions, highest stall priority (accesses x AOL) first, so a
    //    unit with a big AOL but only a handful of accesses does not take
    //    a FAST slot from a busier one.
    auto prio = [](const DataUnit& u) { return static_cast<double>(u.accessCount()) * u.aol.value_or(0.0); };
    std::sort(promote.begin(), promote.end(), [&](const Candidate& a, const Candidate& b) {
        if (prio(*a.u) != prio(*b.u)) return prio(*a.u) > prio(*b.u);
        return a.u->id < b.u->id;
    });
    for (const auto& c : promote) {
        int fast = 0;
        for (const auto& kv : work)
            if (kv.second == Tier::FAST) ++fast;
        if (fast < cfg_.fast_capacity) {
            record(*c.u, Tier::SLOW, Tier::FAST, c.why);
            continue;
        }
        // FAST is full: find the FAST unit with the lowest stall priority
        // that is not in cooldown. Swap only if the candidate's priority is
        // at least (1 + swap_margin) times larger - a hysteresis band that
        // stops near-equal units from trading places every epoch.
        const DataUnit* victim = nullptr;
        double victim_p = 0;
        for (const auto& v : units) {
            if (tierOf(v.id) != Tier::FAST) continue;
            if (inCooldown(state_[v.id], epoch)) continue;
            double pv = prio(v);
            if (!victim || pv < victim_p || (pv == victim_p && v.id < victim->id)) {
                victim = &v;
                victim_p = pv;
            }
        }
        double cand_p = prio(*c.u);
        if (victim && cand_p >= victim_p * (1.0 + cfg_.swap_margin) && cand_p > victim_p) {
            record(*victim, Tier::FAST, Tier::SLOW,
                   "swapped out for " + c.u->id + " (stall priority " + formatDouble(victim_p, 0) + " vs " +
                       formatDouble(cand_p, 0) + ", margin " + formatDouble(cfg_.swap_margin * 100, 0) + "%)");
            record(*c.u, Tier::SLOW, Tier::FAST, c.why + "; replaced " + victim->id);
        }
        // otherwise: stay in SLOW this epoch (capacity-limited)
    }

    if (!cfg_.dry_run) placement = work;
    return out;
}

}  // namespace memopt
