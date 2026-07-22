// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Filters.hpp"

// Keep unit-test assertions active even when the project is configured as Release.
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace px;

namespace {

std::size_t index_of(const Document& doc, int x, int y) {
    return static_cast<std::size_t>(doc.pixel_index(x, y));
}

Document patterned_document(int width = 9, int height = 7) {
    Document doc = Document::create(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto red = static_cast<std::uint8_t>((x * 29 + y * 17 + 13) % 256);
            const auto green = static_cast<std::uint8_t>((x * 11 + y * 41 + 47) % 256);
            const auto blue = static_cast<std::uint8_t>((x * 53 + y * 7 + 89) % 256);
            doc.active_cel().pixels[index_of(doc, x, y)] = rgba(red, green, blue, 255);
        }
    }
    return doc;
}

void assert_shape_unchanged(const Document& doc, int width, int height) {
    assert(doc.width == width);
    assert(doc.height == height);
    assert(doc.active_cel().pixels.size() ==
           static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
}

void assert_all_opaque(const Document& doc) {
    assert(std::all_of(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(),
                       [](Pixel pixel) { return a(pixel) == 255; }));
}

template <typename Effect>
Document apply_and_expect_change(Effect effect, int width = 9, int height = 7) {
    Document doc = patterned_document(width, height);
    const std::vector<Pixel> before = doc.active_cel().pixels;
    effect(doc);
    assert_shape_unchanged(doc, width, height);
    assert(doc.active_cel().pixels != before);
    return doc;
}

template <typename Effect>
void expect_identity(Effect effect, int width = 9, int height = 7) {
    Document doc = patterned_document(width, height);
    const std::vector<Pixel> before = doc.active_cel().pixels;
    effect(doc);
    assert_shape_unchanged(doc, width, height);
    assert(doc.active_cel().pixels == before);
}

template <typename Effect>
void expect_deterministic(Effect effect, int width = 9, int height = 7) {
    Document first = patterned_document(width, height);
    Document second = patterned_document(width, height);
    effect(first);
    effect(second);
    assert(first.active_cel().pixels == second.active_cel().pixels);
}

void test_adjustments_and_channel_controls() {
    {
        Document doc = Document::create(3, 1);
        doc.active_cel().pixels = {
            rgba(20, 40, 60, 255),
            rgba(80, 100, 120, 127),
            rgba(140, 160, 180, 0),
        };
        doc.selection.select_rect(1, 0, 1, 0, true);
        const std::vector<Pixel> before = doc.active_cel().pixels;
        apply_brightness_contrast(doc, 30, 0);
        assert(doc.active_cel().pixels[0] == before[0]);
        assert(r(doc.active_cel().pixels[1]) == 110);
        assert(g(doc.active_cel().pixels[1]) == 130);
        assert(b(doc.active_cel().pixels[1]) == 150);
        assert(a(doc.active_cel().pixels[1]) == 127);
        assert(doc.active_cel().pixels[2] == before[2]);
    }

    {
        Document doc = Document::create(2, 1);
        doc.active_cel().pixels = {rgba(255, 0, 0, 91), rgba(10, 20, 30, 0)};
        apply_hsv(doc, 120.0f, 0.0f, 0.0f);
        assert(r(doc.active_cel().pixels[0]) == 0);
        assert(g(doc.active_cel().pixels[0]) == 255);
        assert(b(doc.active_cel().pixels[0]) == 0);
        assert(a(doc.active_cel().pixels[0]) == 91);
        assert(doc.active_cel().pixels[1] == rgba(10, 20, 30, 0));
    }

    {
        Document warm = Document::create(1, 1);
        warm.active_cel().pixels[0] = rgba(100, 100, 100, 203);
        apply_temperature(warm, 1000);
        assert(r(warm.active_cel().pixels[0]) == 145);
        assert(g(warm.active_cel().pixels[0]) == 112);
        assert(b(warm.active_cel().pixels[0]) == 65);
        assert(a(warm.active_cel().pixels[0]) == 203);

        Document cool = Document::create(1, 1);
        cool.active_cel().pixels[0] = rgba(100, 100, 100, 203);
        apply_temperature(cool, -1000);
        assert(r(cool.active_cel().pixels[0]) == 72);
        assert(g(cool.active_cel().pixels[0]) == 108);
        assert(b(cool.active_cel().pixels[0]) == 145);
    }

    {
        Document doc = Document::create(1, 1);
        doc.active_cel().pixels[0] = rgba(64, 73, 91, 177);
        LevelsSettings levels;
        levels.in_black = 64;
        levels.in_white = 192;
        levels.out_black = 10;
        levels.out_white = 210;
        levels.red = true;
        levels.green = false;
        levels.blue = false;
        apply_levels(doc, levels);
        assert(r(doc.active_cel().pixels[0]) == 10);
        assert(g(doc.active_cel().pixels[0]) == 73);
        assert(b(doc.active_cel().pixels[0]) == 91);
        assert(a(doc.active_cel().pixels[0]) == 177);

        const Pixel before = doc.active_cel().pixels[0];
        levels.red = false;
        apply_levels(doc, levels);
        assert(doc.active_cel().pixels[0] == before);
    }

    {
        Document doc = Document::create(4, 1);
        doc.active_cel().pixels = {
            rgba(12, 12, 12, 255),
            rgba(64, 64, 64, 255),
            rgba(192, 192, 192, 255),
            rgba(245, 245, 245, 123),
        };
        apply_tonal_range(doc, 35, -40, 45, -30);
        assert(r(doc.active_cel().pixels[1]) > 64);
        assert(r(doc.active_cel().pixels[2]) < 192);
        assert(a(doc.active_cel().pixels[3]) == 123);
    }

    {
        CurvesSettings identity;
        identity.point_count = 3;
        identity.x = {0.0f, 0.5f, 1.0f};
        identity.y = {0.0f, 0.5f, 1.0f};
        expect_identity([&](Document& doc) { apply_curves(doc, identity); });

        Document channels = Document::create(1, 1);
        channels.active_cel().pixels[0] = rgba(64, 96, 128, 171);
        CurvesSettings curve;
        curve.point_count = 3;
        curve.x = {0.0f, 0.5f, 1.0f};
        curve.y = {0.0f, 0.85f, 1.0f};
        curve.luma = false;
        curve.red = true;
        curve.green = false;
        curve.blue = false;
        apply_curves(channels, curve);
        assert(r(channels.active_cel().pixels[0]) > 64);
        assert(g(channels.active_cel().pixels[0]) == 96);
        assert(b(channels.active_cel().pixels[0]) == 128);
        assert(a(channels.active_cel().pixels[0]) == 171);
    }

    {
        Document doc = Document::create(3, 1);
        doc.active_cel().pixels = {
            rgba(50, 30, 10, 255),
            rgba(100, 80, 60, 128),
            rgba(150, 130, 110, 0),
        };
        apply_auto_level(doc);
        assert(doc.active_cel().pixels[0] == rgba(0, 0, 0, 255));
        assert(doc.active_cel().pixels[1] == rgba(255, 255, 255, 128));
        assert(doc.active_cel().pixels[2] == rgba(150, 130, 110, 0));
    }

    {
        Document doc = Document::create(1, 1);
        doc.active_cel().pixels[0] = rgba(80, 170, 200, 77);
        apply_posterize(doc, -20);
        const Pixel pixel = doc.active_cel().pixels[0];
        assert((r(pixel) == 0 || r(pixel) == 255));
        assert((g(pixel) == 0 || g(pixel) == 255));
        assert((b(pixel) == 0 || b(pixel) == 255));
        assert(a(pixel) == 77);
    }

    {
        Document colors = Document::create(1, 1);
        colors.active_cel().pixels[0] = rgba(20, 70, 130, 44);
        apply_invert(colors, false);
        assert(colors.active_cel().pixels[0] == rgba(235, 185, 125, 44));
        apply_invert(colors, true);
        assert(colors.active_cel().pixels[0] == rgba(235, 185, 125, 211));
    }

    {
        Document gray = Document::create(1, 1);
        gray.active_cel().pixels[0] = rgba(20, 100, 220, 99);
        apply_grayscale(gray);
        assert(r(gray.active_cel().pixels[0]) == g(gray.active_cel().pixels[0]));
        assert(g(gray.active_cel().pixels[0]) == b(gray.active_cel().pixels[0]));
        assert(a(gray.active_cel().pixels[0]) == 99);

        Document sepia = Document::create(1, 1);
        sepia.active_cel().pixels[0] = rgba(80, 100, 120, 66);
        apply_sepia(sepia);
        assert(r(sepia.active_cel().pixels[0]) >= g(sepia.active_cel().pixels[0]));
        assert(g(sepia.active_cel().pixels[0]) >= b(sepia.active_cel().pixels[0]));
        assert(a(sepia.active_cel().pixels[0]) == 66);
    }
}

void test_depth_of_field_and_palette() {
    {
        Document doc = Document::create(5, 1);
        for (int x = 0; x < doc.width; ++x) {
            doc.active_cel().pixels[static_cast<std::size_t>(x)] =
                rgba(static_cast<std::uint8_t>(x * 50), 0, 0, static_cast<std::uint8_t>(220 - x * 10));
        }
        const std::vector<Pixel> before = doc.active_cel().pixels;
        const std::vector<Pixel> depth = {
            rgba(0, 0, 0), rgba(64, 64, 64), rgba(128, 128, 128),
            rgba(192, 192, 192), rgba(255, 255, 255),
        };
        DepthOfFieldSettings settings;
        settings.focus_depth = 128;
        settings.aperture = 100;
        settings.falloff = 32;
        settings.max_radius = 2;
        apply_depth_of_field(doc, depth, settings);
        assert(doc.active_cel().pixels[2] == before[2]);
        assert(doc.active_cel().pixels[0] != before[0]);
        assert(a(doc.active_cel().pixels[0]) < a(before[0]));

        Document mismatch = patterned_document(3, 2);
        const std::vector<Pixel> mismatch_before = mismatch.active_cel().pixels;
        apply_depth_of_field(mismatch, {}, settings);
        assert(mismatch.active_cel().pixels == mismatch_before);
    }

    {
        Palette palette;
        palette.colors = {rgba(0, 0, 0), rgba(255, 255, 255), rgba(255, 0, 0)};
        Document doc = patterned_document(7, 5);
        doc.active_cel().pixels[0] = rgba(220, 20, 20, 73);
        apply_palette_quantize(doc, palette, false);
        assert(r(doc.active_cel().pixels[0]) == 255);
        assert(g(doc.active_cel().pixels[0]) == 0);
        assert(b(doc.active_cel().pixels[0]) == 0);
        assert(a(doc.active_cel().pixels[0]) == 73);
        for (Pixel pixel : doc.active_cel().pixels) {
            const Pixel opaque = with_alpha(pixel, 255);
            assert(std::find(palette.colors.begin(), palette.colors.end(), opaque) != palette.colors.end());
        }

        Document dithered = patterned_document(7, 5);
        apply_palette_quantize(dithered, palette, true);
        for (Pixel pixel : dithered.active_cel().pixels) {
            assert(std::find(palette.colors.begin(), palette.colors.end(), pixel) != palette.colors.end());
        }

        Palette empty;
        expect_identity([&](Document& unchanged) { apply_palette_quantize(unchanged, empty, true); });
    }
}

void test_selection_clipping_for_filter_families() {
    {
        Document doc = patterned_document(7, 5);
        doc.selection.select_rect(2, 1, 4, 3, true);
        const std::vector<Pixel> before = doc.active_cel().pixels;
        apply_gaussian_blur(doc, 2);
        bool selected_changed = false;
        for (int y = 0; y < doc.height; ++y) {
            for (int x = 0; x < doc.width; ++x) {
                const std::size_t index = index_of(doc, x, y);
                if (doc.selection.contains(x, y)) {
                    selected_changed = selected_changed || doc.active_cel().pixels[index] != before[index];
                } else {
                    assert(doc.active_cel().pixels[index] == before[index]);
                }
            }
        }
        assert(selected_changed);
    }

    {
        Document doc = patterned_document(7, 5);
        doc.selection.select_rect(1, 1, 2, 2, true);
        const std::vector<Pixel> before = doc.active_cel().pixels;
        apply_clouds(doc, 5, 3, rgba(10, 20, 30), rgba(230, 220, 210));
        for (int y = 0; y < doc.height; ++y) {
            for (int x = 0; x < doc.width; ++x) {
                if (!doc.selection.contains(x, y)) {
                    assert(doc.active_cel().pixels[index_of(doc, x, y)] == before[index_of(doc, x, y)]);
                }
            }
        }
        assert(doc.active_cel().pixels[index_of(doc, 1, 1)] != before[index_of(doc, 1, 1)]);
    }

    {
        Document doc = patterned_document(5, 3);
        doc.selection.select_rect(1, 1, 1, 1, true);
        const std::vector<Pixel> before = doc.active_cel().pixels;
        apply_flip_horizontal(doc);
        assert(doc.active_cel().pixels[index_of(doc, 1, 1)] == before[index_of(doc, 3, 1)]);
        assert(doc.active_cel().pixels[index_of(doc, 1, 1)] != before[index_of(doc, 1, 1)]);
        for (int y = 0; y < doc.height; ++y) {
            for (int x = 0; x < doc.width; ++x) {
                if (!doc.selection.contains(x, y)) {
                    assert(doc.active_cel().pixels[index_of(doc, x, y)] == before[index_of(doc, x, y)]);
                }
            }
        }
    }
}

void test_flips_rotations_and_resampling() {
    Document source = patterned_document(3, 3);
    const std::vector<Pixel> before = source.active_cel().pixels;

    {
        Document doc = source;
        apply_flip_horizontal(doc);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                assert(doc.active_cel().pixels[index_of(doc, x, y)] == before[index_of(source, 2 - x, y)]);
            }
        }
    }

    {
        Document doc = source;
        apply_flip_vertical(doc);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                assert(doc.active_cel().pixels[index_of(doc, x, y)] == before[index_of(source, x, 2 - y)]);
            }
        }
    }

    {
        Document doc = source;
        apply_rotate_180(doc);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                assert(doc.active_cel().pixels[index_of(doc, x, y)] == before[index_of(source, 2 - x, 2 - y)]);
            }
        }
    }

    {
        Document clockwise = source;
        apply_rotate_90_clockwise(clockwise);
        assert(clockwise.active_cel().pixels != before);
        assert(clockwise.active_cel().pixels[index_of(clockwise, 0, 0)] == before[index_of(source, 2, 0)]);

        Document counter_clockwise = source;
        apply_rotate_90_counter_clockwise(counter_clockwise);
        assert(counter_clockwise.active_cel().pixels != before);
        assert(counter_clockwise.active_cel().pixels[index_of(counter_clockwise, 0, 0)] == before[index_of(source, 0, 2)]);
    }

    expect_identity([](Document& doc) {
        apply_rotate_zoom(doc, 0.0f, 1.0f, 0, 0, ResamplingMode::Nearest);
    });
    expect_identity([](Document& doc) {
        apply_rotate_zoom(doc, 0.0f, 1.0f, 0, 0, ResamplingMode::Bilinear);
    });
    expect_identity([](Document& doc) {
        apply_rotate_zoom(doc, 0.0f, 1.0f, 0, 0, ResamplingMode::Bicubic);
    });

    apply_and_expect_change([](Document& doc) {
        apply_rotate_zoom(doc, 21.0f, 1.15f, 1, -1, ResamplingMode::Nearest);
    });
    apply_and_expect_change([](Document& doc) {
        apply_rotate_zoom(doc, 21.0f, 1.15f, 1, -1, ResamplingMode::Bilinear);
    });
    apply_and_expect_change([](Document& doc) {
        apply_rotate_zoom(doc, 21.0f, 1.15f, 1, -1, ResamplingMode::Bicubic);
    });
    apply_and_expect_change([](Document& doc) {
        apply_straighten(doc, -13.0f, ResamplingMode::Bilinear);
    });
}

