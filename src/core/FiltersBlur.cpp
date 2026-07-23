// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Filters.hpp"
#include "core/FiltersCommon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

namespace px {

using namespace filter_detail;
void apply_square_blur(Document& doc, int radius) {
    const int rad = std::clamp(radius, 1, 32);
    transform_from_source(doc, "Square Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        return blur_at(source, doc.width, doc.height, x, y, rad);
    });
}

void apply_gaussian_blur(Document& doc, int radius) {
    const int rad = std::clamp(radius, 1, 32);
    transform_from_source(doc, "Gaussian Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        float weights = 0.0f;
        const float sigma = std::max(1.0f, static_cast<float>(rad) * 0.5f);
        const float sigma2 = 2.0f * sigma * sigma;
        for (int yy = y - rad; yy <= y + rad; ++yy) {
            for (int xx = x - rad; xx <= x + rad; ++xx) {
                const float dx = static_cast<float>(xx - x);
                const float dy = static_cast<float>(yy - y);
                const float weight = std::exp(-(dx * dx + dy * dy) / sigma2);
                const Pixel pixel = sample_clamped(source, doc.width, doc.height, xx, yy);
                red += static_cast<float>(r(pixel)) * weight;
                green += static_cast<float>(g(pixel)) * weight;
                blue += static_cast<float>(b(pixel)) * weight;
                alpha += static_cast<float>(a(pixel)) * weight;
                weights += weight;
            }
        }
        return rgba(to_u8(red / weights), to_u8(green / weights), to_u8(blue / weights), to_u8(alpha / weights));
    });
}

void apply_motion_blur(Document& doc, int distance, float angle_degrees) {
    const int dist = std::clamp(distance, 1, 128);
    const float angle = angle_degrees * 3.14159265358979323846f / 180.0f;
    const float dx = std::cos(angle);
    const float dy = std::sin(angle);
    transform_from_source(doc, "Motion Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        int count = 0;
        for (int i = -dist; i <= dist; ++i) {
            const Pixel pixel = sample_nearest(source,
                                              doc.width,
                                              doc.height,
                                              static_cast<float>(x) + dx * static_cast<float>(i),
                                              static_cast<float>(y) + dy * static_cast<float>(i));
            red += static_cast<float>(r(pixel));
            green += static_cast<float>(g(pixel));
            blue += static_cast<float>(b(pixel));
            alpha += static_cast<float>(a(pixel));
            ++count;
        }
        const float denom = static_cast<float>(count);
        return rgba(to_u8(red / denom), to_u8(green / denom), to_u8(blue / denom), to_u8(alpha / denom));
    });
}

void apply_radial_blur(Document& doc, int amount, int center_x_percent, int center_y_percent) {
    const int samples = std::clamp(amount, 2, 64);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * static_cast<float>(std::clamp(center_x_percent, 0, 100)) / 100.0f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * static_cast<float>(std::clamp(center_y_percent, 0, 100)) / 100.0f;
    transform_from_source(doc, "Radial Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        for (int i = 0; i < samples; ++i) {
            const float t = (static_cast<float>(i) / static_cast<float>(samples - 1) - 0.5f) * 0.12f;
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float cos_a = std::cos(t);
            const float sin_a = std::sin(t);
            const Pixel pixel = sample_nearest(source, doc.width, doc.height, cx + cos_a * dx - sin_a * dy, cy + sin_a * dx + cos_a * dy);
            red += static_cast<float>(r(pixel));
            green += static_cast<float>(g(pixel));
            blue += static_cast<float>(b(pixel));
            alpha += static_cast<float>(a(pixel));
        }
        const float denom = static_cast<float>(samples);
        return rgba(to_u8(red / denom), to_u8(green / denom), to_u8(blue / denom), to_u8(alpha / denom));
    });
}

