// Tests: AOL calculation, invalid latency/MLP inputs, ranking, metric labels.
#include "aol.h"
#include "config.h"
#include "profiler.h"
#include "test_util.h"

using namespace memopt;

static Metric metric(double v, MetricKind k) {
    Metric m;
    m.value = v;
    m.kind = k;
    return m;
}

TEST(aol_basic_ratio) {
    auto r = AOLCalculator::compute(100.0, 10.0);
    CHECK(r.value.has_value());
    CHECK_NEAR(*r.value, 10.0, 1e-9);
    CHECK(r.kind == MetricKind::Measured);
}

TEST(aol_division_by_zero_is_unavailable) {
    auto r = AOLCalculator::compute(100.0, 0.0);
    CHECK(!r.value.has_value());
    CHECK(r.kind == MetricKind::Unavailable);
    CHECK(r.status.find("zero") != std::string::npos);
}

TEST(aol_missing_inputs) {
    CHECK(!AOLCalculator::compute(std::nullopt, 5.0).value);
    CHECK(!AOLCalculator::compute(100.0, std::nullopt).value);
}

TEST(aol_invalid_ranges) {
    CHECK(!AOLCalculator::compute(-5.0, 4.0).value);           // negative latency
    CHECK(!AOLCalculator::compute(0.5, 4.0).value);            // too small
    CHECK(!AOLCalculator::compute(1e9, 4.0).value);            // absurd latency
    CHECK(!AOLCalculator::compute(100.0, 0.5).value);          // MLP < 1
    CHECK(!AOLCalculator::compute(100.0, 5000.0).value);       // MLP too large
    CHECK(!AOLCalculator::compute(std::nan(""), 4.0).value);   // NaN
    CHECK(!AOLCalculator::compute(100.0, INFINITY).value);     // inf
    CHECK(AOLCalculator::validateLatency(100.0).empty());
    CHECK(!AOLCalculator::validateMlp(0.0).empty());
}

TEST(aol_kind_is_weakest_input) {
    auto r = AOLCalculator::compute(metric(100, MetricKind::Synthetic), metric(10, MetricKind::Estimated));
    CHECK(r.kind == MetricKind::Synthetic);
    auto r2 = AOLCalculator::compute(metric(100, MetricKind::Measured), metric(10, MetricKind::Estimated));
    CHECK(r2.kind == MetricKind::Estimated);
    Metric na = Metric::unavailable("mlp", "ratio", "x", "not collected");
    auto r3 = AOLCalculator::compute(metric(100, MetricKind::Measured), na);
    CHECK(!r3.value);
    CHECK(r3.kind == MetricKind::Unavailable);
}

TEST(aol_ranking_highest_first_unavailable_last) {
    std::vector<DataUnit> u(3);
    u[0].id = "a"; u[0].aol = 2.0;
    u[1].id = "b";                     // no AOL
    u[2].id = "c"; u[2].aol = 9.0;
    auto idx = AOLCalculator::rankByAol(u);
    CHECK_EQ(u[idx[0]].id, std::string("c"));
    CHECK_EQ(u[idx[1]].id, std::string("a"));
    CHECK_EQ(u[idx[2]].id, std::string("b"));
}

TEST(estimator_fallback_labels_and_values) {
    AppConfig cfg;  // defaults: base 100 ns, MLP seq 48 / rand 9 / rep 20
    FallbackProfiler prof;
    UnitMetricEstimator est(cfg);
    std::vector<DataUnit> u(3);
    u[0].id = "seq"; u[0].seq_touches = 100;
    u[1].id = "rnd"; u[1].random_touches = 100;
    u[2].id = "cold";
    est.estimate(u, prof);
    AOLCalculator::apply(u);
    CHECK(u[0].latency.kind == MetricKind::Synthetic);
    CHECK(u[0].mlp.kind == MetricKind::Estimated);
    CHECK(u[0].aol_kind == MetricKind::Synthetic);  // weakest input
    CHECK_NEAR(*u[0].mlp.value, 48.0, 1e-9);
    CHECK_NEAR(*u[1].mlp.value, 9.0, 1e-9);
    CHECK(*u[1].aol > *u[0].aol);                   // random lookups stall more per access
    CHECK(!u[2].aol.has_value());                   // no accesses -> no AOL
    // Latency jitter stays within +/-10 %.
    double l = est.syntheticLatencyFor("orders_s03");
    CHECK(l >= 90.0 && l <= 110.0);
}

TEST(estimator_mixed_pattern_harmonic_mean) {
    AppConfig cfg;
    UnitMetricEstimator est(cfg);
    DataUnit u;
    u.seq_touches = 50;
    u.random_touches = 50;
    // 1 / (0.5/48 + 0.5/9) = 15.157...
    CHECK_NEAR(*est.estimatedMlpFor(u), 1.0 / (0.5 / 48.0 + 0.5 / 9.0), 1e-9);
}

TEST(measured_mode_never_substitutes) {
    AppConfig cfg;
    cfg.metric_source = "measured";
    FallbackProfiler prof;
    std::vector<DataUnit> u(1);
    u[0].id = "x";
    u[0].random_touches = 10;
    UnitMetricEstimator(cfg).estimate(u, prof);
    AOLCalculator::apply(u);
    CHECK(u[0].latency.kind == MetricKind::Unavailable);
    CHECK(u[0].mlp.kind == MetricKind::Unavailable);
    CHECK(!u[0].aol.has_value());
}

int main() { return RUN_ALL(); }