void test_blur_and_pixel_effects() {
    Document square = apply_and_expect_change([](Document& doc) { apply_square_blur(doc, 0); });
    assert_all_opaque(square);
    Document gaussian = apply_and_expect_change([](Document& doc) { apply_gaussian_blur(doc, 2); });
    assert_all_opaque(gaussian);
    apply_and_expect_change([](Document& doc) { apply_motion_blur(doc, 3, 20.0f); });
    apply_and_expect_change([](Document& doc) { apply_radial_blur(doc, 8, -50, 150); }, 31, 21);
    apply_and_expect_change([](Document& doc) { apply_zoom_blur(doc, 8, 35, 65); }, 31, 21);
    apply_and_expect_change([](Document& doc) { apply_median_blur(doc, 2); });
    apply_and_expect_change([](Document& doc) { apply_surface_blur(doc, 2, 1000); });

    Document sketch = apply_and_expect_change([](Document& doc) { apply_sketch_blur(doc, 2); });
    for (Pixel pixel : sketch.active_cel().pixels) {
        assert(r(pixel) == g(pixel));
        assert(g(pixel) == b(pixel));
    }

    apply_and_expect_change([](Document& doc) { apply_pixelate(doc, 3); });
    apply_and_expect_change([](Document& doc) { apply_crystalize(doc, 3); });
    apply_and_expect_change([](Document& doc) { apply_frosted_glass(doc, 4); });
    expect_deterministic([](Document& doc) { apply_crystalize(doc, 3); });
    expect_deterministic([](Document& doc) { apply_frosted_glass(doc, 4); });
}

