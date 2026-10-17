# Final demo and viva notes

## 10-minute demo script

**Before the session:** PostgreSQL service running, `scripts\setup_windows.ps1` done (or re-run it to reset), build done, `scripts\run_windows.ps1 reset-tiers`.

1. **Start** – `scripts\run_windows.ps1`. Browser opens on *Overview*. Point to the green “connected” pill and the label legend (measured / estimated / synthetic / unavailable / simulated). *(1 min)*
2. **Database** – *Test connection*, then *Verify tables*: 4 demo tables with 47,000 rows, 5 result tables. Mention `.env` and that the password is never sent to the browser. *(1 min)*
3. **Workload** – mixed, 3 repetitions, 1 warm-up → *Run*. 2,895 real queries, 0 failures, per-query min/avg/max. Say: “these are client round-trip times, not memory latency.” *(1.5 min)*
4. **Placement** – *Simulate placement* (both). Show the scatter: sequential scan units are far right (hottest) but low AOL; random-lookup units are top. Hotness picks `orders_s00`, `transactions_s00`…; MemOpt picks `orders_s08`, `users_s02`, `products_s0x`…. Open `fast_memory\` in Explorer to show the six `.unit` files. *(2 min)*
5. **Migration** – Workload tab: run *random* (different access mix), back to Placement, mode *migrate* → Simulate. *Migrations* tab shows old/new tier, AOL trigger, reason. Mention cooldown + dead band against ping-pong. *(1.5 min)*
6. **Profiling** – run it; on Windows the hardware counters say *unavailable* and why; explain WSL2/Linux option. *(1 min)*
7. **Benchmark** – 4 epochs × 3 reps → Run. Point out: identical query counts; time difference is noise; stall cost is a model biased towards MemOpt; migrations count. *Export benchmark CSV*. *(2 min)*

## Short viva explanation (≈1 minute)

> Classic tiered-memory systems keep the *hottest* data in fast memory. The OSDI 2025 paper “Tiered Memory Management Beyond Hotness” argues that hotness is the wrong signal: what matters is how much each access **stalls** the CPU. A sequential scan is hot, but the CPU issues many misses in parallel (high memory-level parallelism), so slow memory barely hurts it. Random pointer-chasing lookups have low MLP, so each miss stalls fully. The paper captures this with a metric called AOL.
>
> MemOpt demonstrates the idea on a real PostgreSQL workload. We split four tables into 34 data units, run real SQL, count accesses per unit by pattern, compute AOL = latency / MLP, and compare two policies: hotness (top-N by access count) and MemOpt (top-N by accesses × AOL, then an AOL-threshold migration controller with hysteresis). Placement is simulated with two folders, and every number is labelled as measured, estimated, synthetic or simulated, because a laptop cannot measure per-page latency or move pages between real memory tiers.

## Likely viva questions

| Question | Answer |
|---|---|
| What is MLP? | Memory-level parallelism: how many cache misses are outstanding at the same time. With MLP 10, ten misses overlap, so each costs about 1/10 of the latency in stall time. |
| Why AOL = latency / MLP? | It approximates the stall time per access. High AOL → each access hurts → put it in FAST. |
| Is your AOL measured? | No. In fallback mode latency is an assumed 100 ns ± 10 % (synthetic) and MLP is estimated from the real access-pattern mix. In measured mode, without PEBS/IBS support, AOL is reported unavailable and placement refuses. |
| Why not use query time as latency? | Query time includes parsing, planning, execution, IPC and result transfer. It is milliseconds; memory latency is ~100 nanoseconds. Mixing them would be wrong. |
| Does MemOpt make PostgreSQL faster? | No. Both policies run the same SQL on the same database; placement is simulated. The benchmark shows policy behaviour and a model cost, not a speed-up. |
| Why is the stall-cost comparison fair/unfair? | It is biased: it uses MemOpt's own AOL estimates. It shows that *if* AOL is right, ranking by accesses × AOL reduces modelled stall time compared with hotness. |
| What do SOAR and ALTO do in your version? | SOAR-inspired: initial top-N by accesses × AOL under a capacity limit. ALTO-inspired: each epoch, AOL > 7 → FAST, AOL < 3 → SLOW, otherwise keep; confirm, cooldown and swap margin stop oscillation. |
| How do you avoid ping-pong migrations? | Dead band (3–7), confirmation epochs, cooldown epochs, and a +25 % priority margin before evicting a FAST unit. Tested in `test_alto.cpp`. |
| How is the data reproducible? | `setseed(0.42)` in SQL; workload parameters from `mt19937` with a portable mapping and fixed seed. Simulated stall costs are identical between runs. |
| How do you prevent SQL injection? | Only fixed queries from `workload.sql`; every value is a bound parameter through `PQexecParams`. A test sends `1; DROP TABLE users` and checks the table is still there. |
| What would make this real? | Linux with PEBS/IBS sampling for per-page latency, offcore events for MLP, NUMA nodes (or CXL) as the two tiers, and `move_pages()` for migration (see phase3_plan.md). |

## Future improvements (summary)

See `docs/phase3_plan.md`: PEBS/IBS sampling, NUMA-node emulation of slow memory with `numactl`, real `move_pages` migration of a custom in-memory store, per-page units, multi-threaded server, statistical significance (confidence intervals) for timings, more workloads (TPC-C-like), and automatic threshold tuning.
