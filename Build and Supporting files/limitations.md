# Limitations and research integrity

MemOpt is **inspired by** *“Tiered Memory Management Beyond Hotness”* (OSDI 2025). It borrows the paper's central idea — place data by how much its accesses actually **stall** the program, not only by how often it is accessed — and implements a small, honest, educational version of it. It does **not** reproduce the paper's system or results.

## 1. What this prototype does not do

1. **The AOL formula is simplified.** MemOpt uses `AOL = latency / MLP` for each data unit. The paper derives its metric from hardware performance events; as far as we know it expands AOL as *amortized offcore latency*. The project brief called it “Access On Latency”; we keep the abbreviation and use only the simple ratio.
2. **SOAR-inspired and ALTO-inspired modules are approximations.** Allocation ranks by `accesses × AOL`; migration uses fixed AOL thresholds (7 / 3), a dead band, confirmation, cooldown, and a swap margin. All numbers are illustrative prototype settings, not values from the paper, and need experimentation.
3. **The paper works at the system level** (kernel page tables, hardware sampling, real fast/slow memory, real page migration). MemOpt works at the application level on logical units.
4. **Hardware profiling depends on the processor and OS.** `LinuxPerfProfiler` reads generic counters through `perf_event_open` on Linux/WSL2. Many VMs, containers and Windows itself expose none; MemOpt then reports them as *unavailable*. Intel PEBS load-latency sampling (or AMD IBS) and offcore outstanding-request events — what you would need for real per-access latency and MLP — depend on the CPU model, kernel, drivers, permissions and event availability, and are **not implemented**.
5. **PostgreSQL query time is not memory latency.** A query's client-side time includes IPC/network, parsing, planning, executor work, buffer-cache hits, locks and result transfer. MemOpt never substitutes it for memory latency.
6. **MLP cannot be inferred reliably from query timing.** In `fallback` mode MLP is *estimated* from the measured access pattern using assumed per-pattern values (48 / 9 / 20). These are assumptions, labelled as such.
7. **FAST/SLOW folders are a simulation.** `fast_memory/` and `slow_memory/` are two folders on one disk. They have no DRAM-vs-CXL performance difference. Moving a `.unit` file does not move PostgreSQL's buffer pages or any memory.
8. **No CXL hardware or kernel page migration is used or required.**
9. **Results must come from real runs.** Query timings, row counts and access counts in the CSV files are real measurements from your machine. The “simulated stall cost” is a model output. The files in `docs/examples/` are examples from one sandbox run, labelled as such — do not present them as your results.

## 2. Things to say carefully in the report

- The benchmark runs **the same SQL** for both policies. Differences in measured query time between “hotness” and “MemOpt” are run-to-run noise (we have seen −9 % and +6 % on consecutive runs). They are **not** a speed-up.
- The simulated stall cost uses the **same AOL estimates** that MemOpt ranks by, so it is biased in MemOpt's favour. It demonstrates the policy logic: *if* AOL were measured correctly, ranking by `accesses × AOL` would reduce modelled stall time.
- The measured hotness/AOL disagreement (sequential scans are the hottest units but have the lowest AOL) is a property of the workload design plus the MLP assumptions.
- Data units are fixed key ranges; real systems use 4 KB pages or huge pages.
- The single-threaded HTTP server and the Windows build were designed for a local demo, not production.

## 3. Paper vs. this project

| Aspect | Research paper (OSDI 2025) | MemOpt (this project) |
|---|---|---|
| Goal | Reduce slowdown on tiered memory (e.g. DRAM + CXL) | Demonstrate the “beyond hotness” idea end-to-end |
| Unit of placement | Memory pages / objects in a real system | 34 logical key-range units over PostgreSQL tables |
| Metric | AOL derived from hardware events | `latency / MLP`; latency synthetic, MLP estimated (or unavailable) |
| Profiling | Hardware sampling on supported CPUs | Generic `perf_event_open` counters where available; fallback otherwise |
| Initial placement | SOAR | SOAR-*inspired*: rank by accesses × AOL, capacity-limited |
| Runtime adaptation | ALTO | ALTO-*inspired*: thresholds + dead band + confirm + cooldown + swap margin |
| Migration | Real page migration between memory tiers | Renaming placeholder files between two folders |
| Hardware | Real fast and slow memory tiers | None needed |
| Evaluation | Application slowdown on real hardware | Real SQL timings + simulated stall-cost model |

## 4. Scope boundary: OS memory management

Real OS operations (`mbind`, `move_pages`, NUMA nodes, `numactl`, kernel tiering) are **out of scope** for the initial project. See `docs/phase3_plan.md` for how they could be added and verified.
