#include "app.h"

#include <algorithm>

#include "aol.h"
#include "dataset.h"
#include "hotness.h"
#include "soar.h"

namespace memopt {

static AltoConfig altoFrom(const AppConfig& c) {
    AltoConfig a;
    a.aol_high = c.aol_high;
    a.aol_low = c.aol_low;
    a.cooldown_epochs = c.cooldown_epochs;
    a.confirm_epochs = c.confirm_epochs;
    a.swap_margin = c.swap_margin;
    a.fast_capacity = c.fast_capacity_units;
    a.dry_run = c.dry_run;
    return a;
}

AppState::AppState(AppConfig c)
    : cfg(std::move(c)), tiers(cfg.project_root), logger(cfg.path("results")), alto(altoFrom(cfg)) {
    if (!lib.load(cfg.path("workloads/workload.sql"), lib_error)) {
        // keep running: the API reports lib_error
    }
    profiler = createProfiler(cfg, profiler_note);
    std::string err;
    if (!tiers.init(err)) warn(err);
    units = buildUnitCatalog();
}

void AppState::warn(const std::string& w) {
    warnings.push_back(nowIso8601() + " " + w);
    if (warnings.size() > 20) warnings.erase(warnings.begin());
}

bool AppState::connectDb(std::string& error, bool force) {
    if (!force && db.isConnected()) return true;
    bool ok = force ? db.connect(cfg) : db.ensureConnected(cfg);
    if (!ok) error = db.lastError();
    return ok;
}

std::optional<OpError> AppState::requireDb() {
    std::string err;
    if (!connectDb(err)) return OpError{503, err};
    if (!lib_error.empty()) return OpError{500, lib_error};
    std::string terr;
    auto tables = inspectTables(db, terr);
    if (!terr.empty()) return OpError{503, "cannot inspect tables: " + terr};
    std::vector<std::string> missing;
    for (const auto& t : tables)
        if (!t.exists) missing.push_back(t.name);
    if (!missing.empty()) {
        std::string m = "Missing tables:";
        for (auto& x : missing) m += " " + x;
        m += ". Run scripts/setup_windows.ps1 (or scripts/setup_linux.sh) to create and load the database.";
        return OpError{503, m};
    }
    for (const auto& t : tables) {
        long long exp = expectedRows(t.name);
        if (exp > 0 && t.rows != exp) {
            warn("table " + t.name + " has " + std::to_string(t.rows) + " rows, expected " + std::to_string(exp) +
                 " (data-unit layout assumes the demo dataset)");
        }
    }
    return std::nullopt;
}

void AppState::refreshUnitMetrics(std::vector<DataUnit>& u) {
    UnitMetricEstimator(cfg).estimate(u, *profiler);
    AOLCalculator::apply(u);
}

std::optional<OpError> AppState::runWorkload(WorkloadType type, int reps, int warmup, const ProgressFn& progress) {
    if (auto e = requireDb()) return e;
    WorkloadRunner runner(db, lib, cfg.seed);
    WorkloadResult r = runner.run(type, reps, warmup, 0, progress);
    if (r.success == 0 && r.failure > 0) {
        return OpError{500, "every query failed: " + (r.errors.empty() ? std::string("unknown") : r.errors.front())};
    }
    refreshUnitMetrics(r.units);
    units = r.units;
    std::string err;
    if (!ResultLogger::saveWorkload(db, r, err)) warn(err);
    if (logger.writeWorkloadCsv(r, err).empty()) warn("CSV export failed: " + err);
    latest_workload = std::move(r);
    return std::nullopt;
}

std::optional<OpError> AppState::runProfile(WorkloadType type, int reps, const ProgressFn& progress) {
    if (auto e = requireDb()) return e;
    WorkloadRunner runner(db, lib, cfg.seed);
    runner.run(type, 1, 0, 0);  // warm-up (not profiled, not recorded)
    ProfileRunResult pr;
    pr.workload = type;
    pr.repetitions = reps;
    pr.started_at = nowIso8601();
    profiler->start(db.backendPid());
    WorkloadResult r = runner.run(type, reps, 0, 0, progress);
    pr.report = profiler->stop();
    pr.total_query_ms = 0;
    for (const auto& q : r.queries) {
        pr.total_query_ms += q.total_ms;
        pr.queries += q.executions;
    }
    pr.avg_query_ms = pr.queries ? pr.total_query_ms / pr.queries : 0.0;
    latest_profile = pr;
    return std::nullopt;
}

std::optional<OpError> AppState::simulatePlacement(const std::string& policy, const std::string& mode, bool dry_run,
                                                   std::string& summary) {
    if (!latest_workload) return OpError{409, "No workload data yet. Run a workload first."};
    std::string pol = toLower(policy.empty() ? "both" : policy);
    std::string md = toLower(mode.empty() ? "auto" : mode);
    if (pol != "both" && pol != "memopt" && pol != "hotness") return OpError{400, "policy must be both, memopt or hotness"};
    if (md != "auto" && md != "allocate" && md != "migrate") return OpError{400, "mode must be auto, allocate or migrate"};

    std::string err;
    Placement current = tiers.current();
    for (const auto& u : units)
        if (!current.count(u.id)) current[u.id] = Tier::NONE;

    if (pol == "both" || pol == "hotness") {
        hotness_decisions = HotnessBaseline(cfg.fast_capacity_units).allocate(units, current);
        if (!ResultLogger::savePlacements(db, hotness_decisions, err)) warn(err);
        if (logger.writePlacementCsv(hotness_decisions, "hotness", err).empty()) warn("CSV export failed: " + err);
        summary += "Hotness baseline ranked " + std::to_string(units.size()) + " units (not applied to folders). ";
    }
    if (pol == "both" || pol == "memopt") {
        AllocationResult ar = SoarInspiredAllocator(cfg.fast_capacity_units).allocate(units, current);
        if (!ar.ok) return OpError{409, ar.message};
        bool placed_before = false;
        for (const auto& kv : current)
            if (kv.second != Tier::NONE) placed_before = true;
        bool do_alloc = md == "allocate" || (md == "auto" && !placed_before);

        if (do_alloc) {
            memopt_decisions = ar.decisions;
            if (!dry_run) {
                int moved = 0;
                if (!tiers.applyPlacement(placementFrom(ar.decisions), moved, err))
                    return OpError{500, "tier folders: " + err};
                alto.reset();
                live_epoch = 0;
            }
            summary += ar.message + (dry_run ? ". Dry run: folders unchanged." : ". Applied to tier folders.");
        } else {
            Placement p;
            for (const auto& kv : current) p[kv.first] = kv.second == Tier::NONE ? Tier::SLOW : kv.second;
            alto.setDryRun(dry_run);
            auto moves = alto.review(units, p, ++live_epoch);
            alto.setDryRun(cfg.dry_run);
            for (auto& m : moves) {
                if (!dry_run) {
                    std::string merr;
                    if (tiers.locate(m.unit_id) == Tier::NONE && !tiers.place(m.unit_id, m.old_tier, merr)) {
                        warn(merr);
                        continue;
                    }
                    m.applied = tiers.migrate(m.unit_id, m.old_tier, m.new_tier, merr);
                    if (!m.applied) warn("migration " + m.unit_id + ": " + merr);
                }
            }
            if (!dry_run) tiers.saveManifest(err);
            // Show the ranking, with the tier that ALTO actually decided.
            memopt_decisions = ar.decisions;
            for (auto& d : memopt_decisions) {
                auto it = p.find(d.unit_id);
                d.proposed = (dry_run || it == p.end()) ? d.proposed : it->second;
                d.current = current.count(d.unit_id) ? current[d.unit_id] : Tier::NONE;
                auto mv = std::find_if(moves.begin(), moves.end(),
                                       [&](const MigrationRecord& m) { return m.unit_id == d.unit_id; });
                if (mv != moves.end()) {
                    d.proposed = mv->new_tier;
                    d.reason = "ALTO: " + mv->reason;
                } else {
                    d.proposed = d.current == Tier::NONE ? Tier::SLOW : d.current;
                    d.reason = "ALTO: keep (" +
                               (d.aol ? "AOL " + formatDouble(*d.aol, 2) + " in dead band, confirm/cooldown or capacity"
                                      : std::string("no AOL")) + ")";
                }
            }
            if (!ResultLogger::saveMigrations(db, moves, err)) warn(err);
            if (!moves.empty() && logger.writeMigrationsCsv(moves, err).empty()) warn("CSV export failed: " + err);
            live_migrations.insert(live_migrations.end(), moves.begin(), moves.end());
            summary += "ALTO-inspired review (epoch " + std::to_string(live_epoch) + "): " +
                       std::to_string(moves.size()) + " migration(s)" + (dry_run ? " proposed (dry run)." : " applied.");
        }
        if (!ResultLogger::savePlacements(db, memopt_decisions, err)) warn(err);
        if (logger.writePlacementCsv(memopt_decisions, "memopt", err).empty()) warn("CSV export failed: " + err);
    }
    return std::nullopt;
}

std::optional<OpError> AppState::runBenchmark(int epochs, int reps, int warmup, const ProgressFn& progress) {
    if (auto e = requireDb()) return e;
    BenchmarkEngine engine(db, lib, cfg, *profiler);
    BenchmarkResult b = engine.run(epochs, reps, warmup, progress);
    if (!b.ok && b.hotness.epoch_ms.empty()) return OpError{500, b.error};
    std::string err;
    b.csv_path = logger.writeBenchmarkCsv(b, err);
    if (b.csv_path.empty()) warn("CSV export failed: " + err);
    if (!ResultLogger::saveBenchmark(db, b, err)) warn(err);
    std::vector<MigrationRecord> all = b.hotness.migrations;
    all.insert(all.end(), b.memopt.migrations.begin(), b.memopt.migrations.end());
    if (!ResultLogger::saveMigrations(db, all, err)) warn(err);
    latest_benchmark = std::move(b);
    return std::nullopt;
}

std::optional<OpError> AppState::resetTiers() {
    std::string err;
    if (!tiers.reset(err)) return OpError{500, err};
    alto.reset();
    live_epoch = 0;
    live_migrations.clear();
    memopt_decisions.clear();
    return std::nullopt;
}

}  // namespace memopt
