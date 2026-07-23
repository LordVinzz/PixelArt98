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
void apply_edge_detect(Document& doc, int strength) {
    const float amount = static_cast<float>(std::clamp(strength, 1, 200)) / 100.0f;
    const std::array<float, 9> kernel{
        -amount, -amount, -amount,
        -amount, 8.0f * amount, -amount,
        -amount, -amount, -amount
    };
    transform_from_source(doc, "Edge Detect", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel edge = convolve_at(source, doc.width, doc.height, x, y, kernel);
        const std::uint8_t value = to_u8(luminance(edge));
        return rgba(value, value, value, a(sample_clamped(source, doc.width, doc.height, x, y)));
    });
}

void apply_emboss(Document& doc, float angle_degrees) {
    const float angle = angle_degrees * 3.14159265358979323846f / 180.0f;
    const int ox = std::clamp(static_cast<int>(std::round(std::cos(angle))), -1, 1);
    const int oy = std::clamp(static_cast<int>(std::round(std::sin(angle))), -1, 1);
    transform_from_source(doc, "Emboss", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel a_pixel = sample_clamped(source, doc.width, doc.height, x - ox, y - oy);
        const Pixel b_pixel = sample_clamped(source, doc.width, doc.height, x + ox, y + oy);
        const int value = static_cast<int>(luminance(a_pixel) - luminance(b_pixel) + 128.0f);
        const auto channel = to_u8_int(value);
        return rgba(channel, channel, channel, a(sample_clamped(source, doc.width, doc.height, x, y)));
    });
}

void apply_outline(Document& doc, int thickness, int intensity) {
    const int rad = std::clamp(thickness, 1, 8);
    const int amount = std::clamp(intensity, 1, 255);
    transform_from_source(doc, "Outline", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel center = sample_clamped(source, doc.width, doc.height, x, y);
        int edge = 0;
        for (int yy = y - rad; yy <= y + rad; ++yy) {
            for (int xx = x - rad; xx <= x + rad; ++xx) {
                edge = std::max(edge, color_distance(center, sample_clamped(source, doc.width, doc.height, xx, yy), true));
            }
        }
        const std::uint8_t value = to_u8_int(edge * amount / 255);
        return rgba(value, value, value, a(center));
    });
}

void apply_relief(Document& doc, float angle_degrees) {
    apply_emboss(doc, angle_degrees);
    apply_brightness_contrast(doc, 0, 35);
}

void apply_oil_painting(Document& doc, int brush_size, int coarseness) {
    const int rad = std::clamp(brush_size, 1, 12);
    const int bins = std::clamp(coarseness, 4, 64);
    transform_from_source(doc, "Oil Painting", [&](int x, int y, const std::vector<Pixel>& source) {
        std::vector<int> counts(static_cast<std::size_t>(bins), 0);
        std::vector<int> reds(static_cast<std::size_t>(bins), 0);
        std::vector<int> greens(static_cast<std::size_t>(bins), 0);
        std::vector<int> blues(static_cast<std::size_t>(bins), 0);
        std::vector<int> alphas(static_cast<std::size_t>(bins), 0);
        for (int yy = y - rad; yy <= y + rad; ++yy) {
            for (int xx = x - rad; xx <= x + rad; ++xx) {
                const Pixel pixel = sample_clamped(source, doc.width, doc.height, xx, yy);
                const int bin = std::clamp(static_cast<int>(luminance(pixel) * static_cast<float>(bins) / 256.0f), 0, bins - 1);
                const std::size_t index = static_cast<std::size_t>(bin);
                ++counts[index];
                reds[index] += r(pixel);
                greens[index] += g(pixel);
                blues[index] += b(pixel);
                alphas[index] += a(pixel);
            }
        }
        const auto best = static_cast<std::size_t>(std::distance(counts.begin(), std::max_element(counts.begin(), counts.end())));
        const int count = std::max(1, counts[best]);
        return rgba(to_u8_int(reds[best] / count), to_u8_int(greens[best] / count), to_u8_int(blues[best] / count), to_u8_int(alphas[best] / count));
    });
}

void apply_ink_sketch(Document& doc, int outline, int coloring) {
    const float color_amount = static_cast<float>(std::clamp(coloring, 0, 100)) / 100.0f;
    const float edge_amount = static_cast<float>(std::clamp(outline, 1, 200)) / 100.0f;
    const std::array<float, 9> kernel{
        -edge_amount, -edge_amount, -edge_amount,
        -edge_amount, 8.0f * edge_amount, -edge_amount,
        -edge_amount, -edge_amount, -edge_amount
    };
    transform_from_source(doc, "Ink Sketch", [&](int x, int y, const std::vector<Pixel>& original) {
        const Pixel base = original[static_cast<std::size_t>(doc.pixel_index(x, y))];
        const Pixel edge = convolve_at(original, doc.width, doc.height, x, y, kernel);
        const float ink = static_cast<float>(r(edge)) / 255.0f;
        const Pixel washed = mix_pixels(rgba(to_u8(luminance(base)), to_u8(luminance(base)), to_u8(luminance(base)), a(base)), base, color_amount);
        return rgba(to_u8(static_cast<float>(r(washed)) * (1.0f - ink)),
                    to_u8(static_cast<float>(g(washed)) * (1.0f - ink)),
                    to_u8(static_cast<float>(b(washed)) * (1.0f - ink)),
                    a(base));
    });
}

