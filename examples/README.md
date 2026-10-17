# EXAMPLE output — not your results

These two CSV files were produced by one run of MemOpt in a Linux sandbox (Ubuntu 24.04, GCC 13, PostgreSQL 16.15, no hardware PMU) on 13 Oct 2026. They show the file format only.

- `EXAMPLE_benchmark_linux_sandbox.csv` — `memopt benchmark` (4 epochs × 3 repetitions, FAST capacity 6)
- `EXAMPLE_workload_mixed_linux_sandbox.csv` — `memopt workload mixed --reps 3`

Query times depend on your machine and will differ. The simulated stall costs are deterministic for a given dataset and settings. With PostgreSQL 15 or newer (same `random()` generator) your run will usually reproduce them (318,779 hotness / 311,403 MemOpt); a different PostgreSQL version can generate different demo data and therefore different numbers. Do not put these numbers in your report as your own measurements; generate your own files in `results/`.
