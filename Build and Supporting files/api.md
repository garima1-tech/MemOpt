# REST API

Base URL: `http://127.0.0.1:8080`. JSON in, JSON out. Errors: `{"ok": false, "error": "..."}`.

| Status | Meaning |
|---|---|
| 200 | OK |
| 400 | invalid JSON or invalid field |
| 404 | unknown endpoint / nothing recorded yet |
| 405 | wrong HTTP method |
| 409 | prerequisite missing (e.g. placement before any workload, or no AOL available) |
| 413 | body larger than 64 KB |
| 503 | PostgreSQL unreachable or tables missing (message explains how to fix) |

There is no endpoint that runs user-supplied SQL. The database password is never returned.

| Method | Path | Body (all fields optional) | Returns |
|---|---|---|---|
| GET | `/api/health` | – | service, version, profiler note |
| GET | `/api/database/status` | – | connected, host, port, database, user, `password_set`, row total, latest workload/benchmark, warnings |
| POST | `/api/database/test` | – | forces a reconnect; 200 or 503 |
| GET | `/api/database/tables` | – | each required table: exists, rows, expected rows |
| POST | `/api/workload/run` | `workload`: sequential\|random\|popular\|mixed (default mixed), `repetitions` 1–20 (3), `warmup` 0–5 (1) | per-query executions, total/avg/min/max ms, rows, success/failure |
| GET | `/api/workload/latest` | – | same as above, or 404 |
| POST | `/api/profiling/run` | `workload`, `repetitions` 1–20 (2) | profiler name, scope (which process), counters with value/unit/source/kind/status, measured query duration |
| GET | `/api/profiling/latest` | – | same, or 404 |
| GET | `/api/placement` | – | every data unit: accesses by pattern, latency/MLP metrics, AOL, hotness rank, AOL rank, MemOpt rank, current folder tier, proposals + reasons |
| POST | `/api/placement/simulate` | `policy`: both\|memopt\|hotness, `mode`: auto\|allocate\|migrate, `dry_run`: bool | summary + decisions. `auto` allocates when the folders are empty, otherwise runs the ALTO-inspired review |
| GET | `/api/migrations` | – | live migrations + latest benchmark migrations (old/new tier, trigger, value, reason, time) |
| POST | `/api/benchmark/run` | `epochs` 2–10 (4), `repetitions` 1–10 (3), `warmup` 0–5 (1) | both policies: times, success/failure, FAST/SLOW, migrations, simulated stall cost, epoch series, notes, CSV path |
| GET | `/api/benchmark/latest` | – | same, or 404 |
| GET | `/api/results/export?type=benchmark\|workload\|placement\|migrations` | – | newest CSV of that type (`text/csv`, download) |
| POST | `/api/tiers/reset` | – | removes `.unit` files from both tier folders |
| GET | `/api/config` | – | non-secret settings and model assumptions |

Examples (PowerShell):

```powershell
Invoke-RestMethod http://127.0.0.1:8080/api/health
Invoke-RestMethod -Method Post http://127.0.0.1:8080/api/workload/run -ContentType application/json -Body '{"workload":"random","repetitions":2}'
Invoke-WebRequest "http://127.0.0.1:8080/api/results/export?type=benchmark" -OutFile bench.csv
```

curl (Linux/WSL):

```bash
curl -s -X POST localhost:8080/api/benchmark/run -H 'Content-Type: application/json' -d '{"epochs":4,"repetitions":3}'
```

The server is single-threaded: a long request (benchmark) blocks others until it finishes. That is fine for a one-person demo.
