#include "tier_manager.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace memopt {

bool writeFileAtomic(const std::string& path, const std::string& content, std::string& error) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "cannot write temporary file " + tmp;
            return false;
        }
        f << content;
        f.flush();
        if (!f) {
            error = "write failed for " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);  // replaces the old file atomically
    if (ec) {
        error = "cannot rename " + tmp + " -> " + path + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

TierManager::TierManager(std::string root) : root_(std::move(root)) {}

std::string TierManager::dirFor(Tier t) const {
    return (fs::path(root_) / (t == Tier::FAST ? "fast_memory" : "slow_memory")).string();
}

std::string TierManager::manifestPath() const { return (fs::path(root_) / "tier_manifest.csv").string(); }

std::string TierManager::fileFor(const std::string& unit_id, Tier t) const {
    return (fs::path(dirFor(t)) / (unit_id + ".unit")).string();
}

bool TierManager::init(std::string& error) {
    std::error_code ec;
    for (Tier t : {Tier::FAST, Tier::SLOW}) {
        fs::create_directories(dirFor(t), ec);
        if (ec) {
            error = "cannot create tier folder " + dirFor(t) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

bool TierManager::validUnitId(const std::string& id) {
    if (id.empty() || id.size() > 40) return false;
    for (unsigned char c : id)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

Tier TierManager::locate(const std::string& unit_id) const {
    if (!validUnitId(unit_id)) return Tier::NONE;
    std::error_code ec;
    bool f = fs::exists(fileFor(unit_id, Tier::FAST), ec);
    bool s = fs::exists(fileFor(unit_id, Tier::SLOW), ec);
    if (f && !s) return Tier::FAST;
    if (s && !f) return Tier::SLOW;
    return Tier::NONE;  // missing (or inconsistent: in both)
}

bool TierManager::place(const std::string& unit_id, Tier tier, std::string& error) {
    if (!validUnitId(unit_id)) {
        error = "invalid unit id '" + unit_id + "'";
        return false;
    }
    if (tier == Tier::NONE) {
        error = "invalid tier";
        return false;
    }
    std::error_code ec;
    if (fs::exists(fileFor(unit_id, Tier::FAST), ec) || fs::exists(fileFor(unit_id, Tier::SLOW), ec)) {
        error = "unit " + unit_id + " already placed; refusing to overwrite (use migrate)";
        return false;
    }
    std::ostringstream body;
    body << "unit_id=" << unit_id << "\n"
         << "tier=" << tierName(tier) << "\n"
         << "placed_at=" << nowIso8601() << "\n"
         << "note=Simulated placement placeholder. Contains no database pages.\n";
    return writeFileAtomic(fileFor(unit_id, tier), body.str(), error);
}

bool TierManager::migrate(const std::string& unit_id, Tier from, Tier to, std::string& error) {
    if (!validUnitId(unit_id)) {
        error = "invalid unit id '" + unit_id + "'";
        return false;
    }
    if (from == Tier::NONE || to == Tier::NONE || from == to) {
        error = "invalid migration " + tierName(from) + " -> " + tierName(to);
        return false;
    }
    std::string src = fileFor(unit_id, from);
    std::string dst = fileFor(unit_id, to);
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        error = "source missing: " + src;
        return false;
    }
    if (fs::exists(dst, ec)) {
        error = "destination already exists: " + dst + " (refusing to overwrite)";
        return false;
    }
    fs::rename(src, dst, ec);
    if (ec) {
        error = "move failed " + src + " -> " + dst + ": " + ec.message();
        return false;
    }
    // Append a history line inside the placeholder.
    std::ofstream f(dst, std::ios::app);
    if (f) f << "migrated=" << tierName(from) << "->" << tierName(to) << " at " << nowIso8601() << "\n";
    return true;
}

bool TierManager::applyPlacement(const Placement& target, int& moved, std::string& error) {
    moved = 0;
    if (!init(error)) return false;
    // Demotions first, then promotions, then brand-new units.
    for (int pass = 0; pass < 2; ++pass) {
        Tier want = pass == 0 ? Tier::SLOW : Tier::FAST;
        for (const auto& kv : target) {
            if (kv.second != want) continue;
            Tier cur = locate(kv.first);
            if (cur == want) continue;
            bool ok = (cur == Tier::NONE) ? place(kv.first, want, error)
                                          : migrate(kv.first, cur, want, error);
            if (!ok) return false;
            if (cur != Tier::NONE) ++moved;
        }
    }
    return saveManifest(error);
}

Placement TierManager::current() const {
    Placement p;
    std::error_code ec;
    for (Tier t : {Tier::FAST, Tier::SLOW}) {
        if (!fs::exists(dirFor(t), ec)) continue;
        for (const auto& e : fs::directory_iterator(dirFor(t), ec)) {
            if (e.path().extension() != ".unit") continue;
            std::string id = e.path().stem().string();
            if (validUnitId(id)) p[id] = t;
        }
    }
    return p;
}

bool TierManager::reset(std::string& error) {
    std::error_code ec;
    for (Tier t : {Tier::FAST, Tier::SLOW}) {
        if (!fs::exists(dirFor(t), ec)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dirFor(t), ec))
            if (e.path().extension() == ".unit") files.push_back(e.path());
        for (const auto& f : files) {
            fs::remove(f, ec);
            if (ec) {
                error = "cannot remove " + f.string() + ": " + ec.message();
                return false;
            }
        }
    }
    return saveManifest(error);
}

bool TierManager::saveManifest(std::string& error) const {
    std::ostringstream os;
    os << "unit_id,tier,saved_at\n";
    std::string ts = nowIso8601();
    for (const auto& kv : current()) os << kv.first << "," << tierName(kv.second) << "," << ts << "\n";
    return writeFileAtomic(manifestPath(), os.str(), error);
}

}  // namespace memopt
