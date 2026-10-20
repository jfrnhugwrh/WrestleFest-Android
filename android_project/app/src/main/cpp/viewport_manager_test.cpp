#include "viewport_manager.h"

#include <cassert>
#include <cmath>
#include <limits>

using legacy_viewport::IntRect;
using legacy_viewport::Point;
using legacy_viewport::Rect;
using legacy_viewport::ViewportManager;

namespace {

IntRect last_gl_rect{};
int gl_call_count = 0;

void recordGlRect(int x, int y, int width, int height) {
    last_gl_rect = {x, y, width, height};
    ++gl_call_count;
}

bool near(double actual, double expected) {
    return std::fabs(actual - expected) < 0.0001;
}

void testGalaxyLandscapeLetterboxes() {
    ViewportManager viewport;
    assert(viewport.setSurfaceSize(2400, 1080));
    assert(viewport.ready());
    assert(near(viewport.scale(), 3.375));
    assert(near(viewport.logicalHalfExtents().x, 240.0));
    assert(near(viewport.logicalHalfExtents().y, 160.0));

    const Rect content = viewport.contentRect();
    assert(near(content.left, 390.0));
    assert(near(content.top, 0.0));
    assert(near(content.right, 2010.0));
    assert(near(content.bottom, 1080.0));

    IntRect mapped{};
    assert(viewport.mapGlRect({0, 0, 480, 320}, &mapped));
    assert(mapped.x == 390 && mapped.y == 0);
    assert(mapped.width == 1620 && mapped.height == 1080);
}

void testAlternativeLogicalCanvasAndDynamicResize() {
    ViewportManager viewport(960, 640);
    assert(viewport.setSurfaceSize(2400, 1080));

    IntRect mapped{};
    assert(viewport.mapGlRect({0, 0, 960, 640}, &mapped));
    assert(mapped.x == 390 && mapped.y == 0);
    assert(mapped.width == 1620 && mapped.height == 1080);

    assert(viewport.setSurfaceSize(1920, 1080));
    assert(viewport.mapGlRect({0, 0, 960, 640}, &mapped));
    assert(mapped.x == 150 && mapped.width == 1620);
}

void testTouchAndHitboxMapping() {
    ViewportManager viewport;
    assert(viewport.setSurfaceSize(2400, 1080));

    Point logical{};
    assert(!viewport.mapTouchToLogical({389.0, 540.0}, &logical));
    assert(viewport.mapTouchToLogical({390.0, 0.0}, &logical));
    assert(near(logical.x, 0.0) && near(logical.y, 0.0));
    assert(viewport.mapTouchToLogical({1200.0, 540.0}, &logical));
    assert(near(logical.x, 240.0) && near(logical.y, 160.0));
    assert(!viewport.mapTouchToLogical({2010.0, 540.0}, &logical));

    Rect hitbox{};
    assert(viewport.mapHitboxToPixels({100.0, 50.0, 200.0, 250.0}, &hitbox));
    assert(near(hitbox.left, 727.5));
    assert(near(hitbox.top, 168.75));
    assert(near(hitbox.right, 1065.0));
    assert(near(hitbox.bottom, 843.75));
}

void testPillarboxingAndGlHooks() {
    ViewportManager viewport;
    assert(viewport.setSurfaceSize(1080, 2400));

    const Rect content = viewport.contentRect();
    assert(near(content.left, 0.0));
    assert(near(content.top, 840.0));
    assert(near(content.bottom, 1560.0));

    gl_call_count = 0;
    legacy_viewport::forwardGlViewport(viewport, recordGlRect, 0, 0, 480, 320);
    assert(gl_call_count == 1);
    assert(last_gl_rect.x == 0 && last_gl_rect.y == 840);
    assert(last_gl_rect.width == 1080 && last_gl_rect.height == 720);

    legacy_viewport::forwardGlScissor(viewport, recordGlRect, 0, 0, 480, 320);
    assert(gl_call_count == 2);
    assert(last_gl_rect.x == 0 && last_gl_rect.y == 840);
    assert(last_gl_rect.width == 1080 && last_gl_rect.height == 720);
}

void testInvalidDimensionsAndPassThrough() {
    ViewportManager viewport;
    assert(!viewport.setSurfaceSize(0, 1080));
    assert(!viewport.ready());

    gl_call_count = 0;
    legacy_viewport::forwardGlViewport(viewport, recordGlRect, 1, 2, 3, 4);
    assert(gl_call_count == 1);
    assert(last_gl_rect.x == 1 && last_gl_rect.y == 2);
    assert(last_gl_rect.width == 3 && last_gl_rect.height == 4);
    assert(viewport.setSurfaceSize(2400, 1080));
    assert(!viewport.mapGlRect({0, 0, -1, 10}, &last_gl_rect));
    assert(!viewport.mapGlRect(
        {std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::max(), 10},
        &last_gl_rect));
    assert(!viewport.setLogicalSize(0, 320));
    assert(!viewport.setSurfaceSize(0, 1080));
    assert(!viewport.ready());
}

}  // namespace

int main() {
    testGalaxyLandscapeLetterboxes();
    testAlternativeLogicalCanvasAndDynamicResize();
    testTouchAndHitboxMapping();
    testPillarboxingAndGlHooks();
    testInvalidDimensionsAndPassThrough();
}
