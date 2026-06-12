// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Document.hpp"

#include <array>
#include <vector>

namespace px {

constexpr int kMaxCurvePoints = 8;

struct LevelsSettings {
    int in_black = 0;
    int in_white = 255;
    float gamma = 1.0f;
    int out_black = 0;
    int out_white = 255;
    bool red = true;
    bool green = true;
    bool blue = true;
};

struct CurvesSettings {
    std::array<float, kMaxCurvePoints> x = {0.0f, 1.0f};
    std::array<float, kMaxCurvePoints> y = {0.0f, 1.0f};
    int point_count = 2;
    bool luma = true;
    bool red = true;
    bool green = true;
    bool blue = true;
};

struct DepthOfFieldSettings {
    int focus_depth = 128;
    int aperture = 45;
    int falloff = 35;
    int max_radius = 12;
};

enum class ResamplingMode {
    Nearest,
    Bilinear,
    Bicubic
};

void apply_brightness_contrast(Document& doc, int brightness, int contrast);
void apply_hsv(Document& doc, float hue_degrees, float saturation_delta, float value_delta);
void apply_temperature(Document& doc, int temperature);
void apply_levels(Document& doc, const LevelsSettings& settings);
void apply_tonal_range(Document& doc, int white_point, int highlights, int shadows, int black_point);
void apply_curves(Document& doc, const CurvesSettings& settings);
void apply_depth_of_field(Document& doc, const std::vector<Pixel>& depth_pixels, const DepthOfFieldSettings& settings);
void apply_auto_level(Document& doc);
void apply_posterize(Document& doc, int levels);
void apply_invert(Document& doc, bool alpha);
void apply_grayscale(Document& doc);
void apply_sepia(Document& doc);
void apply_palette_quantize(Document& doc, const Palette& palette, bool dither);

void apply_flip_horizontal(Document& doc);
void apply_flip_vertical(Document& doc);
void apply_rotate_90_clockwise(Document& doc);
void apply_rotate_90_counter_clockwise(Document& doc);
void apply_rotate_180(Document& doc);
void apply_rotate_zoom(Document& doc,
                       float angle_degrees,
                       float zoom,
                       int pan_x,
                       int pan_y,
                       ResamplingMode resampling = ResamplingMode::Nearest);

void apply_oil_painting(Document& doc, int brush_size, int coarseness);
void apply_ink_sketch(Document& doc, int outline, int coloring);
void apply_pencil_sketch(Document& doc, int tip_size, int range);
void apply_square_blur(Document& doc, int radius);
void apply_gaussian_blur(Document& doc, int radius);
void apply_motion_blur(Document& doc, int distance, float angle_degrees);
void apply_radial_blur(Document& doc, int amount);
void apply_zoom_blur(Document& doc, int amount);
void apply_median_blur(Document& doc, int radius);
void apply_surface_blur(Document& doc, int radius, int threshold);
void apply_sketch_blur(Document& doc, int radius);
void apply_pixelate(Document& doc, int cell_size);
void apply_crystalize(Document& doc, int cell_size);
void apply_frosted_glass(Document& doc, int radius);
void apply_bulge(Document& doc, float strength);
void apply_twist(Document& doc, float turns);
void apply_tile_reflection(Document& doc, int tile_size);
void apply_dents(Document& doc, int scale, int amount);
void apply_morphology(Document& doc, int radius, bool erode);
void apply_polar_inversion(Document& doc, float scale);
void apply_add_noise(Document& doc, int intensity, int coverage, int color_saturation);
void apply_reduce_noise(Document& doc, int radius);
void apply_glow(Document& doc, int radius, int brightness, int contrast);
void apply_red_eye_removal(Document& doc, int strength);
void apply_sharpen(Document& doc, int amount);
void apply_soften_portrait(Document& doc, int softness, int lighting, int warmth);
void apply_vignette(Document& doc, int radius, int strength);
void apply_straighten(Document& doc,
                      float angle_degrees,
                      ResamplingMode resampling = ResamplingMode::Nearest);
void apply_edge_detect(Document& doc, int strength);
void apply_emboss(Document& doc, float angle_degrees);
void apply_outline(Document& doc, int thickness, int intensity);
void apply_relief(Document& doc, float angle_degrees);
void apply_clouds(Document& doc, int scale, int roughness, Pixel color_a, Pixel color_b);
void apply_julia_fractal(Document& doc, float zoom, float angle_degrees);
void apply_mandelbrot_fractal(Document& doc, float zoom, float angle_degrees, bool invert);
void apply_turbulence(Document& doc, int scale, int octaves);

} // namespace px
