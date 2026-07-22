// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"

// Keep unit-test assertions active even when the project is configured as Release.
#undef NDEBUG
#include <cassert>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace px;

namespace {

std::size_t index_of(const Document& document, int x, int y) {
    return static_cast<std::size_t>(document.pixel_index(x, y));
}

Pixel pixel_at(const Document& document, int x, int y) {
    return document.active_cel().pixels[index_of(document, x, y)];
}

void set_pixel(Document& document, int x, int y, Pixel pixel) {
    document.active_cel().pixels[index_of(document, x, y)] = pixel;
}

int histogram_total(const std::array<int, 256>& histogram) {
    return std::accumulate(histogram.begin(), histogram.end(), 0);
}

void test_selection_mask_rectangles_and_bounds() {
    SelectionMask selection;
    selection.resize(6, 5);
    assert(selection.width == 6);
    assert(selection.height == 5);
    assert(!selection.active);
    assert(selection.selected_count() == 0);
    assert(!selection.bounds().has_value());
    // An inactive selection intentionally means that every in-canvas pixel is usable.
    assert(selection.contains(2, 2));

    selection.select_rect(4, 3, 2, 1, SelectionCombineMode::Replace);
    assert(selection.active);
    assert(selection.selected_count() == 9);
    assert(selection.contains(2, 1));
    assert(selection.contains(4, 3));
    assert(!selection.contains(1, 1));
    assert(!selection.contains(-1, 1));
    assert(!selection.contains(6, 1));
    assert((selection.bounds() == std::optional<std::array<int, 4>>{{2, 1, 4, 3}}));

    selection.select_rect(-4, -3, 1, 1, SelectionCombineMode::Replace);
    assert(selection.selected_count() == 4);
    assert((selection.bounds() == std::optional<std::array<int, 4>>{{0, 0, 1, 1}}));

    SelectionMask empty;
    empty.resize(0, 0);
    empty.select_rect(0, 0, 4, 4, SelectionCombineMode::Replace);
    assert(!empty.active);
    assert(empty.mask.empty());
}

void test_selection_mask_fully_outside_rectangles() {
    SelectionMask selection;
    selection.resize(6, 5);
    // A rectangle fully outside the canvas must remain empty instead of clamping
    // both endpoints onto a border pixel.
    selection.select_rect(-8, -7, -2, -1, SelectionCombineMode::Replace);
    assert(!selection.active);
    assert(selection.selected_count() == 0);
    selection.select_rect(6, 5, 20, 12, SelectionCombineMode::Replace);
    assert(!selection.active);
    assert(selection.selected_count() == 0);
    selection.select_rect(-10, 5, -1, 9, SelectionCombineMode::Replace);
    assert(!selection.active);
    assert(selection.selected_count() == 0);

    selection.select_rect(2, 2, 3, 3, SelectionCombineMode::Replace);
    const auto center = selection.mask;
    selection.select_rect(20, 20, 30, 30, SelectionCombineMode::Add);
    assert(selection.mask == center);
    selection.select_rect(-30, -30, -20, -20, SelectionCombineMode::Subtract);
    assert(selection.mask == center);
    selection.select_rect(20, 20, 30, 30, SelectionCombineMode::Invert);
    assert(selection.mask == center);
    selection.select_rect(20, 20, 30, 30, SelectionCombineMode::Intersect);
    assert(!selection.active);
    assert(selection.selected_count() == 0);
}

void test_selection_mask_combine_polygon_invert_and_translate() {
    SelectionMask selection;
    selection.resize(7, 7);
    selection.select_rect(1, 1, 3, 3, SelectionCombineMode::Replace);
    selection.select_rect(5, 5, 6, 6, SelectionCombineMode::Add);
    assert(selection.selected_count() == 13);

    selection.select_rect(2, 2, 5, 5, SelectionCombineMode::Subtract);
    assert(selection.contains(1, 1));
    assert(!selection.contains(2, 2));
    assert(!selection.contains(5, 5));
    assert(selection.contains(6, 6));

    selection.select_rect(0, 0, 2, 2, SelectionCombineMode::Intersect);
    assert(selection.selected_count() == 3);
    assert(selection.contains(1, 1));
    assert(!selection.contains(3, 1));

    selection.select_rect(1, 1, 2, 2, SelectionCombineMode::Invert);
    assert(!selection.contains(1, 1));
    assert(selection.contains(2, 2));

    const auto before_bad_combine = selection.mask;
    selection.combine_with_mask({1, 0, 1}, SelectionCombineMode::Replace);
    assert(selection.mask == before_bad_combine);

    selection.invert();
    assert(selection.active);
    assert(selection.selected_count() == 49 - 1);
    selection.invert();
    assert(selection.selected_count() == 1);

    selection.translate(2, 1);
    assert(selection.contains(4, 3));
    assert(!selection.contains(0, 0));
    const auto translated = selection.mask;
    selection.translate(0, 0);
    assert(selection.mask == translated);
    selection.translate(20, 20);
    assert(!selection.active);
    assert(selection.selected_count() == 0);

    selection.select_polygon({{1, 1}, {6, 1}, {1, 6}}, SelectionCombineMode::Replace);
    assert(selection.active);
    assert(selection.contains(2, 2));
    assert(!selection.contains(5, 5));
    const int triangle_size = selection.selected_count();
    assert(triangle_size > 0);
    selection.select_polygon({{0, 0}, {2, 0}}, SelectionCombineMode::Add);
    assert(selection.selected_count() == triangle_size);

    selection.select_all();
    assert(selection.active);
    assert(selection.selected_count() == 49);
    selection.clear();
    assert(!selection.active);
    assert(selection.selected_count() == 0);
}

void test_document_creation_validation_and_cel_repair() {
    Document clamped = Document::create(-20, 0);
    assert(clamped.width == 1);
    assert(clamped.height == 1);
    assert(clamped.valid());
    assert(clamped.has_active_cel());
    assert(clamped.active_cel().pixels.size() == 1);
    assert(clamped.layers.front().name == "Background");
    assert(clamped.palette.colors == default_palette());
    assert(clamped.in_bounds(0, 0));
    assert(!clamped.in_bounds(-1, 0));
    assert(!clamped.in_bounds(1, 0));

    Document repaired;
    repaired.width = 3;
    repaired.height = 2;
    repaired.active_frame = 9;
    repaired.active_layer = 7;
    assert(!repaired.valid());
    assert(!repaired.has_active_cel());
    assert(repaired.ensure_active_cel());
    assert(repaired.valid());
    assert(repaired.has_active_cel());
    assert(repaired.active_frame == 0);
    assert(repaired.active_layer == 0);
    assert(repaired.active_cel().pixels.size() == 6);

    repaired.active_cel().pixels.resize(1);
    assert(!repaired.has_active_cel());
    assert(repaired.ensure_active_cel());
    assert(repaired.active_cel().pixels.size() == 6);

    repaired.active_frame = -5;
    repaired.active_layer = -8;
    assert(!repaired.has_active_cel());
    assert(repaired.ensure_active_cel());
    assert(repaired.active_frame == 0);
    assert(repaired.active_layer == 0);

    Document impossible;
    impossible.width = 0;
    impossible.height = 3;
    assert(!impossible.ensure_active_cel());
    assert(impossible.snapshot_active_cel().empty());
}

void test_tile_diffs_and_pixel_history() {
    const std::vector<Pixel> unchanged(15, rgba(1, 2, 3, 4));
    assert(make_tile_diffs(unchanged, unchanged, 5, 3, 0, 0, 2).empty());
    assert(make_tile_diffs(unchanged, std::vector<Pixel>(14), 5, 3, 0, 0).empty());
    assert(make_tile_diffs(unchanged, unchanged, 0, 3, 0, 0).empty());

    auto changed = unchanged;
    changed[0] = rgba(10, 20, 30, 40);
    changed[14] = rgba(50, 60, 70, 80);
    const auto diffs = make_tile_diffs(unchanged, changed, 5, 3, 2, 4, 2, true);
    assert(diffs.size() == 2);
    assert(diffs[0].frame == 2);
    assert(diffs[0].layer == 4);
    assert(diffs[0].x == 0 && diffs[0].y == 0);
    assert(diffs[0].w == 2 && diffs[0].h == 2);
    assert(diffs[0].before.size() == 4);
    assert(diffs[0].after.size() == 4);
    assert(diffs[1].x == 4 && diffs[1].y == 2);
    assert(diffs[1].w == 1 && diffs[1].h == 1);
    assert(diffs[1].before.front() == unchanged[14]);
    assert(diffs[1].after.front() == changed[14]);

    Document document = Document::create(4, 3);
    assert(!document.undo());
    assert(!document.redo());
    assert(!document.has_recent_commit_names());
    const auto initial = document.snapshot_active_cel();
    document.commit_active_cel_edit("No-op", initial);
    document.replace_active_pixels(initial, "No-op replacement");
    document.replace_active_pixels(std::vector<Pixel>(1, rgba(1, 2, 3)), "Wrong size");
    assert(!document.has_recent_commit_names());
    assert(!document.undo());

    auto before_red = document.snapshot_active_cel();
    set_pixel(document, 2, 1, rgba(255, 0, 0));
    document.commit_active_cel_edit("Paint red", std::move(before_red));
    assert(document.has_recent_commit_names());
    assert((document.consume_recent_commit_names() == std::vector<std::string>{"Paint red"}));
    assert(r(pixel_at(document, 2, 1)) == 255);
    assert(document.undo());
    assert(pixel_at(document, 2, 1) == 0);
    assert(document.redo());
    assert(r(pixel_at(document, 2, 1)) == 255);
    assert(!document.redo());

    assert(document.undo());
    auto green = document.snapshot_active_cel();
    green[index_of(document, 1, 1)] = rgba(0, 255, 0);
    document.replace_active_pixels(std::move(green), "Paint green");
    assert(g(pixel_at(document, 1, 1)) == 255);
    assert(!document.redo());
    assert(document.undo_stack_pixel_diff_capacity() > 0);
    assert(document.undo_stack_full_frame_pixel_capacity() == 0);
    assert(document.undo_stack_disk_history_pixel_count() == 0);

    document.clear_history();
    assert(!document.undo());
    assert(!document.redo());
    assert(!document.has_recent_commit_names());
    assert(document.undo_stack_pixel_diff_capacity() == 0);
}

void test_history_limit_and_recent_names() {
    Document document = Document::create(1, 1);
    for (int edit = 0; edit < 130; ++edit) {
        auto before = document.snapshot_active_cel();
        set_pixel(document,
                  0,
                  0,
                  edit % 2 == 0 ? rgba(240, 10, 20) : rgba(10, 20, 240));
        document.commit_active_cel_edit("Edit " + std::to_string(edit), std::move(before));
    }
    const auto names = document.consume_recent_commit_names();
    assert(names.size() == 130);
    assert(names.front() == "Edit 0");
    assert(names.back() == "Edit 129");

    int undo_count = 0;
    while (document.undo()) {
        ++undo_count;
    }
    assert(undo_count == 128);
    int redo_count = 0;
    while (document.redo()) {
        ++redo_count;
    }
    assert(redo_count == 128);
    document.clear_recent_commit_names();
    assert(!document.has_recent_commit_names());
}

void test_selection_palette_and_delete_history() {
    Document document = Document::create(4, 4);
    const SelectionMask no_selection = document.selection;
    document.commit_selection_edit("No-op selection", no_selection);
    assert(!document.undo());

    const SelectionMask before_selection = document.selection;
    document.selection.select_rect(1, 1, 2, 3, SelectionCombineMode::Replace);
    document.commit_selection_edit("Select region", before_selection);
    assert(document.selection.selected_count() == 6);
    assert(document.undo());
    assert(!document.selection.active);
    assert(document.redo());
    assert(document.selection.active);
    assert(document.selection.selected_count() == 6);

    document.clear_history();
    const Palette original_palette = document.palette;
    document.commit_palette_edit("No-op palette", original_palette);
    assert(!document.undo());
    document.palette.colors = {rgba(1, 2, 3), rgba(4, 5, 6), rgba(7, 8, 9)};
    document.palette.active = 2;
    document.commit_palette_edit("Replace palette", original_palette);
    assert((document.consume_recent_commit_names() == std::vector<std::string>{"Replace palette"}));
    assert(document.undo());
    assert(document.palette.colors == original_palette.colors);
    assert(document.palette.active == original_palette.active);
    assert(document.redo());
    assert(document.palette.colors.size() == 3);
    assert(document.palette.active == 2);

    Document deletion = Document::create(3, 2);
    deletion.selection.select_all();
    assert(!deletion.delete_selected_pixels());
    assert(!deletion.undo());
    set_pixel(deletion, 1, 0, rgba(20, 30, 40));
    set_pixel(deletion, 2, 1, rgba(50, 60, 70));
    deletion.selection.select_rect(1, 0, 1, 0, SelectionCombineMode::Replace);
    assert(deletion.delete_selected_pixels("Erase one pixel"));
    assert(pixel_at(deletion, 1, 0) == 0);
    assert(pixel_at(deletion, 2, 1) != 0);
    assert(deletion.undo());
    assert(pixel_at(deletion, 1, 0) == rgba(20, 30, 40));
    assert(deletion.redo());
    assert(pixel_at(deletion, 1, 0) == 0);
    deletion.selection.clear();
    assert(!deletion.delete_selected_pixels());
}

void test_histogram_channels_and_transparency() {
    Document document = Document::create(5, 1);
    document.active_cel().pixels = {
        rgba(255, 0, 0),
        rgba(0, 255, 0),
        rgba(0, 0, 255),
        rgba(12, 34, 56, 0),
        rgba(255, 255, 255)
    };

    const auto red = document.histogram_channel(0);
    const auto green = document.histogram_channel(1);
    const auto blue = document.histogram_channel(2);
    const auto fallback_blue = document.histogram_channel(99);
    assert(histogram_total(red) == 4);
    assert(histogram_total(green) == 4);
    assert(histogram_total(blue) == 4);
    assert(red[0] == 2 && red[255] == 2);
    assert(green[0] == 2 && green[255] == 2);
    assert(blue[0] == 2 && blue[255] == 2);
    assert(fallback_blue == blue);

    const auto luma = document.histogram_luma();
    assert(histogram_total(luma) == 4);
    assert(luma[54] == 1);
    assert(luma[182] == 1);
    assert(luma[18] == 1);
    assert(luma[255] == 1);
}

void test_floating_selection_cancel_and_commit_at_canvas_edge() {
    Document cancelled = Document::create(5, 4);
    set_pixel(cancelled, 1, 1, rgba(220, 10, 10));
    set_pixel(cancelled, 2, 1, rgba(10, 220, 10));
    set_pixel(cancelled, 1, 2, rgba(10, 10, 220));
    const auto original = cancelled.snapshot_active_cel();
    cancelled.selection.select_rect(1, 1, 2, 2, SelectionCombineMode::Replace);
    cancelled.selection.select_rect(2, 2, 2, 2, SelectionCombineMode::Subtract);
    assert(cancelled.begin_floating_selection());
    assert(cancelled.floating_selection.active);
    assert(cancelled.floating_selection.source_x == 1);
    assert(cancelled.floating_selection.source_y == 1);
    assert(cancelled.floating_selection.width == 2);
    assert(cancelled.floating_selection.height == 2);
    assert(cancelled.floating_selection.contains_local(0, 0));
    assert(!cancelled.floating_selection.contains_local(1, 1));
    assert(!cancelled.floating_selection.contains_local(-1, 0));
    assert(!cancelled.floating_selection.contains_local(2, 0));
    assert(pixel_at(cancelled, 1, 1) == 0);
    cancelled.move_floating_selection(20, -20);
    assert(cancelled.floating_selection.offset_x == 20);
    assert(cancelled.floating_selection.offset_y == -20);
    cancelled.cancel_floating_selection();
    assert(!cancelled.floating_selection.active);
    assert(cancelled.floating_selection.pixels.empty());
    assert(cancelled.active_cel().pixels == original);
    assert(!cancelled.undo());

    Document moved = Document::create(5, 3);
    const Pixel red = rgba(255, 0, 0);
    const Pixel green = rgba(0, 255, 0);
    set_pixel(moved, 3, 1, red);
    set_pixel(moved, 4, 1, green);
    const auto before_move = moved.snapshot_active_cel();
    moved.selection.select_rect(3, 1, 4, 1, SelectionCombineMode::Replace);
    assert(moved.begin_floating_selection());
    assert(moved.floating_selection.pixels == std::vector<Pixel>({red, green}));
    moved.move_floating_selection(1, 0);
    moved.commit_floating_selection("Move across edge", before_move);
    assert(!moved.floating_selection.active);
    assert(pixel_at(moved, 3, 1) == 0);
    assert(pixel_at(moved, 4, 1) == red);
    assert(moved.selection.active);
    assert(moved.selection.selected_count() == 1);
    assert((moved.selection.bounds() == std::optional<std::array<int, 4>>{{4, 1, 4, 1}}));
    assert((moved.consume_recent_commit_names() == std::vector<std::string>{"Move across edge"}));
    assert(moved.undo());
    assert(moved.active_cel().pixels == before_move);
    assert(moved.redo());
    assert(pixel_at(moved, 3, 1) == 0);
    assert(pixel_at(moved, 4, 1) == red);

    Document no_selection = Document::create(2, 2);
    assert(!no_selection.begin_floating_selection());
    no_selection.move_floating_selection(2, 3);
    no_selection.cancel_floating_selection();
    no_selection.commit_floating_selection("Inactive", no_selection.snapshot_active_cel());
    assert(!no_selection.undo());
}

void test_frame_duration_tags_and_removal_history() {
    Document document = Document::create(2, 2);
    assert(!document.set_frame_duration(-1, 50));
    assert(!document.set_frame_duration(1, 50));
    assert(document.set_frame_duration(0, 1));
    assert(document.frames[0].duration_ms == kMinimumFrameDurationMs);
    assert(!document.set_frame_duration(0, kMinimumFrameDurationMs - 1));
    assert(document.undo());
    assert(document.frames[0].duration_ms == 100);
    assert(document.redo());
    assert(document.frames[0].duration_ms == kMinimumFrameDurationMs);
    assert(document.set_frame_duration(0, kMaximumFrameDurationMs + 500));
    assert(document.frames[0].duration_ms == kMaximumFrameDurationMs);

    set_pixel(document, 0, 0, rgba(100, 20, 30));
    document.clear_history();
    document.add_frame(false);
    assert(document.frames.size() == 2);
    assert(document.active_frame == 1);
    assert(document.frames[1].duration_ms == kMaximumFrameDurationMs);
    assert(std::all_of(document.active_cel().pixels.begin(),
                       document.active_cel().pixels.end(),
                       [](Pixel pixel) { return pixel == 0; }));
    set_pixel(document, 1, 1, rgba(1, 2, 3));
    document.add_frame(true);
    assert(document.frames.size() == 3);
    assert(document.active_frame == 2);
    assert(pixel_at(document, 1, 1) == rgba(1, 2, 3));

    const auto frames_before_invalid_calls = document.frames;
    document.duplicate_frame(-1);
    document.duplicate_frame(99);
    document.move_frame(-1, 0);
    document.move_frame(0, 99);
    document.move_frame(1, 1);
    assert(document.frames.size() == frames_before_invalid_calls.size());

    document.duplicate_frame(0);
    assert(document.frames.size() == 4);
    assert(document.active_frame == 1);
    document.move_frame(1, 3);
    assert(document.active_frame == 3);

    document.clear_history();
    document.add_tag("", 99, -10);
    assert(document.tags.size() == 1);
    assert(document.tags[0].name == "Tag");
    assert(document.tags[0].from == 0);
    assert(document.tags[0].to == 3);
    assert(document.undo());
    assert(document.tags.empty());
    assert(document.redo());
    assert(document.tags.size() == 1);

    assert(!document.remove_tag(-1));
    assert(!document.remove_tag(1));
    assert(document.remove_frame(3));
    assert(document.frames.size() == 3);
    assert(document.tags[0].to == 2);
    assert(document.undo());
    assert(document.frames.size() == 4);
    assert(document.tags[0].to == 3);
    assert(document.redo());
    assert(document.frames.size() == 3);
    assert(document.tags[0].to == 2);

    assert(document.remove_tag(0));
    assert(document.tags.empty());
    assert(document.undo());
    assert(document.tags.size() == 1);
    assert(document.redo());
    assert(document.tags.empty());

    while (document.frames.size() > 1) {
        assert(document.remove_frame(static_cast<int>(document.frames.size()) - 1));
    }
    assert(!document.remove_frame(0));
    assert(!document.remove_frame(-1));
    assert(!document.remove_frame(2));
}

void test_layer_insert_remove_merge_and_history() {
    Document insertion = Document::create(2, 1);
    insertion.add_frame(false);
    const auto layers_before_invalid = insertion.layers;
    insertion.insert_layer(1, "Bad", {rgba(1, 2, 3)}, "Bad insert");
    assert(insertion.layers.size() == layers_before_invalid.size());
    insertion.insert_layer(99,
                           "",
                           {rgba(9, 8, 7), rgba(6, 5, 4)},
                           "Import pixels");
    assert(insertion.layers.size() == 2);
    assert(insertion.active_layer == 1);
    assert(insertion.layers[1].name == "Layer");
    assert(insertion.cel(insertion.active_frame, 1).pixels[0] == rgba(9, 8, 7));
    assert(insertion.cel(0, 1).pixels[0] == 0);
    assert(insertion.undo());
    assert(insertion.layers.size() == 1);
    assert(insertion.redo());
    assert(insertion.layers.size() == 2);

    Document merged = Document::create(1, 1);
    assert(!merged.merge_layer_down(0));
    assert(!merged.merge_layer_down(1));
    const Pixel bottom = rgba(40, 80, 120, 255);
    const Pixel top = rgba(220, 20, 60, 128);
    set_pixel(merged, 0, 0, bottom);
    merged.add_layer("Ink");
    set_pixel(merged, 0, 0, top);
    merged.layers[1].opacity = 0.5F;
    merged.clear_history();
    const Pixel expected = blend_over(bottom, top, 0.5F);
    assert(merged.merge_layer_down(1));
    assert(merged.layers.size() == 1);
    assert(merged.layers[0].name == "Background + Ink");
    assert(pixel_at(merged, 0, 0) == expected);
    assert(merged.active_layer == 0);
    assert(merged.undo());
    assert(merged.layers.size() == 2);
    assert(merged.layers[1].name == "Ink");
    assert(merged.active_layer == 1);
    assert(merged.cel(0, 0).pixels[0] == bottom);
    assert(merged.cel(0, 1).pixels[0] == top);
    assert(merged.redo());
    assert(merged.layers.size() == 1);
    assert(pixel_at(merged, 0, 0) == expected);

    Document removal = Document::create(2, 2);
    assert(!removal.remove_layer(0));
    removal.add_layer("Temporary");
    removal.floating_selection.active = true;
    removal.clear_history();
    assert(!removal.remove_layer(-1));
    assert(!removal.remove_layer(2));
    assert(removal.remove_layer(1));
    assert(!removal.floating_selection.active);
    assert(removal.layers.size() == 1);
    assert(removal.undo());
    assert(removal.layers.size() == 2);

    removal.clear_history();
    removal.duplicate_layer(-1);
    removal.duplicate_layer(5);
    removal.move_layer(-1, 0);
    removal.move_layer(0, 5);
    removal.move_layer(0, 0);
    assert(!removal.undo());
    removal.duplicate_layer(0);
    assert(removal.layers.size() == 3);
    assert(removal.active_layer == 1);
    removal.move_layer(1, 2);
    assert(removal.active_layer == 2);
}

void test_composite_layer_branches_and_enum_names() {
    Document document = Document::create(1, 1);
    const Pixel bottom = rgba(255, 64, 128, 255);
    const Pixel top = rgba(0, 255, 120, 192);
    set_pixel(document, 0, 0, bottom);
    document.add_layer("Top");
    set_pixel(document, 0, 0, top);
    document.clear_history();

    const std::array<LayerBlendMode, 14> modes = {
        LayerBlendMode::Normal,
        LayerBlendMode::Multiply,
        LayerBlendMode::Additive,
        LayerBlendMode::ColorBurn,
        LayerBlendMode::ColorDodge,
        LayerBlendMode::Reflect,
        LayerBlendMode::Glow,
        LayerBlendMode::Overlay,
        LayerBlendMode::Difference,
        LayerBlendMode::Negation,
        LayerBlendMode::Lighten,
        LayerBlendMode::Darken,
        LayerBlendMode::Screen,
        LayerBlendMode::Xor
    };
    for (LayerBlendMode mode : modes) {
        document.layers[1].blend_mode = mode;
        const auto composited = document.composite_active();
        assert(composited.size() == 1);
        assert(a(composited[0]) == 255);
        assert(std::string(layer_blend_mode_name(mode)) != "");
    }
    assert(std::string(layer_blend_mode_name(static_cast<LayerBlendMode>(999))) == "Normal");
    assert(std::string(playback_mode_name(PlaybackMode::Loop)) == "Loop");
    assert(std::string(playback_mode_name(PlaybackMode::PingPong)) == "Ping-Pong");
    assert(std::string(playback_mode_name(static_cast<PlaybackMode>(999))) == "Loop");

    document.layers[1].blend_mode = LayerBlendMode::Normal;
    document.layers[1].visible = false;
    assert(document.composite_active()[0] == bottom);
    document.layers[1].visible = true;
    document.layers[1].opacity = 0.0F;
    assert(document.composite_active()[0] == bottom);
    document.layers[1].opacity = 1.0F;
    document.layers[1].mask_enabled = true;
    document.layers[1].mask = {0};
    assert(document.composite_active()[0] == bottom);
    document.layers[1].mask = {128};
    assert(document.composite_active()[0] != bottom);

    document.layers[1].mask_enabled = false;
    document.cel(0, 0).pixels[0] = 0;
    document.layers[1].clip_to_below = true;
    assert(document.composite_active()[0] == 0);
    document.layers[1].clip_to_below = false;
    document.cel(0, 0).pixels[0] = bottom;

    const auto valid_top_pixels = document.cel(0, 1).pixels;
    document.cel(0, 1).pixels.clear();
    assert(document.composite_active()[0] == bottom);
    document.cel(0, 1).pixels = valid_top_pixels;
    assert(document.composite_frame(-1)[0] == 0);
    assert(document.composite_frame(4)[0] == 0);

    Document offset = Document::create(2, 1);
    offset.add_layer("Offset");
    offset.cel(0, 1).x = 1;
    offset.cel(0, 1).pixels[0] = rgba(20, 40, 60);
    const auto offset_composite = offset.composite_active();
    assert(offset_composite[0] == 0);
    assert(offset_composite[1] == rgba(20, 40, 60));
}

} // namespace

int main() {
    test_selection_mask_rectangles_and_bounds();
    test_selection_mask_combine_polygon_invert_and_translate();
    test_document_creation_validation_and_cel_repair();
    test_tile_diffs_and_pixel_history();
    test_history_limit_and_recent_names();
    test_selection_palette_and_delete_history();
    test_histogram_channels_and_transparency();
    test_floating_selection_cancel_and_commit_at_canvas_edge();
    test_frame_duration_tags_and_removal_history();
    test_layer_insert_remove_merge_and_history();
    test_composite_layer_branches_and_enum_names();
    test_selection_mask_fully_outside_rectangles();
    std::cout << "document coverage tests passed\n";
    return 0;
}
