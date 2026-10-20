#ifndef LEGACY_VIEWPORT_MANAGER_H
#define LEGACY_VIEWPORT_MANAGER_H

namespace legacy_viewport {

struct Size {
    int width;
    int height;
};

struct Point {
    double x;
    double y;
};

struct Rect {
    double left;
    double top;
    double right;
    double bottom;
};

struct IntRect {
    int x;
    int y;
    int width;
    int height;
};

class ViewportManager {
public:
    explicit ViewportManager(int logical_width = 480, int logical_height = 320) noexcept;

    bool setLogicalSize(int width, int height) noexcept;
    bool setSurfaceSize(int width, int height) noexcept;

    bool ready() const noexcept;
    Size logicalSize() const noexcept;
    Point logicalHalfExtents() const noexcept;
    Size surfaceSize() const noexcept;
    double scale() const noexcept;
    Rect contentRect() const noexcept;

    // Touch input uses Android's top-left origin and surface pixel coordinates.
    // Touches in the letterbox/pillarbox bars are rejected.
    bool mapTouchToLogical(Point surface_point, Point* logical_point) const noexcept;

    // UI, sprite and hitbox rectangles use top-left-origin logical coordinates.
    // Keep simulation hitboxes logical if touch input is mapped back to logical space.
    bool mapRectToPixels(const Rect& logical_rect, Rect* pixel_rect) const noexcept;
    bool mapHitboxToPixels(const Rect& logical_hitbox, Rect* pixel_hitbox) const noexcept;

    // OpenGL viewport and scissor rectangles use a bottom-left origin.
    bool mapGlRect(const IntRect& logical_rect, IntRect* pixel_rect) const noexcept;

private:
    Size logical_;
    Size surface_;
    double scale_;
    Rect content_;
};

using GlRectFunction = void (*)(int x, int y, int width, int height);

// Redirect these wrappers from the GL hook backend in place of glViewport/glScissor.
void forwardGlViewport(const ViewportManager& manager,
                       GlRectFunction original,
                       int x,
                       int y,
                       int width,
                       int height) noexcept;
void forwardGlScissor(const ViewportManager& manager,
                      GlRectFunction original,
                      int x,
                      int y,
                      int width,
                      int height) noexcept;

}  // namespace legacy_viewport

#endif
