# MemOpt — Intelligent Tiered Memory Management Beyond Hotness

A third-year B.Tech systems project. MemOpt runs **real SQL workloads on PostgreSQL**, measures them, scores every *data unit* with a simplified **AOL = latency / MLP** model, decides which units belong in a logical **FAST** or **SLOW** memory tier, and compares that against a classic **hotness (access-count) baseline** in a web dashboard.

It is inspired by *“Tiered Memory Management Beyond Hotness”* (OSDI 2025). It is an educational prototype, **not** a reproduction of the paper — see [docs/limitations.md](docs/limitations.md).

## What is real and what is simulated

| Thing | Status |
|---|---|
| SQL queries, query timings, rows returned, success/failure | **Measured** (real PostgreSQL, `steady_clock`) |
| Access counts per data unit | **Measured** (derived from the real queries' key ranges and returned keys) |
| CPU counters (cycles, instructions, cache misses, page faults…) | **Measured** via `perf_event_open` on Linux/WSL2 when available; otherwise **unavailable** |
| Per-unit memory latency | **Synthetic** (assumed 100 ns ± 10 %) in `fallback` mode; **unavailable** in `measured` mode |
| Per-unit MLP | **Estimated** from the measured access pattern in `fallback` mode; **unavailable** in `measured` mode |
| FAST/SLOW placement, migrations | **Simulated** (two folders on one disk) |
| “Stall cost” in the benchmark | **Simulated** model output |

Query duration is never used as memory latency. Every value in the API and UI carries one of these labels.

## Quick start (Windows + VS Code)

Prerequisites (one time): PostgreSQL 14+ (installer from postgresql.org, keep *Command Line Tools*), **Visual Studio 2022 Build Tools** with *Desktop development with C++*, **CMake 3.16+**. Details: [docs/setup.md](docs/setup.md).

Open **PowerShell** in the project folder:

```powershell
# 1. Create + load the database (asks for the postgres password, writes .env)
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1

# 2. Build and run all tests
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Test

# 3. Start the dashboard (opens http://127.0.0.1:8080/)
powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1
```

Command-line use (no browser):

```powershell
scripts\run_windows.ps1 check                       # connection + tables
scripts\run_windows.ps1 workload mixed --reps 3     # real SQL workload
scripts\run_windows.ps1 simulate                    # hotness vs MemOpt placement
scripts\run_windows.ps1 benchmark --epochs 4 --reps 3
scripts\run_windows.ps1 profile random              # counters (Linux/WSL2)
```

## Quick start (Linux / WSL2)

```bash
sudo apt install postgresql postgresql-client libpq-dev cmake g++
sudo service postgresql start
sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'choose-a-password';"
./scripts/setup_linux.sh
./scripts/build_linux.sh --test
./build/memopt serve        # open http://127.0.0.1:8080/
```

## Project layout

```
MemOpt/
├─ CMakeLists.txt          build: memopt_core library, memopt.exe, 9 test programs
├─ .env.example            configuration template (copy to .env, never commit .env)
├─ include/                public headers, one per module
├─ src/
│  ├─ main.cpp             CLI: serve | check | workload | profile | simulate | benchmark
│  ├─ common/              types, config (.env), JSON
│  ├─ dbms/                libpq wrapper, dataset/data units, workload runner
│  ├─ profiling/           IProfiler, FallbackProfiler, LinuxPerfProfiler, unit metric estimator
│  ├─ aol/                 AOL calculator + validation + ranking
│  ├─ hotness/             access-count baseline
│  ├─ soar/                SOAR-inspired initial allocation
│  ├─ alto/                ALTO-inspired migration controller
│  ├─ tiering/             TierManager (fast_memory/ + slow_memory/ + manifest)
│  ├─ benchmark/           hotness vs MemOpt over workload epochs
│  ├─ results/             CSV export + PostgreSQL persistence
│  └─ api/                 HTTP server, REST API, shared app state
├─ workloads/              schema.sql, sample_data.sql, workload.sql (query library)
├─ frontend/               index.html, style.css, script.js (no external libraries)
├─ fast_memory/ slow_memory/   simulated tiers (one .unit placeholder per data unit)
├─ results/                timestamped CSV output
├─ scripts/                setup/build/run for Windows (.ps1) and Linux (.sh)
├─ tests/                  unit, integration and end-to-end tests
└─ docs/                   architecture, setup, API, testing, limitations, team, demo/viva, plan
```

## Documentation

- [docs/setup.md](docs/setup.md) – installation, database init/reset, troubleshooting
- [docs/architecture.md](docs/architecture.md) – diagram, data units, AOL, SOAR/ALTO logic
- [docs/api.md](docs/api.md) – REST endpoints
- [docs/testing.md](docs/testing.md) – test list and how to verify each feature
- [docs/limitations.md](docs/limitations.md) – research integrity, paper vs this project
- [docs/demo_and_viva.md](docs/demo_and_viva.md) – 10-minute demo script and viva answers
- [docs/team.md](docs/team.md) – four-person split, Git branches, commit messages
- [docs/phase3_plan.md](docs/phase3_plan.md) – future improvements
- [docs/examples/](docs/examples/) – **example** CSV output (labelled; from one sandbox run)
