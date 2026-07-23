// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Tools.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace px;

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, std::string_view expression, int line) {
    if (!condition) {
        throw TestFailure("line " + std::to_string(line) + ": " + std::string(expression));
    }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

[[nodiscard]] std::size_t index_of(const Document& document, int x, int y) {
    return static_cast<std::size_t>(document.pixel_index(x, y));
}

[[nodiscard]] Pixel pixel_at(const Document& document, int x, int y) {
    return document.active_cel().pixels[index_of(document, x, y)];
}

void set_pixel(Document& document, int x, int y, Pixel color) {
    document.active_cel().pixels[index_of(document, x, y)] = color;
}

[[nodiscard]] int non_transparent_count(const Document& document) {
    int count = 0;
    for (Pixel pixel : document.active_cel().pixels) {
        if (a(pixel) != 0) {
            ++count;
        }
    }
    return count;
}

void check_single_commit(Document& document, std::string_view expected) {
    const std::vector<std::string> names = document.consume_recent_commit_names();
    CHECK(names.size() == 1U);
    CHECK(names.front() == expected);
}

void test_tool_names_and_constrained_endpoints() {
    const std::array<std::pair<ToolType, std::string_view>, 16> names = {{
        {ToolType::Pencil, "Pencil"},
        {ToolType::Brush, "Brush"},
        {ToolType::Eraser, "Eraser"},
        {ToolType::Line, "Line"},
        {ToolType::Rectangle, "Rectangle"},
        {ToolType::Ellipse, "Ellipse"},
        {ToolType::Bucket, "Bucket"},
        {ToolType::Gradient, "Gradient"},
        {ToolType::Eyedropper, "Eyedropper"},
        {ToolType::CloneStamp, "Clone Stamp"},
        {ToolType::RectSelect, "Rectangle Select"},
        {ToolType::EllipseSelect, "Ellipse Select"},
        {ToolType::LassoSelect, "Lasso Select"},
        {ToolType::MagicWand, "Magic Wand"},
        {ToolType::MovePixels, "Move Pixels"},
        {ToolType::Text, "Text"},
    }};
    for (const auto& [tool, expected] : names) {
        CHECK(std::string_view(tool_name(tool)) == expected);
    }
    CHECK(std::string_view(tool_name(static_cast<ToolType>(999))) == "Tool");

    CHECK((constrained_tool_endpoint(ToolType::Line, 10, 10, 10, 10, true) ==
           std::array<int, 2>{10, 10}));
    CHECK((constrained_tool_endpoint(ToolType::Rectangle, 10, 10, 13, 5, false) ==
           std::array<int, 2>{13, 5}));
    CHECK((constrained_tool_endpoint(ToolType::Rectangle, 10, 10, 13, 5, true) ==
           std::array<int, 2>{15, 5}));
    CHECK((constrained_tool_endpoint(ToolType::Ellipse, 10, 10, 7, 12, true) ==
           std::array<int, 2>{7, 13}));
    CHECK((constrained_tool_endpoint(ToolType::RectSelect, 10, 10, 10, 7, true) ==
           std::array<int, 2>{13, 7}));
    CHECK((constrained_tool_endpoint(ToolType::EllipseSelect, 10, 10, 7, 10, true) ==
           std::array<int, 2>{7, 13}));
    CHECK((constrained_tool_endpoint(ToolType::Pencil, 10, 10, 4, 19, true) ==
           std::array<int, 2>{4, 19}));

    const std::array<std::array<int, 2>, 8> current = {{
        {20, 11}, {20, 18}, {11, 20}, {2, 20},
        {0, 11}, {0, 2}, {9, 0}, {18, 0},
    }};
    const std::array<std::array<int, 2>, 8> expected = {{
        {20, 10}, {20, 20}, {10, 20}, {0, 20},
        {0, 10}, {0, 0}, {10, 0}, {20, 0},
    }};
    for (std::size_t i = 0; i < current.size(); ++i) {
        CHECK(constrained_tool_endpoint(ToolType::Line,
                                        10,
                                        10,
                                        current[i][0],
                                        current[i][1],
                                        true) == expected[i]);
    }
}

void test_pixels_and_brushes() {
    const Pixel red = rgba(255, 0, 0, 255);
    const Pixel green = rgba(0, 255, 0, 255);
    auto document = Document::create(9, 9);

    put_pixel(document, -1, 0, red, false);
    put_pixel(document, 9, 8, red, false);
    CHECK(non_transparent_count(document) == 0);

    document.selection.select_rect(4, 4, 4, 4, true);
    put_pixel(document, 3, 4, red, false);
    CHECK(pixel_at(document, 3, 4) == 0);
    put_pixel(document, 4, 4, red, false);
    CHECK(pixel_at(document, 4, 4) == red);
    put_pixel(document, 4, 4, green, true);
    CHECK(pixel_at(document, 4, 4) == 0);
    document.selection.clear();

    plot_brush_raw(document, 1, 1, red, 0, false);
    CHECK(pixel_at(document, 1, 1) == red);
    plot_brush_raw(document, 4, 4, green, 2, false);
    CHECK(pixel_at(document, 3, 3) == green);
    CHECK(pixel_at(document, 4, 4) == green);
    CHECK(pixel_at(document, 5, 5) == 0);

    plot_brush_raw(document, 6, 6, red, 5, false);
    CHECK(pixel_at(document, 6, 6) == red);
    CHECK(pixel_at(document, 4, 4) == green);
    CHECK(pixel_at(document, 4, 6) == red);
    CHECK(pixel_at(document, 4, 4) != red);
    plot_brush_raw(document, 0, 0, green, 5, false);
    CHECK(pixel_at(document, 0, 0) == green);

    auto history = Document::create(5, 5);
    ToolContext context;
    context.primary = red;
    context.brush_size = -4;
    draw_brush(history, 2, 2, context);
    CHECK(pixel_at(history, 2, 2) == red);
    CHECK(pixel_at(history, 1, 2) == 0);
    check_single_commit(history, "Brush");
    CHECK(history.undo());
    CHECK(pixel_at(history, 2, 2) == 0);
    CHECK(history.redo());
    CHECK(pixel_at(history, 2, 2) == red);

    draw_brush(history, 2, 2, context, true);
    CHECK(pixel_at(history, 2, 2) == 0);
    check_single_commit(history, "Erase");
    draw_brush(history, -20, -20, context);
    CHECK(history.consume_recent_commit_names().empty());

    auto opacity = Document::create(9, 9);
    plot_brush_raw(opacity, 4, 4, red, 1, false, 0.5f, 1.0f);
    CHECK(a(pixel_at(opacity, 4, 4)) >= 127);
    CHECK(a(pixel_at(opacity, 4, 4)) <= 128);
    plot_brush_raw(opacity, 4, 4, red, 1, true, 0.5f, 1.0f);
    CHECK(a(pixel_at(opacity, 4, 4)) >= 63);
    CHECK(a(pixel_at(opacity, 4, 4)) <= 64);

    auto soft = Document::create(9, 9);
    plot_brush_raw(soft, 4, 4, red, 7, false, 1.0f, 0.0f);
    CHECK(a(pixel_at(soft, 4, 4)) == 255);
    CHECK(a(pixel_at(soft, 4, 1)) > 0);
    CHECK(a(pixel_at(soft, 4, 1)) < a(pixel_at(soft, 4, 4)));
}

void test_floating_selection_transforms() {
    const Pixel red = rgba(255, 0, 0, 255);
    const Pixel green = rgba(0, 255, 0, 255);
    FloatingSelection source;
    source.active = true;
    source.source_x = 2;
    source.source_y = 3;
    source.offset_x = 1;
    source.offset_y = -1;
    source.width = 2;
    source.height = 1;
    source.pixels = {red, green};
    source.mask = {1, 1};

    const FloatingSelection scaled = scale_floating_selection(source, 4, 5, 4, 2);
    CHECK(scaled.active);
    CHECK(scaled.source_x == 4);
    CHECK(scaled.source_y == 5);
    CHECK(scaled.offset_x == 0);
    CHECK(scaled.offset_y == 0);
    CHECK(scaled.width == 4);
    CHECK(scaled.height == 2);
    CHECK(scaled.pixels == std::vector<Pixel>({red, red, green, green,
                                               red, red, green, green}));
    CHECK(scaled.mask == std::vector<std::uint8_t>(8, 1));

    const FloatingSelection rotated = rotate_floating_selection(source, 90.0f);
    CHECK(rotated.active);
    CHECK(rotated.width == 1);
    CHECK(rotated.height == 2);
    CHECK(rotated.pixels == std::vector<Pixel>({red, green}));
    CHECK(rotated.mask == std::vector<std::uint8_t>({1, 1}));

    FloatingSelection invalid = source;
    invalid.mask.clear();
    CHECK(!scale_floating_selection(invalid, 0, 0, 2, 2).active);
    CHECK(!rotate_floating_selection(invalid, 45.0f).active);
}

void test_lines_and_rectangles() {
    const Pixel red = rgba(255, 0, 0, 255);
    const Pixel green = rgba(0, 255, 0, 255);
    const Pixel blue = rgba(0, 0, 255, 255);

    auto lines = Document::create(9, 9);
    draw_line_raw(lines, 1, 1, 7, 1, red, 1, false);
    for (int x = 1; x <= 7; ++x) {
        CHECK(pixel_at(lines, x, 1) == red);
    }
    draw_line_raw(lines, 7, 7, 2, 3, green, 2, false);
    CHECK(pixel_at(lines, 7, 7) == green);
    CHECK(pixel_at(lines, 2, 3) == green);
    draw_line_raw(lines, 7, 7, 2, 3, green, 2, true);
    CHECK(pixel_at(lines, 7, 7) == 0);
    CHECK(pixel_at(lines, 2, 3) == 0);

    lines.clear_history();
    draw_line(lines, 0, 8, 8, 0, blue, 0);
    CHECK(pixel_at(lines, 0, 8) == blue);
    CHECK(pixel_at(lines, 8, 0) == blue);
    check_single_commit(lines, "Line");
    CHECK(lines.undo());
    CHECK(pixel_at(lines, 4, 4) == 0);
    CHECK(lines.redo());
    CHECK(pixel_at(lines, 4, 4) == blue);

    auto filled = Document::create(8, 8);
    draw_rect_raw(filled, 5, 4, 2, 1, red, 7, true);
    CHECK(pixel_at(filled, 2, 1) == red);
    CHECK(pixel_at(filled, 4, 3) == red);
    CHECK(pixel_at(filled, 1, 1) == 0);

    auto outline = Document::create(9, 9);
    draw_rect_raw(outline, 1, 1, 7, 7, green, 2, false);
    CHECK(pixel_at(outline, 1, 1) == green);
    CHECK(pixel_at(outline, 2, 2) == green);
    CHECK(pixel_at(outline, 4, 4) == 0);
    draw_rect_raw(outline, 4, 4, 4, 4, blue, 0, false);
    CHECK(pixel_at(outline, 4, 4) == blue);

    auto history = Document::create(7, 7);
    draw_rect(history, 1, 1, 5, 5, red, 1, false);
    check_single_commit(history, "Rectangle");
    draw_rect(history, 2, 2, 4, 4, green, 1, true);
    check_single_commit(history, "Filled Rectangle");
    CHECK(pixel_at(history, 3, 3) == green);
    CHECK(history.undo());
    CHECK(pixel_at(history, 3, 3) == 0);
}

void test_ellipses() {
    const Pixel color = rgba(180, 60, 220, 255);

    auto horizontal_line = Document::create(10, 5);
    draw_ellipse_raw(horizontal_line, 1, 2, 8, 2, color, 1, false);
    for (int x = 1; x <= 8; ++x) {
        CHECK(pixel_at(horizontal_line, x, 2) == color);
    }

    auto vertical_line = Document::create(5, 10);
    draw_ellipse_raw(vertical_line, 2, 1, 2, 8, color, 2, false);
    for (int y = 1; y <= 8; ++y) {
        CHECK(a(pixel_at(vertical_line, 2, y)) == 255);
    }

    auto wide = Document::create(16, 10);
    draw_ellipse_raw(wide, 1, 2, 14, 7, color, 1, false);
    for (int x = 1; x <= 14; ++x) {
        bool marked = false;
        for (int y = 2; y <= 7; ++y) {
            marked = marked || a(pixel_at(wide, x, y)) != 0;
        }
        CHECK(marked);
    }

    auto tall = Document::create(10, 16);
    draw_ellipse_raw(tall, 2, 1, 7, 14, color, 1, false);
    for (int y = 1; y <= 14; ++y) {
        bool marked = false;
        for (int x = 2; x <= 7; ++x) {
            marked = marked || a(pixel_at(tall, x, y)) != 0;
        }
        CHECK(marked);
    }

    auto filled = Document::create(9, 9);
    draw_ellipse(filled, 7, 7, 1, 1, color, 3, true);
    CHECK(pixel_at(filled, 4, 4) == color);
    CHECK(pixel_at(filled, 1, 1) == 0);
    check_single_commit(filled, "Filled Ellipse");
    CHECK(filled.undo());
    CHECK(pixel_at(filled, 4, 4) == 0);
    draw_ellipse(filled, 1, 2, 7, 6, color, 2, false);
    check_single_commit(filled, "Ellipse");
}

void test_bucket_fills() {
    const Pixel target = rgba(10, 10, 10, 255);
    const Pixel near_target = rgba(13, 10, 10, 255);
    const Pixel barrier = rgba(80, 90, 100, 255);
    const Pixel fill = rgba(250, 40, 20, 255);

    auto contiguous = Document::create(6, 3);
    for (Pixel& pixel : contiguous.active_cel().pixels) {
        pixel = target;
    }
    for (int y = 0; y < contiguous.height; ++y) {
        set_pixel(contiguous, 2, y, barrier);
    }
    set_pixel(contiguous, 1, 1, near_target);
    contiguous.clear_history();
    fill_bucket(contiguous, 0, 1, fill, 3, true);
    CHECK(pixel_at(contiguous, 0, 0) == fill);
    CHECK(pixel_at(contiguous, 1, 1) == fill);
    CHECK(pixel_at(contiguous, 2, 1) == barrier);
    CHECK(pixel_at(contiguous, 5, 1) == target);
    check_single_commit(contiguous, "Fill");
    CHECK(contiguous.undo());
    CHECK(pixel_at(contiguous, 1, 1) == near_target);
    CHECK(contiguous.redo());
    CHECK(pixel_at(contiguous, 1, 1) == fill);

    contiguous.clear_recent_commit_names();
    fill_bucket(contiguous, -1, 0, fill, 0, true);
    fill_bucket(contiguous, 0, 0, fill, 0, true);
    CHECK(contiguous.consume_recent_commit_names().empty());

    auto global = Document::create(6, 2);
    for (Pixel& pixel : global.active_cel().pixels) {
        pixel = barrier;
    }
    set_pixel(global, 0, 0, target);
    set_pixel(global, 2, 0, near_target);
    set_pixel(global, 5, 1, target);
    global.selection.select_rect(0, 0, 2, 1, true);
    global.clear_history();
    fill_bucket(global, 0, 0, fill, 3, false);
    CHECK(pixel_at(global, 0, 0) == fill);
    CHECK(pixel_at(global, 2, 0) == fill);
    CHECK(pixel_at(global, 5, 1) == target);
    CHECK(pixel_at(global, 1, 1) == barrier);
    check_single_commit(global, "Global Fill");
    CHECK(global.undo());
    CHECK(pixel_at(global, 0, 0) == target);
    CHECK(pixel_at(global, 2, 0) == near_target);
}

void test_gradients() {
    const Pixel black = rgba(0, 0, 0, 0);
    const Pixel red = rgba(255, 0, 0, 255);
    auto raw = Document::create(5, 2);
    raw.selection.select_rect(1, 0, 3, 1, true);
    fill_gradient_raw(raw, 1, 0, 3, 0, black, red);
    CHECK(pixel_at(raw, 0, 0) == 0);
    CHECK(pixel_at(raw, 1, 0) == black);
    CHECK(r(pixel_at(raw, 2, 0)) == 128);
    CHECK(a(pixel_at(raw, 2, 0)) == 128);
    CHECK(pixel_at(raw, 3, 1) == red);
    CHECK(pixel_at(raw, 4, 1) == 0);

    const Pixel blue = rgba(0, 0, 255, 255);
    fill_gradient_raw(raw, 2, 1, 2, 1, blue, red);
    CHECK(pixel_at(raw, 1, 0) == blue);
    CHECK(pixel_at(raw, 3, 1) == blue);

    auto history = Document::create(2, 4);
    fill_gradient(history, 0, 0, 0, 3, black, red);
    CHECK(pixel_at(history, 0, 0) == black);
    CHECK(r(pixel_at(history, 0, 1)) == 85);
    CHECK(r(pixel_at(history, 0, 2)) == 170);
    CHECK(pixel_at(history, 1, 3) == red);
    check_single_commit(history, "Gradient");
    CHECK(history.undo());
    CHECK(non_transparent_count(history) == 0);
    CHECK(history.redo());
    CHECK(pixel_at(history, 1, 3) == red);
}

void test_color_picker_and_magic_wand() {
    const Pixel red = rgba(255, 0, 0, 255);
    const Pixel near_red = rgba(250, 0, 0, 255);
    const Pixel blue = rgba(0, 0, 255, 255);

    auto picker = Document::create(2, 1);
    set_pixel(picker, 0, 0, red);
    picker.add_layer("Top");
    set_pixel(picker, 0, 0, blue);
    CHECK(pick_color(picker, 0, 0).has_value());
    CHECK(pick_color(picker, 0, 0).value() == blue);
    picker.layers[1].visible = false;
    CHECK(pick_color(picker, 0, 0).value() == red);
    CHECK(!pick_color(picker, -1, 0).has_value());
    CHECK(!pick_color(picker, 2, 0).has_value());

    auto wand = Document::create(7, 3);
    for (Pixel& pixel : wand.active_cel().pixels) {
        pixel = blue;
    }
    set_pixel(wand, 0, 0, red);
    set_pixel(wand, 1, 0, near_red);
    set_pixel(wand, 0, 1, red);
    set_pixel(wand, 5, 1, red);
    set_pixel(wand, 6, 1, red);

    magic_wand(wand, -1, 0, 0, true);
    CHECK(!wand.selection.active);
    magic_wand(wand, 0, 0, 5, true, true);
    CHECK(wand.selection.selected_count() == 3);
    CHECK(wand.selection.contains(0, 0));
    CHECK(wand.selection.contains(1, 0));
    CHECK(!wand.selection.contains(5, 1));

    magic_wand(wand, 5, 1, 0, true, false);
    CHECK(wand.selection.selected_count() == 5);
    CHECK(wand.selection.contains(6, 1));

    magic_wand(wand, 2, 2, 0, false, SelectionCombineMode::Replace);
    CHECK(wand.selection.selected_count() == 16);
    magic_wand(wand, 0, 0, 0, false, SelectionCombineMode::Add);
    CHECK(wand.selection.selected_count() == 20);
    magic_wand(wand, 2, 2, 0, false, SelectionCombineMode::Subtract);
    CHECK(wand.selection.selected_count() == 4);
    CHECK(wand.selection.contains(0, 0));
    magic_wand(wand, 0, 0, 0, false, SelectionCombineMode::Intersect);
    CHECK(wand.selection.selected_count() == 4);
    magic_wand(wand, 0, 0, 0, false, SelectionCombineMode::Invert);
    CHECK(wand.selection.selected_count() == 0);
    CHECK(!wand.selection.active);
}

void test_clone_stamp() {
    const Pixel red = rgba(255, 0, 0, 255);
    const Pixel green = rgba(0, 255, 0, 255);
    const Pixel blue = rgba(0, 0, 255, 255);
    const Pixel white = rgba(255, 255, 255, 255);

    auto raw = Document::create(6, 6);
    for (int y = 0; y < raw.height; ++y) {
        for (int x = 0; x < raw.width; ++x) {
            set_pixel(raw,
                      x,
                      y,
                      rgba(static_cast<std::uint8_t>(x * 20),
                           static_cast<std::uint8_t>(y * 20),
                           static_cast<std::uint8_t>((x + y) * 10),
                           255));
        }
    }
    const std::vector<Pixel> source = raw.active_cel().pixels;
    const std::vector<Pixel> unchanged = raw.active_cel().pixels;
    clone_stamp_raw(raw, std::vector<Pixel>{red}, 1, 1, 4, 4, 3);
    CHECK(raw.active_cel().pixels == unchanged);

    raw.selection.select_rect(4, 4, 5, 5, true);
    clone_stamp_raw(raw, source, 1, 1, 4, 4, 3);
    CHECK(pixel_at(raw, 4, 4) == source[index_of(raw, 1, 1)]);
    CHECK(pixel_at(raw, 5, 5) == source[index_of(raw, 2, 2)]);
    CHECK(pixel_at(raw, 3, 3) == unchanged[index_of(raw, 3, 3)]);
    raw.selection.clear();
    clone_stamp_raw(raw, source, 0, 0, 5, 5, -8);
    CHECK(pixel_at(raw, 5, 5) == source[index_of(raw, 0, 0)]);

    auto overlapping = Document::create(4, 1);
    set_pixel(overlapping, 0, 0, red);
    set_pixel(overlapping, 1, 0, green);
    set_pixel(overlapping, 2, 0, blue);
    set_pixel(overlapping, 3, 0, white);
    overlapping.clear_history();
    clone_stamp(overlapping, 1, 0, 2, 0, 3);
    CHECK(pixel_at(overlapping, 0, 0) == red);
    CHECK(pixel_at(overlapping, 1, 0) == red);
    CHECK(pixel_at(overlapping, 2, 0) == green);
    CHECK(pixel_at(overlapping, 3, 0) == blue);
    check_single_commit(overlapping, "Clone Stamp");
    CHECK(overlapping.undo());
    CHECK(pixel_at(overlapping, 1, 0) == green);
    CHECK(pixel_at(overlapping, 2, 0) == blue);
    CHECK(pixel_at(overlapping, 3, 0) == white);
    CHECK(overlapping.redo());
    CHECK(pixel_at(overlapping, 3, 0) == blue);

    overlapping.clear_recent_commit_names();
    clone_stamp(overlapping, -1, 0, 2, 0, 1);
    clone_stamp(overlapping, 0, 0, 4, 0, 1);
    CHECK(overlapping.consume_recent_commit_names().empty());
}

void test_text_tools() {
    const std::string every_glyph =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,:!?-_+/\\ @";
    const std::vector<std::array<int, 2>> points = raster_text_points(3, 4, every_glyph + "\nA");
    CHECK(!points.empty());
    bool reached_second_line = false;
    for (const auto& point : points) {
        reached_second_line = reached_second_line || point[1] >= 12;
    }
    CHECK(reached_second_line);
    CHECK(raster_text_points(0, 0, "").empty());
    CHECK(raster_text_points(0, 0, " ").empty());
    CHECK(raster_text_points(7, 9, "a") == raster_text_points(7, 9, "A"));

    const Pixel color = rgba(30, 140, 250, 255);
    auto selected = Document::create(8, 8);
    selected.selection.select_rect(2, 0, 2, 0, true);
    stamp_text(selected, 0, 0, "A", color);
    CHECK(pixel_at(selected, 2, 0) == color);
    CHECK(pixel_at(selected, 1, 0) == 0);
    CHECK(non_transparent_count(selected) == 1);
    check_single_commit(selected, "Text");
    CHECK(selected.undo());
    CHECK(non_transparent_count(selected) == 0);

    selected.selection.clear();
    stamp_text_blocks(selected, 0, 0, "B", color);
    CHECK(pixel_at(selected, 0, 0) == color);
    check_single_commit(selected, "Text");
    stamp_text(selected, 0, 0, "", color);
    CHECK(selected.consume_recent_commit_names().empty());

    auto clipped = Document::create(3, 3);
    stamp_text(clipped, -2, -2, "A", color);
    CHECK(non_transparent_count(clipped) > 0);
}

void test_mask_tools() {
    auto initialization = Document::create(5, 5);
    initialization.active_layer = -1;
    ensure_active_layer_mask(initialization, 17);
    CHECK(initialization.layers[0].mask.empty());
    initialization.active_layer = 0;
    ensure_active_layer_mask(initialization, 17);
    CHECK(initialization.layers[0].mask_enabled);
    CHECK(initialization.layers[0].mask.size() == 25U);
    CHECK(initialization.layers[0].mask.front() == 17);
    initialization.layers[0].mask.front() = 9;
    ensure_active_layer_mask(initialization, 200);
    CHECK(initialization.layers[0].mask.front() == 9);
    initialization.layers[0].mask.resize(1U);
    ensure_active_layer_mask(initialization, 42);
    CHECK(initialization.layers[0].mask.size() == 25U);
    CHECK(initialization.layers[0].mask.back() == 42);

    auto brushes = Document::create(8, 8);
    brushes.selection.select_rect(1, 1, 6, 6, true);
    put_mask_pixel(brushes, 0, 0, 0);
    CHECK(brushes.layers[0].mask.empty());
    put_mask_pixel(brushes, 1, 1, 0);
    CHECK(brushes.layers[0].mask_enabled);
    CHECK(brushes.layers[0].mask[index_of(brushes, 1, 1)] == 0);
    CHECK(brushes.layers[0].mask[index_of(brushes, 0, 0)] == 255);
    put_mask_pixel(brushes, -1, 1, 0);

    plot_mask_brush_raw(brushes, 3, 3, 80, 2);
    CHECK(brushes.layers[0].mask[index_of(brushes, 2, 2)] == 80);
    CHECK(brushes.layers[0].mask[index_of(brushes, 3, 3)] == 80);
    plot_mask_brush_raw(brushes, 5, 5, 60, 5);
    CHECK(brushes.layers[0].mask[index_of(brushes, 5, 5)] == 60);
    CHECK(brushes.layers[0].mask[index_of(brushes, 3, 5)] == 60);
    CHECK(brushes.layers[0].mask[index_of(brushes, 3, 3)] == 80);
    draw_mask_line_raw(brushes, 1, 6, 6, 1, 120, 1);
    CHECK(brushes.layers[0].mask[index_of(brushes, 1, 6)] == 120);
    CHECK(brushes.layers[0].mask[index_of(brushes, 6, 1)] == 120);

    auto contiguous = Document::create(6, 2);
    ensure_active_layer_mask(contiguous, 10);
    contiguous.layers[0].mask[index_of(contiguous, 2, 0)] = 200;
    contiguous.layers[0].mask[index_of(contiguous, 2, 1)] = 200;
    contiguous.layers[0].mask[index_of(contiguous, 1, 1)] = 12;
    fill_mask_bucket(contiguous, 0, 0, 77, 2, true);
    CHECK(contiguous.layers[0].mask[index_of(contiguous, 0, 1)] == 77);
    CHECK(contiguous.layers[0].mask[index_of(contiguous, 1, 1)] == 77);
    CHECK(contiguous.layers[0].mask[index_of(contiguous, 2, 0)] == 200);
    CHECK(contiguous.layers[0].mask[index_of(contiguous, 5, 0)] == 10);

    auto global = Document::create(5, 2);
    ensure_active_layer_mask(global, 10);
    global.layers[0].mask[index_of(global, 1, 0)] = 12;
    global.layers[0].mask[index_of(global, 4, 1)] = 12;
    global.layers[0].mask[index_of(global, 2, 0)] = 100;
    global.selection.select_rect(0, 0, 2, 1, true);
    fill_mask_bucket(global, 0, 0, 90, 2, false);
    CHECK(global.layers[0].mask[index_of(global, 0, 0)] == 90);
    CHECK(global.layers[0].mask[index_of(global, 1, 0)] == 90);
    CHECK(global.layers[0].mask[index_of(global, 4, 1)] == 12);
    CHECK(global.layers[0].mask[index_of(global, 2, 0)] == 100);

    const std::vector<std::uint8_t> before = global.layers[0].mask;
    fill_mask_bucket(global, -1, 0, 0, 0, true);
    fill_mask_bucket(global, 4, 1, 0, 0, true);
    CHECK(global.layers[0].mask == before);

    auto negative_tolerance = Document::create(2, 1);
    ensure_active_layer_mask(negative_tolerance, 10);
    negative_tolerance.layers[0].mask[1] = 12;
    fill_mask_bucket(negative_tolerance, 0, 0, 1, -50, false);
    CHECK(negative_tolerance.layers[0].mask[0] == 1);
    CHECK(negative_tolerance.layers[0].mask[1] == 12);
}

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

} // namespace

int main() {
    const std::array<TestCase, 12> tests = {{
        {"tool names and constrained endpoints", test_tool_names_and_constrained_endpoints},
        {"pixels and brushes", test_pixels_and_brushes},
        {"floating selection transforms", test_floating_selection_transforms},
        {"lines and rectangles", test_lines_and_rectangles},
        {"ellipses", test_ellipses},
        {"bucket fills", test_bucket_fills},
        {"gradients", test_gradients},
        {"color picker and magic wand", test_color_picker_and_magic_wand},
        {"clone stamp", test_clone_stamp},
        {"text tools", test_text_tools},
        {"mask tools", test_mask_tools},
        {"repeatable no-op smoke", [] {
             auto document = Document::create(2, 2);
             ToolContext context;
             draw_brush(document, -10, -10, context);
             fill_bucket(document, 9, 9, context.primary, 0, true);
             clone_stamp(document, -1, -1, 0, 0, 1);
             CHECK(document.consume_recent_commit_names().empty());
             CHECK(!document.undo());
             CHECK(!document.redo());
         }},
    }};

    int passed = 0;
    for (const TestCase& test : tests) {
        try {
            test.function();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << passed << " tool coverage tests passed\n";
    return 0;
}