void apply_zoom_blur(Document& doc, int amount, int center_x_percent, int center_y_percent) {
    const int samples = std::clamp(amount, 2, 64);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * static_cast<float>(std::clamp(center_x_percent, 0, 100)) / 100.0f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * static_cast<float>(std::clamp(center_y_percent, 0, 100)) / 100.0f;
    transform_from_source(doc, "Zoom Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        for (int i = 0; i < samples; ++i) {
            const float t = 1.0f - 0.22f * static_cast<float>(i) / static_cast<float>(samples - 1);
            const Pixel pixel = sample_nearest(source,
                                              doc.width,
                                              doc.height,
                                              cx + (static_cast<float>(x) - cx) * t,
                                              cy + (static_cast<float>(y) - cy) * t);
            red += static_cast<float>(r(pixel));
            green += static_cast<float>(g(pixel));
            blue += static_cast<float>(b(pixel));
            alpha += static_cast<float>(a(pixel));
        }
        const float denom = static_cast<float>(samples);
        return rgba(to_u8(red / denom), to_u8(green / denom), to_u8(blue / denom), to_u8(alpha / denom));
    });
}

void apply_median_blur(Document& doc, int radius) {
    const int rad = std::clamp(radius, 1, 8);
    transform_from_source(doc, "Median Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        std::vector<int> red;
        std::vector<int> green;
        std::vector<int> blue;
        std::vector<int> alpha;
        const std::size_t reserve = static_cast<std::size_t>((rad * 2 + 1) * (rad * 2 + 1));
        red.reserve(reserve);
        green.reserve(reserve);
        blue.reserve(reserve);
        alpha.reserve(reserve);
        for (int yy = y - rad; yy <= y + rad; ++yy) {
            for (int xx = x - rad; xx <= x + rad; ++xx) {
                const Pixel pixel = sample_clamped(source, doc.width, doc.height, xx, yy);
                red.push_back(r(pixel));
                green.push_back(g(pixel));
                blue.push_back(b(pixel));
                alpha.push_back(a(pixel));
            }
        }
        const auto mid = red.begin() + static_cast<std::ptrdiff_t>(red.size() / 2U);
        std::nth_element(red.begin(), mid, red.end());
        std::nth_element(green.begin(), green.begin() + static_cast<std::ptrdiff_t>(green.size() / 2U), green.end());
        std::nth_element(blue.begin(), blue.begin() + static_cast<std::ptrdiff_t>(blue.size() / 2U), blue.end());
        std::nth_element(alpha.begin(), alpha.begin() + static_cast<std::ptrdiff_t>(alpha.size() / 2U), alpha.end());
        return rgba(to_u8_int(red[red.size() / 2U]),
                    to_u8_int(green[green.size() / 2U]),
                    to_u8_int(blue[blue.size() / 2U]),
                    to_u8_int(alpha[alpha.size() / 2U]));
    });
}

void apply_surface_blur(Document& doc, int radius, int threshold) {
    const int rad = std::clamp(radius, 1, 16);
    const int max_distance = std::clamp(threshold, 0, 255);
    transform_from_source(doc, "Surface Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel center = source[static_cast<std::size_t>(doc.pixel_index(x, y))];
        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        int count = 0;
        for (int yy = y - rad; yy <= y + rad; ++yy) {
            for (int xx = x - rad; xx <= x + rad; ++xx) {
                const Pixel pixel = sample_clamped(source, doc.width, doc.height, xx, yy);
                if (color_distance(center, pixel, true) <= max_distance) {
                    red += r(pixel);
                    green += g(pixel);
                    blue += b(pixel);
                    alpha += a(pixel);
                    ++count;
                }
            }
        }
        if (count == 0) {
            return center;
        }
        return rgba(to_u8_int(red / count), to_u8_int(green / count), to_u8_int(blue / count), to_u8_int(alpha / count));
    });
}

void apply_sketch_blur(Document& doc, int radius) {
    const int rad = std::clamp(radius, 1, 16);
    transform_from_source(doc, "Sketch Blur", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel center = sample_clamped(source, doc.width, doc.height, x, y);
        const Pixel smooth = blur_at(source, doc.width, doc.height, x, y, rad);
        const float edge = std::abs(luminance(center) - luminance(smooth));
        const auto channel = to_u8(255.0f - edge * 2.4f);
        return rgba(channel, channel, channel, a(center));
    });
}
} // namespace px
