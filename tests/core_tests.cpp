// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "core/Filters.hpp"
#include "core/Model.hpp"
#include "core/Tools.hpp"
#include "depth/DepthMapExtractor.hpp"
#include "io/ProjectIO.hpp"
#include "render/GpuEffectRenderer.hpp"
#include "render/Renderer3D.hpp"
#include "ui/AppSettings.hpp"

// Keep unit-test assertions active even when the project is configured as Release.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace px;

static std::size_t checked_pixel_index(const Document& doc, int x, int y) {
    return static_cast<std::size_t>(doc.pixel_index(x, y));
}

static bool pixel_marked(const Document& doc, int x, int y) {
    return a(doc.active_cel().pixels[checked_pixel_index(doc, x, y)]) != 0;
}

static void test_canvas_and_undo() {
    auto doc = Document::create(16, 16);
    ToolContext ctx;
    ctx.primary = rgba(255, 0, 0);
    ctx.brush_size = 3;
    draw_brush(doc, 8, 8, ctx);
    assert(a(doc.active_cel().pixels[checked_pixel_index(doc, 8, 8)]) == 255);
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 8, 8)]) == 255);
    auto names = doc.consume_recent_commit_names();
    assert(names.size() == 1);
    assert(names[0] == "Brush");
    auto before = doc.snapshot_active_cel();
    doc.commit_active_cel_edit("No-op", before);
    assert(doc.consume_recent_commit_names().empty());
    assert(doc.undo());
    assert(a(doc.active_cel().pixels[checked_pixel_index(doc, 8, 8)]) == 0);
    assert(doc.redo());
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 8, 8)]) == 255);
}

static void test_fill_and_selection() {
    auto doc = Document::create(8, 8);
    draw_rect(doc, 1, 1, 6, 6, rgba(0, 0, 0), 1, false);
    fill_bucket(doc, 3, 3, rgba(0, 255, 0), 0, true);
    assert(g(doc.active_cel().pixels[checked_pixel_index(doc, 3, 3)]) == 255);
    assert(g(doc.active_cel().pixels[checked_pixel_index(doc, 0, 0)]) == 0);
    magic_wand(doc, 3, 3, 0, true);
    assert(doc.selection.active);
    assert(doc.selection.contains(3, 3));
    assert(!doc.selection.contains(0, 0));
}

static void test_selection_combine_modes_and_nudge() {
    auto doc = Document::create(8, 8);
    doc.selection.select_rect(1, 1, 3, 3, SelectionCombineMode::Replace);
    assert(doc.selection.contains(1, 1));
    doc.selection.select_rect(5, 5, 6, 6, SelectionCombineMode::Add);
    assert(doc.selection.contains(6, 6));
    doc.selection.select_rect(2, 2, 6, 6, SelectionCombineMode::Subtract);
    assert(doc.selection.contains(1, 1));
    assert(!doc.selection.contains(3, 3));
    assert(!doc.selection.contains(6, 6));

    doc.selection.select_rect(0, 0, 7, 7, SelectionCombineMode::Replace);
    doc.selection.select_rect(2, 2, 4, 4, SelectionCombineMode::Intersect);
    assert(doc.selection.contains(3, 3));
    assert(!doc.selection.contains(1, 1));

    doc.selection.select_rect(3, 3, 3, 3, SelectionCombineMode::Invert);
    assert(!doc.selection.contains(3, 3));
    doc.selection.translate(1, -1);
    assert(doc.selection.contains(3, 1));
    assert(!doc.selection.contains(2, 2));
}

static void test_thin_ellipse_strokes_are_continuous() {
    auto horizontal = Document::create(36, 16);
    draw_ellipse(horizontal, 2, 7, 33, 9, rgba(255, 0, 0, 255), 1, false);
    for (int x = 2; x <= 33; ++x) {
        bool marked = false;
        for (int y = 7; y <= 9; ++y) {
            marked = marked || pixel_marked(horizontal, x, y);
        }
        assert(marked);
    }

    auto vertical = Document::create(16, 36);
    draw_ellipse(vertical, 7, 2, 9, 33, rgba(255, 0, 0, 255), 1, false);
    for (int y = 2; y <= 33; ++y) {
        bool marked = false;
        for (int x = 7; x <= 9; ++x) {
            marked = marked || pixel_marked(vertical, x, y);
        }
        assert(marked);
    }
}

static void test_constrained_tool_endpoints() {
    assert((constrained_tool_endpoint(ToolType::Rectangle, 10, 10, 18, 12, true) ==
            std::array<int, 2>{18, 18}));
    assert((constrained_tool_endpoint(ToolType::Ellipse, 10, 10, 7, 20, true) ==
            std::array<int, 2>{0, 20}));
    assert((constrained_tool_endpoint(ToolType::Line, 10, 10, 20, 13, true) ==
            std::array<int, 2>{20, 10}));
    assert((constrained_tool_endpoint(ToolType::Line, 10, 10, 20, 17, true) ==
            std::array<int, 2>{20, 20}));
    assert((constrained_tool_endpoint(ToolType::Line, 10, 10, 13, 25, true) ==
            std::array<int, 2>{10, 25}));
    assert((constrained_tool_endpoint(ToolType::Gradient, 10, 10, 13, 25, true) ==
            std::array<int, 2>{13, 25}));
    assert((constrained_tool_endpoint(ToolType::RectSelect, 10, 10, 18, 12, true) ==
            std::array<int, 2>{18, 18}));
    assert((constrained_tool_endpoint(ToolType::Rectangle, 10, 10, 18, 12, false) ==
            std::array<int, 2>{18, 12}));
}

