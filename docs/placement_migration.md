# MemOpt – Placement and Migration Modules

This README explains the Placement and Migration modules of our MemOpt project. These modules help decide where each data unit should be placed and when it should be moved between the FAST and SLOW memory tiers.

---

## 1. Placement Module

### What is Placement?

The placement module decides which data units should be placed in the FAST tier and which ones should be placed in the SLOW tier.

In our project, we do not decide placement only by checking how frequently a data unit is accessed. We also consider its AOL value to understand how much memory waiting it may cause.

The main aim is to give higher priority to data units that have a greater estimated performance impact.

### Important Files

- `include/soar.h`
- `src/soar/soar.cpp`
- `include/policy_types.h`
- `src/hotness/hotness.cpp`

### 1.1 SOAR-Inspired Placement

Our project uses a simplified SOAR-inspired allocation approach.

Instead of considering only the access count, it calculates a priority score using:

**Priority = Access Count × AOL**

Here:

- **Access Count:** Number of times a data unit was accessed.
- **AOL:** Amortized Offcore Latency of that data unit.
- **Priority:** Estimated overall memory-stall contribution.

For example, a data unit that is accessed frequently but has a very low AOL may receive less priority than another data unit with fewer accesses but a much higher AOL.

This helps us identify data units that may have a greater effect on overall performance.

**How does the placement work?**

1. The system takes the data units and their access information.
2. It checks whether each data unit has a valid AOL value.
3. It calculates the priority score using access count multiplied by AOL.
4. All data units are ranked from highest to lowest priority.
5. The data units with the highest priority are selected for the FAST tier, depending on its available capacity.
6. The remaining data units are placed in the SLOW tier.

If no data unit has a usable AOL value, the SOAR-inspired allocator refuses to make a placement decision rather than guessing.

**Important:** This is a simplified, educational implementation inspired by SOAR. It is not the complete algorithm from the original research paper.

### 1.2 Hotness-Based Placement

Our project also includes a hotness-based approach so that we can compare placement based on access frequency with placement based on AOL.

In this approach, the system considers only the number of accesses made to each data unit.

**How does it work?**

1. The system counts the accesses for every data unit.
2. It sorts the data units according to their access counts.
3. The most frequently accessed units are given the highest priority.
4. These units are placed in the FAST tier until its capacity is reached.
5. The remaining units are placed in the SLOW tier.
6. Data units with zero accesses are treated as cold and placed in the SLOW tier.

This approach gives us a baseline to compare with the AOL-based placement strategy.

### Difference Between the Two Approaches

- **Hotness-based placement:** Considers only how frequently a data unit is accessed.
- **SOAR-inspired placement:** Considers both access frequency and AOL to estimate the overall memory-stall contribution.

This comparison helps demonstrate why access frequency alone may not always be enough to make good memory-placement decisions.

---

## 2. Migration Module

### What is Migration?

Placement decides the initial location of a data unit, whereas migration handles changes in its location over time.

The access pattern of a data unit can change while a workload is running. A data unit that was previously accessed frequently may become less important, while another data unit may start causing more memory waiting.

Migration helps the system adjust to these changes by moving data units between the FAST and SLOW tiers.

### Important Files

- `include/alto.h`
- `src/alto/alto.cpp`
- `include/tier_manager.h`
- `src/tiering/tier_manager.cpp`

### 2.1 ALTO-Inspired Migration Controller

Our project uses a simplified ALTO-inspired migration controller.

It checks the AOL values of data units after each epoch and decides whether they should move between the FAST and SLOW tiers.

An epoch represents one review cycle of the workload.

The controller uses two AOL thresholds:

- **High AOL threshold:** 7
- **Low AOL threshold:** 3

These are prototype settings used in our implementation, not values taken directly from the original research paper.

**How does migration work?**

1. After an epoch, the system checks the updated AOL values of the data units.
2. If a data unit has an AOL greater than 7, it becomes a candidate for promotion to the FAST tier.
3. If its AOL is less than 3, it becomes a candidate for demotion to the SLOW tier.
4. If its AOL is between these two thresholds, the system keeps it in its current tier.
5. Before performing a migration, the controller checks the confirmation and cooldown conditions.
6. If the conditions are satisfied, it records the migration decision.

The gap between the high and low thresholds is called a dead band or hysteresis range. It helps avoid unnecessary movement when AOL values are close to the thresholds.

