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
void apply_pixelate(Document& doc, int cell_size) {
    const int cell = std::clamp(cell_size, 2, 128);
    transform_from_source(doc, "Pixelate", [&](int x, int y, const std::vector<Pixel>& source) {
        const int origin_x = (x / cell) * cell;
        const int origin_y = (y / cell) * cell;
        return sample_clamped(source, doc.width, doc.height, origin_x + cell / 2, origin_y + cell / 2);
    });
}

void apply_crystalize(Document& doc, int cell_size) {
    const int cell = std::clamp(cell_size, 2, 128);
    transform_from_source(doc, "Crystalize", [&](int x, int y, const std::vector<Pixel>& source) {
        const int cell_x = static_cast<int>(std::floor(static_cast<float>(x) / static_cast<float>(cell)));
        const int cell_y = static_cast<int>(std::floor(static_cast<float>(y) / static_cast<float>(cell)));
        int best_x = x;
        int best_y = y;
        float best_distance = std::numeric_limits<float>::max();
        for (int yy = cell_y - 1; yy <= cell_y + 1; ++yy) {
            for (int xx = cell_x - 1; xx <= cell_x + 1; ++xx) {
                const int seed_x = xx * cell + static_cast<int>(noise01(xx, yy, 11) * static_cast<float>(cell));
                const int seed_y = yy * cell + static_cast<int>(noise01(xx, yy, 17) * static_cast<float>(cell));
                const float dx = static_cast<float>(x - seed_x);
                const float dy = static_cast<float>(y - seed_y);
                const float distance = dx * dx + dy * dy;
                if (distance < best_distance) {
                    best_distance = distance;
                    best_x = seed_x;
                    best_y = seed_y;
                }
            }
        }
        return sample_clamped(source, doc.width, doc.height, best_x, best_y);
    });
}

void apply_frosted_glass(Document& doc, int radius) {
    const int rad = std::clamp(radius, 1, 32);
    transform_from_source(doc, "Frosted Glass", [&](int x, int y, const std::vector<Pixel>& source) {
        const int ox = static_cast<int>((noise01(x, y, 31) * 2.0f - 1.0f) * static_cast<float>(rad));
        const int oy = static_cast<int>((noise01(x, y, 37) * 2.0f - 1.0f) * static_cast<float>(rad));
        return sample_clamped(source, doc.width, doc.height, x + ox, y + oy);
    });
}

void apply_bulge(Document& doc, float strength, int center_x_percent, int center_y_percent) {
    const float amount = std::clamp(strength, -2.0f, 2.0f);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * static_cast<float>(std::clamp(center_x_percent, 0, 100)) / 100.0f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * static_cast<float>(std::clamp(center_y_percent, 0, 100)) / 100.0f;
    const float max_radius = std::max({cx, cy, static_cast<float>(doc.width - 1) - cx, static_cast<float>(doc.height - 1) - cy});
    transform_from_source(doc, "Bulge", [&](int x, int y, const std::vector<Pixel>& source) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float radius = std::sqrt(dx * dx + dy * dy) / std::max(1.0f, max_radius);
        const float factor = 1.0f + amount * (1.0f - radius) * (1.0f - radius);
        return sample_nearest(source, doc.width, doc.height, cx + dx / factor, cy + dy / factor);
    });
}

void apply_twist(Document& doc, float turns, int center_x_percent, int center_y_percent, int effect_size_percent) {
    const float amount = std::clamp(turns, -4.0f, 4.0f) * 3.14159265358979323846f;
    const float cx = (static_cast<float>(doc.width) - 1.0f) * static_cast<float>(std::clamp(center_x_percent, 0, 100)) / 100.0f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * static_cast<float>(std::clamp(center_y_percent, 0, 100)) / 100.0f;
    const float base_radius = std::max({cx, cy, static_cast<float>(doc.width - 1) - cx, static_cast<float>(doc.height - 1) - cy});
    const float max_radius = base_radius * static_cast<float>(std::clamp(effect_size_percent, 10, 200)) / 100.0f;
    transform_from_source(doc, "Twist", [&](int x, int y, const std::vector<Pixel>& source) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float radius = std::sqrt(dx * dx + dy * dy) / std::max(1.0f, max_radius);
        const float angle = amount * (1.0f - std::clamp(radius, 0.0f, 1.0f));
        const float cos_a = std::cos(angle);
        const float sin_a = std::sin(angle);
        return sample_nearest(source, doc.width, doc.height, cx + cos_a * dx - sin_a * dy, cy + sin_a * dx + cos_a * dy);
    });
}

