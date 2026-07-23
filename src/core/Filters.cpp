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

void apply_temperature(Document& doc, int temperature) {
    const float t = std::clamp(static_cast<float>(temperature) / 100.0f, -1.0f, 1.0f);
    const float warm = std::max(t, 0.0f);
    const float cool = std::max(-t, 0.0f);
    const float red_offset = warm * 45.0f - cool * 28.0f;
    const float green_offset = warm * 12.0f + cool * 8.0f;
    const float blue_offset = -warm * 35.0f + cool * 45.0f;

    transform_pixels(doc, "Warmth / Coolness", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel)) {
            return pixel;
        }
        return rgba(to_u8(static_cast<float>(r(pixel)) + red_offset),
                    to_u8(static_cast<float>(g(pixel)) + green_offset),
                    to_u8(static_cast<float>(b(pixel)) + blue_offset),
                    a(pixel));
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

void apply_tonal_range(Document& doc, int white_point, int highlights, int shadows, int black_point) {
    const float white = std::clamp(static_cast<float>(white_point) / 100.0f, -1.0f, 1.0f);
    const float high = std::clamp(static_cast<float>(highlights) / 100.0f, -1.0f, 1.0f);
    const float shadow = std::clamp(static_cast<float>(shadows) / 100.0f, -1.0f, 1.0f);
    const float black = std::clamp(static_cast<float>(black_point) / 100.0f, -1.0f, 1.0f);

    auto adjust_channel = [&](std::uint8_t channel, float luma) -> std::uint8_t {
        float value = static_cast<float>(channel) / 255.0f;
        const float black_anchor = std::clamp(black * 0.20f, -0.20f, 0.20f);
        const float white_anchor = std::clamp(1.0f - white * 0.20f, 0.80f, 1.20f);
        value = std::clamp((value - black_anchor) / std::max(0.05f, white_anchor - black_anchor), 0.0f, 1.0f);

        const float tonal_luma = std::clamp(luma, 0.0f, 1.0f);
        const float shadow_weight = std::pow(1.0f - tonal_luma, 1.6f);
        const float highlight_weight = std::pow(tonal_luma, 1.6f);
        value += shadow * 0.45f * shadow_weight * (shadow >= 0.0f ? (1.0f - value) : value);
        value += high * 0.45f * highlight_weight * (high >= 0.0f ? (1.0f - value) : value);
        return to_u8(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    };

    transform_pixels(doc, "Tonal Range", [&](int x, int y, Pixel p) {
        if (!editable(doc, x, y, p)) {
            return p;
        }
        const float luma = luminance(p) / 255.0f;
        return rgba(adjust_channel(r(p), luma),
                    adjust_channel(g(p), luma),
                    adjust_channel(b(p), luma),
                    a(p));
    });
}

static int normalized_curve_point_count(const CurvesSettings& settings) {
    return std::clamp(settings.point_count, 2, kMaxCurvePoints);
}

static float curve_slope(const CurvesSettings& settings, int segment) {
    const float x0 = settings.x[static_cast<std::size_t>(segment)];
    const float x1 = settings.x[static_cast<std::size_t>(segment + 1)];
    return (settings.y[static_cast<std::size_t>(segment + 1)] -
            settings.y[static_cast<std::size_t>(segment)]) /
           std::max(0.001f, x1 - x0);
}

static float curve_tangent(const CurvesSettings& settings, int point, int point_count) {
    if (point == 0) return curve_slope(settings, 0);
    if (point == point_count - 1) return curve_slope(settings, point_count - 2);
    const float left_slope = curve_slope(settings, point - 1);
    const float right_slope = curve_slope(settings, point);
    if (left_slope * right_slope <= 0.0f) return 0.0f;
    const float left_width = settings.x[static_cast<std::size_t>(point)] -
                             settings.x[static_cast<std::size_t>(point - 1)];
    const float right_width = settings.x[static_cast<std::size_t>(point + 1)] -
                              settings.x[static_cast<std::size_t>(point)];
    const float first_weight = 2.0f * right_width + left_width;
    const float second_weight = right_width + 2.0f * left_width;
    return (first_weight + second_weight) /
           (first_weight / left_slope + second_weight / right_slope);
}

static float evaluate_curve(float value, const CurvesSettings& settings) {
    value = std::clamp(value, 0.0f, 1.0f);
    const int point_count = normalized_curve_point_count(settings);
    if (value <= settings.x[0]) {
        return std::clamp(settings.y[0], 0.0f, 1.0f);
    }
    for (int i = 1; i < point_count; ++i) {
        const float x0 = std::clamp(settings.x[static_cast<std::size_t>(i - 1)], 0.0f, 1.0f);
        const float x1 = std::clamp(settings.x[static_cast<std::size_t>(i)], x0 + 0.001f, 1.0f);
        if (value <= x1 || i == point_count - 1) {
            const float y0 = std::clamp(settings.y[static_cast<std::size_t>(i - 1)], 0.0f, 1.0f);
            const float y1 = std::clamp(settings.y[static_cast<std::size_t>(i)], 0.0f, 1.0f);
            const float width = std::max(0.001f, x1 - x0);
            const float t = std::clamp((value - x0) / width, 0.0f, 1.0f);
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float left_tangent = curve_tangent(settings, i - 1, point_count);
            const float right_tangent = curve_tangent(settings, i, point_count);
            return std::clamp((2.0f * t3 - 3.0f * t2 + 1.0f) * y0 +
                              (t3 - 2.0f * t2 + t) * width * left_tangent +
                              (-2.0f * t3 + 3.0f * t2) * y1 +
                              (t3 - t2) * width * right_tangent, 0.0f, 1.0f);
        }
    }
    return std::clamp(settings.y[static_cast<std::size_t>(point_count - 1)], 0.0f, 1.0f);
}

void apply_curves(Document& doc, const CurvesSettings& settings) {
    auto curve_channel = [&](float value) {
        return evaluate_curve(value, settings);
    };

    transform_pixels(doc, "Curves", [&](int x, int y, Pixel pixel) {
        if (!editable(doc, x, y, pixel)) {
            return pixel;
        }
        float red = static_cast<float>(r(pixel)) / 255.0f;
        float green = static_cast<float>(g(pixel)) / 255.0f;
        float blue = static_cast<float>(b(pixel)) / 255.0f;
        if (settings.luma) {
            const float current_luma = std::clamp(luminance(pixel) / 255.0f, 0.0f, 1.0f);
            const float mapped_luma = curve_channel(current_luma);
            if (current_luma > 0.0001f) {
                const float ratio = mapped_luma / current_luma;
                red *= ratio;
                green *= ratio;
                blue *= ratio;
            } else {
                red = green = blue = mapped_luma;
            }
        } else {
            if (settings.red) {
                red = curve_channel(red);
            }
            if (settings.green) {
                green = curve_channel(green);
            }
            if (settings.blue) {
                blue = curve_channel(blue);
            }
        }
        return rgba(to_u8(std::clamp(red, 0.0f, 1.0f) * 255.0f),
                    to_u8(std::clamp(green, 0.0f, 1.0f) * 255.0f),
                    to_u8(std::clamp(blue, 0.0f, 1.0f) * 255.0f),
                    a(pixel));
    });
}

void apply_depth_of_field(Document& doc, const std::vector<Pixel>& depth_pixels, const DepthOfFieldSettings& settings) {
    const std::size_t expected = static_cast<std::size_t>(std::max(0, doc.width) * std::max(0, doc.height));
    if (depth_pixels.size() != expected) {
        return;
    }
    transform_from_source(doc, "Depth of Field", [&](int x, int y, const std::vector<Pixel>& source) {
        const std::size_t index = static_cast<std::size_t>(doc.pixel_index(x, y));
        const int radius = depth_of_field_radius_for_depth(r(depth_pixels[index]), settings);
        if (radius <= 0) {
            return source[index];
        }
        return blur_at(source, doc.width, doc.height, x, y, radius);
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
} // namespace px