void apply_pencil_sketch(Document& doc, int tip_size, int range) {
    const int rad = std::clamp(tip_size, 1, 8);
    const float contrast = 1.0f + static_cast<float>(std::clamp(range, 0, 100)) / 50.0f;
    transform_from_source(doc, "Pencil Sketch", [&](int x, int y, const std::vector<Pixel>& source) {
        const Pixel center = sample_clamped(source, doc.width, doc.height, x, y);
        const Pixel smooth = blur_at(source, doc.width, doc.height, x, y, rad);
        const float value = 255.0f - std::abs(luminance(center) - luminance(smooth)) * contrast;
        const auto channel = to_u8(value);
        return rgba(channel, channel, channel, a(center));
    });
}

void apply_clouds(Document& doc, int scale, int roughness, Pixel color_a, Pixel color_b) {
    const float base_scale = 1.0f / static_cast<float>(std::clamp(scale, 2, 512));
    const int octaves = std::clamp(roughness, 1, 8);
    transform_pixels(doc, "Clouds", [&](int x, int y, Pixel pixel) {
        if (!doc.selection.contains(x, y)) {
            return pixel;
        }
        float value = 0.0f;
        float amplitude = 1.0f;
        float amplitude_sum = 0.0f;
        float frequency = base_scale;
        for (int octave = 0; octave < octaves; ++octave) {
            value += value_noise(static_cast<float>(x) * frequency, static_cast<float>(y) * frequency, 89 + octave) * amplitude;
            amplitude_sum += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        const float t = amplitude_sum <= 0.0f ? 0.0f : value / amplitude_sum;
        return mix_pixels(color_a, color_b, t);
    });
}

static Pixel fractal_color(int iteration, int max_iterations, bool invert) {
    if (iteration >= max_iterations) {
        return invert ? rgba(255, 255, 255, 255) : rgba(0, 0, 0, 255);
    }
    const float t = static_cast<float>(iteration) / static_cast<float>(max_iterations);
    Pixel color = rgba(to_u8(9.0f * (1.0f - t) * t * t * t * 255.0f),
                       to_u8(15.0f * (1.0f - t) * (1.0f - t) * t * t * 255.0f),
                       to_u8(8.5f * (1.0f - t) * (1.0f - t) * (1.0f - t) * t * 255.0f),
                       255);
    return invert ? rgba(static_cast<std::uint8_t>(255 - r(color)),
                         static_cast<std::uint8_t>(255 - g(color)),
                         static_cast<std::uint8_t>(255 - b(color)),
                         255) : color;
}

void apply_julia_fractal(Document& doc, float zoom, float angle_degrees) {
    const float scale = 2.5f / std::max(0.01f, zoom);
    const float angle = angle_degrees * 3.14159265358979323846f / 180.0f;
    const float cos_a = std::cos(angle);
    const float sin_a = std::sin(angle);
    transform_pixels(doc, "Julia Fractal", [&](int x, int y, Pixel pixel) {
        if (!doc.selection.contains(x, y)) {
            return pixel;
        }
        float zx = (static_cast<float>(x) / static_cast<float>(doc.width) - 0.5f) * scale;
        float zy = (static_cast<float>(y) / static_cast<float>(doc.height) - 0.5f) * scale;
        const float rx = cos_a * zx - sin_a * zy;
        const float ry = sin_a * zx + cos_a * zy;
        zx = rx;
        zy = ry;
        int iteration = 0;
        constexpr int max_iterations = 80;
        while (zx * zx + zy * zy < 4.0f && iteration < max_iterations) {
            const float next_x = zx * zx - zy * zy - 0.70176f;
            zy = 2.0f * zx * zy - 0.3842f;
            zx = next_x;
            ++iteration;
        }
        return fractal_color(iteration, max_iterations, false);
    });
}

void apply_mandelbrot_fractal(Document& doc, float zoom, float angle_degrees, bool invert) {
    const float scale = 3.2f / std::max(0.01f, zoom);
    const float angle = angle_degrees * 3.14159265358979323846f / 180.0f;
    const float cos_a = std::cos(angle);
    const float sin_a = std::sin(angle);
    transform_pixels(doc, "Mandelbrot Fractal", [&](int x, int y, Pixel pixel) {
        if (!doc.selection.contains(x, y)) {
            return pixel;
        }
        const float dx = (static_cast<float>(x) / static_cast<float>(doc.width) - 0.62f) * scale;
        const float dy = (static_cast<float>(y) / static_cast<float>(doc.height) - 0.5f) * scale;
        const float cx = cos_a * dx - sin_a * dy;
        const float cy = sin_a * dx + cos_a * dy;
        float zx = 0.0f;
        float zy = 0.0f;
        int iteration = 0;
        constexpr int max_iterations = 100;
        while (zx * zx + zy * zy < 4.0f && iteration < max_iterations) {
            const float next_x = zx * zx - zy * zy + cx;
            zy = 2.0f * zx * zy + cy;
            zx = next_x;
            ++iteration;
        }
        return fractal_color(iteration, max_iterations, invert);
    });
}

void apply_turbulence(Document& doc, int scale, int octaves) {
    const float base_scale = 1.0f / static_cast<float>(std::clamp(scale, 2, 512));
    const int octave_count = std::clamp(octaves, 1, 8);
    transform_pixels(doc, "Turbulence", [&](int x, int y, Pixel pixel) {
        if (!doc.selection.contains(x, y)) {
            return pixel;
        }
        float value = 0.0f;
        float amplitude = 1.0f;
        float amplitude_sum = 0.0f;
        float frequency = base_scale;
        for (int octave = 0; octave < octave_count; ++octave) {
            value += std::abs(value_noise(static_cast<float>(x) * frequency, static_cast<float>(y) * frequency, 113 + octave) - 0.5f) *
                     2.0f * amplitude;
            amplitude_sum += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        const auto channel = to_u8((amplitude_sum <= 0.0f ? 0.0f : value / amplitude_sum) * 255.0f);
        return rgba(channel, channel, channel, 255);
    });
}

static Pixel nearest_palette(Pixel p, const Palette& palette) {
    if (palette.colors.empty()) {
        return p;
    }
    int best = std::numeric_limits<int>::max();
    Pixel best_color = palette.colors.front();
    for (Pixel candidate : palette.colors) {
        int d = color_distance(p, candidate, true);
        if (d < best) {
            best = d;
            best_color = with_alpha(candidate, a(p));
        }
    }
    return best_color;
}

void apply_palette_quantize(Document& doc, const Palette& palette, bool dither) {
    auto before = doc.snapshot_active_cel();
    auto& pixels = doc.active_cel().pixels;
    std::vector<float> er(static_cast<std::size_t>(doc.width * doc.height), 0.0f);
    std::vector<float> eg(er.size(), 0.0f);
    std::vector<float> eb(er.size(), 0.0f);

    auto add_error = [&](int x, int y, float rr, float gg, float bb, float factor) {
        if (!doc.in_bounds(x, y)) {
            return;
        }
        std::size_t i = static_cast<std::size_t>(doc.pixel_index(x, y));
        er[i] += rr * factor;
        eg[i] += gg * factor;
        eb[i] += bb * factor;
    };

    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            std::size_t i = static_cast<std::size_t>(doc.pixel_index(x, y));
            Pixel old = pixels[i];
            if (!editable(doc, x, y, old)) {
                continue;
            }
            Pixel adjusted = rgba(
                static_cast<std::uint8_t>(std::clamp(static_cast<float>(r(old)) + er[i], 0.0f, 255.0f) + 0.5f),
                static_cast<std::uint8_t>(std::clamp(static_cast<float>(g(old)) + eg[i], 0.0f, 255.0f) + 0.5f),
                static_cast<std::uint8_t>(std::clamp(static_cast<float>(b(old)) + eb[i], 0.0f, 255.0f) + 0.5f),
                a(old));
            Pixel next = nearest_palette(adjusted, palette);
            pixels[i] = next;
            if (dither) {
                float rr = static_cast<float>(r(adjusted)) - static_cast<float>(r(next));
                float gg = static_cast<float>(g(adjusted)) - static_cast<float>(g(next));
                float bb = static_cast<float>(b(adjusted)) - static_cast<float>(b(next));
                add_error(x + 1, y, rr, gg, bb, 7.0f / 16.0f);
                add_error(x - 1, y + 1, rr, gg, bb, 3.0f / 16.0f);
                add_error(x, y + 1, rr, gg, bb, 5.0f / 16.0f);
                add_error(x + 1, y + 1, rr, gg, bb, 1.0f / 16.0f);
            }
        }
    }
    doc.commit_active_cel_edit(dither ? "Dither to Palette" : "Quantize to Palette", std::move(before));
}
} // namespace px
