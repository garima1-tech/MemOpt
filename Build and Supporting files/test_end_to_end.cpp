// End-to-end demo workflow through the REST API, against REAL PostgreSQL.
// Uses a temporary copy of workloads/ and frontend/ so it never touches
// your project's fast_memory/, slow_memory/ or results/ folders.
// Exits 77 (CTest "Skipped") when the database is not reachable.
#include <filesystem>

#include "api.h"
#include "dataset.h"
#include "test_util.h"

using namespace memopt;
namespace fs = std::filesystem;

static AppState* g_state = nullptr;

static HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") {
    HttpRequest r;
    std::string raw = method + " " + path + " HTTP/1.1\r\n\r\n";
    std::string err;
    parseHttpRequestHead(raw, r, err);
    r.body = body;
    Api api(*g_state);
    return api.handle(r);
}

TEST(full_demo_flow) {
    CHECK_EQ(call("POST", "/api/database/test").status, 200);
    auto tables = call("GET", "/api/database/tables");
    CHECK_EQ(tables.status, 200);
    CHECK(tables.body.find("\"all_present\":true") != std::string::npos);

    auto wl = call("POST", "/api/workload/run", R"({"workload":"mixed","repetitions":1,"warmup":0})");
    CHECK_EQ(wl.status, 200);
    CHECK(wl.body.find("\"failure\":0") != std::string::npos);
    CHECK_EQ(call("GET", "/api/workload/latest").status, 200);

    auto sim = call("POST", "/api/placement/simulate", R"({"policy":"both"})");
    CHECK_EQ(sim.status, 200);
    Placement p = g_state->tiers.current();
    CHECK_EQ(p.size(), buildUnitCatalog().size());
    CHECK_EQ(countTier(p, Tier::FAST), g_state->cfg.fast_capacity_units);

    // Second call = ALTO-inspired review against the existing folders.
    auto mig = call("POST", "/api/placement/simulate", R"({"policy":"memopt","mode":"migrate","dry_run":true})");
    CHECK_EQ(mig.status, 200);
    CHECK(countTier(g_state->tiers.current(), Tier::FAST) <= g_state->cfg.fast_capacity_units);

    auto pl = call("GET", "/api/placement");
    CHECK_EQ(pl.status, 200);
    CHECK(pl.body.find("\"has_workload_data\":true") != std::string::npos);

    auto prof = call("POST", "/api/profiling/run", R"({"workload":"random","repetitions":1})");
    CHECK_EQ(prof.status, 200);
    CHECK(prof.body.find("\"query_duration\"") != std::string::npos);

    auto bench = call("POST", "/api/benchmark/run", R"({"epochs":3,"repetitions":1,"warmup":0})");
    CHECK_EQ(bench.status, 200);
    CHECK(g_state->latest_benchmark.has_value());
    if (g_state->latest_benchmark) {
        const auto& b = *g_state->latest_benchmark;
        CHECK(b.ok);
        CHECK_EQ(b.hotness.failure, 0);
        CHECK_EQ(b.memopt.failure, 0);
        CHECK_EQ(b.hotness.success, b.memopt.success);  // identical SQL for both
        CHECK(b.hotness.fast_units <= b.fast_capacity);
        CHECK(b.memopt.fast_units <= b.fast_capacity);
        CHECK(b.hotness.simulated_stall_cost.has_value());
        CHECK(!b.csv_path.empty() && fs::exists(b.csv_path));
    }
    auto csv = call("GET", "/api/results/export?type=benchmark");
    CHECK_EQ(csv.status, 200);
    CHECK(csv.content_type.find("text/csv") != std::string::npos);
    CHECK_EQ(call("GET", "/api/migrations").status, 200);
}

int main() {
    AppConfig cfg = AppConfig::load();
    PgConnection probe;
    if (!probe.connect(cfg)) SKIP_TEST("PostgreSQL not reachable (" << probe.lastError() << ")");
    probe.disconnect();

    fs::path tmp = fs::temp_directory_path() / "memopt_e2e";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "workloads");
    fs::copy(fs::path(cfg.project_root) / "workloads", tmp / "workloads", fs::copy_options::recursive, ec);
    fs::copy(fs::path(cfg.project_root) / "frontend", tmp / "frontend", fs::copy_options::recursive, ec);
    cfg.project_root = tmp.string();
    g_state = new AppState(cfg);
    int rc = RUN_ALL();
    delete g_state;
    return rc;
}
