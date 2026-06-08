// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <vector>

namespace px {

static bool editable(const Document& doc, int x, int y, Pixel p) {
    return doc.selection.contains(x, y) && a(p) > 0;
}

static Pixel transform_pixels(Document& doc, const char* name, const std::function<Pixel(int, int, Pixel)>& fn) {
    auto before = doc.snapshot_active_cel();
    auto& pixels = doc.active_cel().pixels;
    Pixel last = 0;
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            std::size_t i = static_cast<std::size_t>(doc.pixel_index(x, y));
            pixels[i] = fn(x, y, pixels[i]);
            last = pixels[i];
        }
    }
    doc.commit_active_cel_edit(name, std::move(before));
    return last;
}

static std::uint8_t to_u8(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}

static std::uint8_t to_u8_int(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

static Pixel sample_clamped(const std::vector<Pixel>& pixels, int width, int height, int x, int y) {
    const int sx = std::clamp(x, 0, width - 1);
    const int sy = std::clamp(y, 0, height - 1);
    return pixels[static_cast<std::size_t>(sy * width + sx)];
}

static Pixel sample_nearest(const std::vector<Pixel>& pixels, int width, int height, float x, float y) {
    const int sx = static_cast<int>(std::floor(x + 0.5f));
    const int sy = static_cast<int>(std::floor(y + 0.5f));
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
        return 0;
    }
    return pixels[static_cast<std::size_t>(sy * width + sx)];
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

static Pixel mix_pixels(Pixel first, Pixel second, float amount) {
    const float t = std::clamp(amount, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return rgba(to_u8(static_cast<float>(r(first)) * inv + static_cast<float>(r(second)) * t),
                to_u8(static_cast<float>(g(first)) * inv + static_cast<float>(g(second)) * t),
                to_u8(static_cast<float>(b(first)) * inv + static_cast<float>(b(second)) * t),
                to_u8(static_cast<float>(a(first)) * inv + static_cast<float>(a(second)) * t));
}

static void transform_from_source(Document& doc,
                                  const char* name,
                                  const std::function<Pixel(int, int, const std::vector<Pixel>&)>& fn) {
    auto before = doc.snapshot_active_cel();
    auto source = before;
    auto& pixels = doc.active_cel().pixels;
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            if (doc.selection.contains(x, y)) {
                pixels[static_cast<std::size_t>(doc.pixel_index(x, y))] = fn(x, y, source);
            }
        }
    }
    doc.commit_active_cel_edit(name, std::move(before));
}

