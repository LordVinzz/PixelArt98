// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "core/DocumentTransforms.hpp"
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

static void test_full_document_transforms_and_history() {
    Document doc = Document::create(3, 2);
    doc.active_cel().pixels[checked_pixel_index(doc, 0, 0)] = rgba(255, 0, 0, 255);
    doc.active_cel().pixels[checked_pixel_index(doc, 2, 1)] = rgba(0, 255, 0, 255);
    doc.layers[0].mask_enabled = true;
    doc.layers[0].mask = {0, 255, 255, 255, 255, 128};
    doc.selection.select_rect(2, 1, 2, 1, true);
    doc.add_frame(true);
    doc.clear_history();
    doc.frames[1].cels[0].pixels[checked_pixel_index(doc, 1, 0)] = rgba(0, 0, 255, 255);
    ModelDocument model = ModelDocument::create_default();
    model.texture_width = doc.width;
    model.texture_height = doc.height;

    assert(rotate_document_90_clockwise(doc, &model));
    assert(doc.width == 2 && doc.height == 3);
    assert(model.texture_width == 2 && model.texture_height == 3);
    assert(r(doc.frames[0].cels[0].pixels[static_cast<std::size_t>(doc.pixel_index(1, 0))]) == 255);
    assert(g(doc.frames[0].cels[0].pixels[static_cast<std::size_t>(doc.pixel_index(0, 2))]) == 255);
    assert(b(doc.frames[1].cels[0].pixels[static_cast<std::size_t>(doc.pixel_index(1, 1))]) == 255);
    assert(doc.layers[0].mask.size() == 6);
    assert(doc.selection.contains(0, 2));

    assert(doc.undo(&model));
    assert(doc.width == 3 && doc.height == 2);
    assert(model.texture_width == 3 && model.texture_height == 2);
    assert(r(doc.frames[0].cels[0].pixels[checked_pixel_index(doc, 0, 0)]) == 255);
    assert(doc.layers[0].mask[0] == 0);
    assert(doc.selection.contains(2, 1));
    assert(doc.redo(&model));
    assert(doc.width == 2 && doc.height == 3);

    assert(crop_document(doc, 0, 1, 2, 2, &model));
    assert(doc.width == 2 && doc.height == 2);
    assert(doc.undo(&model));
    assert(doc.width == 2 && doc.height == 3);

    assert(resize_document_canvas(doc, 4, 5, 1, 1, &model));
    assert(doc.width == 4 && doc.height == 5);
    assert(doc.undo(&model));
    assert(doc.width == 2 && doc.height == 3);

    assert(resize_document_image(doc, 4, 6, ResamplingMode::Nearest, &model));
    assert(doc.width == 4 && doc.height == 6);
    assert(doc.frames[0].cels[0].pixels.size() == 24);
    assert(doc.undo(&model));
    assert(doc.width == 2 && doc.height == 3);
}

static void test_layer_properties_and_masks_are_undoable() {
    Document doc = Document::create(4, 4);
    assert(doc.set_layer_name(0, "Ink"));
    assert(doc.layers[0].name == "Ink");
    assert(doc.undo());
    assert(doc.layers[0].name == "Background");
    assert(doc.redo());
    assert(doc.layers[0].name == "Ink");

    assert(doc.set_layer_visible(0, false));
    assert(doc.undo());
    assert(doc.layers[0].visible);
    assert(doc.set_layer_opacity(0, 0.25f));
    assert(doc.undo());
    assert(std::abs(doc.layers[0].opacity - 1.0f) < 0.001f);
    assert(doc.set_layer_blend_mode(0, LayerBlendMode::Multiply));
    assert(doc.undo());
    assert(doc.layers[0].blend_mode == LayerBlendMode::Normal);
    assert(doc.set_layer_clipped(0, true));
    assert(doc.undo());
    assert(!doc.layers[0].clip_to_below);

    std::vector<std::uint8_t> mask(16, 0);
    mask[5] = 255;
    assert(doc.set_layer_mask(0, mask, true, "Mask from Selection"));
    assert(doc.layers[0].mask_enabled && doc.layers[0].mask[5] == 255);
    assert(doc.undo());
    assert(!doc.layers[0].mask_enabled && doc.layers[0].mask.empty());
    assert(doc.redo());
    assert(doc.layers[0].mask_enabled && doc.layers[0].mask[5] == 255);
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

#include "core_advanced_tests.inc"
int main() {
    test_full_document_transforms_and_history();
    test_layer_properties_and_masks_are_undoable();
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
