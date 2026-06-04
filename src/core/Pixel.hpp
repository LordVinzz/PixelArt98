// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace px {

using Pixel = std::uint32_t;

inline Pixel rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return static_cast<Pixel>(r) |
           (static_cast<Pixel>(g) << 8) |
           (static_cast<Pixel>(b) << 16) |
           (static_cast<Pixel>(a) << 24);
}

inline std::uint8_t r(Pixel c) { return static_cast<std::uint8_t>(c & 0xffu); }
inline std::uint8_t g(Pixel c) { return static_cast<std::uint8_t>((c >> 8) & 0xffu); }
inline std::uint8_t b(Pixel c) { return static_cast<std::uint8_t>((c >> 16) & 0xffu); }
inline std::uint8_t a(Pixel c) { return static_cast<std::uint8_t>((c >> 24) & 0xffu); }

inline Pixel with_alpha(Pixel c, std::uint8_t alpha) {
    return rgba(r(c), g(c), b(c), alpha);
}

inline Pixel blend_over(Pixel dst, Pixel src, float opacity = 1.0f) {
    float sa = (static_cast<float>(a(src)) / 255.0f) * std::clamp(opacity, 0.0f, 1.0f);
    float da = static_cast<float>(a(dst)) / 255.0f;
    float out_a = sa + da * (1.0f - sa);
    if (out_a <= 0.00001f) {
        return 0;
    }

    auto channel = [&](std::uint8_t sc, std::uint8_t dc) -> std::uint8_t {
        float out = ((static_cast<float>(sc) / 255.0f) * sa +
                     (static_cast<float>(dc) / 255.0f) * da * (1.0f - sa)) / out_a;
        return static_cast<std::uint8_t>(std::clamp(out * 255.0f, 0.0f, 255.0f) + 0.5f);
    };

    return rgba(channel(r(src), r(dst)),
                channel(g(src), g(dst)),
                channel(b(src), b(dst)),
                static_cast<std::uint8_t>(std::clamp(out_a * 255.0f, 0.0f, 255.0f) + 0.5f));
}

inline int color_distance(Pixel a_color, Pixel b_color, bool straight_alpha = false) {
    int dr = static_cast<int>(r(a_color)) - static_cast<int>(r(b_color));
    int dg = static_cast<int>(g(a_color)) - static_cast<int>(g(b_color));
    int db = static_cast<int>(b(a_color)) - static_cast<int>(b(b_color));
    int da = static_cast<int>(a(a_color)) - static_cast<int>(a(b_color));

    if (!straight_alpha && a(a_color) == 0 && a(b_color) == 0) {
        dr = dg = db = 0;
    }

    return static_cast<int>(std::sqrt(static_cast<float>(dr * dr + dg * dg + db * db + da * da)));
}

inline float luminance(Pixel c) {
    return 0.2126f * static_cast<float>(r(c)) +
           0.7152f * static_cast<float>(g(c)) +
           0.0722f * static_cast<float>(b(c));
}

inline Pixel checker(int x, int y) {
    bool dark = ((x / 8) + (y / 8)) % 2 == 0;
    return dark ? rgba(178, 178, 178, 255) : rgba(220, 220, 220, 255);
}

} // namespace px