### 2.2 Preventing Unnecessary Migrations

Moving data units too frequently can create unnecessary overhead. To avoid this, our migration controller uses a few additional conditions.

**1. Confirmation Epochs**

The system checks whether the same migration preference continues for the required number of epochs before performing a move.

This prevents migration decisions based on temporary changes.

**2. Cooldown Period**

After a data unit has been moved, it cannot be moved again immediately.

Our default configuration uses a cooldown of one epoch.

**3. Swap Margin**

Sometimes the FAST tier is already full when a new data unit needs to be promoted.

In this situation, the controller identifies the FAST-tier data unit with the lowest stall priority.

It replaces that unit only if the new candidate has a sufficiently higher priority.

The default swap margin is 25%. This helps avoid unnecessary swapping between data units with almost similar priority values.

**4. Demotion of Idle Units**

If a data unit is currently in the FAST tier but receives no accesses during the current epoch, the controller can consider moving it to the SLOW tier.

### 2.3 Dry Run Mode

The migration controller also supports a dry-run mode.

In this mode, the system generates migration decisions and records them without changing the actual placement.

This is useful for testing and observing what the migration controller would do before applying the changes.

---

## 3. Tier Manager

### What does the Tier Manager do?

The Tier Manager handles the actual placement and movement of the data-unit placeholder files between the FAST and SLOW tier folders.

It is responsible for creating tier folders, placing units, migrating units, checking their current location, and maintaining the placement manifest.

### Important File

- `include/tier_manager.h`
- `src/tiering/tier_manager.cpp`

### How are the FAST and SLOW tiers represented?

Our current implementation represents the two tiers using separate folders:

- `fast_memory/`
- `slow_memory/`

Each data unit is represented by a `.unit` placeholder file.

For example:

- `fast_memory/orders_s01.unit`
- `slow_memory/orders_s02.unit`

The location of the file tells us which logical tier the data unit belongs to.

### Main Operations

**1. Initialization**

The `init()` function creates the FAST and SLOW folders if they do not already exist.

**2. Placement**

The `place()` function creates a placeholder file for a data unit in the selected tier.

It also checks whether the unit already exists so that an existing file is not accidentally overwritten.

**3. Migration**

The `migrate()` function moves a unit from one tier folder to another using a file rename operation.

It checks that the source exists and that the destination does not already contain the same unit.

It also adds a migration history line to the moved placeholder file.

**4. Apply Placement**

The `applyPlacement()` function compares the current placement with the target placement.

It performs demotions first, followed by promotions and the placement of new units. It also counts how many existing units were migrated.

**5. Locate**

The `locate()` function checks the folders and identifies whether a data unit is currently in FAST or SLOW.

**6. Manifest**

The `saveManifest()` function saves the current placement information in `tier_manifest.csv`.

The manifest contains:

- Unit ID
- Tier name
- Time when the manifest was saved

**7. Reset**

The `reset()` function removes `.unit` placeholder files from both tier folders and updates the manifest.

---

## 4. How Placement and Migration Work Together

Both modules work together to manage the logical location of data units.

The overall process is:

1. The profiling and metric modules collect or prepare the required performance information.
2. The AOL module calculates the AOL value for each data unit.
3. The placement module ranks the data units according to their estimated performance impact.
4. Based on the ranking and FAST-tier capacity, the system decides which units should be placed in FAST and SLOW.
5. The Tier Manager creates the corresponding placeholder files.
6. After every epoch, the migration controller checks whether the data units still belong in their current tiers.
7. If the migration conditions are satisfied, the controller generates migration decisions.
8. The Tier Manager can then apply the updated placement.
9. The updated tier information is saved in the manifest.

---

## 5. Important Note About Our Implementation

Our current MemOpt project uses a logical simulation of FAST and SLOW memory tiers.

Both `fast_memory` and `slow_memory` are folders located on the same disk. The `.unit` files are small placeholder files used to represent the location of data units.

Moving a file between these folders demonstrates the placement and migration decisions, but it does not physically move data between different memory hardware tiers.

It also does not move PostgreSQL's internal buffer pages.

The main purpose of this implementation is to demonstrate how AOL-based placement, migration policies, tier capacity, and migration history can work together in a memory-management system.

**The SOAR-inspired allocator and ALTO-inspired migration controller are simplified versions created for our project. They demonstrate the main concepts but do not implement the complete algorithms from the original research papers.**