static void test_selection_clips_drawing_and_delete() {
    auto doc = Document::create(8, 8);
    for (auto& pixel : doc.active_cel().pixels) {
        pixel = rgba(200, 0, 0, 255);
    }
    doc.selection.select_rect(2, 2, 4, 4, true);

    ToolContext ctx;
    ctx.primary = rgba(0, 255, 0, 255);
    ctx.brush_size = 5;
    draw_brush(doc, 3, 3, ctx);
    assert(g(doc.active_cel().pixels[checked_pixel_index(doc, 3, 3)]) == 255);
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 1, 3)]) == 200);
    assert(g(doc.active_cel().pixels[checked_pixel_index(doc, 1, 3)]) == 0);

    draw_line(doc, 0, 0, 7, 7, rgba(0, 0, 255, 255), 1);
    assert(b(doc.active_cel().pixels[checked_pixel_index(doc, 2, 2)]) == 255);
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 1, 1)]) == 200);
    assert(b(doc.active_cel().pixels[checked_pixel_index(doc, 1, 1)]) == 0);

    assert(doc.delete_selected_pixels());
    assert(a(doc.active_cel().pixels[checked_pixel_index(doc, 3, 3)]) == 0);
    assert(a(doc.active_cel().pixels[checked_pixel_index(doc, 1, 3)]) == 255);
    assert(doc.selection.active);
    assert(doc.undo());
    assert(a(doc.active_cel().pixels[checked_pixel_index(doc, 3, 3)]) == 255);

    doc.selection.clear();
    assert(!doc.delete_selected_pixels());
}

static void test_filters() {
    auto doc = Document::create(4, 4);
    fill_bucket(doc, 0, 0, rgba(120, 80, 40), 255, false);
    apply_auto_level(doc);
    apply_sepia(doc);
    apply_grayscale(doc);
    Pixel p = doc.active_cel().pixels[0];
    assert(r(p) == g(p) && g(p) == b(p));
    apply_invert(doc, false);
    Pixel inv = doc.active_cel().pixels[0];
    assert(r(inv) == static_cast<unsigned char>(255 - r(p)));
    apply_posterize(doc, 2);
    Pixel post = doc.active_cel().pixels[0];
    assert((r(post) == 0 || r(post) == 255));

    auto tone = Document::create(4, 1);
    tone.active_cel().pixels[checked_pixel_index(tone, 0, 0)] = rgba(12, 12, 12, 255);
    tone.active_cel().pixels[checked_pixel_index(tone, 1, 0)] = rgba(64, 64, 64, 255);
    tone.active_cel().pixels[checked_pixel_index(tone, 2, 0)] = rgba(192, 192, 192, 255);
    tone.active_cel().pixels[checked_pixel_index(tone, 3, 0)] = rgba(245, 245, 245, 128);
    apply_tonal_range(tone, 35, -40, 45, -30);
    const Pixel lifted_shadow = tone.active_cel().pixels[checked_pixel_index(tone, 1, 0)];
    const Pixel recovered_highlight = tone.active_cel().pixels[checked_pixel_index(tone, 2, 0)];
    assert(r(lifted_shadow) > 64);
    assert(r(recovered_highlight) < 192);
    assert(a(tone.active_cel().pixels[checked_pixel_index(tone, 3, 0)]) == 128);

    auto curves = Document::create(2, 1);
    curves.active_cel().pixels[checked_pixel_index(curves, 0, 0)] = rgba(80, 120, 180, 255);
    curves.active_cel().pixels[checked_pixel_index(curves, 1, 0)] = rgba(32, 32, 32, 0);
    CurvesSettings curve_settings;
    curve_settings.point_count = 3;
    curve_settings.x = {0.0f, 0.35f, 1.0f};
    curve_settings.y = {0.0f, 0.72f, 1.0f};
    curve_settings.luma = false;
    curve_settings.red = true;
    curve_settings.green = false;
    curve_settings.blue = false;
    apply_curves(curves, curve_settings);
    const Pixel curved = curves.active_cel().pixels[checked_pixel_index(curves, 0, 0)];
    assert(r(curved) > 80);
    assert(g(curved) == 120);
    assert(b(curved) == 180);
    assert(a(curves.active_cel().pixels[checked_pixel_index(curves, 1, 0)]) == 0);

    auto identity_curves = Document::create(1, 1);
    const Pixel identity_source = rgba(37, 128, 219, 173);
    identity_curves.active_cel().pixels[0] = identity_source;
    CurvesSettings identity_curve_settings;
    identity_curve_settings.point_count = 3;
    identity_curve_settings.x = {0.0f, 0.5f, 1.0f};
    identity_curve_settings.y = {0.0f, 0.5f, 1.0f};
    assert(identity_curve_settings.luma);
    assert(!identity_curve_settings.red);
    assert(!identity_curve_settings.green);
    assert(!identity_curve_settings.blue);
    apply_curves(identity_curves, identity_curve_settings);
    assert(identity_curves.active_cel().pixels[0] == identity_source);

    auto luminance_only = Document::create(1, 1);
    luminance_only.active_cel().pixels[0] = rgba(48, 96, 160, 255);
    CurvesSettings luminance_settings;
    luminance_settings.point_count = 3;
    luminance_settings.x = {0.0f, 0.5f, 1.0f};
    luminance_settings.y = {0.0f, 0.8f, 1.0f};
    auto invalid_mixed_mode = luminance_only;
    CurvesSettings mixed_settings = luminance_settings;
    mixed_settings.red = true;
    mixed_settings.green = true;
    mixed_settings.blue = true;
    apply_curves(luminance_only, luminance_settings);
    apply_curves(invalid_mixed_mode, mixed_settings);
    assert(invalid_mixed_mode.active_cel().pixels == luminance_only.active_cel().pixels);

    auto temperature = Document::create(2, 1);
    temperature.active_cel().pixels[checked_pixel_index(temperature, 0, 0)] = rgba(100, 100, 100, 255);
    temperature.active_cel().pixels[checked_pixel_index(temperature, 1, 0)] = rgba(100, 100, 100, 0);
    apply_temperature(temperature, 60);
    const Pixel warm_pixel = temperature.active_cel().pixels[checked_pixel_index(temperature, 0, 0)];
    assert(r(warm_pixel) > 100);
    assert(g(warm_pixel) > 100);
    assert(b(warm_pixel) < 100);
    assert(a(temperature.active_cel().pixels[checked_pixel_index(temperature, 1, 0)]) == 0);
    apply_temperature(temperature, -60);
    const Pixel cooled_pixel = temperature.active_cel().pixels[checked_pixel_index(temperature, 0, 0)];
    assert(b(cooled_pixel) > b(warm_pixel));

    auto effects = Document::create(12, 12);
    for (int y = 0; y < effects.height; ++y) {
        for (int x = 0; x < effects.width; ++x) {
            effects.active_cel().pixels[checked_pixel_index(effects, x, y)] =
                rgba(static_cast<std::uint8_t>(x * 20),
                     static_cast<std::uint8_t>(y * 20),
                     static_cast<std::uint8_t>((x + y) * 8),
                     255);
        }
    }
    const Pixel before = effects.active_cel().pixels[checked_pixel_index(effects, 2, 3)];
    apply_flip_horizontal(effects);
    assert(effects.active_cel().pixels[checked_pixel_index(effects, effects.width - 3, 3)] == before);
    assert(effects.undo());
    apply_gaussian_blur(effects, 2);
    apply_sharpen(effects, 40);
    apply_pixelate(effects, 3);
    apply_add_noise(effects, 12, 100, 50);
    apply_edge_detect(effects, 70);
    apply_clouds(effects, 8, 3, rgba(0, 0, 0), rgba(255, 255, 255));
    apply_mandelbrot_fractal(effects, 1.0f, 0.0f, false);
    assert(a(effects.active_cel().pixels[checked_pixel_index(effects, 6, 6)]) == 255);
}

