#include "menu_screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace wrestlefest {

namespace {

// Splits a caption into at most `lines` lines that each fit in `max_width`.
std::vector<std::string> wrapText(const std::string &text, float max_width,
                                  const BitmapFont &font, float scale, int lines) {
    std::vector<std::string> out;
    std::string current;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t space = text.find(' ', start);
        const std::string word = text.substr(start, space == std::string::npos ? std::string::npos
                                                                              : space - start);
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && font.measure(candidate, scale) > max_width) {
            out.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
        if (space == std::string::npos) break;
        start = space + 1;
    }
    if (!current.empty()) out.push_back(current);
    if (static_cast<int>(out.size()) > lines) {
        out.resize(static_cast<std::size_t>(lines));
        out.back() += "...";
    }
    return out;
}

}  // namespace

void MenuScreens::drawBackdrop(const Texture &background) {
    SpriteBatch &batch = renderer_->batch();
    if (background.valid()) {
        batch.draw(background, 0.0f, 0.0f, menu::kCanvasWidth, menu::kCanvasHeight, Color{});
    } else {
        batch.draw(renderer_->white(), 0.0f, 0.0f, menu::kCanvasWidth, menu::kCanvasHeight,
                   Color{0.10f, 0.12f, 0.16f, 1.0f});
    }
}

void MenuScreens::drawHeading(const char *text, const Color &color) {
    renderer_->drawMenuTextCentered(
        text, menu::kCanvasWidth * 0.5f, menu::headingInkTop(renderer_->menuFont().metrics()),
        menu::kHeadingSize, color, menu::kHeadingTracking, menu::kHeadingShadow);
}

void MenuScreens::drawCaption(const std::string &text, float center_x, float ink_top, float size,
                              const Color &color, float tracking) {
    renderer_->drawMenuTextCentered(text, center_x, ink_top, size, color, tracking,
                                    menu::kLabelShadow);
}

void MenuScreens::drawBackButton(float alpha) {
    const Texture &back = renderer_->backButton();
    if (!back.valid()) return;
    renderer_->batch().draw(back, menu::kBackX, menu::kBackY, menu::kBackSize, menu::kBackSize,
                            Color{1.0f, 1.0f, 1.0f, alpha});
}

void MenuScreens::drawDots(int page, int page_count) {
    if (page_count < 2) return;
    SpriteBatch &batch = renderer_->batch();
    for (int i = 0; i < page_count; ++i) {
        const Texture &dot = i == page ? renderer_->arrowPointSelect() : renderer_->arrowPoint();
        if (!dot.valid()) continue;
        batch.draw(dot, menu::dotX(i, page_count), menu::kDotY, menu::kDotSize, menu::kDotSize,
                   Color{});
    }
}

void MenuScreens::drawArrows(int page, int page_count) {
    SpriteBatch &batch = renderer_->batch();
    if (page > 0 && renderer_->arrowPrevious().valid()) {
        batch.draw(renderer_->arrowPrevious(), 26.0f, menu::kArrowY, menu::kArrowWidth,
                   menu::kArrowHeight, Color{});
    }
    if (page + 1 < page_count && renderer_->arrowNext().valid()) {
        batch.draw(renderer_->arrowNext(), menu::kCanvasWidth - 68.0f, menu::kArrowY,
                   menu::kArrowWidth, menu::kArrowHeight, Color{});
    }
}

