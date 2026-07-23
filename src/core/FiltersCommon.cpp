// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/FiltersCommon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace px::filter_detail {

Pixel sample_clamped(const std::vector<Pixel>& pixels, int width, int height, int x, int y) {
    const int sample_x = std::clamp(x, 0, width - 1);
    const int sample_y = std::clamp(y, 0, height - 1);
    return pixels[static_cast<std::size_t>(sample_y * width + sample_x)];
}

Pixel sample_nearest(const std::vector<Pixel>& pixels, int width, int height, float x, float y) {
    const int sample_x = static_cast<int>(std::floor(x + 0.5f));
    const int sample_y = static_cast<int>(std::floor(y + 0.5f));
    if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        return 0;
    }
    return pixels[static_cast<std::size_t>(sample_y * width + sample_x)];
}

static Pixel sample_transparent(const std::vector<Pixel>& pixels, int width, int height, int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return 0;
    }
    return pixels[static_cast<std::size_t>(y * width + x)];
}

static Pixel sample_bilinear(const std::vector<Pixel>& pixels, int width, int height, float x, float y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const Pixel p00 = sample_transparent(pixels, width, height, x0, y0);
    const Pixel p10 = sample_transparent(pixels, width, height, x0 + 1, y0);
    const Pixel p01 = sample_transparent(pixels, width, height, x0, y0 + 1);
    const Pixel p11 = sample_transparent(pixels, width, height, x0 + 1, y0 + 1);
    auto channel = [&](auto fn) {
        const float top = static_cast<float>(fn(p00)) * (1.0f - tx) + static_cast<float>(fn(p10)) * tx;
        const float bottom = static_cast<float>(fn(p01)) * (1.0f - tx) + static_cast<float>(fn(p11)) * tx;
        return to_u8(top * (1.0f - ty) + bottom * ty);
    };
    return rgba(channel(r), channel(g), channel(b), channel(a));
}

static float cubic_weight(float value) {
    value = std::fabs(value);
    if (value <= 1.0f) {
        return 1.0f - 3.0f * value * value + 2.0f * value * value * value;
    }
    if (value < 2.0f) {
        const float t = 2.0f - value;
        return t * t * t;
    }
    return 0.0f;
}

static Pixel sample_bicubic(const std::vector<Pixel>& pixels, int width, int height, float x, float y) {
    const int base_x = static_cast<int>(std::floor(x));
    const int base_y = static_cast<int>(std::floor(y));
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    float total_weight = 0.0f;
    for (int yy = base_y - 1; yy <= base_y + 2; ++yy) {
        const float wy = cubic_weight(y - static_cast<float>(yy));
        for (int xx = base_x - 1; xx <= base_x + 2; ++xx) {
            const float weight = wy * cubic_weight(x - static_cast<float>(xx));
            if (weight <= 0.0f) {
                continue;
            }
            const Pixel pixel = sample_transparent(pixels, width, height, xx, yy);
            red += static_cast<float>(r(pixel)) * weight;
            green += static_cast<float>(g(pixel)) * weight;
            blue += static_cast<float>(b(pixel)) * weight;
            alpha += static_cast<float>(a(pixel)) * weight;
            total_weight += weight;
        }
    }
    if (total_weight <= 0.0f) {
        return 0;
    }
    return rgba(to_u8(red / total_weight),
                to_u8(green / total_weight),
                to_u8(blue / total_weight),
                to_u8(alpha / total_weight));
}

static Pixel sample_resampled(const std::vector<Pixel>& pixels,
                              int width,
                              int height,
                              float x,
                              float y,
                              ResamplingMode mode) {
    switch (mode) {
        case ResamplingMode::Nearest:
            return sample_nearest(pixels, width, height, x, y);
        case ResamplingMode::Bilinear:
            return sample_bilinear(pixels, width, height, x, y);
        case ResamplingMode::Bicubic:
            return sample_bicubic(pixels, width, height, x, y);
    }
    return sample_nearest(pixels, width, height, x, y);
}

Pixel mix_pixels(Pixel first, Pixel second, float amount) {
    const float weight = std::clamp(amount, 0.0f, 1.0f);
    const float inverse_weight = 1.0f - weight;
    return rgba(to_u8(static_cast<float>(r(first)) * inverse_weight +
                      static_cast<float>(r(second)) * weight),
                to_u8(static_cast<float>(g(first)) * inverse_weight +
                      static_cast<float>(g(second)) * weight),
                to_u8(static_cast<float>(b(first)) * inverse_weight +
                      static_cast<float>(b(second)) * weight),
                to_u8(static_cast<float>(a(first)) * inverse_weight +
                      static_cast<float>(a(second)) * weight));
}

