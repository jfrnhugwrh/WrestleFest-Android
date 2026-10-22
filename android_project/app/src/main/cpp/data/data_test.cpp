// Data layer acceptance tests: SML parsing, the typed tables and the .tyF font
// descriptors.
//
// Run: ./data_test [assets/data/Data directory]

#include "../data/asset_source.h"
#include "../data/game_data.h"
#include "../data/sml_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace wrestlefest;

#ifndef WRESTLEFEST_DATA_ROOT
#define WRESTLEFEST_DATA_ROOT "android_project/app/src/main/assets/data/Data"
#endif

namespace {

int g_failures = 0;

void check(bool condition, const std::string &what) {
    if (condition) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

void checkNear(float actual, float expected, float tolerance, const std::string &what) {
    if (std::fabs(actual - expected) <= tolerance) {
        std::printf("  ok   %s (%.4f)\n", what.c_str(), actual);
    } else {
        std::printf("  FAIL %s (got %.4f, expected %.4f)\n", what.c_str(), actual, expected);
        ++g_failures;
    }
}

std::string readText(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return {};
    std::string text;
    char buffer[8192];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, read);
    }
    std::fclose(file);
    return text;
}

void testParser() {
    std::printf("[sml parser]\n");
    const char *fixture =
        "<SML V1.1>\n"
        "<Fixture>\n"
        "<ARRAY|Row> 2\n"
        "{\n"
        "<U16|Id> 7\n"
        "<STR|Name> \"TEST ROW\"\n"
        "<F32|Speed> 1.500000\n"
        "<S16|Offset> -1s\n"
        "<S16A3|Triple> 1 2 3\n"
        "}\n"
        "{\n"
        "<U16|Id> 8\n"
        "<STR|Name> \"SECOND\"\n"
        "<F32|Speed> 2.250000\n"
        "<S16|Offset> 4\n"
        "<S16A3|Triple> 4 5 6\n"
        "}\n"
        "<ARRAYEND|Row>\n"
        "<ARRAY|Table> 1\n"
        "{\n"
        "<U16|Id> 0\n"
        "<STR|Label> \"OTHER BLOCK\"\n"
        "}\n"
        "<ARRAYEND|Table>\n";

    SmlDocument doc;
    std::string error;
    check(doc.parse(fixture, std::char_traits<char>::length(fixture), &error),
          "fixture parses: " + error);
    check(doc.root_name == "Fixture", "root name read");
    check(doc.records.size() == 2, "two records in the first array");
    check(doc.arrays.size() == 2, "both top-level blocks kept");
    if (doc.records.size() == 2) {
        check(doc.records[0].integer("Id") == 7, "U16 read");
        check(doc.records[0].string("Name") == "TEST ROW", "quoted string read");
        check(doc.records[1].string("Name") == "SECOND", "second string read");
        // SML numbers can carry the original tool's suffix junk ("-1s").
        check(doc.records[0].integer("Offset") == -1, "suffixed negative number read");
        checkNear(static_cast<float>(doc.records[1].number("Speed")), 2.25f, 0.0001f,
                  "float read");
        check(doc.records[1].numberAt("Triple", 2, -99.0) == 6.0, "array field element read");
        check(doc.records[1].numberAt("Triple", 5, -99.0) == -99.0, "array field bounds checked");
    }
    check(doc.array("Table") != nullptr && doc.array("Table")->records.size() == 1,
          "named array lookup");

    // Re-parsing the same document must not accumulate records.
    SmlDocument reused;
    reused.parse(fixture, std::char_traits<char>::length(fixture), &error);
    reused.parse(fixture, std::char_traits<char>::length(fixture), &error);
    check(reused.records.size() == 2, "parse() resets previous contents");
}