static void test_animation_layers() {
    auto doc = Document::create(4, 4);
    doc.add_layer("Ink");
    assert(doc.layers.size() == 2);
    assert(doc.frames[0].cels.size() == 2);
    doc.add_frame(true);
    assert(doc.frames.size() == 2);
    assert(doc.frames[1].cels.size() == 2);
    assert(doc.remove_layer(0));
    assert(doc.layers.size() == 1);
    assert(doc.frames[0].cels.size() == 1);

    auto last_layer = Document::create(7, 5);
    assert(!last_layer.remove_layer(0));
    assert(last_layer.layers.size() == 1);
    assert(last_layer.frames.size() == 1);
    assert(last_layer.frames[0].cels.size() == 1);
    assert(last_layer.active_cel().pixels.size() == 35);

    auto repaired = Document::create(7, 5);
    repaired.layers.clear();
    repaired.frames[0].cels.clear();
    assert(!repaired.has_active_cel());
    assert(repaired.ensure_active_cel());
    assert(repaired.layers.size() == 1);
    assert(repaired.frames.size() == 1);
    assert(repaired.frames[0].cels.size() == 1);
    assert(repaired.active_layer == 0);
    assert(repaired.active_cel().pixels.size() == 35);

    auto timing = Document::create(4, 4);
    timing.clear_history();
    assert(timing.set_frame_duration(0, 460));
    assert(timing.frames[0].duration_ms == 460);
    assert(timing.undo_stack_full_frame_pixel_capacity() == 0);
    assert(timing.undo());
    assert(timing.frames[0].duration_ms == 100);
    assert(timing.redo());
    assert(timing.frames[0].duration_ms == 460);
    assert(timing.set_frame_duration(0, 1));
    assert(timing.frames[0].duration_ms == kMinimumFrameDurationMs);
    assert(!timing.set_frame_duration(-1, 100));
}

static void test_layer_masks_and_clipping() {
    auto doc = Document::create(2, 1);
    doc.active_cel().pixels[0] = rgba(20, 20, 20, 255);
    doc.active_cel().pixels[1] = rgba(20, 20, 20, 0);
    doc.add_layer("Clipped");
    doc.layers[1].mask_enabled = true;
    doc.layers[1].clip_to_below = true;
    doc.layers[1].mask = {255, 255};
    doc.cel(0, 1).pixels[0] = rgba(200, 0, 0, 255);
    doc.cel(0, 1).pixels[1] = rgba(0, 200, 0, 255);

    auto clipped = doc.composite_active();
    assert(r(clipped[0]) == 200);
    assert(g(clipped[1]) == 0);
    assert(a(clipped[1]) == 0);

    doc.layers[1].clip_to_below = false;
    doc.layers[1].mask[0] = 0;
    auto masked = doc.composite_active();
    assert(r(masked[0]) == 20);
    assert(g(masked[1]) == 200);
}

static void test_mask_editing_tools() {
    auto doc = Document::create(5, 3);
    doc.active_cel().pixels[0] = rgba(255, 0, 0, 255);
    doc.active_cel().pixels[static_cast<std::size_t>(doc.pixel_index(1, 0))] = rgba(255, 0, 0, 255);
    doc.selection.select_rect(1, 0, 4, 2, true);

    plot_mask_brush_raw(doc, 0, 0, 0, 1);
    assert(!doc.layers[0].mask_enabled);
    assert(doc.layers[0].mask.empty());
    plot_mask_brush_raw(doc, 1, 0, 0, 1);
    assert(doc.layers[0].mask_enabled);
    assert(doc.layers[0].mask[static_cast<std::size_t>(doc.pixel_index(0, 0))] == 255);
    assert(doc.layers[0].mask[static_cast<std::size_t>(doc.pixel_index(1, 0))] == 0);

    draw_mask_line_raw(doc, 1, 1, 4, 1, 128, 1);
    assert(doc.layers[0].mask[static_cast<std::size_t>(doc.pixel_index(2, 1))] == 128);

    fill_mask_bucket(doc, 2, 1, 255, 0, true);
    assert(doc.layers[0].mask[static_cast<std::size_t>(doc.pixel_index(2, 1))] == 255);
    assert(doc.layers[0].mask[static_cast<std::size_t>(doc.pixel_index(1, 0))] == 0);

    auto composite = doc.composite_active();
    assert(a(composite[static_cast<std::size_t>(doc.pixel_index(1, 0))]) == 0);
}