void apply_tile_reflection(Document& doc, int tile_size) {
    const int tile = std::clamp(tile_size, 2, 256);
    transform_from_source(doc, "Tile Reflection", [&](int x, int y, const std::vector<Pixel>& source) {
        int local_x = x % tile;
        int local_y = y % tile;
        if ((x / tile) % 2 != 0) {
            local_x = tile - 1 - local_x;
        }
        if ((y / tile) % 2 != 0) {
            local_y = tile - 1 - local_y;
        }
        return sample_clamped(source, doc.width, doc.height, local_x, local_y);
    });
}

void apply_dents(Document& doc, int scale, int amount) {
    const float noise_scale = 1.0f / static_cast<float>(std::clamp(scale, 2, 256));
    const float displacement = static_cast<float>(std::clamp(amount, 1, 64));
    transform_from_source(doc, "Dents", [&](int x, int y, const std::vector<Pixel>& source) {
        const float nx = value_noise(static_cast<float>(x) * noise_scale, static_cast<float>(y) * noise_scale, 43);
        const float ny = value_noise(static_cast<float>(x) * noise_scale, static_cast<float>(y) * noise_scale, 47);
        return sample_nearest(source,
                              doc.width,
                              doc.height,
                              static_cast<float>(x) + (nx - 0.5f) * displacement,
                              static_cast<float>(y) + (ny - 0.5f) * displacement);
    });
}

void apply_morphology(Document& doc, int radius, bool erode) {
    const int rad = std::clamp(radius, 1, 16);
    transform_from_source(doc, erode ? "Erode" : "Dilate", [&](int x, int y, const std::vector<Pixel>& source) {
        Pixel best = source[static_cast<std::size_t>(doc.pixel_index(x, y))];
        float best_luma = luminance(best);
        for (int yy = y - rad; yy <= y + rad; ++yy) {
            for (int xx = x - rad; xx <= x + rad; ++xx) {
                const Pixel pixel = sample_clamped(source, doc.width, doc.height, xx, yy);
                const float luma = luminance(pixel) + static_cast<float>(a(pixel)) * 0.01f;
                if ((erode && luma < best_luma) || (!erode && luma > best_luma)) {
                    best = pixel;
                    best_luma = luma;
                }
            }
        }
        return best;
    });
}

void apply_polar_inversion(Document& doc, float scale) {
    const float amount = std::max(0.01f, scale);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
    const float radius_scale = std::max(cx, cy);
    transform_from_source(doc, "Polar Inversion", [&](int x, int y, const std::vector<Pixel>& source) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float radius = std::sqrt(dx * dx + dy * dy);
        if (radius <= 0.0001f) {
            return sample_clamped(source, doc.width, doc.height, x, y);
        }
        const float inverted = radius_scale * radius_scale * amount / radius;
        return sample_nearest(source, doc.width, doc.height, cx + dx * inverted / radius, cy + dy * inverted / radius);
    });
}

void apply_add_noise(Document& doc, int intensity, int coverage, int color_saturation) {
    const int spread = std::clamp(intensity, 0, 255);
    const int cover = std::clamp(coverage, 0, 100);
    const int saturation = std::clamp(color_saturation, 0, 100);
    transform_pixels(doc, "Add Noise", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel) || noise01(x, y, 53) * 100.0f > static_cast<float>(cover)) {
            return pixel;
        }
        const float gray_noise = (noise01(x, y, 59) * 2.0f - 1.0f) * static_cast<float>(spread);
        const float color_amount = static_cast<float>(saturation) / 100.0f;
        const float nr = gray_noise + (noise01(x, y, 61) * 2.0f - 1.0f) * static_cast<float>(spread) * color_amount;
        const float ng = gray_noise + (noise01(x, y, 67) * 2.0f - 1.0f) * static_cast<float>(spread) * color_amount;
        const float nb = gray_noise + (noise01(x, y, 71) * 2.0f - 1.0f) * static_cast<float>(spread) * color_amount;
        return rgba(to_u8(static_cast<float>(r(pixel)) + nr),
                    to_u8(static_cast<float>(g(pixel)) + ng),
                    to_u8(static_cast<float>(b(pixel)) + nb),
                    a(pixel));
    });
}

void apply_reduce_noise(Document& doc, int radius) {
    apply_median_blur(doc, radius);
}

