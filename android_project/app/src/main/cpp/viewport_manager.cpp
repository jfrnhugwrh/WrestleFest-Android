#include "viewport_manager.h"

#include <cmath>
#include <limits>

namespace legacy_viewport {
namespace {

bool isFinite(const Rect& rect) noexcept {
    return std::isfinite(rect.left) && std::isfinite(rect.top) &&
           std::isfinite(rect.right) && std::isfinite(rect.bottom);
}

bool roundToInt(double value, int* result) noexcept {
    if (result == nullptr || !std::isfinite(value)) {
        return false;
    }
    const double rounded = std::round(value);
    if (rounded < std::numeric_limits<int>::min() ||
        rounded > std::numeric_limits<int>::max()) {
        return false;
    }
    *result = static_cast<int>(rounded);
    return true;
}

}  // namespace

ViewportManager::ViewportManager(int logical_width, int logical_height) noexcept
    : logical_{0, 0}, surface_{0, 0}, scale_(0.0), content_{0.0, 0.0, 0.0, 0.0} {
    setLogicalSize(logical_width, logical_height);
}

bool ViewportManager::setLogicalSize(int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }
    logical_ = {width, height};

    if (surface_.width > 0 && surface_.height > 0) {
        setSurfaceSize(surface_.width, surface_.height);
    }
    return true;
}

bool ViewportManager::setSurfaceSize(int width, int height) noexcept {
    if (width <= 0 || height <= 0) {
        surface_ = {0, 0};
        scale_ = 0.0;
        content_ = {0.0, 0.0, 0.0, 0.0};
        return false;
    }
    surface_ = {width, height};
    if (logical_.width <= 0 || logical_.height <= 0) {
        scale_ = 0.0;
        content_ = {0.0, 0.0, 0.0, 0.0};
        return false;
    }

    scale_ = std::fmin(static_cast<double>(width) / logical_.width,
                       static_cast<double>(height) / logical_.height);
    const double content_width = logical_.width * scale_;
    const double content_height = logical_.height * scale_;
    const double left = (width - content_width) * 0.5;
    const double top = (height - content_height) * 0.5;
    content_ = {left, top, left + content_width, top + content_height};
    return true;
}

bool ViewportManager::ready() const noexcept {
    return logical_.width > 0 && logical_.height > 0 &&
           surface_.width > 0 && surface_.height > 0 && scale_ > 0.0;
}

Size ViewportManager::logicalSize() const noexcept {
    return logical_;
}

Point ViewportManager::logicalHalfExtents() const noexcept {
    return {logical_.width * 0.5, logical_.height * 0.5};
}

Size ViewportManager::surfaceSize() const noexcept {
    return surface_;
}

double ViewportManager::scale() const noexcept {
    return scale_;
}

Rect ViewportManager::contentRect() const noexcept {
    return content_;
}

bool ViewportManager::mapTouchToLogical(Point surface_point, Point* logical_point) const noexcept {
    if (!ready() || logical_point == nullptr ||
        !std::isfinite(surface_point.x) || !std::isfinite(surface_point.y) ||
        surface_point.x < content_.left || surface_point.x >= content_.right ||
        surface_point.y < content_.top || surface_point.y >= content_.bottom) {
        return false;
    }

    logical_point->x = (surface_point.x - content_.left) / scale_;
    logical_point->y = (surface_point.y - content_.top) / scale_;
    return true;
}

bool ViewportManager::mapRectToPixels(const Rect& logical_rect, Rect* pixel_rect) const noexcept {
    if (!ready() || pixel_rect == nullptr || !isFinite(logical_rect) ||
        logical_rect.right < logical_rect.left || logical_rect.bottom < logical_rect.top) {
        return false;
    }

    const Rect mapped{
        content_.left + logical_rect.left * scale_,
        content_.top + logical_rect.top * scale_,
        content_.left + logical_rect.right * scale_,
        content_.top + logical_rect.bottom * scale_,
    };
    if (!isFinite(mapped)) {
        return false;
    }
    *pixel_rect = mapped;
    return true;
}

bool ViewportManager::mapHitboxToPixels(const Rect& logical_hitbox, Rect* pixel_hitbox) const noexcept {
    return mapRectToPixels(logical_hitbox, pixel_hitbox);
}

bool ViewportManager::mapGlRect(const IntRect& logical_rect, IntRect* pixel_rect) const noexcept {
    if (!ready() || pixel_rect == nullptr ||
        logical_rect.width < 0 || logical_rect.height < 0) {
        return false;
    }

    const double gl_content_bottom = surface_.height - content_.bottom;
    int left = 0;
    int bottom = 0;
    int right = 0;
    int top = 0;
    if (!roundToInt(content_.left + logical_rect.x * scale_, &left) ||
        !roundToInt(gl_content_bottom + logical_rect.y * scale_, &bottom) ||
        !roundToInt(content_.left +
                        (static_cast<double>(logical_rect.x) + logical_rect.width) * scale_,
                    &right) ||
        !roundToInt(gl_content_bottom +
                        (static_cast<double>(logical_rect.y) + logical_rect.height) * scale_,
                    &top)) {
        return false;
    }

    const long long mapped_width = static_cast<long long>(right) - left;
    const long long mapped_height = static_cast<long long>(top) - bottom;
    if (mapped_width < 0 || mapped_width > std::numeric_limits<int>::max() ||
        mapped_height < 0 || mapped_height > std::numeric_limits<int>::max()) {
        return false;
    }
    *pixel_rect = {left, bottom, static_cast<int>(mapped_width), static_cast<int>(mapped_height)};
    return true;
}

void forwardGlViewport(const ViewportManager& manager,
                       GlRectFunction original,
                       int x,
                       int y,
                       int width,
                       int height) noexcept {
    if (original == nullptr) {
        return;
    }

    IntRect mapped{};
    if (manager.mapGlRect({x, y, width, height}, &mapped)) {
        original(mapped.x, mapped.y, mapped.width, mapped.height);
    } else {
        original(x, y, width, height);
    }
}

void forwardGlScissor(const ViewportManager& manager,
                      GlRectFunction original,
                      int x,
                      int y,
                      int width,
                      int height) noexcept {
    if (original == nullptr) {
        return;
    }

    IntRect mapped{};
    if (manager.mapGlRect({x, y, width, height}, &mapped)) {
        original(mapped.x, mapped.y, mapped.width, mapped.height);
    } else {
        original(x, y, width, height);
    }
}

}  // namespace legacy_viewport
