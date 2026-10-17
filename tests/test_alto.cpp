// Tests: ALTO-inspired migration thresholds, hysteresis, cooldown,
// confirmation, swap margin, capacity and dry-run mode.
#include "alto.h"
#include "test_util.h"

using namespace memopt;

static DataUnit unit(const std::string& id, std::optional<double> aol, uint64_t accesses = 100) {
    DataUnit u;
    u.id = id;
    u.random_touches = accesses;
    u.aol = aol;
    u.aol_kind = aol ? MetricKind::Synthetic : MetricKind::Unavailable;
    return u;
}

static AltoConfig cfg(int capacity = 2, int cooldown = 1, int confirm = 1) {
    AltoConfig c;
    c.aol_high = 7.0;
    c.aol_low = 3.0;
    c.fast_capacity = capacity;
    c.cooldown_epochs = cooldown;
    c.confirm_epochs = confirm;
    c.swap_margin = 0.25;
    return c;
}

TEST(preference_thresholds) {
    AltoInspiredMigrationController a(cfg());
    CHECK(a.preferenceFor(8.0) == Tier::FAST);
    CHECK(a.preferenceFor(2.0) == Tier::SLOW);
    CHECK(a.preferenceFor(5.0) == Tier::NONE);
    CHECK(a.preferenceFor(7.0) == Tier::NONE);  // boundaries keep current tier
    CHECK(a.preferenceFor(3.0) == Tier::NONE);
}

TEST(promote_high_aol_and_demote_low_aol) {
    AltoInspiredMigrationController a(cfg());
    Placement p = {{"hi", Tier::SLOW}, {"lo", Tier::FAST}};
    auto m = a.review({unit("hi", 9.0), unit("lo", 1.5)}, p, 1);
    CHECK_EQ(static_cast<int>(m.size()), 2);
    CHECK(p["hi"] == Tier::FAST);
    CHECK(p["lo"] == Tier::SLOW);
    for (const auto& r : m) {
        CHECK(r.trigger_metric == "aol");
        CHECK(r.trigger_value.has_value());
        CHECK(!r.reason.empty());
    }
}

TEST(dead_band_keeps_placement) {
    AltoInspiredMigrationController a(cfg());
    Placement p = {{"x", Tier::FAST}, {"y", Tier::SLOW}};
    auto m = a.review({unit("x", 5.0), unit("y", 6.9)}, p, 1);
    CHECK(m.empty());
    CHECK(p["x"] == Tier::FAST);
    CHECK(p["y"] == Tier::SLOW);
}

TEST(cooldown_prevents_ping_pong) {
    AltoInspiredMigrationController a(cfg(2, /*cooldown=*/2));
    Placement p = {{"u", Tier::SLOW}};
    int moves = 0;
    // AOL flips every epoch: 9, 1, 9, 1, 9, 1
    for (int e = 1; e <= 6; ++e) {
        double aol = (e % 2 == 1) ? 9.0 : 1.0;
        moves += static_cast<int>(a.review({unit("u", aol)}, p, e).size());
    }
    // Without cooldown it would move 6 times; with cooldown 2 it moves at
    // most every third epoch.
    CHECK(moves <= 2);
    CHECK(moves >= 1);
}

TEST(confirmation_ignores_single_spike) {
    AltoInspiredMigrationController a(cfg(2, 0, /*confirm=*/2));
    Placement p = {{"u", Tier::SLOW}};
    CHECK(a.review({unit("u", 9.0)}, p, 1).empty());  // first signal
    CHECK(a.review({unit("u", 5.0)}, p, 2).empty());  // spike gone
    CHECK(a.review({unit("u", 9.0)}, p, 3).empty());  // streak restarts
    auto m = a.review({unit("u", 9.0)}, p, 4);        // confirmed twice in a row
    CHECK_EQ(static_cast<int>(m.size()), 1);
    CHECK(p["u"] == Tier::FAST);
}

TEST(capacity_respected_and_swap_margin) {
    AltoInspiredMigrationController a(cfg(/*capacity=*/1, /*cooldown=*/0));
    Placement p = {{"in", Tier::FAST}, {"c1", Tier::SLOW}};
    // FAST is full; priorities: resident 100*6.5=650, candidate 100*8=800.
    // 800 < 650*1.25=812.5 -> inside the swap margin -> no swap.
    auto m = a.review({unit("in", 6.5), unit("c1", 8.0)}, p, 1);
    CHECK(m.empty());
    CHECK(p["in"] == Tier::FAST);
    // Candidate 100*12=1200 >= 812.5 -> swap.
    m = a.review({unit("in", 6.5), unit("c1", 12.0)}, p, 2);
    CHECK_EQ(static_cast<int>(m.size()), 2);
    CHECK(p["c1"] == Tier::FAST);
    CHECK(p["in"] == Tier::SLOW);
    int fast = 0;
    for (auto& kv : p)
        if (kv.second == Tier::FAST) ++fast;
    CHECK_EQ(fast, 1);
}

TEST(promotion_order_uses_stall_priority) {
    AltoInspiredMigrationController a(cfg(/*capacity=*/1, 0));
    Placement p = {{"rare", Tier::SLOW}, {"busy", Tier::SLOW}};
    // "rare" has the bigger AOL but only 5 accesses; "busy" wins the slot.
    a.review({unit("rare", 12.0, 5), unit("busy", 9.0, 400)}, p, 1);
    CHECK(p["busy"] == Tier::FAST);
    CHECK(p["rare"] == Tier::SLOW);
}

TEST(idle_fast_unit_is_demoted) {
    AltoInspiredMigrationController a(cfg());
    Placement p = {{"idle", Tier::FAST}};
    auto m = a.review({unit("idle", std::nullopt, 0)}, p, 1);
    CHECK_EQ(static_cast<int>(m.size()), 1);
    CHECK(p["idle"] == Tier::SLOW);
    CHECK(m[0].trigger_metric == "access_count");
}

TEST(dry_run_does_not_change_placement) {
    AltoConfig c = cfg();
    c.dry_run = true;
    AltoInspiredMigrationController a(c);
    Placement p = {{"hi", Tier::SLOW}};
    auto m = a.review({unit("hi", 9.0)}, p, 1);
    CHECK_EQ(static_cast<int>(m.size()), 1);
    CHECK(m[0].dry_run);
    CHECK(p["hi"] == Tier::SLOW);
}

int main() { return RUN_ALL(); }
