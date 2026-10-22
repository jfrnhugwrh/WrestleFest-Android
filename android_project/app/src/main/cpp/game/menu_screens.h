#ifndef WRESTLEFEST_MENU_SCREENS_H
#define WRESTLEFEST_MENU_SCREENS_H

// Front-end screens: the loading card, the title, the main menu, the game mode
// list and the two selector grids.
//
// The drawing follows the original scenes (TitleMenu.cpp) through the geometry
// in menu_layout.h: a backdrop, a caption drawn in the tyF font with the
// original's colours, one strip per entry, and the shipped selector frames for
// the grids. Keeping it in its own file leaves GameApp with the flow only.

#include "game_renderer.h"
#include "menu_layout.h"
#include "road.h"

#include <string>

namespace wrestlefest {

class MenuScreens {
public:
    void attach(GameRenderer *renderer, const GameData *data) {
        renderer_ = renderer;
        data_ = data;
    }

    // Loading card, shown while the tables and the atlas load.
    void drawLoading(float clock, bool failed, const std::string &error);
    void drawTitle(float clock);

    void drawMainMenu(int selected);
    void drawModes(int selected);
    // One page of the superstar grid (eight cards). `selected` is a slot on
    // this page (-1 when nothing is picked yet).
    void drawSuperstarPage(int page, int selected, int page_count);
    void drawArenaPage(int page, int selected, int page_count);

    // Road to WrestleMania: one card of the ladder at a time, the level name
    // above it and the match name below. `cursor` is the card being looked at;
    // cards past the run's progress are locked behind it.
    void drawRoad(const road::Progress &progress, int cursor, const std::string &result);

    // Small centred message used for the modes this build cannot start yet.
    void drawNotice(const std::string &text, float alpha);

private:
    void drawBackdrop(const Texture &background);
    void drawHeading(const char *text, const Color &color);
    void drawBackButton(float alpha);
    void drawDots(int page, int page_count);
    void drawArrows(int page, int page_count);
    void drawCaption(const std::string &text, float center_x, float ink_top, float size,
                     const Color &color, float tracking = menu::kLabelTracking);
    void drawCardName(const WrestlerProfile &profile, float center_x, float ink_top);

    GameRenderer *renderer_ = nullptr;
    const GameData *data_ = nullptr;
};

}  // namespace wrestlefest

#endif  // WRESTLEFEST_MENU_SCREENS_H
