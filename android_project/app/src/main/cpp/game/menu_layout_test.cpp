// Geometry checks for the front end: the numbers in game/menu_layout.h drive
// both what is drawn and what is tappable, so they are tested together.
//
//   tools/host_check/tests.sh   (runs this alongside the data/sim/image tests)

#include "menu_layout.h"

#include <cstdio>
#include <string>

using namespace wrestlefest;
using namespace wrestlefest::menu;

namespace {

int failures = 0;

void check(bool condition, const std::string &what) {
    if (condition) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        std::printf("  FAIL %s\n", what.c_str());
        ++failures;
    }
}

void checkNear(float value, float expected, float tolerance, const std::string &what) {
    const bool ok = value >= expected - tolerance && value <= expected + tolerance;
    if (!ok) {
        std::printf("  FAIL %s (got %.2f, expected %.2f +- %.2f)\n", what.c_str(), value, expected,
                    tolerance);
        ++failures;
    } else {
        std::printf("  ok   %s (%.2f)\n", what.c_str(), value);
    }
}

// A stand-in for the metrics BitmapFont measures from the Century Gothic atlas.
FontMetrics gothicMetrics() {
    FontMetrics metrics;
    metrics.cell_height = 36.0f;
    metrics.ink_top = 9.0f;
    metrics.cap_height = 21.0f;
    metrics.measured = true;
    return metrics;
}

void testHeadings() {
    std::printf("[headings]\n");
    const FontMetrics metrics = gothicMetrics();
    const float scale = scaleForSize(metrics, kHeadingSize);
    // The heading size is fitted to the reference screens (74 units against the
    // 36-unit glyph cell); with 21-unit capitals that is a 43 px cap.
    checkNear(metrics.cap_height * scale, 43.0f, 1.0f, "heading capitals are 43 px tall");
    checkNear(scale, kHeadingSize / metrics.cell_height, 0.001f,
              "the heading scale comes from the glyph cell");
    check(kHeadingSize > 0.0f && kHeadingSize < 96.0f, "the heading size is a sane cell height");
    // The original prints the heading from the top of the glyph cell at y = 18,
    // which is where headingCellTop() puts it; the ink follows from that.
    checkNear(headingCellTop(), 18.0f, 0.01f, "the heading cell top is the original's y = 18");
    checkNear(headingInkTop(metrics), 18.0f + metrics.ink_top * scale, 0.01f,
              "headingInkTop() is the cell top plus the atlas ink offset");
    checkNear(headingInkTop(metrics), 36.5f, 0.5f, "the capital ink starts just under 36 px");
    // inkTopToCellTop is the inverse of that conversion.
    const float ink = headingInkTop(metrics);
    checkNear(inkTopToCellTop(metrics, ink, kHeadingSize) + metrics.ink_top * scale, ink, 0.01f,
              "inkTopToCellTop() inverts the ink placement");

    // A caption centred in a strip keeps equal air above and below its capitals.
    const float row_ink = centreInBox(metrics, kMainStripY, kMainStripHeight, kMainLabelSize);
    const float cap = metrics.cap_height * scaleForSize(metrics, kMainLabelSize);
    checkNear(row_ink, kMainStripY + (kMainStripHeight - cap) * 0.5f, 0.01f,
              "centreInBox centres the caps");
    checkNear(row_ink + cap + (row_ink - kMainStripY), kMainStripY + kMainStripHeight, 0.01f,
              "the space above equals the space below");
}

void testMainMenu() {
    std::printf("[main menu]\n");
    for (int i = 0; i < kMainEntryCount; ++i) {
        const float y = mainRowY(i);
        check(insideRect(kMainStripX + 4.0f, y + kMainStripHeight * 0.5f, kMainStripX, y,
                         kMainStripWidth, kMainStripHeight),
              std::string("row ") + mainEntryLabel(i) + " is tappable in its middle");
        check(mainEntryAt(kMainStripX + kMainStripWidth * 0.5f,
                          y + kMainStripHeight * 0.5f) == i,
              std::string("mainEntryAt() finds ") + mainEntryLabel(i));
    }
    check(mainEntryAt(kMainStripX + 10.0f, mainRowY(0) - 10.0f) == -1, "the gap above the rows misses");
    check(mainRowY(kMainEntryCount - 1) + kMainStripHeight < kBackY, "the rows clear the back button");
    // MainScene loads the strips at i * 83 + 167.
    checkNear(mainRowY(1) - mainRowY(0), 83.0f, 0.01f, "the rows step 83 px, as the original does");
    checkNear(mainRowY(3), 416.0f, 0.01f, "the OPTIONS row is at y = 416");
    // Every row is centred on the canvas except SHOP, which leaves room for the
    // WWE mark the original draws beside it.
    check(mainEntryLeftAligned(2) && !mainEntryLeftAligned(0) && !mainEntryLeftAligned(3),
          "only SHOP is printed left aligned");
    check(kShopLabelX > 0.0f && kShopLabelX < kCanvasWidth * 0.5f,
          "the SHOP caption starts left of centre");
    check(kWweLogoX + 96.0f * kWweLogoScale < kShopLabelX, "the WWE mark sits left of SHOP");
    check(kMainStripX > 0.0f && kMainStripX + kMainStripWidth < kCanvasWidth,
          "the strips sit inside the canvas");
    check(mainEntryAt(480.0f, 200.0f) == 0, "the first row is the default highlight");
    check(!mainEntryEnabled(1), "MULTIPLAYER has no Xbox LIVE backend here");
}