static void test_v2_tools_and_undo() {
    auto doc = Document::create(12, 12);

    doc.selection.select_polygon({{2, 2}, {9, 2}, {5, 9}}, true);
    assert(doc.selection.active);
    assert(doc.selection.contains(5, 4));
    assert(!doc.selection.contains(1, 1));

    doc.selection.clear();
    doc.active_cel().pixels[checked_pixel_index(doc, 2, 2)] = rgba(240, 10, 10);
    doc.active_cel().pixels[checked_pixel_index(doc, 3, 2)] = rgba(10, 240, 10);
    doc.selection.select_rect(2, 2, 3, 2, true);
    auto before_move = doc.snapshot_active_cel();
    assert(doc.begin_floating_selection());
    assert(a(doc.active_cel().pixels[checked_pixel_index(doc, 2, 2)]) == 0);
    doc.move_floating_selection(4, 3);
    doc.commit_floating_selection("Move Pixels", std::move(before_move));
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 6, 5)]) == 240);
    assert(g(doc.active_cel().pixels[checked_pixel_index(doc, 7, 5)]) == 240);
    assert(doc.undo());
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 2, 2)]) == 240);
    assert(doc.redo());
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 6, 5)]) == 240);

    doc.selection.clear();
    auto text_points = raster_text_points(0, 0, "A");
    assert(!text_points.empty());
    stamp_text(doc, 0, 0, "A", rgba(0, 0, 255));
    assert(b(doc.active_cel().pixels[checked_pixel_index(doc, 2, 0)]) == 255);

    auto source = doc.active_cel().pixels;
    doc.active_cel().pixels[checked_pixel_index(doc, 1, 1)] = rgba(22, 33, 44);
    source = doc.active_cel().pixels;
    clone_stamp_raw(doc, source, 1, 1, 9, 9, 1);
    assert(r(doc.active_cel().pixels[checked_pixel_index(doc, 9, 9)]) == 22);
}

static void test_v2_layers_frames_tags() {
    auto doc = Document::create(4, 4);
    doc.add_layer("Ink");
    doc.active_cel().pixels[0] = rgba(10, 0, 0);
    doc.duplicate_layer(1);
    assert(doc.layers.size() == 3);
    doc.move_layer(2, 1);
    assert(doc.active_layer == 1);
    assert(doc.merge_layer_down(1));
    assert(doc.layers.size() == 2);
    assert(doc.undo());
    assert(doc.layers.size() == 3);
    assert(doc.redo());
    assert(doc.layers.size() == 2);

    doc.add_frame(false);
    doc.duplicate_frame(1);
    assert(doc.frames.size() == 3);
    doc.move_frame(2, 0);
    assert(doc.active_frame == 0);
    doc.add_tag("Walk", 0, 2);
    doc.playback_mode = PlaybackMode::PingPong;
    assert(doc.tags.size() == 1);
    assert(doc.tags[0].name == "Walk");
    assert(doc.remove_tag(0));
    assert(doc.tags.empty());
}

static void test_gpu_chunking_policy() {
    const GpuEffectMode chunk_safe[] = {
        GpuEffectMode::BrightnessContrast,
        GpuEffectMode::Hsv,
        GpuEffectMode::Temperature,
        GpuEffectMode::Levels,
        GpuEffectMode::TonalRange,
        GpuEffectMode::Curves,
        GpuEffectMode::PaletteQuantize,
        GpuEffectMode::PaletteDither,
        GpuEffectMode::AutoLevel,
        GpuEffectMode::Grayscale,
        GpuEffectMode::Sepia,
        GpuEffectMode::InvertColors,
        GpuEffectMode::InvertAlpha,
        GpuEffectMode::Posterize,
        GpuEffectMode::OilPainting,
        GpuEffectMode::InkSketch,
        GpuEffectMode::PencilSketch,
        GpuEffectMode::GaussianBlur,
        GpuEffectMode::MotionBlur,
        GpuEffectMode::MedianBlur,
        GpuEffectMode::SurfaceBlur,
        GpuEffectMode::ReduceNoise,
        GpuEffectMode::Glow,
        GpuEffectMode::RedEyeRemoval,
        GpuEffectMode::Sharpen,
        GpuEffectMode::SoftenPortrait,
        GpuEffectMode::DepthOfField,
        GpuEffectMode::EdgeDetect,
        GpuEffectMode::Emboss,
        GpuEffectMode::Outline,
        GpuEffectMode::Relief
    };
    const GpuEffectMode full_image_only[] = {
        GpuEffectMode::RadialBlur,
        GpuEffectMode::ZoomBlur,
        GpuEffectMode::Pixelate,
        GpuEffectMode::Crystalize,
        GpuEffectMode::FrostedGlass,
        GpuEffectMode::Bulge,
        GpuEffectMode::Twist,
        GpuEffectMode::TileReflection,
        GpuEffectMode::Dents,
        GpuEffectMode::PolarInversion,
        GpuEffectMode::AddNoise,
        GpuEffectMode::Vignette,
        GpuEffectMode::Clouds,
        GpuEffectMode::JuliaFractal,
        GpuEffectMode::MandelbrotFractal,
        GpuEffectMode::Turbulence,
        GpuEffectMode::AffineTransform
    };

    bool covered[48] = {};
    for (GpuEffectMode mode : chunk_safe) {
        GpuEffectRequest request;
        request.mode = mode;
        assert(GpuEffectRenderer::effect_supports_chunking(request));
        covered[static_cast<int>(mode)] = true;
    }
    for (GpuEffectMode mode : full_image_only) {
        GpuEffectRequest request;
        request.mode = mode;
        assert(!GpuEffectRenderer::effect_supports_chunking(request));
        covered[static_cast<int>(mode)] = true;
    }
    for (bool mode_covered : covered) {
        assert(mode_covered);
    }

    GpuEffectRequest blur;
    blur.mode = GpuEffectMode::GaussianBlur;
    blur.params = {12.0f, 0.0f, 0.0f, 0.0f};
    assert(GpuEffectRenderer::effect_chunk_halo(blur) == 12);

    GpuEffectRequest depth_of_field;
    depth_of_field.mode = GpuEffectMode::DepthOfField;
    depth_of_field.params = {128.0f / 255.0f, 1.0f, 32.0f, 9.0f};
    assert(GpuEffectRenderer::effect_chunk_halo(depth_of_field) == 9);

    GpuEffectRequest motion;
    motion.mode = GpuEffectMode::MotionBlur;
    motion.params = {64.0f, 0.0f, 0.0f, 0.0f};
    assert(GpuEffectRenderer::effect_chunk_halo(motion) == 24);

    GpuEffectRequest twist;
    twist.mode = GpuEffectMode::Twist;
    assert(GpuEffectRenderer::effect_chunk_halo(twist) == 0);

    GpuBackendCapabilities older_gpu;
    older_gpu.max_texture_size = 4096;
    older_gpu.working_texture_budget = 64ULL * 1024ULL * 1024ULL;
    older_gpu.supports_chunking = true;
    const int older_extent = GpuEffectRenderer::choose_chunk_extent(9921, 14031, 12, older_gpu);
    assert(older_extent > 0);
    assert(older_extent <= older_gpu.max_texture_size - 24);

    GpuBackendCapabilities modern_gpu;
    modern_gpu.max_texture_size = 16384;
    modern_gpu.working_texture_budget = 512ULL * 1024ULL * 1024ULL;
    modern_gpu.supports_chunking = true;
    const int modern_extent = GpuEffectRenderer::choose_chunk_extent(9921, 14031, 12, modern_gpu);
    assert(modern_extent >= older_extent);
    assert(modern_extent <= modern_gpu.max_texture_size - 24);
}