// ---------------------------------------------------------------------------
// Loading card: the tilted logo over the dimmed backdrop, "Loading ..." in the
// bottom right corner. It stands in for the original's attract/loading scene.
// ---------------------------------------------------------------------------
void MenuScreens::drawLoading(float clock, bool failed, const std::string &error) {
    SpriteBatch &batch = renderer_->batch();
    // background_2.png is the shipped loading card: the menu backdrop with the
    // WrestleFest logo already ghosted into it, tilted. The reference capture is
    // that art with "Loading ..." drawn in the corner, so nothing else goes on
    // top of it - an extra logo doubled the ghost and a tint darkened a plate
    // that is already the right colour (its corners are the capture's corners).
    if (renderer_->uiBackground2().valid()) {
        batch.draw(renderer_->uiBackground2(), 0.0f, 0.0f, menu::kCanvasWidth,
                   menu::kCanvasHeight, Color{});
    } else if (renderer_->titleLogo().valid()) {
        const float width = 585.0f;
        const float height = width * 204.0f / 790.0f;
        renderer_->drawRotated(renderer_->titleLogo(), menu::kCanvasWidth * 0.5f, 187.0f, width,
                               height, menu::kLoadingLogoTilt, Color{1.0f, 1.0f, 1.0f, 1.0f});
    } else {
        batch.draw(renderer_->white(), 0.0f, 0.0f, menu::kCanvasWidth, menu::kCanvasHeight,
                   Color{0.14f, 0.17f, 0.21f, 1.0f});
    }

    if (failed) {
        drawCaption("COULD NOT LOAD THE GAME DATA", 480.0f, 470.0f, 40.0f,
                    Color{1.0f, 0.45f, 0.4f, 1.0f});
        if (!error.empty()) {
            drawCaption(error, 480.0f, 512.0f, 26.0f, Color{0.9f, 0.9f, 0.95f, 1.0f});
        }
        return;
    }

    // "Loading ..." with the dots cycling (the reference capture catches it on
    // its first dot), bottom right, in the light grey it uses rather than white.
    const int dots = 1 + (static_cast<int>(clock * 2.0f) % 3);
    std::string label = "Loading ";
    label.append(static_cast<std::size_t>(dots), '.');
    const float size = menu::kLoadingLabelSize;
    const Color color{0.80f, 0.85f, 0.88f, 1.0f};
    const float width = renderer_->measureMenuText(label, size, menu::kLabelTracking);
    renderer_->drawMenuText(label, menu::kCanvasWidth - menu::kLoadingLabelRight - width,
                            menu::kLoadingLabelInkTop, size, color, menu::kLabelTracking,
                            menu::kNameShadow);
}

// ---------------------------------------------------------------------------
// Title / attract card: the logo and the blinking "tap to start".
// ---------------------------------------------------------------------------
void MenuScreens::drawTitle(float clock) {
    SpriteBatch &batch = renderer_->batch();
    drawBackdrop(renderer_->uiBackground());
    if (renderer_->titleLogo().valid()) {
        const float width = 790.0f * 0.85f;
        const float height = 204.0f * 0.85f;
        batch.draw(renderer_->titleLogo(), menu::kCanvasWidth * 0.5f - width * 0.5f, 120.0f,
                   width, height, Color{});
    } else {
        drawHeading("WRESTLEFEST", menu::titleRed());
    }

    const float pulse = 0.55f + 0.45f * std::fabs(std::sin(clock * 2.2f));
    if (renderer_->tapToStart().valid()) {
        const float w = 374.0f;
        const float h = 47.0f;
        // taptostart_en.png sits at (289, 439) in the original's title menu.
        batch.draw(renderer_->tapToStart(), menu::kCanvasWidth * 0.5f - w * 0.5f, 439.0f, w, h,
                   Color{1.0f, 1.0f, 1.0f, pulse});
    } else {
        drawCaption("TAP TO START", 480.0f, 452.0f, 40.0f,
                    Color{1.0f, 1.0f, 1.0f, pulse});
    }
}

// ---------------------------------------------------------------------------
// Main menu: MAIN MENU / GAME MODES / MULTIPLAYER / SHOP / OPTIONS.
// ---------------------------------------------------------------------------
void MenuScreens::drawMainMenu(int selected) {
    SpriteBatch &batch = renderer_->batch();
    drawBackdrop(renderer_->uiBackground());
    drawHeading("MAIN MENU", menu::titleRed());

    for (int i = 0; i < menu::kMainEntryCount; ++i) {
        const float y = menu::mainRowY(i);
        const Texture &strip = renderer_->mainButton(i);
        if (strip.valid()) {
            batch.draw(strip, menu::kMainStripX, y, menu::kMainStripWidth, menu::kMainStripHeight,
                       Color{});
        } else {
            batch.draw(renderer_->white(), menu::kMainStripX, y, menu::kMainStripWidth,
                       menu::kMainStripHeight, Color{0.55f, 0.58f, 0.65f, 0.35f});
        }
        const Color color = i == selected ? menu::selectedText() : menu::normalText();
        const FontMetrics &metrics = renderer_->menuFont().metrics();
        const float ink_top =
            y + menu::centreInBox(metrics, 0.0f, menu::kMainStripHeight, menu::kMainLabelSize);
        drawCaption(menu::mainEntryLabel(i), menu::kCanvasWidth * 0.5f, ink_top,
                    menu::kMainLabelSize, color, menu::kLabelTracking);
    }

    // The WWE mark sits to the left of the centred SHOP caption.
    if (renderer_->wweLogo().valid()) {
        const float shop_width = renderer_->measureMenuText(
            menu::mainEntryLabel(2), menu::kMainLabelSize, menu::kLabelTracking);
        batch.draw(renderer_->wweLogo(), menu::wweLogoX(shop_width), menu::kWweLogoY,
                   menu::kWweLogoWidth,
                   renderer_->wweLogo().height * menu::kWweLogoScale, Color{});
    }

    // The main menu is the top of the stack: the original still draws the bar
    // but leaves the arrow inert, so it is shown faded.
    drawBackButton(0.5f);
}