void apply_glow(Document& doc, int radius, int brightness, int contrast) {
    const int rad = std::clamp(radius, 1, 32);
    transform_from_source(doc, "Glow", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel base = source[static_cast<std::size_t>(doc.pixel_index(x, y))];
        const Pixel glow = blur_at(source, doc.width, doc.height, x, y, rad);
        Pixel mixed = mix_pixels(base,
                                 rgba(to_u8(static_cast<float>(r(glow)) + static_cast<float>(brightness)),
                                      to_u8(static_cast<float>(g(glow)) + static_cast<float>(brightness)),
                                      to_u8(static_cast<float>(b(glow)) + static_cast<float>(brightness)),
                                      a(glow)),
                                 0.45f);
        const float factor = 1.0f + static_cast<float>(contrast) / 100.0f;
        return rgba(to_u8((static_cast<float>(r(mixed)) - 128.0f) * factor + 128.0f),
                    to_u8((static_cast<float>(g(mixed)) - 128.0f) * factor + 128.0f),
                    to_u8((static_cast<float>(b(mixed)) - 128.0f) * factor + 128.0f),
                    a(mixed));
    });
}

void apply_red_eye_removal(Document& doc, int strength) {
    const float amount = static_cast<float>(std::clamp(strength, 0, 100)) / 100.0f;
    transform_pixels(doc, "Red Eye Removal", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel)) {
            return pixel;
        }
        const int red = r(pixel);
        const int green = g(pixel);
        const int blue = b(pixel);
        if (red <= green + blue / 2) {
            return pixel;
        }
        const float replacement = static_cast<float>(green + blue) * 0.5f;
        return rgba(to_u8(static_cast<float>(red) * (1.0f - amount) + replacement * amount), g(pixel), b(pixel), a(pixel));
    });
}

void apply_sharpen(Document& doc, int amount) {
    const float power = static_cast<float>(std::clamp(amount, 1, 200)) / 100.0f;
    const std::array<float, 9> kernel{
        0.0f, -power, 0.0f,
        -power, 1.0f + 4.0f * power, -power,
        0.0f, -power, 0.0f
    };
    transform_from_source(doc, "Sharpen", [&](int x, int y, const std::vector<Pixel>& source) {
        return convolve_at(source, doc.width, doc.height, x, y, kernel);
    });
}

void apply_soften_portrait(Document& doc, int softness, int lighting, int warmth) {
    const int rad = std::clamp(softness, 1, 16);
    const float light = static_cast<float>(lighting) / 100.0f;
    const float warm = static_cast<float>(warmth) / 100.0f;
    transform_from_source(doc, "Soften Portrait", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel base = source[static_cast<std::size_t>(doc.pixel_index(x, y))];
        const Pixel smooth = blur_at(source, doc.width, doc.height, x, y, rad);
        const Pixel mixed = mix_pixels(base, smooth, 0.55f);
        return rgba(to_u8(static_cast<float>(r(mixed)) + light * 24.0f + warm * 18.0f),
                    to_u8(static_cast<float>(g(mixed)) + light * 18.0f + warm * 6.0f),
                    to_u8(static_cast<float>(b(mixed)) + light * 12.0f - warm * 8.0f),
                    a(mixed));
    });
}

void apply_vignette(Document& doc, int radius, int strength, int center_x_percent, int center_y_percent) {
    const float radius_value = std::max(0.1f, static_cast<float>(radius) / 100.0f);
    const float amount = static_cast<float>(std::clamp(strength, 0, 100)) / 100.0f;
    const float cx = (static_cast<float>(doc.width) - 1.0f) * static_cast<float>(std::clamp(center_x_percent, 0, 100)) / 100.0f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * static_cast<float>(std::clamp(center_y_percent, 0, 100)) / 100.0f;
    const float far_x = std::max(cx, static_cast<float>(doc.width - 1) - cx);
    const float far_y = std::max(cy, static_cast<float>(doc.height - 1) - cy);
    const float max_radius = std::sqrt(far_x * far_x + far_y * far_y) * radius_value;
    transform_pixels(doc, "Vignette", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel)) {
            return pixel;
        }
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float t = std::clamp(std::sqrt(dx * dx + dy * dy) / std::max(1.0f, max_radius), 0.0f, 1.0f);
        const float darken = 1.0f - amount * t * t;
        return rgba(to_u8(static_cast<float>(r(pixel)) * darken),
                    to_u8(static_cast<float>(g(pixel)) * darken),
                    to_u8(static_cast<float>(b(pixel)) * darken),
                    a(pixel));
    });
}

void apply_straighten(Document& doc, float angle_degrees, ResamplingMode resampling) {
    apply_affine(doc, "Straighten", angle_degrees, 1.0f, 0, 0, resampling);
}
} // namespace px