void testModes() {
    std::printf("[game modes]\n");
    float previous_bottom = 0.0f;
    for (int i = 0; i < kModeCount; ++i) {
        const float width = modeStripWidth(i);
        const float height = modeStripHeight(i);
        const float x = (kCanvasWidth - width) * 0.5f;
        check(modeEntryAt(480.0f, modeRowY(i) + height * 0.5f) == i,
              std::string("modeEntryAt() finds ") + modeLabel(i));
        check(modeRowY(i) > previous_bottom, std::string("row ") + modeLabel(i) + " does not overlap");
        previous_bottom = modeRowY(i) + height;
        check(x > 0.0f && x + width < kCanvasWidth, std::string(modeLabel(i)) + " fits the width");
    }
    check(previous_bottom < kBackY, "the last row clears the back button");
    // The two long captions get the 875 px plate, the other four the 558 px one.
    check(modeUsesWidePlate(1) && modeUsesWidePlate(3), "ROAD TO WRESTLEMANIA and the tournament use the wide plate");
    check(!modeUsesWidePlate(0) && !modeUsesWidePlate(2) && !modeUsesWidePlate(4) &&
              !modeUsesWidePlate(5),
          "the short captions use the narrow plate");
    check(modeStripWidth(3) == 875.0f, "the wide plate is the 875 px art");
    check(modeStripWidth(0) == 558.0f, "the narrow plate is the 558 px art");
    // Both kinds of plate end up centred on the canvas.
    for (int i = 0; i < kModeCount; ++i) {
        checkNear(modeStripX(i) + modeStripWidth(i) * 0.5f, kCanvasWidth * 0.5f, 0.01f,
                  std::string("plate ") + std::to_string(i) + " is centred");
    }
    // The plates are the original's own y values, one per row.
    checkNear(modeRowY(0), 161.0f, 0.01f, "the first plate is at y = 161");
    checkNear(modeRowY(5), 441.0f, 0.01f, "the last plate is at y = 441");
    // The three modes the build can start map to the match rules.
    check(modeGameRule(0) == 0 && modeGameRule(2) == 2 && modeGameRule(4) == 1,
          "exhibition/rumble/tag map to the 1v1, rumble and tag rules");
    check(modeGameRule(1) < 0 && modeGameRule(3) < 0 && modeGameRule(5) < 0,
          "the unimplemented modes report no rule");
}

void testSelectors() {
    std::printf("[selectors]\n");
    // Superstar grid: 4 columns x 2 rows, column-major like SuperstarScene.
    int seen[kGridPageSize] = {0};
    for (int column = 0; column < kGridColumns; ++column) {
        for (int row = 0; row < kGridRows; ++row) {
            const int slot = gridSlot(column, row);
            check(slot >= 0 && slot < kGridPageSize, "the grid slot is on the page");
            if (slot >= 0 && slot < kGridPageSize) ++seen[slot];
            const float x = superstarCardX(column);
            const float y = superstarCardY(row);
            check(superstarCardAt(x + kCardSize * 0.5f, y + kCardSize * 0.5f) == slot,
                  "superstarCardAt() finds the card it was drawn at");
            check(x >= 0.0f && x + kCardSize <= kCanvasWidth, "the card fits horizontally");
        }
    }
    bool every_slot_once = true;
    for (int slot = 0; slot < kGridPageSize; ++slot) every_slot_once = every_slot_once && seen[slot] == 1;
    check(every_slot_once, "every grid slot is used exactly once");
    check(superstarCardY(kGridRows - 1) + kCardNameInkOffset + kCardNameLineStep < kDotY,
          "the names clear the page dots");
    check(superstarCardX(0) > kBackX, "the grid clears the back button");

    // Arena grid: 2 columns x 2 rows.
    int arena_seen[kArenaPageSize] = {0};
    for (int column = 0; column < kArenaColumns; ++column) {
        for (int row = 0; row < kArenaRows; ++row) {
            const int slot = arenaGridSlot(column, row);
            if (slot >= 0 && slot < kArenaPageSize) ++arena_seen[slot];
            const float x = arenaCardX(column);
            const float y = arenaCardY(row);
            check(arenaCardAt(x + kArenaCardWidth * 0.5f, y + kArenaCardHeight * 0.5f) == slot,
                  "arenaCardAt() finds the card it was drawn at");
            check(x >= 0.0f && x + kArenaCardWidth <= kCanvasWidth, "the arena card fits horizontally");
        }
    }
    bool every_arena_once = true;
    for (int slot = 0; slot < kArenaPageSize; ++slot) every_arena_once = every_arena_once && arena_seen[slot] == 1;
    check(every_arena_once, "every arena slot is used exactly once");
    check(arenaCardY(kArenaRows - 1) + kArenaCardHeight + kArenaNameLineStep < kDotY,
          "the arena names clear the page dots");
}

