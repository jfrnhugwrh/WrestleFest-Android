#ifndef WRESTLEFEST_MENU_LAYOUT_H
#define WRESTLEFEST_MENU_LAYOUT_H

// Geometry of the front-end screens, in the logical 960x640 canvas.
//
// Everything here is taken from the shipped art and from the original's scene
// code (TitleMenu.cpp, decompiled in "WrestleFest Premium.c"):
//
//   MainScene::Draw       - "MAIN MENU", the four main_button_*.png strips
//   ModeScene::Draw       - "GAME MODES", the six mode_button_*.png strips
//   SuperstarScene::Draw  - "SELECT SUPERSTAR", 4x2 avatar cards
//   ArenaScene::Draw      - "SELECT ARENA", 2x2 arena cards
//   Points::Create/Draw   - the page dots
//   InfoBar::Draw         - the back button
//
// The strips and the cards are drawn at their native art size, which is the
// same size the original lays them out at, so the numbers below are the art
// sizes rather than a scale factor. The originals position strips from their
// top-left corner and text from the top of the tyF glyph cell; the helpers
// below convert between the two so the caps sit in the middle of a strip,
// which is what the reference screenshots show.
//
// This header is shared with tools/preview so the software compositor draws
// the same pixels as the GL renderer.

#include "../render/gl_sprite.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace wrestlefest {
namespace menu {

constexpr float kCanvasWidth = 960.0f;
constexpr float kCanvasHeight = 640.0f;

// ---------------------------------------------------------------------------
// Font metrics
// ---------------------------------------------------------------------------
// Both atlases ship beside each other: Arial Rounded MT Bold is the body face
// and Century Gothic the menu face. The ink box is measured from the atlas at
// load time (BitmapFont::measureInk), so the numbers here come from
// FontMetrics rather than being hard-coded.
//
// The original passes a "size" that is the height of the glyph cell in pixels
// (tyCFontTT::Print(..., 0x48, ...) is the 72 px heading) and prints from the
// top of the cell, not from the top of the capitals.
inline float scaleForSize(const FontMetrics &metrics, float size) {
    return size / (metrics.cell_height > 0.0f ? metrics.cell_height : 24.0f);
}

// Glyph-cell top that puts the capital ink at `ink_top`.
inline float inkTopToCellTop(const FontMetrics &metrics, float ink_top, float size) {
    return ink_top - metrics.ink_top * scaleForSize(metrics, size);
}

// Ink top that centres the capitals in [box_top, box_top + box_h).
inline float centreInBox(const FontMetrics &metrics, float box_top, float box_h, float size) {
    const float cap = metrics.cap_height * scaleForSize(metrics, size);
    return box_top + (box_h - cap) * 0.5f;
}

// Sizes are glyph-cell heights in logical pixels. They are fitted to the widths
// of the captions on the reference screenshots of the 960x640 canvas (MAIN MENU
// 337 px, MULTIPLAYER 270 px, ROAD TO WRESTLEMANIA 473 px, Jake "The Snake"
// 177 px, Loading ... 121 px), which is the only measurement of them that does
// not depend on the captures' vertical scaling. The original's tyCFontTT::Print
// sizes for the same lines are 0x48 / 0x2d / 0x28 / 0x18.
//
// The screens are set in the plain face: no outline, and no extra weight beyond
// what the atlas already has, so the faux-bold ring an earlier pass applied is
// off.
constexpr float kLabelTracking = 0.0f;
// The headings are spaced out a little; the row labels are not.
constexpr float kHeadingTracking = 2.0f;
// Faux-bold ring applied to the menu face (logical pixels). 0 = off: the
// reference captions are the plain atlas, a single soft drop shadow.
constexpr float kBoldPx = 0.0f;

// ---------------------------------------------------------------------------
// Colours (CColor in the original's data segment), sampled from the reference
// screenshots.
// ---------------------------------------------------------------------------
inline Color titleRed() { return Color{0.76f, 0.08f, 0.03f, 1.0f}; }        // LightRed
inline Color titleOrange() { return Color{0.97f, 0.56f, 0.05f, 1.0f}; }     // Orange
inline Color normalText() { return Color{1.0f, 1.0f, 1.0f, 1.0f}; }         // White
inline Color selectedText() { return Color{0.90f, 0.55f, 0.12f, 1.0f}; }    // SelectedString
inline Color disabledText() { return Color{0.55f, 0.57f, 0.60f, 1.0f}; }    // DisableString
inline Color shadowColor() { return Color{0.0f, 0.0f, 0.0f, 0.75f}; }

// Drop shadow the original sets before every menu caption: dropShadowOffset is
// 4/4 for the headings and the row labels, 2/2 for the labels under a card.
constexpr float kHeadingShadow = 4.0f;
constexpr float kLabelShadow = 3.0f;
constexpr float kNameShadow = 2.0f;

// ---------------------------------------------------------------------------
// Page headings: tyCFontTT::Print(0, 18, 960, 0, text, 0x48, colour, centered).
// y = 18 is the top of the glyph cell; with the menu face that puts the tops
// of the capitals at 36, which is where the reference screens have them.
// ---------------------------------------------------------------------------
constexpr float kHeadingSize = 74.0f;    // tyCFontTT::Print(..., 0x48, ...)
constexpr float kHeadingCellY = 18.0f;   // the original's print origin

// Glyph-cell top for the heading, and the ink line it produces. The cell top is
// the original's y; the ink line is what the callers centre and compare on.
inline float headingCellTop() { return kHeadingCellY; }
inline float headingInkTop(const FontMetrics &metrics) {
    return kHeadingCellY + metrics.ink_top * scaleForSize(metrics, kHeadingSize);
}

// ---------------------------------------------------------------------------
// Loading card: the logo ghosted over background_2.png and the "Loading ..."
// label in the bottom right corner.
// ---------------------------------------------------------------------------
// The logo on this screen is part of background_2.png; the only thing drawn on
// top of the art is the label.
constexpr float kLoadingLogoTilt = 8.5f;      // fallback logo, degrees clockwise
constexpr float kLoadingLabelSize = 28.0f;
constexpr float kLoadingLabelInkTop = 595.0f;
constexpr float kLoadingLabelRight = 46.0f;   // margin from the canvas edge

// ---------------------------------------------------------------------------
// Back button (InfoBar::Draw): back.png 78x78 at the bottom left.
// ---------------------------------------------------------------------------
constexpr float kBackX = 14.0f;
constexpr float kBackY = 546.0f;
constexpr float kBackSize = 78.0f;

// ---------------------------------------------------------------------------
// Page dots (Points::Create): 22x22, 25 px apart, 590 px down.
// ---------------------------------------------------------------------------
constexpr float kDotY = 590.0f;
constexpr float kDotSize = 22.0f;
constexpr float kDotStep = 25.0f;

inline float dotX(int index, int count) {
    if (count <= 0) return kCanvasWidth * 0.5f;
    const float base = (count & 1) ? 468.0f : 480.0f;
    return base - static_cast<float>(count / 2 - index) * kDotStep;
}

// ---------------------------------------------------------------------------
// Paging arrows (selector/next.png, previous.png): 41x64.
// ---------------------------------------------------------------------------
constexpr float kArrowY = 304.0f;
constexpr float kArrowWidth = 41.0f;
constexpr float kArrowHeight = 64.0f;
constexpr float kArrowNextX = kCanvasWidth - 68.0f;
constexpr float kArrowPreviousX = 26.0f;

// ---------------------------------------------------------------------------
// Main menu (MainScene::Draw): four 776x49 strips starting at (91, 167) and
// stepping 83 px down - the original loads them at `i * 83 + 167`.
// ---------------------------------------------------------------------------
constexpr int kMainEntryCount = 4;
constexpr float kMainStripX = 91.0f;
constexpr float kMainStripY = 167.0f;
constexpr float kMainStripWidth = 776.0f;
constexpr float kMainStripHeight = 49.0f;
constexpr float kMainRowStep = 83.0f;
constexpr float kMainLabelSize = 54.0f;  // tyCFontTT::Print(..., 0x2d, ...)
// SHOP is the one row the original prints left aligned, to leave room for the
// WWE mark beside it: Print(440, 328, 960, 0, "SHOP", 0x2d, ..., 0).
constexpr float kShopLabelX = 440.0f;

// The WWE mark that sits beside the SHOP row (title_menu/wwe_logo_for_shop.png
// at 0.7, 96x107 -> 67x75).
constexpr float kWweLogoX = 358.0f;
constexpr float kWweLogoY = 296.0f;
constexpr float kWweLogoScale = 0.7f;

enum class MainEntry { GameModes = 0, Multiplayer, Shop, Options, Count };

inline const char *mainEntryLabel(int entry) {
    switch (entry) {
        case 0: return "GAME MODES";
        case 1: return "MULTIPLAYER";
        case 2: return "SHOP";
        case 3: return "OPTIONS";
        default: return "";
    }
}

inline float mainRowY(int entry) { return kMainStripY + kMainRowStep * entry; }

// A row is left aligned only when it shares its line with art.
inline bool mainEntryLeftAligned(int entry) { return entry == 2; }

// MULTIPLAYER needs Xbox LIVE, which this port does not have. The reference
// screen (the full build) draws it like any other row, so it keeps the normal
// colour and only reports the gap when it is tapped.
inline bool mainEntryEnabled(int entry) { return entry != 1; }

// ---------------------------------------------------------------------------
// Game modes (ModeScene::Draw): six plates 56 px apart from y = 161.
//
// The original loads all six mode_button_N.png plates and positions them
// individually - the 558 px plates at x = 200, the 875 px ones at x = 43, so
// both kinds are centred on the 960 px canvas:
//
//   mode_button_0 (558x35)  (200, 161)   EXHIBITION
//   mode_button_1 (875x36)  ( 43, 216)   ROAD TO WRESTLEMANIA
//   mode_button_2 (558x36)  (200, 273)   ROYAL RUMBLE
//   mode_button_3 (875x36)  ( 43, 329)   SATURDAY NIGHT'S MAIN EVENT ...
//   mode_button_4 (558x36)  (201, 385)   TAG TEAM
//   mode_button_5 (558x36)  (201, 441)   GAUNTLET MATCH
//
// The plate is picked by the length of its caption, which is why the long
// Road to WrestleMania line has the wide plate too.
// ---------------------------------------------------------------------------
constexpr int kModeCount = 6;
constexpr float kModeLabelSize = 51.0f;  // tyCFontTT::Print(..., 0x28, ...)

inline const char *modeLabel(int entry) {
    switch (entry) {
        case 0: return "EXHIBITION";
        case 1: return "ROAD TO WRESTLEMANIA";
        case 2: return "ROYAL RUMBLE";
        case 3: return "SATURDAY NIGHT'S MAIN EVENT TOURNAMENT";
        case 4: return "TAG TEAM";
        case 5: return "GAUNTLET MATCH";
        default: return "";
    }
}

// Native size of each plate, measured from the shipped art. The two long
// captions - Road to WrestleMania and the Saturday Night's Main Event
// tournament - are the ones that need the 875 px plate.
inline bool modeUsesWidePlate(int entry) { return entry == 1 || entry == 3; }

inline float modeStripWidth(int entry) { return modeUsesWidePlate(entry) ? 875.0f : 558.0f; }

inline float modeStripHeight(int entry) {
    // mode_button_0 is the only 35 px plate; the rest are 36.
    return entry == 0 ? 35.0f : 36.0f;
}

// The y each plate is loaded at (the original's own numbers).
inline float modeRowY(int entry) {
    switch (entry) {
        case 0: return 161.0f;
        case 1: return 216.0f;
        case 2: return 273.0f;
        case 3: return 329.0f;
        case 4: return 385.0f;
        case 5: return 441.0f;
        default: return 161.0f;
    }
}

inline float modeStripX(int entry) {
    return (kCanvasWidth - modeStripWidth(entry)) * 0.5f;
}

inline int modeGameRule(int entry) {
    // -1 means "no match rule": the mode is not playable in this build.
    switch (entry) {
        case 0: return 0;  // Exhibition          -> 1 vs 1
        case 2: return 2;  // Royal Rumble        -> rumble
        case 4: return 1;  // Tag Team            -> 2 vs 2
        default: return -1;
    }
}

// ---------------------------------------------------------------------------
// Card frames (SuperstarScene::Draw / ArenaScene::Draw).
//
// Both frames are PNGs with a transparent window in the middle; the art for
// the card is drawn inside that window and must not spill over the frame. The
// window is measured from the shipped PNGs (selector/superstar_frame.png and
// selector/arena_frame.png) rather than guessed, so the art inside a card can
// never spill over the frame's border.
// ---------------------------------------------------------------------------
constexpr int kGridColumns = 4;
constexpr int kGridRows = 2;
constexpr int kGridPageSize = kGridColumns * kGridRows;
constexpr float kCardSize = 155.0f;
constexpr float kCardNameSize = 26.0f;
constexpr float kCardNameTracking = 0.5f;
// The first line of the name sits just under the card, the second 19 px
// below it, as on the reference select screens.
constexpr float kCardNameInkOffset = 166.0f;   // below the card's top edge
constexpr float kCardNameLineStep = 23.0f;

// Window of superstar_frame.png: 109x109 at (23, 23) inside the 155 px card.
constexpr float kAvatarWindowX = 23.0f;
constexpr float kAvatarWindowY = 23.0f;
constexpr float kAvatarWindowSize = 109.0f;

inline float superstarCardX(int column) { return 92.5f + 202.0f * column; }
inline float superstarCardY(int row) { return 140.0f + 216.0f * row; }
// Column-major slot used by the original's SuperstarScene constructor.
inline int gridSlot(int column, int row) { return column * kGridRows + row; }

constexpr int kArenaColumns = 2;
constexpr int kArenaRows = 2;
constexpr int kArenaPageSize = kArenaColumns * kArenaRows;
constexpr float kArenaCardWidth = 236.0f;
constexpr float kArenaCardHeight = 169.0f;
constexpr float kArenaNameSize = 26.0f;
constexpr float kArenaNameTracking = 0.5f;
constexpr float kArenaNameInkOffset = 188.0f;   // below the card's top edge
constexpr float kArenaNameLineStep = 23.0f;

// Window of arena_frame.png: 190x125 at (23, 22) inside the 236x169 card.
constexpr float kArenaSceneWindowX = 23.0f;
constexpr float kArenaSceneWindowY = 22.0f;
constexpr float kArenaSceneWindowWidth = 190.0f;
constexpr float kArenaSceneWindowHeight = 125.0f;

inline float arenaCardX(int column) { return 177.0f + 364.0f * column; }
inline float arenaCardY(int row) { return 147.0f + 218.0f * row; }
inline int arenaGridSlot(int column, int row) { return column * kArenaRows + row; }

// ---------------------------------------------------------------------------
// Fitting a card's art into the frame's window. The avatars (125x125) and the
// arena scenes (212x146) are both bigger than their windows, so they are
// scaled down uniformly and centred; drawing them at their native size spills
// over the frame's border.
// ---------------------------------------------------------------------------
struct Placement {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline Placement fitInWindow(float window_x, float window_y, float window_w, float window_h,
                             float art_w, float art_h) {
    Placement placement;
    if (art_w <= 0.0f || art_h <= 0.0f || window_w <= 0.0f || window_h <= 0.0f) return placement;
    const float scale = std::min(window_w / art_w, window_h / art_h);
    placement.width = art_w * scale;
    placement.height = art_h * scale;
    placement.x = window_x + (window_w - placement.width) * 0.5f;
    placement.y = window_y + (window_h - placement.height) * 0.5f;
    return placement;
}

// Card-local placement of the two pieces of card art.
inline Placement avatarPlacement(float art_w, float art_h) {
    return fitInWindow(kAvatarWindowX, kAvatarWindowY, kAvatarWindowSize, kAvatarWindowSize, art_w,
                       art_h);
}

inline Placement arenaScenePlacement(float art_w, float art_h) {
    return fitInWindow(kArenaSceneWindowX, kArenaSceneWindowY, kArenaSceneWindowWidth,
                       kArenaSceneWindowHeight, art_w, art_h);
}

// ---------------------------------------------------------------------------
// Hit tests. All of them take logical canvas coordinates.
// ---------------------------------------------------------------------------
inline bool insideRect(float x, float y, float left, float top, float width, float height) {
    return x >= left && x <= left + width && y >= top && y <= top + height;
}

inline int mainEntryAt(float x, float y) {
    for (int i = 0; i < kMainEntryCount; ++i) {
        if (insideRect(x, y, kMainStripX, mainRowY(i), kMainStripWidth, kMainStripHeight)) return i;
    }
    return -1;
}

inline int modeEntryAt(float x, float y) {
    for (int i = 0; i < kModeCount; ++i) {
        if (insideRect(x, y, modeStripX(i), modeRowY(i), modeStripWidth(i), modeStripHeight(i))) {
            return i;
        }
    }
    return -1;
}

inline int superstarCardAt(float x, float y) {
    for (int column = 0; column < kGridColumns; ++column) {
        for (int row = 0; row < kGridRows; ++row) {
            if (insideRect(x, y, superstarCardX(column), superstarCardY(row), kCardSize,
                           kCardNameInkOffset + kCardNameLineStep)) {
                return gridSlot(column, row);
            }
        }
    }
    return -1;
}

inline int arenaCardAt(float x, float y) {
    for (int column = 0; column < kArenaColumns; ++column) {
        for (int row = 0; row < kArenaRows; ++row) {
            if (insideRect(x, y, arenaCardX(column), arenaCardY(row), kArenaCardWidth,
                           kArenaNameInkOffset + kArenaNameLineStep)) {
                return arenaGridSlot(column, row);
            }
        }
    }
    return -1;
}

inline bool backHit(float x, float y) {
    return insideRect(x, y, kBackX, kBackY, kBackSize, kBackSize);
}

inline bool nextArrowHit(float x, float y) {
    return insideRect(x, y, kArrowNextX, kArrowY, kArrowWidth, kArrowHeight);
}

inline bool previousArrowHit(float x, float y) {
    return insideRect(x, y, kArrowPreviousX, kArrowY, kArrowWidth, kArrowHeight);
}

}  // namespace menu
}  // namespace wrestlefest

#endif  // WRESTLEFEST_MENU_LAYOUT_H
