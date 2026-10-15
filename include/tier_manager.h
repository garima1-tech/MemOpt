// tier_manager.h - Logical FAST / SLOW tiers backed by two folders.
//
//   fast_memory/<unit_id>.unit   -> unit is in the logical FAST tier
//   slow_memory/<unit_id>.unit   -> unit is in the logical SLOW tier
//   tier_manifest.csv            -> unit, tier, last_changed
//
// THIS IS A SIMULATION. Both folders live on the same disk; moving a file
// between them does not change any memory hardware, and it does not move
// PostgreSQL's internal buffer pages. The files are small metadata
// placeholders that make the policy decisions visible and auditable.
#pragma once

#include <string>
#include <vector>

#include "common.h"
#include "policy_types.h"

namespace memopt {

class TierManager {
public:
    explicit TierManager(std::string root);

    // Creates the folders if needed. Safe to call repeatedly.
    bool init(std::string& error);

    // Only [A-Za-z0-9_-], 1..40 chars: blocks path traversal like "../x".
    static bool validUnitId(const std::string& id);

    // Creates the placeholder file in `tier`. Fails if the unit already
    // exists in either tier (use migrate) - never silently overwrites.
    bool place(const std::string& unit_id, Tier tier, std::string& error);

    // Moves the placeholder from one tier to the other (atomic rename).
    // Fails if the source is missing or the destination already exists.
    bool migrate(const std::string& unit_id, Tier from, Tier to, std::string& error);

    // Makes the folders match `target` (place missing units, migrate
    // others). `moved` = number of migrations performed.
    bool applyPlacement(const Placement& target, int& moved, std::string& error);

    // Where is the unit right now (reads the file system)?
    Tier locate(const std::string& unit_id) const;
    Placement current() const;

    // Removes only *.unit files from both tier folders.
    bool reset(std::string& error);

    bool saveManifest(std::string& error) const;

    std::string dirFor(Tier t) const;
    std::string manifestPath() const;
    const std::string& root() const { return root_; }

private:
    std::string root_;
    std::string fileFor(const std::string& unit_id, Tier t) const;
};

// Writes to "<path>.tmp" then renames over <path> (atomic on one volume).
bool writeFileAtomic(const std::string& path, const std::string& content, std::string& error);

}  // namespace memopt
