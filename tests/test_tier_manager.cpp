// Tests: TierManager folder operations, path validation, no-overwrite,
// manifest, applyPlacement and reset. Uses a temporary folder only.
#include <filesystem>
#include <fstream>

#include "test_util.h"
#include "tier_manager.h"

using namespace memopt;
namespace fs = std::filesystem;

static std::string tempRoot(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("memopt_test_" + name);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p.string();
}

TEST(valid_unit_ids) {
    CHECK(TierManager::validUnitId("orders_s03"));
    CHECK(TierManager::validUnitId("a-b_C9"));
    CHECK(!TierManager::validUnitId(""));
    CHECK(!TierManager::validUnitId("../etc/passwd"));
    CHECK(!TierManager::validUnitId("a/b"));
    CHECK(!TierManager::validUnitId("a\\b"));
    CHECK(!TierManager::validUnitId("x y"));
    CHECK(!TierManager::validUnitId(std::string(41, 'a')));
}

TEST(place_and_locate) {
    TierManager tm(tempRoot("place"));
    std::string err;
    CHECK(tm.init(err));
    CHECK(tm.place("u1", Tier::FAST, err));
    CHECK(tm.locate("u1") == Tier::FAST);
    CHECK(tm.locate("missing") == Tier::NONE);
    CHECK(fs::exists(fs::path(tm.dirFor(Tier::FAST)) / "u1.unit"));
}

TEST(place_refuses_overwrite) {
    TierManager tm(tempRoot("overwrite"));
    std::string err;
    tm.init(err);
    CHECK(tm.place("u1", Tier::SLOW, err));
    CHECK(!tm.place("u1", Tier::SLOW, err));
    CHECK(!tm.place("u1", Tier::FAST, err));
    CHECK(err.find("refusing") != std::string::npos);
}

TEST(place_rejects_bad_id) {
    TierManager tm(tempRoot("badid"));
    std::string err;
    tm.init(err);
    CHECK(!tm.place("../evil", Tier::FAST, err));
    CHECK(!fs::exists(fs::path(tm.root()) / "evil.unit"));
}

TEST(migrate_moves_file) {
    TierManager tm(tempRoot("migrate"));
    std::string err;
    tm.init(err);
    tm.place("u1", Tier::SLOW, err);
    CHECK(tm.migrate("u1", Tier::SLOW, Tier::FAST, err));
    CHECK(tm.locate("u1") == Tier::FAST);
    CHECK(!fs::exists(fs::path(tm.dirFor(Tier::SLOW)) / "u1.unit"));
    // history line appended
    std::ifstream f(fs::path(tm.dirFor(Tier::FAST)) / "u1.unit");
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(all.find("migrated=SLOW->FAST") != std::string::npos);
}

TEST(migrate_errors) {
    TierManager tm(tempRoot("migerr"));
    std::string err;
    tm.init(err);
    CHECK(!tm.migrate("ghost", Tier::SLOW, Tier::FAST, err));  // source missing
    tm.place("u1", Tier::SLOW, err);
    CHECK(!tm.migrate("u1", Tier::SLOW, Tier::SLOW, err));     // same tier
    // Destination exists -> refuse (create an inconsistent state by hand).
    std::ofstream(fs::path(tm.dirFor(Tier::FAST)) / "u1.unit") << "x";
    CHECK(!tm.migrate("u1", Tier::SLOW, Tier::FAST, err));
    CHECK(err.find("already exists") != std::string::npos);
}

TEST(apply_placement_and_manifest) {
    TierManager tm(tempRoot("apply"));
    std::string err;
    int moved = 0;
    Placement target = {{"a", Tier::FAST}, {"b", Tier::SLOW}, {"c", Tier::SLOW}};
    CHECK(tm.applyPlacement(target, moved, err));
    CHECK_EQ(moved, 0);  // all new
    CHECK(tm.current() == target);
    target["a"] = Tier::SLOW;
    target["c"] = Tier::FAST;
    CHECK(tm.applyPlacement(target, moved, err));
    CHECK_EQ(moved, 2);
    CHECK(tm.current() == target);
    std::ifstream f(tm.manifestPath());
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(all.find("unit_id,tier") == 0);
    CHECK(all.find("c,FAST") != std::string::npos);
}

TEST(reset_removes_only_unit_files) {
    TierManager tm(tempRoot("reset"));
    std::string err;
    tm.init(err);
    tm.place("a", Tier::FAST, err);
    std::ofstream(fs::path(tm.dirFor(Tier::FAST)) / "README.txt") << "keep me";
    CHECK(tm.reset(err));
    CHECK(tm.current().empty());
    CHECK(fs::exists(fs::path(tm.dirFor(Tier::FAST)) / "README.txt"));
}

TEST(atomic_write_replaces_file) {
    std::string root = tempRoot("atomic");
    std::string path = (fs::path(root) / "f.txt").string();
    std::string err;
    CHECK(writeFileAtomic(path, "one", err));
    CHECK(writeFileAtomic(path, "two", err));
    std::ifstream f(path);
    std::string s;
    f >> s;
    CHECK_EQ(s, std::string("two"));
    CHECK(!fs::exists(path + ".tmp"));
}

int main() { return RUN_ALL(); }