static void test_depth_map_chunking_and_normalization() {
    const std::vector<DepthTile> tiles = build_depth_tiles(2500, 1300, 1024, 128);
    assert(tiles.size() == 6);
    std::vector<bool> covered(static_cast<std::size_t>(2500 * 1300), false);
    for (const DepthTile& tile : tiles) {
        assert(tile.width > 0);
        assert(tile.height > 0);
        assert(tile.expanded_x <= tile.x);
        assert(tile.expanded_y <= tile.y);
        assert(tile.expanded_x + tile.expanded_width >= tile.x + tile.width);
        assert(tile.expanded_y + tile.expanded_height >= tile.y + tile.height);
        for (int y = tile.y; y < tile.y + tile.height; ++y) {
            for (int x = tile.x; x < tile.x + tile.width; ++x) {
                covered[static_cast<std::size_t>(y * 2500 + x)] = true;
            }
        }
    }
    assert(std::all_of(covered.begin(), covered.end(), [](bool value) { return value; }));

    std::vector<float> depth = {0.0f, 0.25f, 0.5f, 1.0f};
    std::vector<Pixel> source = {
        rgba(10, 10, 10, 255),
        rgba(20, 20, 20, 255),
        rgba(30, 30, 30, 255),
        rgba(40, 40, 40, 0)
    };
    std::vector<Pixel> pixels = normalize_depth_to_pixels(depth, source, 2, 2);
    assert(pixels.size() == 4);
    assert(r(pixels[0]) == 0);
    assert(r(pixels[1]) > r(pixels[0]));
    assert(r(pixels[2]) > r(pixels[1]));
    assert(r(pixels[2]) == 255);
    assert(r(pixels[3]) == 0);
    assert(a(pixels[3]) == 255);
    assert(depth_pixels_have_full_range(pixels));

    std::vector<float> flat_depth = {0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<Pixel> flat_pixels = normalize_depth_to_pixels(flat_depth, source, 2, 2);
    assert(!depth_pixels_have_full_range(flat_pixels));
}

static void test_depth_map_stitching_aligns_overlapped_tiles() {
    constexpr int width = 8;
    constexpr int height = 4;
    auto expected = [](int x, int y) {
        return static_cast<float>(x) + static_cast<float>(y) * 0.25f;
    };

    DepthTile left;
    left.x = 0;
    left.y = 0;
    left.width = 4;
    left.height = height;
    left.expanded_x = 0;
    left.expanded_y = 0;
    left.expanded_width = 5;
    left.expanded_height = height;

    DepthTile right;
    right.x = 4;
    right.y = 0;
    right.width = 4;
    right.height = height;
    right.expanded_x = 3;
    right.expanded_y = 0;
    right.expanded_width = 5;
    right.expanded_height = height;

    DepthTilePrediction left_prediction;
    left_prediction.tile = left;
    left_prediction.depth.resize(static_cast<std::size_t>(left.expanded_width * left.expanded_height));
    for (int y = 0; y < left.expanded_height; ++y) {
        for (int x = 0; x < left.expanded_width; ++x) {
            left_prediction.depth[static_cast<std::size_t>(y * left.expanded_width + x)] =
                expected(left.expanded_x + x, left.expanded_y + y);
        }
    }

    DepthTilePrediction right_prediction;
    right_prediction.tile = right;
    right_prediction.depth.resize(static_cast<std::size_t>(right.expanded_width * right.expanded_height));
    for (int y = 0; y < right.expanded_height; ++y) {
        for (int x = 0; x < right.expanded_width; ++x) {
            const float true_depth = expected(right.expanded_x + x, right.expanded_y + y);
            right_prediction.depth[static_cast<std::size_t>(y * right.expanded_width + x)] = true_depth * 2.0f + 7.0f;
        }
    }

    const std::vector<float> stitched = stitch_depth_tiles({left_prediction, right_prediction}, width, height);
    assert(stitched.size() == static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float actual = stitched[static_cast<std::size_t>(y * width + x)];
            assert(std::abs(actual - expected(x, y)) < 0.05f);
        }
    }
}

static void test_depth_map_layer_insert_is_undoable() {
    auto doc = Document::create(3, 2);
    doc.layers[0].name = "Photo";
    std::vector<Pixel> depth(static_cast<std::size_t>(doc.width * doc.height), rgba(128, 128, 128, 255));
    doc.insert_layer(1, "Photo Depth Map", depth, "Generate Depth Map");
    assert(doc.layers.size() == 2);
    assert(doc.active_layer == 1);
    assert(doc.layers[1].name == "Photo Depth Map");
    assert(r(doc.active_cel().pixels[0]) == 128);
    assert(doc.undo());
    assert(doc.layers.size() == 1);
    assert(doc.layers[0].name == "Photo");
    assert(doc.redo());
    assert(doc.layers.size() == 2);
    assert(r(doc.active_cel().pixels[0]) == 128);
}

