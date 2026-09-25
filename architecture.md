# MemOpt System Architecture

## Overview

MemOpt is a system-level prototype for tiered memory management beyond simple memory hotness. The prototype uses memory latency and Memory-Level Parallelism (MLP) to calculate an AOL value and compare tiering decisions with a traditional hotness-based policy.

## Main Components

1. **Memory Profiling Module**
   - Collects or represents memory latency, MLP and access information.
   - The current prototype uses sample profiling values.

2. **AOL Calculation**
   - Combines latency and MLP into an AOL value for the prototype.
   - The current student implementation uses a simplified calculation and should be treated as a prototype rather than an exact reproduction of the research implementation.

3. **Tiering Policies**
   - Hotness-based policy uses access frequency.
   - AOL-based policy uses the calculated AOL value.

4. **Tier Manager**
   - Maintains the selected memory tier for pages.
   - DRAM is represented as the fast tier and a slower/remote tier as the slow tier.

5. **Migration Controller**
   - Tracks software-level page migration decisions in the prototype.
   - Actual CXL hardware migration is not assumed.

6. **PostgreSQL**
   - Stores performance metrics, AOL values, tier decisions and migration statistics.
   - It also supports the database workload used for benchmarking.

7. **Frontend**
   - HTML, CSS and JavaScript dashboard.
   - Displays profiling metrics, tiering decisions and migration statistics.

## Data Flow

```text
Frontend
   |
C++ Backend
   |
PostgreSQL Database Workload
   |
Memory Profiling
   |
Latency + MLP
   |
AOL Calculation
   |
Tiering Decision
   |
Fast Tier / Slow Tier
   |
Migration Statistics
   |
Frontend Dashboard
```

## Technologies

- C++
- PostgreSQL
- Linux perf/PMU
- HTML/CSS/JavaScript
- Git/GitHub
