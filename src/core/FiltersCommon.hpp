// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Filters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace px::filter_detail {

bool editable(const Document& doc, int x, int y, Pixel pixel);
Pixel transform_pixels(Document& doc,
                       const char* name,
                       const std::function<Pixel(int, int, Pixel)>& transform);

std::uint8_t to_u8(float value);
std::uint8_t to_u8_int(int value);

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

void transform_from_source(
    Document& doc,
    const char* name,
    const std::function<Pixel(int, int, const std::vector<Pixel>&)>& transform);
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

} // namespace px::filter_detail
