// Tests: hotness ranking, FAST/SLOW allocation, capacity limits, SOAR priority.
#include "hotness.h"
#include "soar.h"
#include "test_util.h"

using namespace memopt;

static DataUnit unit(const std::string& id, uint64_t seq, uint64_t rnd, std::optional<double> aol) {
    DataUnit u;
    u.id = id;
    u.seq_touches = seq;
    u.random_touches = rnd;
    u.aol = aol;
    u.aol_kind = aol ? MetricKind::Synthetic : MetricKind::Unavailable;
    return u;
}

// A small scenario: "scan" is very hot but prefetch-friendly (low AOL),
// "lookup" is less hot but stalls a lot (high AOL).
static std::vector<DataUnit> scenario() {
    return {unit("scan", 1000, 0, 2.0), unit("lookup", 0, 300, 11.0), unit("warm", 0, 100, 5.0),
            unit("cold", 0, 0, std::nullopt)};
}

TEST(hotness_ranks_by_access_count) {
    auto u = scenario();
    auto idx = HotnessBaseline::rankByAccess(u);
    CHECK_EQ(u[idx[0]].id, std::string("scan"));
    CHECK_EQ(u[idx[1]].id, std::string("lookup"));
    CHECK_EQ(u[idx[2]].id, std::string("warm"));
    CHECK_EQ(u[idx[3]].id, std::string("cold"));
}

TEST(hotness_capacity_limit) {
    auto u = scenario();
    auto d = HotnessBaseline(1).allocate(u, {});
    Placement p = placementFrom(d);
    CHECK_EQ(countTier(p, Tier::FAST), 1);
    CHECK(p["scan"] == Tier::FAST);
    CHECK(p["lookup"] == Tier::SLOW);
}

TEST(hotness_cold_never_fast) {
    auto u = scenario();
    auto d = HotnessBaseline(10).allocate(u, {});
    Placement p = placementFrom(d);
    CHECK(p["cold"] == Tier::SLOW);
    CHECK_EQ(countTier(p, Tier::FAST), 3);
}

TEST(hotness_zero_capacity) {
    auto u = scenario();
    Placement p = placementFrom(HotnessBaseline(0).allocate(u, {}));
    CHECK_EQ(countTier(p, Tier::FAST), 0);
}

TEST(soar_prefers_high_stall_priority) {
    auto u = scenario();
    // priorities: scan 1000*2=2000, lookup 300*11=3300, warm 100*5=500
    auto r = SoarInspiredAllocator(1).allocate(u, {});
    CHECK(r.ok);
    Placement p = placementFrom(r.decisions);
    CHECK(p["lookup"] == Tier::FAST);
    CHECK(p["scan"] == Tier::SLOW);  // hot, but low AOL -> beyond hotness
    CHECK_EQ(countTier(p, Tier::FAST), 1);
}

TEST(soar_and_hotness_disagree_in_scenario) {
    auto u = scenario();
    Placement hot = placementFrom(HotnessBaseline(1).allocate(u, {}));
    Placement mem = placementFrom(SoarInspiredAllocator(1).allocate(u, {}).decisions);
    CHECK(hot["scan"] != mem["scan"]);
}

TEST(soar_respects_capacity_and_skips_missing_aol) {
    auto u = scenario();
    auto r = SoarInspiredAllocator(10).allocate(u, {});
    Placement p = placementFrom(r.decisions);
    CHECK_EQ(countTier(p, Tier::FAST), 3);
    CHECK(p["cold"] == Tier::SLOW);
}

TEST(soar_refuses_without_any_aol) {
    std::vector<DataUnit> u = {unit("a", 10, 0, std::nullopt), unit("b", 0, 5, std::nullopt)};
    auto r = SoarInspiredAllocator(2).allocate(u, {});
    CHECK(!r.ok);
    CHECK(r.decisions.empty());
}

TEST(decisions_have_reasons_and_ranks) {
    auto u = scenario();
    auto r = SoarInspiredAllocator(2).allocate(u, {});
    for (const auto& d : r.decisions) {
        CHECK(!d.reason.empty());
        CHECK(d.rank >= 1);
        CHECK(d.policy == "memopt");
    }
}

TEST(hotness_rebalance_moves_and_respects_capacity) {
    auto u = scenario();
    HotnessBaseline hb(1);
    Placement p = placementFrom(hb.allocate(u, {}));  // scan in FAST
    // Hotness shifts: lookup becomes hottest.
    u[1].random_touches = 5000;
    auto moves = hb.rebalance(u, p, 1);
    CHECK_EQ(static_cast<int>(moves.size()), 2);  // scan out, lookup in
    CHECK(p["lookup"] == Tier::FAST);
    CHECK(p["scan"] == Tier::SLOW);
    CHECK_EQ(countTier(p, Tier::FAST), 1);
}

int main() { return RUN_ALL(); }