void test_distortions_and_morphology() {
    expect_identity([](Document& doc) { apply_bulge(doc, 0.0f); });
    expect_identity([](Document& doc) { apply_twist(doc, 0.0f); });
    apply_and_expect_change([](Document& doc) { apply_bulge(doc, 1.4f, 45, 55); });
    apply_and_expect_change([](Document& doc) { apply_twist(doc, 0.8f, 50, 50, 70); });
    apply_and_expect_change([](Document& doc) { apply_tile_reflection(doc, 3); });
    apply_and_expect_change([](Document& doc) { apply_dents(doc, 3, 9); });
    apply_and_expect_change([](Document& doc) { apply_polar_inversion(doc, 1.0f); });
    expect_deterministic([](Document& doc) { apply_dents(doc, 3, 9); });

    {
        Document source = Document::create(3, 3);
        std::fill(source.active_cel().pixels.begin(), source.active_cel().pixels.end(), rgba(20, 20, 20));
        source.active_cel().pixels[index_of(source, 1, 1)] = rgba(240, 240, 240);

        Document dilated = source;
        apply_morphology(dilated, 1, false);
        assert(r(dilated.active_cel().pixels[index_of(dilated, 0, 0)]) == 240);
        assert(r(dilated.active_cel().pixels[index_of(dilated, 2, 2)]) == 240);

        Document eroded = source;
        apply_morphology(eroded, 1, true);
        assert(r(eroded.active_cel().pixels[index_of(eroded, 1, 1)]) == 20);
    }
}