// ---------------------------------------------------------------------------
// Game modes.
// ---------------------------------------------------------------------------
void MenuScreens::drawModes(int selected) {
    SpriteBatch &batch = renderer_->batch();
    drawBackdrop(renderer_->uiBackground());
    drawHeading("GAME MODES", menu::titleRed());

    for (int i = 0; i < menu::kModeCount; ++i) {
        const float width = menu::modeStripWidth(i);
        const float height = menu::modeStripHeight(i);
        const float x = menu::modeStripX(i);
        const float y = menu::modeRowY(i);
        // Each row uses its own plate: the original loads all six, and they
        // differ in size (and in the width of the gradient they carry).
        const Texture &strip = renderer_->modeButton(i);
        if (strip.valid()) {
            batch.draw(strip, x, y, width, height, Color{});
        } else {
            batch.draw(renderer_->white(), x, y, width, height, Color{0.55f, 0.58f, 0.65f, 0.35f});
        }
        const Color color = i == selected ? menu::selectedText() : menu::normalText();
        const FontMetrics &metrics = renderer_->menuFont().metrics();
        // The long captions shrink to stay inside their strip, as the original
        // does instead of overflowing it.
        float size = menu::kModeLabelSize;
        const float room = width - 32.0f;
        const float natural =
            renderer_->measureMenuText(menu::modeLabel(i), size, menu::kLabelTracking);
        if (natural > room && natural > 0.0f) size *= room / natural;
        drawCaption(menu::modeLabel(i), menu::kCanvasWidth * 0.5f,
                    y + menu::centreInBox(metrics, 0.0f, height, size), size, color,
                    menu::kLabelTracking);
    }

    drawBackButton(1.0f);
}

// ---------------------------------------------------------------------------
// Selector grids.
// ---------------------------------------------------------------------------
void MenuScreens::drawCardName(const WrestlerProfile &profile, float center_x, float ink_top) {
    drawCaption(profile.name_line1, center_x, ink_top, menu::kCardNameSize, menu::normalText(),
                menu::kCardNameTracking);
    if (!profile.name_line2.empty()) {
        drawCaption(profile.name_line2, center_x, ink_top + menu::kCardNameLineStep,
                    menu::kCardNameSize, menu::normalText(), menu::kCardNameTracking);
    }
}

void MenuScreens::drawSuperstarPage(int page, int selected, int page_count) {
    SpriteBatch &batch = renderer_->batch();
    drawBackdrop(renderer_->uiBackground2());
    drawHeading("SELECT SUPERSTAR", menu::titleOrange());

    if (data_ != nullptr) {
        const int count = static_cast<int>(data_->wrestlers().size());
        const int first = page * menu::kGridPageSize;
        for (int slot = 0; slot < menu::kGridPageSize; ++slot) {
            const int index = first + slot;
            if (index >= count) break;
            const WrestlerProfile &profile = data_->wrestlers()[static_cast<std::size_t>(index)];
            const int column = slot / menu::kGridRows;
            const int row = slot % menu::kGridRows;
            const float x = menu::superstarCardX(column);
            const float y = menu::superstarCardY(row);

            const Texture &frame =
                slot == selected ? renderer_->superstarSelect() : renderer_->superstarFrame();
            if (frame.valid()) {
                batch.draw(frame, x, y, menu::kCardSize, menu::kCardSize, Color{});
            } else {
                batch.draw(renderer_->white(), x, y, menu::kCardSize, menu::kCardSize,
                           Color{0.2f, 0.25f, 0.35f, 1.0f});
            }
            const Texture &avatar = renderer_->avatarFor(profile.folder);
            if (avatar.valid()) {
                // The avatar is cropped to cover the frame's window, so the
                // portrait fills it with no grey border or letterbox band.
                const menu::CoverCrop crop = menu::avatarCrop(
                    static_cast<float>(avatar.width), static_cast<float>(avatar.height));
                batch.drawRegion(avatar, x + menu::kAvatarWindowX, y + menu::kAvatarWindowY,
                                 menu::kAvatarWindowSize, menu::kAvatarWindowSize, crop.u0,
                                 crop.v0, crop.u1, crop.v1,
                                 slot == selected ? Color{} : Color{0.94f, 0.94f, 0.97f, 1.0f});
            }
            drawCardName(profile, x + menu::kCardSize * 0.5f, y + menu::kCardNameInkOffset);
        }
    }

    drawArrows(page, page_count);
    drawDots(page, page_count);
    drawBackButton(1.0f);
}

