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

## TierManager

TierManager simulates two memory tiers using two folders:
- fast_memory/ is the logical FAST tier
- slow_memory/ is the logical SLOW tier

Each data unit is a small placeholder file <unit_id>.unit. This is a simulation: both folders are on the same disk, so no real memory hardware changes.

### place() and unit id validation

`place()` creates the `.unit` file in the chosen tier. It refuses to overwrite a unit that already exists in either tier (use `migrate()` instead).

Unit ids are validated: only letters, numbers, `_` and `-` are allowed, with length 1 to 40. This blocks path traversal such as `../x`.

### migrate()

migrate() moves a unit between tiers with a file rename. It fails if the source file is missing or if the destination already exists, so nothing is overwritten silently. After a successful move, a migrated=FROM->TO history line is added inside the file.

### applyPlacement()

applyPlacement() makes the folders match a target placement. It does two passes: first it handles units that should be in SLOW (this frees space in FAST), then units that should be in FAST. Units that do not exist yet are placed, and units in the wrong tier are migrated. The moved counter counts only migrations, not new placements.