static void apply_affine(Document& doc,
                         const char* name,
                         float angle_degrees,
                         float zoom,
                         int pan_x,
                         int pan_y,
                         ResamplingMode resampling = ResamplingMode::Nearest) {
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

static float noise01(int x, int y, int seed) {
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const auto us = static_cast<std::uint32_t>(seed);
    return static_cast<float>(hash_u32(ux * 374761393U + uy * 668265263U + us * 2246822519U) & 0xffffU) / 65535.0f;
}

static float value_noise(float x, float y, int seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float n00 = noise01(x0, y0, seed);
    const float n10 = noise01(x0 + 1, y0, seed);
    const float n01 = noise01(x0, y0 + 1, seed);
    const float n11 = noise01(x0 + 1, y0 + 1, seed);
    const float nx0 = n00 + (n10 - n00) * sx;
    const float nx1 = n01 + (n11 - n01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}

static Pixel blur_at(const std::vector<Pixel>& source, int width, int height, int x, int y, int radius) {
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

static Pixel convolve_at(const std::vector<Pixel>& source,
                         int width,
                         int height,
                         int x,
                         int y,
                         const std::array<float, 9>& kernel,
                         float bias = 0.0f) {
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

void apply_brightness_contrast(Document& doc, int brightness, int contrast) {
    float bdelta = static_cast<float>(brightness);
    float factor = (259.0f * (static_cast<float>(contrast) + 255.0f)) /
                   (255.0f * (259.0f - static_cast<float>(contrast)));
    transform_pixels(doc, "Brightness / Contrast", [&](int x, int y, Pixel p) {
        if (!editable(doc, x, y, p)) {
            return p;
        }
        auto adj = [&](std::uint8_t c) -> std::uint8_t {
            float out = factor * (static_cast<float>(c) - 128.0f) + 128.0f + bdelta;
            return static_cast<std::uint8_t>(std::clamp(out, 0.0f, 255.0f) + 0.5f);
        };
        return rgba(adj(r(p)), adj(g(p)), adj(b(p)), a(p));
    });
}

static void rgb_to_hsv(Pixel p, float& h, float& s, float& v) {
    float rf = r(p) / 255.0f;
    float gf = g(p) / 255.0f;
    float bf = b(p) / 255.0f;
    float maxv = std::max({rf, gf, bf});
    float minv = std::min({rf, gf, bf});
    float d = maxv - minv;
    h = 0.0f;
    if (d > 0.0001f) {
        if (maxv == rf) h = 60.0f * std::fmod(((gf - bf) / d), 6.0f);
        else if (maxv == gf) h = 60.0f * (((bf - rf) / d) + 2.0f);
        else h = 60.0f * (((rf - gf) / d) + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
    s = maxv <= 0.0f ? 0.0f : d / maxv;
    v = maxv;
}

static Pixel hsv_to_rgb(float h, float s, float v, std::uint8_t alpha) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0, gf = 0, bf = 0;
    if (h < 60) { rf = c; gf = x; }
    else if (h < 120) { rf = x; gf = c; }
    else if (h < 180) { gf = c; bf = x; }
    else if (h < 240) { gf = x; bf = c; }
    else if (h < 300) { rf = x; bf = c; }
    else { rf = c; bf = x; }
    return rgba(static_cast<std::uint8_t>((rf + m) * 255.0f + 0.5f),
                static_cast<std::uint8_t>((gf + m) * 255.0f + 0.5f),
                static_cast<std::uint8_t>((bf + m) * 255.0f + 0.5f),
                alpha);
}

void apply_hsv(Document& doc, float hue_degrees, float saturation_delta, float value_delta) {
    transform_pixels(doc, "Hue / Saturation", [&](int x, int y, Pixel p) {
        if (!editable(doc, x, y, p)) {
            return p;
        }
        float h, s, v;
        rgb_to_hsv(p, h, s, v);
        h += hue_degrees;
        s = std::clamp(s + saturation_delta, 0.0f, 1.0f);
        v = std::clamp(v + value_delta, 0.0f, 1.0f);
        return hsv_to_rgb(h, s, v, a(p));
    });
}

void apply_levels(Document& doc, const LevelsSettings& settings) {
    int in_black = std::clamp(settings.in_black, 0, 254);
    int in_white = std::clamp(settings.in_white, in_black + 1, 255);
    float gamma = std::max(0.05f, settings.gamma);
    int out_black = std::clamp(settings.out_black, 0, 255);
    int out_white = std::clamp(settings.out_white, 0, 255);
    auto adjust = [&](std::uint8_t c, bool enabled) -> std::uint8_t {
        if (!enabled) {
            return c;
        }
        float n = (static_cast<float>(c) - static_cast<float>(in_black)) /
                  static_cast<float>(in_white - in_black);
        n = std::pow(std::clamp(n, 0.0f, 1.0f), 1.0f / gamma);
        float out = static_cast<float>(out_black) + n * static_cast<float>(out_white - out_black);
        return static_cast<std::uint8_t>(std::clamp(out, 0.0f, 255.0f) + 0.5f);
    };
    transform_pixels(doc, "Levels", [&](int x, int y, Pixel p) {
        if (!editable(doc, x, y, p)) {
            return p;
        }
        return rgba(adjust(r(p), settings.red), adjust(g(p), settings.green), adjust(b(p), settings.blue), a(p));
    });
}

void apply_auto_level(Document& doc) {
    int min_red = 255;
    int min_green = 255;
    int min_blue = 255;
    int max_red = 0;
    int max_green = 0;
    int max_blue = 0;
    for (Pixel pixel : doc.active_cel().pixels) {
        if (a(pixel) == 0) {
            continue;
        }
        min_red = std::min(min_red, static_cast<int>(r(pixel)));
        min_green = std::min(min_green, static_cast<int>(g(pixel)));
        min_blue = std::min(min_blue, static_cast<int>(b(pixel)));
        max_red = std::max(max_red, static_cast<int>(r(pixel)));
        max_green = std::max(max_green, static_cast<int>(g(pixel)));
        max_blue = std::max(max_blue, static_cast<int>(b(pixel)));
    }
    auto normalize = [](std::uint8_t channel, int min_value, int max_value) -> std::uint8_t {
        if (max_value <= min_value) {
            return channel;
        }
        const float value = (static_cast<float>(channel) - static_cast<float>(min_value)) * 255.0f /
                            static_cast<float>(max_value - min_value);
        return to_u8(value);
    };
    transform_pixels(doc, "Auto-Level", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel)) {
            return pixel;
        }
        return rgba(normalize(r(pixel), min_red, max_red),
                    normalize(g(pixel), min_green, max_green),
                    normalize(b(pixel), min_blue, max_blue),
                    a(pixel));
    });
}

void apply_posterize(Document& doc, int levels) {
    int lv = std::clamp(levels, 2, 64);
    transform_pixels(doc, "Posterize", [&](int x, int y, Pixel p) {
        if (!editable(doc, x, y, p)) {
            return p;
        }
        auto q = [&](std::uint8_t c) -> std::uint8_t {
            int bucket = static_cast<int>(std::round((static_cast<float>(c) / 255.0f) * static_cast<float>(lv - 1)));
            return static_cast<std::uint8_t>((bucket * 255) / (lv - 1));
        };
        return rgba(q(r(p)), q(g(p)), q(b(p)), a(p));
    });
}

void apply_invert(Document& doc, bool alpha) {
    transform_pixels(doc, alpha ? "Invert Alpha" : "Invert Colors", [&](int x, int y, Pixel p) {
        if (!doc.selection.contains(x, y)) {
            return p;
        }
        if (alpha) {
            return rgba(r(p), g(p), b(p), static_cast<std::uint8_t>(255 - a(p)));
        }
        return rgba(static_cast<std::uint8_t>(255 - r(p)),
                    static_cast<std::uint8_t>(255 - g(p)),
                    static_cast<std::uint8_t>(255 - b(p)),
                    a(p));
    });
}

void apply_grayscale(Document& doc) {
    transform_pixels(doc, "Grayscale", [&](int x, int y, Pixel p) {
        if (!editable(doc, x, y, p)) {
            return p;
        }
        auto l = static_cast<std::uint8_t>(std::clamp(static_cast<int>(luminance(p) + 0.5f), 0, 255));
        return rgba(l, l, l, a(p));
    });
}

void apply_sepia(Document& doc) {
    transform_pixels(doc, "Sepia", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel)) {
            return pixel;
        }
        const float red = static_cast<float>(r(pixel));
        const float green = static_cast<float>(g(pixel));
        const float blue = static_cast<float>(b(pixel));
        return rgba(to_u8(0.393f * red + 0.769f * green + 0.189f * blue),
                    to_u8(0.349f * red + 0.686f * green + 0.168f * blue),
                    to_u8(0.272f * red + 0.534f * green + 0.131f * blue),
                    a(pixel));
    });
}