void testTables(const std::string &assets) {
    std::printf("[game tables]\n");
    DirectoryAssetSource source(assets);
    GameData data;
    std::string error;
    if (!data.load(&source, "", &error)) {
        std::printf("  FAIL loading %s: %s\n", assets.c_str(), error.c_str());
        ++g_failures;
        return;
    }
    check(data.states().size() == 117, "StateTable has 117 rows");
    check(data.anims().size() == 269, "WrestlerAT has 269 animations");
    check(data.moves().size() == 95, "WrestleStyleList has 95 moves");
    check(data.rules().size() == 5, "InGameTable has 5 rule sets");
    check(data.wrestlers().size() == 38, "38 wrestlers loaded from disk");
    check(!data.arenas().empty(), "arena table loaded");
    auto has_arena = [&data](const char *name, const char *folder) {
        for (const ArenaInfo &arena : data.arenas()) {
            if (arena.name == name && arena.folder == folder) return true;
        }
        return false;
    };
    check(has_arena("Old Survivor Series Arena", "ARENA08"),
          "Old Survivor Series resolves to ARENA08");
    check(has_arena("WrestleMania-27", "ARENA09"),
          "WrestleMania 27 resolves to ARENA09");

    // Rule set 0 is the 1VS1 match: six minutes, 20 counts outside.
    const RuleInfo *one_on_one = data.rule(0);
    check(one_on_one != nullptr, "rule 0 present");
    if (one_on_one != nullptr) {
        checkNear(one_on_one->max_match_time, 360.0f, 0.01f, "1VS1 lasts 360 s");
        check(one_on_one->max_outside_count == 20, "1VS1 counts to 20");
        check(one_on_one->can_pin, "1VS1 allows pins");
    }

    // Animation table: the offset that snaps the opponent into a grapple and
    // the facing multipliers the state machine applies on entry.
    // Grapple entries snap the opponent by xOffset; a few rows are known from
    // the decompilation (grapple 166, headlock 120, bodyslam 27).
    bool have_grapple = false;
    bool have_headlock = false;
    int offsets = 0;
    for (const AnimInfo &anim : data.anims()) {
        if (anim.x_offset != 0.0f) ++offsets;
        if (std::fabs(anim.x_offset - 166.0f) < 0.01f) have_grapple = true;
        if (std::fabs(anim.x_offset - 120.0f) < 0.01f) have_headlock = true;
    }
    check(offsets > 50, "grapple animations carry snap offsets");
    check(have_grapple, "a 166 px grapple snap exists");
    check(have_headlock, "a 120 px headlock snap exists");
    int with_hurt_timing = 0;
    for (const AnimInfo &anim : data.anims()) {
        if (anim.hasHurtTiming()) ++with_hurt_timing;
    }
    check(with_hurt_timing >= 1, "signature moves carry HurtTimming");

    // Move families resolve through _stateMaping.
    check(moveFamilyEntryState(0) == state::USE_CL_WS, "family 0 maps to USE_CL_WS");
    check(moveFamilyEntryState(12) == state::USE_RFWS,
          "family 12 (Spear/Trouble in Paradise/La Mistica) maps to USE_RFWS");
    check(data.moveInfo(0) != nullptr && data.moveInfo(0)->name == "WS_PUNCH",
          "move 0 is WS_PUNCH");

    // Official animation tables (RefereeAT.sml + AnnouncerAT.sml).
    check(data.refereeAnims().size() == 6, "RefereeAT has 6 animations");
    const AnimInfo *ref_count_anim = data.refereeAnimInfo(2);
    check(ref_count_anim != nullptr && ref_count_anim->frame_count == 16 &&
              ref_count_anim->frames[0] == 5 && ref_count_anim->frames[10] == 6,
          "RefereeAT countdown anim plays [5x10, 6x6] mat slap");
    check(data.announcerAnims().size() == 6, "AnnouncerAT has 6 animations");
    const AnimInfo *ann_exit = data.announcerAnimInfo(5);
    check(ann_exit != nullptr && ann_exit->frame_count == 16 && ann_exit->frames[0] == 9,
          "AnnouncerAT pass-rope-out anim loaded");

    // WSTable rows are reachable by id.
    check(data.forceMove(0) != nullptr, "ForceWSTable row 0");
    check(data.ramMove(0) != nullptr, "RamWSTable row 0");
    check(data.hitTargetMove(0) != nullptr, "HitTargetWSTable row 0");
    check(data.springboardMove(0) != nullptr, "SpringboardWSTable row 0");
    check(data.submissionMove(0) != nullptr, "SubmissionWSTable row 0");

    // Every wrestler must expose a usable profile and sprite table.
    int bad = 0;
    for (const WrestlerProfile &w : data.wrestlers()) {
        if (w.base.energy <= 0.0f || w.sprites.empty() || w.folder.empty()) ++bad;
    }
    check(bad == 0, "every wrestler has energy, sprites and a bundle folder");

    const WrestlerProfile *cena = data.wrestlerByFolder("00_John Cena");
    check(cena != nullptr, "roster lookup by folder");
    if (cena != nullptr) {
        const SpriteEntry *idle = cena->spriteFor(cena->sprite_index.at("idle.sst"));
        check(idle != nullptr && idle->name == "idle.sst", "sprite index resolves idle.sst");
    }
}

void testFont(const std::string &assets) {
    std::printf("[tyF font]\n");
    const std::string path = assets + "/../../graphics/Fonts/Arial Rounded MT Bold.tyF";
    const std::string text = readText(path);
    if (text.empty()) {
        std::printf("  info %s not readable, font test skipped\n", path.c_str());
        return;
    }
    SmlDocument doc;
    std::string error;
    check(doc.parse(text.data(), text.size(), &error), "font descriptor parses: " + error);
    const SmlArray *header = doc.array("Header");
    const SmlArray *char_table = doc.array("CharTable");
    check(header != nullptr && header->records.size() == 1, "font has one Header row");
    check(char_table != nullptr && char_table->records.size() == 256,
          "font has 256 CharTable rows");
    if (header != nullptr && !header->records.empty()) {
        const std::string texture = header->records[0].string("texture");
        check(!texture.empty(), "Header names an atlas texture (" + texture + ")");
    }
    if (char_table != nullptr && char_table->records.size() > 65) {
        const SmlRecord &a = char_table->records[65];
        check(a.number("w") > 0.0, "glyph 'A' has a width");
        check(a.number("u1") > a.number("u0"), "glyph 'A' has a u range");
    }
}

}  // namespace

int main(int argc, char **argv) {
    const std::string assets = argc > 1 ? argv[1] : WRESTLEFEST_DATA_ROOT;
    testParser();
    testTables(assets);
    testFont(assets);

    if (g_failures == 0) {
        std::printf("\ndata_test: PASS\n");
        return 0;
    }
    std::printf("\ndata_test: %d failure(s)\n", g_failures);
    return 1;
}
