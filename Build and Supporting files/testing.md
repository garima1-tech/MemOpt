# Testing guide

## 1. Running the tests

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Test
# or, after a build:
$env:PATH = "C:\Program Files\PostgreSQL\16\bin;" + $env:PATH
cd build; ctest -C Release --output-on-failure
```

```bash
# Linux / WSL2
./scripts/build_linux.sh --test
cd build && ctest --output-on-failure            # all
ctest -L database --output-on-failure            # only the PostgreSQL tests
./test_alto                                      # one program, verbose
```

The tests are plain C++ programs (`tests/test_util.h`), no GoogleTest download needed.

**Database tests** (`test_db_workload`, `test_end_to_end`) need the loaded `memopt_demo` database and the same `.env` the app uses. When PostgreSQL is unreachable they exit with code **77**, which CTest shows as **Skipped** — they never pass silently. Example:

```
8/9 Test #8: test_db_workload .................***Skipped
```

## 2. What is tested

| Program | Covers |
|---|---|
| `test_aol` | AOL ratio; division by zero; missing/NaN/inf/out-of-range latency and MLP; weakest-label rule; ranking; fallback estimator labels and values; harmonic-mean MLP; `measured` mode never substitutes values |
| `test_hotness_soar` | hotness ranking; FAST capacity limit; zero capacity; cold units stay SLOW; SOAR priority beats hotness for a high-AOL unit; SOAR refuses without any AOL; reasons/ranks; hotness rebalancing |
| `test_alto` | thresholds and dead band; promote/demote; cooldown stops ping-pong; confirmation ignores a single spike; capacity + swap margin; promotion order by stall priority; idle demotion; dry run |
| `test_tier_manager` | unit-id validation (path traversal); place/locate; no overwrite; migrate + history; error cases; applyPlacement + manifest; reset keeps non-unit files; atomic writes |
| `test_csv_json` | CSV escaping incl. formula injection; workload CSV file; unique file names; query stats; JSON parse/dump/errors; `.env` parsing; password hidden in descriptions; query-library parsing; 34-unit catalog; access tracker; safe percentage difference |
| `test_api_validation` | 404/405/400 handling; field validation; 503 for every DB endpoint when the DB is down; no password in responses; 409 before a workload; static-file traversal blocked; HTTP request parser |
| `test_db_connection_failure` | refused port → friendly message without password; queries when not connected; message mapping for wrong password / missing DB / refused |
| `test_db_workload` *(PostgreSQL)* | all 9 tables and row counts; each workload type's query/row counts; warm-up + repetitions; min ≤ avg ≤ max; deterministic parameters; SQL-injection attempt rejected by parameter binding; wrong password; read-only mode blocks writes |
| `test_end_to_end` *(PostgreSQL)* | full API demo in a temporary folder: test connection → tables → workload → simulate (34 files, FAST = capacity) → migrate (dry run) → placement → profiling → benchmark (both policies, identical query counts) → CSV export → migrations |

## 3. Results actually executed

Executed on **13 Oct 2026** in a Linux sandbox: Ubuntu 24.04, GCC 13.3, CMake 3.28, PostgreSQL 16.15 (local server, password auth), build type Release:

```
 1/9 test_aol ...................   Passed
 2/9 test_hotness_soar ..........   Passed
 3/9 test_alto ..................   Passed
 4/9 test_tier_manager ..........   Passed
 5/9 test_csv_json ..............   Passed
 6/9 test_api_validation ........   Passed
 7/9 test_db_connection_failure .   Passed
 8/9 test_db_workload ...........   Passed
 9/9 test_end_to_end ............   Passed
100% tests passed, 0 tests failed out of 9
```

With the database stopped (`MEMOPT_DB_PORT=1`) the two database tests were reported as **Skipped**, as intended.

Also executed there: `scripts/setup_linux.sh`, the CLI commands `check`, `workload`, `simulate`, `profile`, `benchmark`, and every dashboard tab in headless Chromium (desktop light theme and phone-width dark theme). The three `.ps1` scripts were syntax-checked with PowerShell 7.4 and `setup_windows.ps1` was executed under PowerShell on Linux against the same PostgreSQL server.

**Not yet executed:** the MSVC build on Windows, `build_windows.ps1` / `run_windows.ps1` on Windows, and `LinuxPerfProfiler` hardware counters (the sandbox VM exposes no hardware PMU events; only the software counters were measured). Run `build_windows.ps1 -Test` on your machine and record your own results here.

## 4. How to verify each feature by hand

| Feature | Steps | Expected |
|---|---|---|
| DB connection | `run_windows.ps1 check` | “Connected. PostgreSQL 16.x”, all 9 tables present, row counts 5000/2000/20000/20000 |
| Error handling | stop the PostgreSQL service, click *Test connection* | red message explaining the service is not running; no password shown |
| Workload | *Workload* → mixed → Run | 965 queries per repetition, 0 failed, per-query min/avg/max; new `results\workload_mixed_*.csv`; rows in `workload_runs`, `query_metrics` |
| Profiling | *Profiling* → Run | Windows: counters “unavailable” with reason; WSL2/Linux: measured counters + scope |
| AOL labels | *Profiling* tab, bottom table | latency **synthetic**, MLP **estimated**, AOL **synthetic** |
| `measured` mode | set `MEMOPT_METRIC_SOURCE=measured`, restart, run workload, simulate | AOL unavailable; simulate → HTTP 409 with an explanation |
| Placement | *Placement* → Simulate | 34 files across `fast_memory\` / `slow_memory\`, FAST = capacity (6); highlighted rows where policies disagree |
| Migration | change mode to *migrate*, simulate again (or run another workload first) | entries in *Migrations* with old/new tier, trigger AOL, reason; `migration_history` rows |
| Dry run | tick *dry run* | migrations listed as “dry run”; folders unchanged |
| Benchmark | *Benchmark* → Run | identical query counts for both policies; charts; `results\benchmark_*.csv`; `benchmark_results` rows |
| Export | *Export benchmark CSV* | browser downloads the newest CSV |
| Determinism | run the benchmark twice | simulated stall costs identical; real times differ slightly (noise) |

SQL check:

```sql
SELECT workload_type, COUNT(*), SUM(success_count) FROM workload_runs GROUP BY 1;
SELECT policy, total_workload_ms, migration_count, simulated_stall_cost FROM benchmark_results ORDER BY result_id DESC LIMIT 2;
```