void apply_affine(Document& doc,
                  const char* name,
                  float angle_degrees,
                  float zoom,
                  int pan_x,
                  int pan_y,
                  ResamplingMode resampling) {
    const float angle = angle_degrees * 3.14159265358979323846f / 180.0f;
    const float cos_a = std::cos(angle);
    const float sin_a = std::sin(angle);
    const float scale = std::max(0.01f, zoom);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
    transform_from_source(doc, name, [&](int x, int y, const std::vector<Pixel>& source) {
        const float dx = (static_cast<float>(x - pan_x) - cx) / scale;
        const float dy = (static_cast<float>(y - pan_y) - cy) / scale;
        const float src_x = cos_a * dx + sin_a * dy + cx;
        const float src_y = -sin_a * dx + cos_a * dy + cy;
        return sample_resampled(source, doc.width, doc.height, src_x, src_y, resampling);
    });
}

static std::uint32_t hash_u32(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return value;
}

float noise01(int x, int y, int seed) {
    const auto unsigned_x = static_cast<std::uint32_t>(x);
    const auto unsigned_y = static_cast<std::uint32_t>(y);
    const auto unsigned_seed = static_cast<std::uint32_t>(seed);
    return static_cast<float>(
               hash_u32(unsigned_x * 374761393U +
                        unsigned_y * 668265263U +
                        unsigned_seed * 2246822519U) &
               0xffffU) /
           65535.0f;
}

float value_noise(float x, float y, int seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float smooth_x = tx * tx * (3.0f - 2.0f * tx);
    const float smooth_y = ty * ty * (3.0f - 2.0f * ty);
    const float top_left = noise01(x0, y0, seed);
    const float top_right = noise01(x0 + 1, y0, seed);
    const float bottom_left = noise01(x0, y0 + 1, seed);
    const float bottom_right = noise01(x0 + 1, y0 + 1, seed);
    const float top = top_left + (top_right - top_left) * smooth_x;
    const float bottom = bottom_left + (bottom_right - bottom_left) * smooth_x;
    return top + (bottom - top) * smooth_y;
}

Pixel blur_at(const std::vector<Pixel>& source, int width, int height, int x, int y, int radius) {
    const int rad = std::max(0, radius);
    int red = 0;
    int green = 0;
    int blue = 0;
    int alpha = 0;
    int count = 0;
    for (int yy = y - rad; yy <= y + rad; ++yy) {
        for (int xx = x - rad; xx <= x + rad; ++xx) {
            const Pixel pixel = sample_clamped(source, width, height, xx, yy);
            red += r(pixel);
            green += g(pixel);
            blue += b(pixel);
            alpha += a(pixel);
            ++count;
        }
    }
    return rgba(to_u8_int(red / count), to_u8_int(green / count), to_u8_int(blue / count), to_u8_int(alpha / count));
}

int depth_of_field_radius_for_depth(std::uint8_t depth, const DepthOfFieldSettings& settings) {
    const int max_radius = std::clamp(settings.max_radius, 1, 32);
    const float aperture = std::clamp(static_cast<float>(settings.aperture) / 100.0f, 0.0f, 1.0f);
    const float falloff = std::max(1.0f, static_cast<float>(std::clamp(settings.falloff, 1, 100)));
    const float focus = static_cast<float>(std::clamp(settings.focus_depth, 0, 255));
    const float distance = std::abs(static_cast<float>(depth) - focus);
    const float normalized = std::clamp(distance / falloff, 0.0f, 1.0f);
    return static_cast<int>(std::round(normalized * aperture * static_cast<float>(max_radius)));
}

Pixel convolve_at(const std::vector<Pixel>& source,
                  int width,
                  int height,
                  int x,
                  int y,
                  const std::array<float, 9>& kernel,
                  float bias) {
    float red = bias;
    float green = bias;
    float blue = bias;
    float alpha = static_cast<float>(a(sample_clamped(source, width, height, x, y)));
    int k = 0;
    for (int yy = y - 1; yy <= y + 1; ++yy) {
        for (int xx = x - 1; xx <= x + 1; ++xx) {
            const Pixel pixel = sample_clamped(source, width, height, xx, yy);
            const float weight = kernel[static_cast<std::size_t>(k)];
            red += static_cast<float>(r(pixel)) * weight;
            green += static_cast<float>(g(pixel)) * weight;
            blue += static_cast<float>(b(pixel)) * weight;
            ++k;
        }
    }
    return rgba(to_u8(red), to_u8(green), to_u8(blue), to_u8(alpha));
}
} // namespace px::filter_detail
