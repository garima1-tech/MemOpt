## Example Output Files 

The following two CSV files were generated during a sample MemOpt run in a Linux environment. The test was performed on 13 October 2026 using Ubuntu 24.04, GCC 13, and PostgreSQL 16.15, without hardware PMU support.

These files are provided only to show how the output of the project looks. They are not the results of your own system.

- `EXAMPLE_benchmark_linux_sandbox.csv` – Generated using the `memopt benchmark` command with 4 epochs, 3 repetitions, and a FAST tier capacity of 6.
- `EXAMPLE_workload_mixed_linux_sandbox.csv` – Generated using the `memopt workload mixed --reps 3` command.

The query execution times may vary depending on the system and its configuration, so your results may be different.

The simulated stall-cost values are deterministic for the same dataset and settings. When using PostgreSQL 15 or a newer version with the same `random()` generator, the demo data will usually produce similar values:

- Hotness-based approach: `318,779`
- MemOpt approach: `311,403`

However, these values may change when using a different PostgreSQL version because the generated demo data can be different.

**Important:** These are only sample results used to explain the CSV output format. Do not include them in your project report as your own experimental results. Run MemOpt on your own system and generate your own CSV files inside the `results/` folder.