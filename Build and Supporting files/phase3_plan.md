# Phase 3 plan and future improvements

The current project (phases 1–2) is complete and runnable: real PostgreSQL workloads, labelled metrics, AOL, hotness vs SOAR/ALTO-inspired policies, simulated tiers, benchmark, dashboard, tests. Phase 3 moves from *simulated* toward *measured*. Each item says how to verify it so nothing is claimed without evidence.

## A. Better measurements (P1)

1. **Load-latency sampling** – on Intel, `perf_event_open` with the `mem-loads` / `MEM_TRANS_RETIRED.LOAD_LATENCY` event and `PERF_SAMPLE_ADDR | PERF_SAMPLE_WEIGHT` (PEBS); on AMD, IBS op sampling. Map sampled addresses to data units → *measured* per-unit latency. *Verify:* `perf mem record` on the same machine gives similar averages.
2. **MLP from offcore events** – `OFFCORE_REQUESTS_OUTSTANDING.*` (cycles with ≥1 outstanding / total outstanding) to estimate MLP per interval. *Verify:* microbenchmarks with known pointer-chasing (MLP≈1) vs streaming (high MLP).
3. **Profile the server process** – run MemOpt as the `postgres` user or with `CAP_PERFMON` so counters attach to the backend PID.
4. **Statistics** – repeat benchmarks ≥ 10 times, report mean, standard deviation and 95 % confidence intervals; only claim differences outside the interval.

## B. Real tiers (P2)

5. **NUMA emulation of slow memory** – on a two-socket machine (or a VM with two NUMA nodes), treat the remote node as SLOW. Allocate a custom in-memory key-value store (not PostgreSQL) with `numa_alloc_onnode`.
6. **Real migration** – move pages with `move_pages(2)` / `mbind(2)`; record success per page. *Verify:* `/proc/<pid>/numa_maps` before and after.
7. **Page-granularity units** – 4 KB pages instead of key ranges; compare with huge pages.
8. **Kernel tiering comparison** – compare against Linux's built-in NUMA balancing / memory tiering (`demotion_enabled`) as an additional baseline.

## C. Workloads and database (P3)

9. TPC-C / YCSB-like workloads with skew changes over time.
10. Use `pg_stat_statements` and `EXPLAIN (ANALYZE, BUFFERS)` to relate query plans to access patterns.
11. Optional read-only analysis of a user's own database (already supported read-only by default).

## D. Platform and UX (P4)

12. Multi-threaded HTTP server with progress streaming (Server-Sent Events) for long benchmarks.
13. Automatic threshold tuning: grid search over `aol_high`, `aol_low`, `cooldown`, `swap_margin`, reported with the stall-cost model *and* (after B) measured slowdown.
14. History page that reads `benchmark_results` from PostgreSQL across sessions.
15. CI (GitHub Actions) building on Windows (MSVC) and Ubuntu and running `ctest` with a PostgreSQL service container.

## Verification rule for phase 3

A metric may be labelled **measured** only when it comes from a counter or sample on the machine where the experiment ran, the event name and tool are recorded in the CSV, and the raw output is kept in `results/`.
