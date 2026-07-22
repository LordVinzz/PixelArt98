// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace px {

// Pixel coordinates use an exclusive right/bottom edge. Keeping this geometry
// independent from Qt makes the amount of work done by paintGL easy to test.
struct CanvasPixelBounds {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] bool empty() const noexcept { return left >= right || top >= bottom; }
    [[nodiscard]] int width() const noexcept { return std::max(0, right - left); }
    [[nodiscard]] int height() const noexcept { return std::max(0, bottom - top); }
    [[nodiscard]] std::size_t pixel_count() const noexcept {
        return static_cast<std::size_t>(width()) * static_cast<std::size_t>(height());
    }
};

[[nodiscard]] inline CanvasPixelBounds visible_canvas_pixels(double image_left,
                                                               double image_top,
                                                               double zoom,
                                                               int image_width,
                                                               int image_height,
                                                               int viewport_width,
                                                               int viewport_height) noexcept {
    if (zoom <= 0.0 || image_width <= 0 || image_height <= 0 || viewport_width <= 0 || viewport_height <= 0) {
        return {};
    }

    const double clipped_left = std::max(0.0, image_left);
    const double clipped_top = std::max(0.0, image_top);
    const double clipped_right = std::min(static_cast<double>(viewport_width),
                                          image_left + static_cast<double>(image_width) * zoom);
    const double clipped_bottom = std::min(static_cast<double>(viewport_height),
                                           image_top + static_cast<double>(image_height) * zoom);
    if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) {
        return {};
    }

    const int left = std::clamp(static_cast<int>(std::floor((clipped_left - image_left) / zoom)), 0, image_width);
    const int top = std::clamp(static_cast<int>(std::floor((clipped_top - image_top) / zoom)), 0, image_height);
    const int right = std::clamp(static_cast<int>(std::ceil((clipped_right - image_left) / zoom)), left, image_width);
    const int bottom = std::clamp(static_cast<int>(std::ceil((clipped_bottom - image_top) / zoom)), top, image_height);
    return {left, top, right, bottom};
}

} // namespace px