void test_noise_photo_and_edge_effects() {
    expect_identity([](Document& doc) { apply_add_noise(doc, 200, 0, 100); });
    expect_identity([](Document& doc) { apply_add_noise(doc, 0, 100, 100); });
    apply_and_expect_change([](Document& doc) { apply_add_noise(doc, 35, 100, 70); });
    expect_deterministic([](Document& doc) { apply_add_noise(doc, 35, 100, 70); });

    apply_and_expect_change([](Document& doc) { apply_reduce_noise(doc, 2); });
    apply_and_expect_change([](Document& doc) { apply_glow(doc, 2, 25, 35); });

    {
        Document doc = Document::create(2, 1);
        doc.active_cel().pixels = {rgba(240, 30, 20, 81), rgba(50, 100, 100, 92)};
        apply_red_eye_removal(doc, 100);
        assert(r(doc.active_cel().pixels[0]) < 240);
        assert(g(doc.active_cel().pixels[0]) == 30);
        assert(b(doc.active_cel().pixels[0]) == 20);
        assert(a(doc.active_cel().pixels[0]) == 81);
        assert(doc.active_cel().pixels[1] == rgba(50, 100, 100, 92));
    }

    apply_and_expect_change([](Document& doc) { apply_sharpen(doc, 80); });
    apply_and_expect_change([](Document& doc) { apply_soften_portrait(doc, 2, 30, 40); });

    {
        Document doc = Document::create(7, 7);
        std::fill(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(), rgba(200, 160, 120, 109));
        apply_vignette(doc, 70, 100, 50, 50);
        const Pixel center = doc.active_cel().pixels[index_of(doc, 3, 3)];
        const Pixel corner = doc.active_cel().pixels[index_of(doc, 0, 0)];
        assert(r(center) == 200);
        assert(r(corner) < r(center));
        assert(a(corner) == 109);
    }

    Document edges = apply_and_expect_change([](Document& doc) { apply_edge_detect(doc, 100); });
    for (Pixel pixel : edges.active_cel().pixels) {
        assert(r(pixel) == g(pixel));
        assert(g(pixel) == b(pixel));
    }
    apply_and_expect_change([](Document& doc) { apply_emboss(doc, 35.0f); });
    apply_and_expect_change([](Document& doc) { apply_outline(doc, 2, 220); });
    apply_and_expect_change([](Document& doc) { apply_relief(doc, 120.0f); });
}

