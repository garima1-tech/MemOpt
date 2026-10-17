# Setup guide

## 1. Windows (primary target)

### 1.1 Install the tools (one time)

| Tool | How | Check |
|---|---|---|
| PostgreSQL 14, 15, 16 or 17 | Installer from <https://www.postgresql.org/download/windows/>. Keep **PostgreSQL Server** and **Command Line Tools** ticked. Remember the password you give the `postgres` user. Port 5432. | `"C:\Program Files\PostgreSQL\16\bin\psql.exe" --version` |
| C++ compiler | **Visual Studio 2022 Build Tools** (free) → workload **Desktop development with C++** (MSVC + Windows SDK). `winget install Microsoft.VisualStudio.2022.BuildTools` then add the workload in the installer. | `cl` inside “Developer PowerShell for VS 2022” |
| CMake ≥ 3.16 | `winget install Kitware.CMake` (or installer, tick *Add to PATH*) | `cmake --version` |
| VS Code (optional) | Extensions: C/C++, CMake Tools, PowerShell (suggested automatically from `.vscode/extensions.json`) | |

MSVC is recommended because the PostgreSQL installer ships `libpq.lib` built for MSVC. MinGW is not supported by the scripts.

### 1.2 PostgreSQL client-library paths

CMake looks for `include\libpq-fe.h` and `lib\libpq.lib` in the newest `C:\Program Files\PostgreSQL\<version>` automatically. For another location:

```powershell
scripts\build_windows.ps1 -PgRoot "D:\Apps\PostgreSQL\16"
```

At run time `memopt.exe` needs `libpq.dll` and its helper DLLs (`libssl`, `libcrypto`, `libintl`, `libiconv`, `zlib`) from PostgreSQL's `bin` folder. `run_windows.ps1` and `build_windows.ps1 -Test` add that folder to `PATH` for you. If you start `memopt.exe` yourself:

```powershell
$env:PATH = "C:\Program Files\PostgreSQL\16\bin;" + $env:PATH
```

### 1.3 Create the database (no manual SQL)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
```

It finds `psql.exe`, checks/starts the `postgresql-x64-NN` service, asks for the password (hidden), creates `memopt_demo`, runs `workloads\schema.sql` and `workloads\sample_data.sql` (≈47,000 deterministic rows) and offers to create `.env`.

**Reset** the database at any time by running the same script again (schema.sql drops and recreates every table).

### 1.4 Credentials

`.env` (created by the script, or copy `.env.example`):

```
MEMOPT_DB_HOST=localhost
MEMOPT_DB_PORT=5432
MEMOPT_DB_NAME=memopt_demo
MEMOPT_DB_USER=postgres
MEMOPT_DB_PASSWORD=your-password
```

`.env` is in `.gitignore`. Environment variables with the same names override it. The password is never printed, logged, or returned by the API. A database other than `memopt_demo` is opened **read-only** unless you set `MEMOPT_DB_READONLY=0`.

### 1.5 Build, test, run

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1          # build
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Test    # build + ctest
powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1            # dashboard
powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1 check      # CLI check
```

In VS Code: **Terminal → Run Task** → *MemOpt: setup database / build / build + test / run dashboard*.

The dashboard is at **http://127.0.0.1:8080/** (localhost only). Use `-Port 8081` if 8080 is busy.

### 1.6 Export benchmark results

Dashboard → *Benchmark* → **Export benchmark CSV**, or open `results\benchmark_<timestamp>.csv`. Rows are also stored in the `benchmark_results` table.

## 2. Linux / WSL2 (optional, needed for hardware counters)

```bash
sudo apt update
sudo apt install postgresql postgresql-client libpq-dev cmake g++
sudo service postgresql start
sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'choose-a-password';"
./scripts/setup_linux.sh
./scripts/build_linux.sh --test
./build/memopt serve
```

Hardware counters: `perf_event_open` must be allowed. Check `cat /proc/sys/kernel/perf_event_paranoid`:

- `≤ 1`: counters for the PostgreSQL backend process can be read if MemOpt runs as the same user (or root).
- `2` (default on many distros): only user-space counters of processes you own. MemOpt then measures **its own client process** and says so in the “scope” field.
- Change temporarily: `sudo sysctl kernel.perf_event_paranoid=1`.

WSL2 exposes hardware counters on many recent Windows 11 + Intel/AMD systems, but not all. Virtual machines and containers often expose **no** hardware events; MemOpt then reports them as *unavailable* and still reports software counters (task clock, page faults, context switches).

To use the Windows PostgreSQL from WSL2, set `MEMOPT_DB_HOST` to the Windows host IP and allow the connection in `pg_hba.conf`; installing PostgreSQL inside WSL2 is simpler.

## 3. Troubleshooting

| Message | Fix |
|---|---|
| *PostgreSQL is not running or not listening on localhost:5432* | `services.msc` → start `postgresql-x64-16` (Windows) / `sudo service postgresql start` |
| *Wrong user name or password* | Fix `MEMOPT_DB_PASSWORD` in `.env` |
| *Database 'memopt_demo' does not exist yet* | Run the setup script |
| *Missing tables: …* | Run the setup script (it recreates the schema) |
| *libpq (PostgreSQL client library) not found* (CMake) | Pass `-PgRoot`, or install the *Command Line Tools* component |
| `memopt.exe` exits immediately / “libpq.dll was not found” | Start via `run_windows.ps1`, or add PostgreSQL `bin` to `PATH` |
| *cannot bind 127.0.0.1:8080* | Another program uses the port: `run_windows.ps1 -Port 8081` |
| PowerShell: *running scripts is disabled* | Use `powershell -ExecutionPolicy Bypass -File ...` as shown |
