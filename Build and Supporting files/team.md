# Team responsibilities

Four people, one module group each. Everyone owns the tests and docs for their own modules.

| Person | Area | Files |
|---|---|---|
| **P1 – Profiling and AOL** | profiler interface, perf + fallback profilers, per-unit metric estimator, AOL calculator, validation | `include/profiler.h`, `include/aol.h`, `src/profiling/*`, `src/aol/*`, `tests/test_aol.cpp` |
| **P2 – Placement and Migration** | hotness baseline, SOAR-inspired allocator, ALTO-inspired controller, TierManager | `include/hotness.h`, `soar.h`, `alto.h`, `tier_manager.h`, `policy_types.h`, `src/hotness/*`, `src/soar/*`, `src/alto/*`, `src/tiering/*`, `tests/test_hotness_soar.cpp`, `test_alto.cpp`, `test_tier_manager.cpp` |
| **P3 – Database and Workloads** | libpq wrapper, schema + data, query library, workload runner, data units, setup scripts | `include/postgres.h`, `workload.h`, `dataset.h`, `src/dbms/*`, `workloads/*`, `scripts/setup_*`, `tests/test_db_*.cpp` |
| **P4 – Frontend, Benchmarking and Integration** | HTTP server, API, app state, benchmark, CSV/DB results, dashboard, build/run scripts, docs, end-to-end test | `include/api.h`, `app.h`, `http_server.h`, `benchmark.h`, `result_logger.h`, `json.h`, `src/api/*`, `src/benchmark/*`, `src/results/*`, `src/main.cpp`, `frontend/*`, `scripts/build_*`, `scripts/run_*`, `docs/*`, `tests/test_api_validation.cpp`, `test_csv_json.cpp`, `test_end_to_end.cpp` |

Shared (agree changes in a PR review): `include/common.h`, `include/config.h`, `CMakeLists.txt`, `.env.example`.

## Interfaces between people

- P3 → P1: `std::vector<DataUnit>` with `seq/random/repeated_touches` filled.
- P1 → P2: the same vector with `latency`, `mlp`, `aol`, `aol_kind` filled.
- P2 → P4: `PlacementDecision` / `MigrationRecord` (`policy_types.h`), `Placement` map.
- P4 → everyone: `AppState` operations used by both the CLI and the API.

## Git workflow

```bash
git checkout -b feature/p1-profiling-aol
# work, then
git add -A && git commit -m "..."
git push -u origin feature/p1-profiling-aol   # open a pull request into main
```

Rules: `main` must always build and pass `ctest`; one reviewer per PR; never commit `.env`, `build/`, or `results/*.csv`.

| Person | Branches | Example commit messages |
|---|---|---|
| P1 | `feature/p1-profiler-interface`, `feature/p1-aol-calculator`, `feature/p1-linux-perf` | `profiling: add IProfiler and FallbackProfiler` · `aol: validate latency and MLP ranges, handle MLP=0` · `profiling: read cycles/instructions via perf_event_open with fallback` · `test: AOL edge cases and measured-mode no-substitution` |
| P2 | `feature/p2-hotness-baseline`, `feature/p2-soar-allocator`, `feature/p2-alto-migration`, `feature/p2-tier-manager` | `hotness: rank units by access count with capacity limit` · `soar: allocate by accesses x AOL stall priority` · `alto: add dead band, cooldown and swap margin` · `tiering: atomic place/migrate with path validation` |
| P3 | `feature/p3-schema-data`, `feature/p3-libpq-wrapper`, `feature/p3-workload-runner`, `feature/p3-setup-scripts` | `db: schema with FKs and indexes, deterministic sample data` · `db: PgConnection with PQexecParams and friendly errors` · `workload: sequential/random/popular/mixed with warm-up` · `scripts: setup_windows.ps1 creates and loads memopt_demo` |
| P4 | `feature/p4-http-api`, `feature/p4-dashboard`, `feature/p4-benchmark`, `feature/p4-docs` | `api: localhost HTTP server and validated endpoints` · `frontend: placement tab with hotness vs AOL scatter` · `benchmark: epochs, same seeds, CSV + benchmark_results` · `docs: setup, testing, limitations, viva notes` |

## Suggested order (keeps the app runnable at each step)

1. P3 schema + connection, P4 server skeleton with `/api/health` and `/api/database/status`.
2. P3 one workload, P2 hotness baseline, P4 CSV + basic dashboard → **first end-to-end demo**.
3. P1 fallback metrics + AOL, P2 SOAR allocator + TierManager, P4 placement tab.
4. P2 ALTO, P4 benchmark + charts, P1 Linux perf profiler.
5. Everyone: tests, docs, rehearsal.