void test_artistic_effects() {
    apply_and_expect_change([](Document& doc) { apply_oil_painting(doc, 2, 12); });
    apply_and_expect_change([](Document& doc) { apply_ink_sketch(doc, 110, 45); });

    Document pencil = apply_and_expect_change([](Document& doc) { apply_pencil_sketch(doc, 2, 70); });
    for (Pixel pixel : pencil.active_cel().pixels) {
        assert(r(pixel) == g(pixel));
        assert(g(pixel) == b(pixel));
        assert(a(pixel) == 255);
    }
}

void test_procedural_generators() {
    const Pixel cloud_a = rgba(5, 25, 80, 71);
    const Pixel cloud_b = rgba(230, 210, 160, 211);
    Document clouds = apply_and_expect_change([&](Document& doc) {
        apply_clouds(doc, 4, 4, cloud_a, cloud_b);
    });
    for (Pixel pixel : clouds.active_cel().pixels) {
        assert(r(pixel) >= r(cloud_a) && r(pixel) <= r(cloud_b));
        assert(g(pixel) >= g(cloud_a) && g(pixel) <= g(cloud_b));
        assert(a(pixel) >= a(cloud_a) && a(pixel) <= a(cloud_b));
    }
    expect_deterministic([&](Document& doc) { apply_clouds(doc, 4, 4, cloud_a, cloud_b); });

    Document julia = apply_and_expect_change([](Document& doc) { apply_julia_fractal(doc, 1.2f, 17.0f); }, 11, 9);
    assert_all_opaque(julia);
    expect_deterministic([](Document& doc) { apply_julia_fractal(doc, 1.2f, 17.0f); }, 11, 9);

    Document mandelbrot = apply_and_expect_change([](Document& doc) {
        apply_mandelbrot_fractal(doc, 1.1f, -12.0f, false);
    }, 11, 9);
    assert_all_opaque(mandelbrot);
    Document inverted = patterned_document(11, 9);
    apply_mandelbrot_fractal(inverted, 1.1f, -12.0f, true);
    assert(inverted.active_cel().pixels != mandelbrot.active_cel().pixels);

    Document turbulence = apply_and_expect_change([](Document& doc) { apply_turbulence(doc, 5, 4); });
    for (Pixel pixel : turbulence.active_cel().pixels) {
        assert(r(pixel) == g(pixel));
        assert(g(pixel) == b(pixel));
        assert(a(pixel) == 255);
    }
    expect_deterministic([](Document& doc) { apply_turbulence(doc, 5, 4); });
}

} // namespace

int main() {
    test_adjustments_and_channel_controls();
    test_depth_of_field_and_palette();
    test_selection_clipping_for_filter_families();
    test_flips_rotations_and_resampling();
    test_blur_and_pixel_effects();
    test_distortions_and_morphology();
    test_noise_photo_and_edge_effects();
    test_artistic_effects();
    test_procedural_generators();
    std::cout << "pixelart filter coverage tests passed\n";
    return 0;
}