static void test_depth_of_field_filter() {
    auto doc = Document::create(5, 1);
    for (int x = 0; x < doc.width; ++x) {
        doc.active_cel().pixels[static_cast<std::size_t>(x)] =
            rgba(static_cast<std::uint8_t>(x * 50), 0, 0, static_cast<std::uint8_t>(220 - x * 10));
    }
    const std::vector<Pixel> before = doc.active_cel().pixels;
    std::vector<Pixel> depth = {
        rgba(0, 0, 0, 255),
        rgba(64, 64, 64, 255),
        rgba(128, 128, 128, 255),
        rgba(192, 192, 192, 255),
        rgba(255, 255, 255, 255)
    };
    DepthOfFieldSettings settings;
    settings.focus_depth = 128;
    settings.aperture = 100;
    settings.falloff = 32;
    settings.max_radius = 2;
    apply_depth_of_field(doc, depth, settings);
    const auto& pixels = doc.active_cel().pixels;
    assert(pixels[2] == before[2]);
    assert(pixels[0] != before[0]);
    assert(a(pixels[0]) < a(before[0]));
    assert(a(pixels[0]) > 0);

    auto selected = Document::create(3, 1);
    selected.active_cel().pixels[0] = rgba(0, 0, 0, 255);
    selected.active_cel().pixels[1] = rgba(128, 0, 0, 255);
    selected.active_cel().pixels[2] = rgba(255, 0, 0, 255);
    std::vector<Pixel> selected_depth = {
        rgba(0, 0, 0, 255),
        rgba(0, 0, 0, 255),
        rgba(0, 0, 0, 255)
    };
    selected.selection.resize(3, 1);
    selected.selection.active = true;
    selected.selection.mask = {0, 255, 0};
    apply_depth_of_field(selected, selected_depth, settings);
    assert(r(selected.active_cel().pixels[0]) == 0);
    assert(r(selected.active_cel().pixels[1]) != 128);
    assert(r(selected.active_cel().pixels[2]) == 255);

    auto mismatch = Document::create(2, 1);
    const auto mismatch_before = mismatch.active_cel().pixels;
    apply_depth_of_field(mismatch, {}, settings);
    assert(mismatch.active_cel().pixels == mismatch_before);
}

static Pixel composite_blend_sample(LayerBlendMode mode) {
    auto doc = Document::create(1, 1);
    doc.active_cel().pixels[0] = rgba(80, 120, 160, 255);
    doc.add_layer("Blend");
    doc.frames[0].cels[1].pixels[0] = rgba(40, 200, 90, 255);
    doc.layers[1].blend_mode = mode;
    return doc.composite_active()[0];
}

static void test_layer_blend_modes() {
    assert(std::string(layer_blend_mode_name(LayerBlendMode::Additive)) == "Additive");
    assert(std::string(layer_blend_mode_name(LayerBlendMode::ColorBurn)) == "Color Burn");
    assert(std::string(layer_blend_mode_name(LayerBlendMode::ColorDodge)) == "Color Dodge");
    assert(std::string(layer_blend_mode_name(LayerBlendMode::Xor)) == "Xor");

    Pixel normal = composite_blend_sample(LayerBlendMode::Normal);
    assert(r(normal) == 40);
    assert(g(normal) == 200);
    assert(b(normal) == 90);

    Pixel multiply = composite_blend_sample(LayerBlendMode::Multiply);
    assert(r(multiply) == (80 * 40) / 255);
    assert(g(multiply) == (120 * 200) / 255);
    assert(b(multiply) == (160 * 90) / 255);

    Pixel additive = composite_blend_sample(LayerBlendMode::Additive);
    assert(r(additive) == 120);
    assert(g(additive) == 255);
    assert(b(additive) == 250);

    Pixel difference = composite_blend_sample(LayerBlendMode::Difference);
    assert(r(difference) == 40);
    assert(g(difference) == 80);
    assert(b(difference) == 70);

    Pixel negation = composite_blend_sample(LayerBlendMode::Negation);
    assert(r(negation) == 120);
    assert(g(negation) == 190);
    assert(b(negation) == 250);

    Pixel lighten = composite_blend_sample(LayerBlendMode::Lighten);
    assert(r(lighten) == 80);
    assert(g(lighten) == 200);
    assert(b(lighten) == 160);

    Pixel darken = composite_blend_sample(LayerBlendMode::Darken);
    assert(r(darken) == 40);
    assert(g(darken) == 120);
    assert(b(darken) == 90);

    Pixel screen = composite_blend_sample(LayerBlendMode::Screen);
    assert(r(screen) == 108);
    assert(g(screen) == 226);
    assert(b(screen) == 194);

    Pixel xored = composite_blend_sample(LayerBlendMode::Xor);
    assert(r(xored) == (80 ^ 40));
    assert(g(xored) == (120 ^ 200));
    assert(b(xored) == (160 ^ 90));

    const LayerBlendMode modes[] = {LayerBlendMode::ColorBurn, LayerBlendMode::ColorDodge, LayerBlendMode::Reflect,
                                    LayerBlendMode::Glow, LayerBlendMode::Overlay};
    for (LayerBlendMode mode : modes) {
        assert(a(composite_blend_sample(mode)) == 255);
    }
}

