// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Filters.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace px::filter_detail {

#if defined(NDEBUG) && defined(_MSC_VER)
#define PIXELART_FILTER_FORCE_INLINE __forceinline
#elif defined(NDEBUG) && (defined(__GNUC__) || defined(__clang__))
#define PIXELART_FILTER_FORCE_INLINE inline __attribute__((always_inline))
#else
#define PIXELART_FILTER_FORCE_INLINE inline
#endif

inline bool editable(const Document& doc, int x, int y, Pixel pixel) {
    return doc.selection.contains(x, y) && a(pixel) > 0;
}

template <typename Transform>
PIXELART_FILTER_FORCE_INLINE Pixel transform_pixels(Document& doc,
                                                    const char* name,
                                                    Transform&& transform) {
    auto before = doc.snapshot_active_cel();
    auto& pixels = doc.active_cel().pixels;
    Pixel last = 0;
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            const auto index = static_cast<std::size_t>(doc.pixel_index(x, y));
            pixels[index] = transform(x, y, pixels[index]);
            last = pixels[index];
        }
    }
    doc.commit_active_cel_edit(name, std::move(before));
    return last;
}

inline std::uint8_t to_u8(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}

inline std::uint8_t to_u8_int(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

Pixel sample_clamped(const std::vector<Pixel>& pixels,
                     int width,
                     int height,
                     int x,
                     int y);
Pixel sample_nearest(const std::vector<Pixel>& pixels,
                     int width,
                     int height,
                     float x,
                     float y);
Pixel mix_pixels(Pixel first, Pixel second, float amount);

template <typename Transform>
PIXELART_FILTER_FORCE_INLINE void transform_from_source(Document& doc,
                                                        const char* name,
                                                        Transform&& transform) {
    auto before = doc.snapshot_active_cel();
    const auto source = before;
    auto& pixels = doc.active_cel().pixels;
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            if (doc.selection.contains(x, y)) {
                pixels[static_cast<std::size_t>(doc.pixel_index(x, y))] =
                    transform(x, y, source);
            }
        }
    }
    doc.commit_active_cel_edit(name, std::move(before));
}

void apply_affine(Document& doc,
                  const char* name,
                  float angle_degrees,
                  float zoom,
                  int pan_x,
                  int pan_y,
                  ResamplingMode resampling = ResamplingMode::Nearest);

float noise01(int x, int y, int seed);
float value_noise(float x, float y, int seed);

Pixel blur_at(const std::vector<Pixel>& source,
              int width,
              int height,
              int x,
              int y,
              int radius);
int depth_of_field_radius_for_depth(std::uint8_t depth,
                                    const DepthOfFieldSettings& settings);
Pixel convolve_at(const std::vector<Pixel>& source,
                  int width,
                  int height,
                  int x,
                  int y,
                  const std::array<float, 9>& kernel,
                  float bias = 0.0f);

#undef PIXELART_FILTER_FORCE_INLINE

} // namespace px::filter_detail