void MenuScreens::drawArenaPage(int page, int selected, int page_count) {
    SpriteBatch &batch = renderer_->batch();
    drawBackdrop(renderer_->uiBackground2());
    drawHeading("SELECT ARENA", menu::titleOrange());

    if (data_ != nullptr) {
        const int count = static_cast<int>(data_->arenas().size());
        const int first = page * menu::kArenaPageSize;
        for (int slot = 0; slot < menu::kArenaPageSize; ++slot) {
            const int index = first + slot;
            if (index >= count) break;
            const ArenaInfo &arena = data_->arenas()[static_cast<std::size_t>(index)];
            const int column = slot / menu::kArenaRows;
            const int row = slot % menu::kArenaRows;
            const float x = menu::arenaCardX(column);
            const float y = menu::arenaCardY(row);

            const Texture &frame =
                slot == selected ? renderer_->arenaSelect() : renderer_->arenaFrame();
            if (frame.valid()) {
                batch.draw(frame, x, y, menu::kArenaCardWidth, menu::kArenaCardHeight, Color{});
            } else {
                batch.draw(renderer_->white(), x, y, menu::kArenaCardWidth, menu::kArenaCardHeight,
                           Color{0.2f, 0.25f, 0.35f, 1.0f});
            }
            const Texture &scene = renderer_->arenaSceneFor(arena.folder);
            if (scene.valid()) {
                // scene.png is 212x146 against a 190x125 window: its white rim is
                // cut away and the rest cropped to cover the window.
                const menu::CoverCrop crop = menu::arenaSceneCrop(
                    static_cast<float>(scene.width), static_cast<float>(scene.height));
                batch.drawRegion(scene, x + menu::kArenaSceneWindowX,
                                 y + menu::kArenaSceneWindowY, menu::kArenaSceneWindowWidth,
                                 menu::kArenaSceneWindowHeight, crop.u0, crop.v0, crop.u1,
                                 crop.v1, Color{});
            }

            // Long venue names wrap inside the card.
            const std::vector<std::string> lines =
                wrapText(arena.name, menu::kArenaCardWidth - 10.0f, renderer_->menuFont(),
                         menu::scaleForSize(renderer_->menuFont().metrics(),
                                            menu::kArenaNameSize),
                         2);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                drawCaption(lines[i], x + menu::kArenaCardWidth * 0.5f,
                            y + menu::kArenaNameInkOffset +
                                static_cast<float>(i) * menu::kArenaNameLineStep,
                            menu::kArenaNameSize, menu::normalText(), menu::kArenaNameTracking);
            }
        }
    }

    drawArrows(page, page_count);
    drawDots(page, page_count);
    drawBackButton(1.0f);
}

void MenuScreens::drawNotice(const std::string &text, float alpha) {
    if (alpha <= 0.0f) return;
    SpriteBatch &batch = renderer_->batch();
    const float size = 34.0f;
    const float width =
        std::max(560.0f, renderer_->measureMenuText(text, size, menu::kLabelTracking) + 80.0f);
    const float x = (menu::kCanvasWidth - width) * 0.5f;
    batch.draw(renderer_->white(), x, 486.0f, width, 68.0f,
               Color{0.05f, 0.07f, 0.10f, 0.78f * alpha});
    renderer_->drawMenuTextCentered(text, menu::kCanvasWidth * 0.5f, 506.0f, size,
                                    Color{1.0f, 1.0f, 1.0f, alpha}, menu::kLabelTracking,
                                    menu::kLabelShadow);
}

}  // namespace wrestlefest