static void test_model_json() {
    auto model = ModelDocument::create_default();
    model.add_cuboid();
    model.cuboids[0].uv[0] = {-10, 2, 999, 3};
    clamp_model_uvs(model);
    assert(uv_rect_valid(model.cuboids[0].uv[0], model.texture_width, model.texture_height));
    std::string json = model_to_json(model);
    ModelDocument decoded;
    std::string error;
    assert(model_from_json(json, decoded, &error));
    assert(decoded.cuboids.size() == model.cuboids.size());
    assert(decoded.texture_width == model.texture_width);

    Renderer3D renderer;
    FaceHit hit = renderer.pick_face(decoded, ModelViewportState{}, 128, 128, 64.0f, 64.0f);
    assert(hit.hit);
    float projected_x = 0.0f;
    float projected_y = 0.0f;
    assert(renderer.project_first_cuboid_corner(decoded, ModelViewportState{}, 128, 128, projected_x, projected_y));
    assert(renderer.project_world_point(decoded, ModelViewportState{}, 128, 128, 0.0f, 0.0f, 0.0f, projected_x, projected_y));

    assert(model.remove_selected());
    assert(model.remove_selected());
    assert(model.cuboids.empty());
    clamp_model_uvs(model);
    assert(model.cuboids.empty());
    assert(model.selected_cuboid == -1);
    std::string empty_json = model_to_json(model);
    ModelDocument empty_decoded;
    assert(model_from_json(empty_json, empty_decoded, &error));
    assert(empty_decoded.cuboids.empty());
    FaceHit empty_hit = renderer.pick_face(empty_decoded, ModelViewportState{}, 128, 128, 64.0f, 64.0f);
    assert(!empty_hit.hit);
}

static void test_model_transform_helpers() {
    Cuboid cuboid;
    cuboid.from = {1.0f, 2.0f, 3.0f};
    cuboid.to = {5.0f, 8.0f, 13.0f};
    cuboid.rotation_origin = {3.0f, 5.0f, 8.0f};

    assert(std::abs(cuboid_axis_size(cuboid, 0) - 4.0f) < 0.001f);
    translate_cuboid(cuboid, 0, 5.9f, true);
    assert(std::abs(cuboid.from[0] - 5.0f) < 0.001f);
    assert(std::abs(cuboid.to[0] - 9.0f) < 0.001f);
    assert(std::abs(cuboid.rotation_origin[0] - 7.0f) < 0.001f);

    scale_cuboid(cuboid, 1, 1.25f, true);
    assert(std::abs(cuboid_axis_size(cuboid, 1) - 12.0f) < 0.001f);
    assert(std::abs(cuboid.rotation_origin[1] - 5.0f) < 0.001f);

    rotate_cuboid(cuboid, 2, 67.0f, true);
    assert(cuboid.rotation_axis == 2);
    assert(std::abs(cuboid.rotation_angle - 60.0f) < 0.001f);
}