void apply_flip_horizontal(Document& doc) {
    transform_from_source(doc, "Flip Horizontal", [&](int x, int y, const std::vector<Pixel>& source) {
        return source[static_cast<std::size_t>(doc.pixel_index(doc.width - 1 - x, y))];
    });
}

void apply_flip_vertical(Document& doc) {
    transform_from_source(doc, "Flip Vertical", [&](int x, int y, const std::vector<Pixel>& source) {
        return source[static_cast<std::size_t>(doc.pixel_index(x, doc.height - 1 - y))];
    });
}

void apply_rotate_90_clockwise(Document& doc) {
    apply_affine(doc, "Rotate 90 Clockwise", -90.0f, 1.0f, 0, 0);
}

void apply_rotate_90_counter_clockwise(Document& doc) {
    apply_affine(doc, "Rotate 90 Counter-Clockwise", 90.0f, 1.0f, 0, 0);
}

void apply_rotate_180(Document& doc) {
    transform_from_source(doc, "Rotate 180", [&](int x, int y, const std::vector<Pixel>& source) {
        return source[static_cast<std::size_t>(doc.pixel_index(doc.width - 1 - x, doc.height - 1 - y))];
    });
}

void apply_rotate_zoom(Document& doc,
                       float angle_degrees,
                       float zoom,
                       int pan_x,
                       int pan_y,
                       ResamplingMode resampling) {
    apply_affine(doc, "Rotate / Zoom", angle_degrees, zoom, pan_x, pan_y, resampling);
}

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

void apply_radial_blur(Document& doc, int amount) {
    const int samples = std::clamp(amount, 2, 64);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
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

void apply_zoom_blur(Document& doc, int amount) {
    const int samples = std::clamp(amount, 2, 64);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
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

void apply_bulge(Document& doc, float strength) {
    const float amount = std::clamp(strength, -2.0f, 2.0f);
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
    const float max_radius = std::max(cx, cy);
    transform_from_source(doc, "Bulge", [&](int x, int y, const std::vector<Pixel>& source) {
        const float dx = static_cast<float>(x) - cx;
        const float dy = static_cast<float>(y) - cy;
        const float radius = std::sqrt(dx * dx + dy * dy) / std::max(1.0f, max_radius);
        const float factor = 1.0f + amount * (1.0f - radius) * (1.0f - radius);
        return sample_nearest(source, doc.width, doc.height, cx + dx / factor, cy + dy / factor);
    });
}

void apply_twist(Document& doc, float turns) {
    const float amount = std::clamp(turns, -4.0f, 4.0f) * 3.14159265358979323846f;
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
    const float max_radius = std::max(cx, cy);
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

void apply_vignette(Document& doc, int radius, int strength) {
    const float radius_value = std::max(0.1f, static_cast<float>(radius) / 100.0f);
    const float amount = static_cast<float>(std::clamp(strength, 0, 100)) / 100.0f;
    const float cx = (static_cast<float>(doc.width) - 1.0f) * 0.5f;
    const float cy = (static_cast<float>(doc.height) - 1.0f) * 0.5f;
    const float max_radius = std::sqrt(cx * cx + cy * cy) * radius_value;
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