void testChrome() {
    std::printf("[chrome]\n");
    check(backHit(kBackX + kBackSize * 0.5f, kBackY + kBackSize * 0.5f), "the back button is tappable");
    check(!backHit(kBackX, kBackY - 20.0f), "above the back button misses");
    check(nextArrowHit(kCanvasWidth - 40.0f, kArrowY + 10.0f), "the next arrow is tappable");
    check(!nextArrowHit(kCanvasWidth - 140.0f, kArrowY + 10.0f), "left of the next arrow misses");
    check(previousArrowHit(40.0f, kArrowY + 10.0f), "the previous arrow is tappable");
    check(!previousArrowHit(140.0f, kArrowY + 10.0f), "right of the previous arrow misses");
    check(!backHit(480.0f, 480.0f), "the middle of the screen is not the back button");

    check(kBackY + kBackSize <= kCanvasHeight, "the back button is on screen");
    check(kDotY + kDotSize <= kCanvasHeight, "the dots are on screen");

    // Dots: centred on the canvas, 25 px apart, and symmetric. Points::Create
    // anchors them at 468/480 depending on the parity of the count, so the
    // original's own row is up to 2 px left of centre; that is kept.
    for (int count = 2; count <= 5; ++count) {
        const float first = dotX(0, count);
        const float last = dotX(count - 1, count);
        checkNear((first + last) * 0.5f + kDotSize * 0.5f, kCanvasWidth * 0.5f, 2.0f,
                  "the dots are centred");
        for (int i = 1; i < count; ++i) {
            checkNear(dotX(i, count) - dotX(i - 1, count), kDotStep, 0.01f,
                      "the dots are evenly spaced");
        }
    }
}

// The card art has to land inside the frame's transparent window: drawing an
// avatar or a scene at its native size spills over the frame's border, which is
// what the earlier pass did.
void testCardWindows() {
    std::printf("[card windows]\n");
    const float avatar = 125.0f;   // graphics/Sprites/Character/*/avatar.png
    const menu::Placement a = avatarPlacement(avatar, avatar);
    checkNear(a.width, kAvatarWindowSize, 0.01f, "the avatar is fitted to the window width");
    checkNear(a.height, kAvatarWindowSize, 0.01f, "the avatar is fitted to the window height");
    checkNear(a.x, kAvatarWindowX, 0.01f, "the square avatar is centred in its window");
    checkNear(a.y, kAvatarWindowY, 0.01f, "the square avatar is centred in its window");
    check(a.width <= kAvatarWindowSize + 0.01f && a.height <= kAvatarWindowSize + 0.01f,
          "the avatar does not cover the frame");
    check(kAvatarWindowX + kAvatarWindowSize < kCardSize,
          "the frame keeps a border on the right of the window");
    check(kAvatarWindowY + kAvatarWindowSize < kCardSize,
          "the frame keeps a border under the window");

    const float scene_w = 212.0f;  // graphics/Sprites/Arena/*/scene.png
    const float scene_h = 146.0f;
    const menu::Placement s = arenaScenePlacement(scene_w, scene_h);
    check(s.width <= kArenaSceneWindowWidth + 0.01f && s.height <= kArenaSceneWindowHeight + 0.01f,
          "the arena scene stays inside the window");
    checkNear(s.width / s.height, scene_w / scene_h, 0.001f, "the scene keeps its aspect ratio");
    checkNear(s.x + s.width * 0.5f, kArenaSceneWindowX + kArenaSceneWindowWidth * 0.5f, 0.01f,
              "the scene is centred horizontally");
    checkNear(s.y + s.height * 0.5f, kArenaSceneWindowY + kArenaSceneWindowHeight * 0.5f, 0.01f,
              "the scene is centred vertically");
    check(kArenaSceneWindowX + kArenaSceneWindowWidth < kArenaCardWidth,
          "the frame keeps a border on the right of the window");
    check(kArenaSceneWindowY + kArenaSceneWindowHeight < kArenaCardHeight,
          "the frame keeps a border under the window");

    // Degenerate art sizes must not produce off-screen quads.
    const menu::Placement empty = avatarPlacement(0.0f, 0.0f);
    check(empty.width == 0.0f && empty.height == 0.0f, "missing art produces an empty placement");
}

}  // namespace

int main() {
    testHeadings();
    testMainMenu();
    testModes();
    testSelectors();
    testCardWindows();
    testChrome();
    if (failures == 0) {
        std::printf("\nmenu_layout_test: PASS\n");
        return 0;
    }
    std::printf("\nmenu_layout_test: %d failure(s)\n", failures);
    return 1;
}