static void test_mesh_uv_unwrap() {
    ModelDocument model;
    model.cuboids.clear();
    MeshObject mesh;
    mesh.name = "Unwrap Test";
    auto add_triangle = [&](std::array<float, 3> a,
                            std::array<float, 3> b,
                            std::array<float, 3> c) {
        const int base = static_cast<int>(mesh.vertices.size());
        MeshVertex va;
        va.position = a;
        MeshVertex vb;
        vb.position = b;
        MeshVertex vc;
        vc.position = c;
        mesh.vertices.push_back(va);
        mesh.vertices.push_back(vb);
        mesh.vertices.push_back(vc);
        MeshTriangle triangle;
        triangle.indices = {base, base + 1, base + 2};
        mesh.triangles.push_back(triangle);
    };
    add_triangle({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    add_triangle({1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    add_triangle({4.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 0.0f});
    model.meshes.push_back(std::move(mesh));

    const MeshUvUnwrapResult result = unwrap_model_mesh_uvs(model, 16, 16);
    assert(result.changed);
    assert(result.mesh_count == 1);
    assert(result.triangle_count == 3);
    assert(result.island_count == 2);
    assert(result.recommended_width >= 16);
    assert(result.recommended_height >= 16);
    assert(model.meshes.size() == 1U);
    assert(model.meshes[0].vertices.size() == 9U);
    for (const MeshVertex& vertex : model.meshes[0].vertices) {
        assert(vertex.uv[0] >= 0.0f && vertex.uv[0] <= 1.0f);
        assert(vertex.uv[1] >= 0.0f && vertex.uv[1] <= 1.0f);
    }
}

static void test_app_settings_roundtrip() {
    const auto settings_path = std::filesystem::temp_directory_path() / "pixelart98_settings_test.json";
    std::string error;
    AppSettings settings;
    settings.show_splash_screen = false;
    settings.auto_open_error_console = false;
    settings.heavy_gpu_optimization = false;
    settings.mps_backend = true;
    settings.depth_allow_cpu_fallback = false;
    settings.depth_tile_size = 768;
    settings.depth_tile_overlap = 96;
    settings.show_tools_panel = false;
    settings.show_colors_panel = false;
    settings.show_layers_panel = false;
    settings.show_adjustments_panel = false;
    settings.show_animation_panel = true;
    settings.show_history_panel = true;
    settings.show_model_uv_panel = true;
    settings.show_3d_preview = true;
    settings.show_canvas_cuboid_uv_overlay = true;
    assert(save_app_settings(settings, settings_path, &error));

    const AppSettings loaded = load_app_settings(settings_path, &error);
    assert(!loaded.show_splash_screen);
    assert(!loaded.auto_open_error_console);
    assert(!loaded.heavy_gpu_optimization);
    assert(!loaded.mps_backend);
    assert(!loaded.depth_allow_cpu_fallback);
    assert(loaded.depth_tile_size == 768);
    assert(loaded.depth_tile_overlap == 96);
    assert(!loaded.show_tools_panel);
    assert(!loaded.show_colors_panel);
    assert(!loaded.show_layers_panel);
    assert(!loaded.show_adjustments_panel);
    assert(loaded.show_animation_panel);
    assert(loaded.show_history_panel);
    assert(loaded.show_model_uv_panel);
    assert(loaded.show_3d_preview);
    assert(loaded.show_canvas_cuboid_uv_overlay);
    const AppSettings defaults = load_app_settings(settings_path.parent_path() / "pixelart98_missing_settings_test.json", &error);
    assert(defaults.heavy_gpu_optimization);
    assert(!defaults.mps_backend);
    assert(defaults.depth_allow_cpu_fallback);
    assert(defaults.depth_tile_size == 1024);
    assert(defaults.depth_tile_overlap == 128);
    assert(defaults.show_tools_panel);
    assert(defaults.show_colors_panel);
    assert(defaults.show_layers_panel);
    assert(defaults.show_adjustments_panel);
    assert(!defaults.show_animation_panel);
    assert(!defaults.show_history_panel);
    assert(!defaults.show_model_uv_panel);
    assert(!defaults.show_3d_preview);
    assert(!defaults.show_canvas_cuboid_uv_overlay);
    settings.heavy_gpu_optimization = true;
    settings.mps_backend = true;
    assert(save_app_settings(settings, settings_path, &error));
    const AppSettings loaded_mps = load_app_settings(settings_path, &error);
    assert(loaded_mps.heavy_gpu_optimization);
    assert(loaded_mps.mps_backend);
    std::filesystem::remove(settings_path);
}

static void test_project_io() {
    auto doc = Document::create(8, 8);
    fill_bucket(doc, 0, 0, rgba(10, 20, 30), 255, false);
    doc.layers[0].blend_mode = LayerBlendMode::Add;
    doc.add_frame(true);
    doc.frames[1].duration_ms = 250;
    doc.add_tag("Blink", 0, 1);
    doc.playback_mode = PlaybackMode::PingPong;
    auto model = ModelDocument::create_default();

    auto base = std::filesystem::temp_directory_path() / "pixelart98_test.pixart";
    std::string error;
    assert(save_project(base.string(), doc, model, &error));
    ProjectBundle bundle;
    assert(load_project(base.string(), bundle, &error));
    assert(bundle.document.width == 8);
    assert(bundle.document.frames.size() == 2);
    assert(r(bundle.document.frames[0].cels[0].pixels[0]) == 10);
    assert(bundle.document.layers[0].blend_mode == LayerBlendMode::Add);
    assert(bundle.document.tags.size() == 1);
    assert(bundle.document.playback_mode == PlaybackMode::PingPong);

    auto png = std::filesystem::temp_directory_path() / "pixelart98_test.png";
    auto sheet = std::filesystem::temp_directory_path() / "pixelart98_sheet.png";
    auto sheet_json = std::filesystem::temp_directory_path() / "pixelart98_sheet.json";
    auto apng = std::filesystem::temp_directory_path() / "pixelart98_anim.png";
    auto ase = std::filesystem::temp_directory_path() / "pixelart98_sprite.aseprite";
    auto mc = std::filesystem::temp_directory_path() / "pixelart98_minecraft.json";
    auto mc_rotated = std::filesystem::temp_directory_path() / "pixelart98_rotated_minecraft.json";
    assert(export_png(png.string(), bundle.document, 0, &error));
    assert(export_spritesheet(sheet.string(), sheet_json.string(), bundle.document, &error));
    assert(export_apng(apng.string(), bundle.document, &error));
    Document imported_png;
    assert(import_image(png.string(), imported_png, &error));
    assert(imported_png.width == 8);
    assert(r(imported_png.active_cel().pixels[0]) == 10);
    assert(import_image_as_layer(png.string(), imported_png, "Imported", &error));
    assert(imported_png.layers.size() == 2);
    assert(export_aseprite(ase.string(), bundle.document, &error));
    Document imported_ase;
    assert(import_aseprite(ase.string(), imported_ase, &error));
    assert(imported_ase.frames.size() == 2);
    assert(imported_ase.layers.size() == 1);
    assert(r(imported_ase.frames[0].cels[0].pixels[0]) == 10);
    assert(export_minecraft_model(mc.string(), model, "texture.png", &error));
    ModelDocument imported_model;
    assert(import_minecraft_model(mc.string(), imported_model, &error));
    assert(imported_model.cuboids.size() == model.cuboids.size());
    {
        std::ofstream file(mc_rotated);
        file << R"JSON({
          "texture_size": [32, 32],
          "textures": {"0": "panel"},
          "elements": [{
            "name": "panel",
            "from": [1, 9, 0],
            "to": [17, 10, 16],
            "rotation": {"angle": 22.5, "axis": "z", "origin": [1, 9, 0]},
            "faces": {
              "north": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "south": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "east": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "west": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "up": {"uv": [0, 0, 16, 16], "texture": "#0"},
              "down": {"uv": [0, 0, 16, 16], "texture": "#0"}
            }
          }]
        })JSON";
    }
    ModelDocument rotated_model;
    assert(import_minecraft_model(mc_rotated.string(), rotated_model, &error));
    assert(rotated_model.texture_width == 32);
    assert(rotated_model.texture_height == 32);
    assert(rotated_model.cuboids.size() == 1);
    assert(std::abs(rotated_model.cuboids[0].rotation_angle - 22.5f) < 0.001f);
    assert(rotated_model.cuboids[0].rotation_axis == 2);
    assert(rotated_model.cuboids[0].rotation_origin[0] == 1.0f);
    assert(rotated_model.cuboids[0].rotation_origin[1] == 9.0f);
    assert(rotated_model.cuboids[0].rotation_origin[2] == 0.0f);
    std::filesystem::remove(base);
    std::filesystem::remove(png);
    std::filesystem::remove(sheet);
    std::filesystem::remove(sheet_json);
    std::filesystem::remove(apng);
    std::filesystem::remove(ase);
    std::filesystem::remove(mc);
    std::filesystem::remove(mc_rotated);
}

int main() {
    test_canvas_and_undo();
    test_fill_and_selection();
    test_selection_combine_modes_and_nudge();
    test_thin_ellipse_strokes_are_continuous();
    test_constrained_tool_endpoints();
    test_selection_clips_drawing_and_delete();
    test_filters();
    test_animation_layers();
    test_layer_masks_and_clipping();
    test_mask_editing_tools();
    test_v2_tools_and_undo();
    test_v2_layers_frames_tags();
    test_gpu_chunking_policy();
    test_depth_map_chunking_and_normalization();
    test_depth_map_stitching_aligns_overlapped_tiles();
    test_depth_map_layer_insert_is_undoable();
    test_depth_of_field_filter();
    test_layer_blend_modes();
    test_model_json();
    test_model_transform_helpers();
    test_mesh_uv_unwrap();
    test_app_settings_roundtrip();
    test_project_io();
    std::cout << "pixelart core tests passed\n";
    return 0;
}
