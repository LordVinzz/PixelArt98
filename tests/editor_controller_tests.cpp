// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/EditorController.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>
#include <utility>

using namespace px;

namespace {

int painted_pixels(const EditorController& editor) {
    int count = 0;
    for (Pixel pixel : editor.document().active_cel().pixels) {
        if (a(pixel) != 0) ++count;
    }
    return count;
}

Pixel editor_pixel(const EditorController& editor, int x, int y) {
    return editor.document().active_cel().pixels[
        static_cast<std::size_t>(editor.document().pixel_index(x, y))];
}

} // namespace

int main() {
    EditorController editor;
    editor.new_document(16, 16);
    editor.tool().primary = rgba(255, 0, 0, 255);
    editor.tool().tool = ToolType::Pencil;
    editor.begin_stroke(2, 2, false, SelectionCombineMode::Replace);
    editor.update_stroke(5, 2, false);
    editor.end_stroke(5, 2, false);
    assert(r(editor.document().active_cel().pixels[static_cast<std::size_t>(editor.document().pixel_index(4, 2))]) == 255);
    assert(editor.undo());
    assert(a(editor.document().active_cel().pixels[static_cast<std::size_t>(editor.document().pixel_index(4, 2))]) == 0);
    assert(editor.redo());

    editor.tool().tool = ToolType::RectSelect;
    editor.begin_stroke(1, 1, false, SelectionCombineMode::Replace);
    editor.end_stroke(3, 3, false);
    assert(editor.document().selection.contains(2, 2));
    editor.begin_stroke(8, 8, false, SelectionCombineMode::Add);
    editor.end_stroke(9, 9, false);
    assert(editor.document().selection.contains(2, 2));
    assert(editor.document().selection.contains(9, 9));

    EditorController ellipse_selector;
    ellipse_selector.new_document(12, 12);
    ellipse_selector.tool().tool = ToolType::EllipseSelect;
    ellipse_selector.begin_stroke(1, 1, false, SelectionCombineMode::Replace);
    ellipse_selector.end_stroke(7, 5, false);
    assert(ellipse_selector.document().selection.active);
    assert(ellipse_selector.document().selection.contains(4, 3));
    assert(!ellipse_selector.document().selection.contains(1, 1));
    const int ellipse_count = ellipse_selector.document().selection.selected_count();
    assert(ellipse_count > 1);
    assert(ellipse_selector.undo());
    assert(!ellipse_selector.document().selection.active);
    assert(ellipse_selector.redo());
    assert(ellipse_selector.document().selection.selected_count() == ellipse_count);

    EditorController refiner;
    refiner.new_document(12, 12);
    refiner.document().selection.select_rect(4, 4, 6, 6, SelectionCombineMode::Replace);
    assert(refiner.expand_selection(1));
    assert(refiner.document().selection.selected_count() == 25);
    assert(refiner.undo());
    assert(refiner.document().selection.selected_count() == 9);
    assert(refiner.redo());
    assert(refiner.document().selection.selected_count() == 25);
    assert(refiner.contract_selection(1));
    assert(refiner.document().selection.selected_count() == 9);
    assert(refiner.border_selection(1));
    assert(!refiner.document().selection.contains(5, 5));
    assert(refiner.smooth_selection(1));

    editor.begin_stroke(4, 4, false, SelectionCombineMode::Replace);
    editor.end_stroke(4, 4, false);
    assert(!editor.document().selection.active);
    assert(editor.document().selection.selected_count() == 0);

    editor.document().selection.select_rect(1, 1, 3, 3, true);
    editor.tool().tool = ToolType::LassoSelect;
    editor.begin_stroke(5, 5, false, SelectionCombineMode::Replace);
    editor.end_stroke(5, 5, false);
    assert(!editor.document().selection.active);
    assert(editor.document().selection.selected_count() == 0);

    editor.document().active_cel().pixels[static_cast<std::size_t>(editor.document().pixel_index(10, 10))] = rgba(12, 34, 56, 255);
    editor.tool().tool = ToolType::MagicWand;
    editor.tool().tolerance = 0;
    editor.begin_stroke(10, 10, false, SelectionCombineMode::Replace);
    assert(!editor.document().selection.active);
    assert(editor.document().selection.selected_count() == 0);

    editor.clear_selection();
    editor.tool().tool = ToolType::Line;
    editor.begin_stroke(4, 4, false, SelectionCombineMode::Replace);
    editor.end_stroke(9, 6, true);
    assert(r(editor.document().active_cel().pixels[static_cast<std::size_t>(editor.document().pixel_index(9, 4))]) == 255);

    EditorController repaired;
    repaired.new_document(8, 8);
    repaired.document().layers.clear();
    repaired.document().frames[0].cels.clear();
    repaired.tool().primary = rgba(0, 255, 0, 255);
    repaired.tool().tool = ToolType::Pencil;
    repaired.begin_stroke(1, 1, false, SelectionCombineMode::Replace);
    repaired.end_stroke(1, 1, false);
    assert(repaired.document().layers.size() == 1);
    assert(repaired.document().active_cel().pixels.size() == 64);
    assert(g(repaired.document().active_cel().pixels[static_cast<std::size_t>(repaired.document().pixel_index(1, 1))]) == 255);

    EditorController mover;
    mover.new_document(8, 8);
    mover.document().active_cel().pixels[static_cast<std::size_t>(mover.document().pixel_index(1, 1))] = rgba(0, 0, 255, 255);
    mover.document().selection.select_rect(1, 1, 1, 1, true);
    mover.tool().tool = ToolType::MovePixels;
    mover.begin_stroke(1, 1, false, SelectionCombineMode::Replace);
    assert(mover.document().floating_selection.active);
    assert(a(mover.document().active_cel().pixels[static_cast<std::size_t>(mover.document().pixel_index(1, 1))]) == 0);
    mover.update_stroke(3, 1, false);
    assert(mover.document().floating_selection.offset_x == 2);
    assert(a(mover.document().active_cel().pixels[static_cast<std::size_t>(mover.document().pixel_index(3, 1))]) == 0);
    mover.end_stroke(3, 1, false);
    assert(!mover.document().floating_selection.active);
    assert(b(mover.document().active_cel().pixels[static_cast<std::size_t>(mover.document().pixel_index(3, 1))]) == 255);

    EditorController transformer;
    transformer.new_document(12, 12);
    transformer.document().active_cel().pixels[
        static_cast<std::size_t>(transformer.document().pixel_index(2, 2))] = rgba(255, 0, 0, 255);
    transformer.document().active_cel().pixels[
        static_cast<std::size_t>(transformer.document().pixel_index(3, 2))] = rgba(0, 255, 0, 255);
    transformer.document().selection.select_rect(2, 2, 3, 2, SelectionCombineMode::Replace);
    assert(transformer.apply_selection_transform(2.0f, 1.0f, 0.0f));
    assert(!transformer.document().floating_selection.active);
    assert(r(editor_pixel(transformer, 2, 2)) == 255);
    assert(r(editor_pixel(transformer, 3, 2)) == 255);
    assert(g(editor_pixel(transformer, 4, 2)) == 255);
    assert(g(editor_pixel(transformer, 5, 2)) == 255);
    assert(transformer.document().selection.selected_count() == 4);
    assert(transformer.undo());
    assert(r(editor_pixel(transformer, 2, 2)) == 255);
    assert(g(editor_pixel(transformer, 3, 2)) == 255);
    assert(a(editor_pixel(transformer, 4, 2)) == 0);
    assert(transformer.document().selection.selected_count() == 2);
    assert(transformer.redo());
    assert(transformer.document().selection.selected_count() == 4);

    transformer.document().selection.select_rect(2, 2, 5, 2, SelectionCombineMode::Replace);
    assert(transformer.begin_selection_transform(SelectionTransformHandle::SouthEast, 5, 2));
    transformer.update_selection_transform(7, 3, false);
    assert(transformer.selection_transform_active());
    assert(transformer.end_selection_transform());
    assert(!transformer.selection_transform_active());
    assert(transformer.document().selection.selected_count() > 4);

    EditorController pressured_brush;
    pressured_brush.new_document(32, 32);
    pressured_brush.tool().tool = ToolType::Brush;
    pressured_brush.tool().primary = rgba(255, 0, 0, 255);
    pressured_brush.tool().brush_size = 9;
    pressured_brush.tool().brush_opacity = 0.5f;
    pressured_brush.tool().brush_hardness = 0.0f;
    pressured_brush.tool().pressure_controls_size = true;
    pressured_brush.tool().pressure_controls_opacity = true;
    pressured_brush.begin_stroke(16, 16, false, SelectionCombineMode::Replace, 0.25f);
    pressured_brush.end_stroke(16, 16, false, 0.0f);
    const int low_pressure_count = painted_pixels(pressured_brush);
    const int low_pressure_alpha = a(editor_pixel(pressured_brush, 16, 16));
    assert(low_pressure_count > 0);
    assert(low_pressure_alpha > 0 && low_pressure_alpha < 64);

    EditorController full_pressure_brush;
    full_pressure_brush.new_document(32, 32);
    full_pressure_brush.tool() = pressured_brush.tool();
    full_pressure_brush.begin_stroke(16, 16, false, SelectionCombineMode::Replace, 1.0f);
    full_pressure_brush.end_stroke(16, 16, false, 0.0f);
    assert(painted_pixels(full_pressure_brush) > low_pressure_count);
    assert(a(editor_pixel(full_pressure_brush, 16, 16)) > low_pressure_alpha);

    EditorController sparse_brush;
    sparse_brush.new_document(32, 12);
    sparse_brush.tool().tool = ToolType::Brush;
    sparse_brush.tool().primary = rgba(255, 255, 255, 255);
    sparse_brush.tool().brush_size = 4;
    sparse_brush.tool().brush_spacing = 2.0f;
    sparse_brush.tool().pressure_controls_size = false;
    sparse_brush.tool().pressure_controls_opacity = false;
    sparse_brush.begin_stroke(2, 6, false, SelectionCombineMode::Replace);
    sparse_brush.end_stroke(26, 6, false);
    assert(a(editor_pixel(sparse_brush, 6, 6)) == 0);

    EditorController dense_brush;
    dense_brush.new_document(32, 12);
    dense_brush.tool() = sparse_brush.tool();
    dense_brush.tool().brush_spacing = 0.25f;
    dense_brush.begin_stroke(2, 6, false, SelectionCombineMode::Replace);
    dense_brush.end_stroke(26, 6, false);
    assert(a(editor_pixel(dense_brush, 6, 6)) == 255);
    assert(painted_pixels(dense_brush) > painted_pixels(sparse_brush));

    EditorController smoothed_brush;
    smoothed_brush.new_document(32, 8);
    smoothed_brush.tool().tool = ToolType::Brush;
    smoothed_brush.tool().primary = rgba(255, 255, 255, 255);
    smoothed_brush.tool().brush_size = 1;
    smoothed_brush.tool().brush_spacing = 1.0f;
    smoothed_brush.tool().brush_smoothing = 0.9f;
    smoothed_brush.tool().pressure_controls_size = false;
    smoothed_brush.tool().pressure_controls_opacity = false;
    smoothed_brush.begin_stroke(2, 4, false, SelectionCombineMode::Replace);
    smoothed_brush.end_stroke(22, 4, false);
    assert(a(editor_pixel(smoothed_brush, 22, 4)) == 0);

    EditorController unsmoothed_brush;
    unsmoothed_brush.new_document(32, 8);
    unsmoothed_brush.tool() = smoothed_brush.tool();
    unsmoothed_brush.tool().brush_smoothing = 0.0f;
    unsmoothed_brush.begin_stroke(2, 4, false, SelectionCombineMode::Replace);
    unsmoothed_brush.end_stroke(22, 4, false);
    assert(a(editor_pixel(unsmoothed_brush, 22, 4)) == 255);

    EditorController pasted_mover;
    pasted_mover.new_document(10, 10);
    FloatingSelection pasted;
    pasted.active = true;
    pasted.source_x = 2;
    pasted.source_y = 2;
    pasted.width = 2;
    pasted.height = 1;
    pasted.pixels = {rgba(255, 0, 0, 255), rgba(0, 255, 0, 255)};
    pasted.mask = {1, 1};
    pasted_mover.begin_pasted_selection(std::move(pasted));
    assert(pasted_mover.document().floating_selection.active);
    assert(pasted_mover.document().active_cel().pixels[static_cast<std::size_t>(pasted_mover.document().pixel_index(2, 2))] == 0);
    pasted_mover.tool().tool = ToolType::MovePixels;
    pasted_mover.begin_stroke(2, 2, false, SelectionCombineMode::Replace);
    pasted_mover.update_stroke(4, 3, false);
    pasted_mover.end_stroke(4, 3, false);
    assert(pasted_mover.document().floating_selection.active);
    assert(pasted_mover.document().floating_selection.offset_x == 2);
    assert(pasted_mover.document().floating_selection.offset_y == 1);
    pasted_mover.begin_stroke(4, 3, false, SelectionCombineMode::Replace);
    pasted_mover.update_stroke(5, 5, false);
    pasted_mover.end_stroke(5, 5, false);
    assert(pasted_mover.document().floating_selection.active);
    assert(pasted_mover.document().floating_selection.offset_x == 3);
    assert(pasted_mover.document().floating_selection.offset_y == 3);
    pasted_mover.clear_selection();
    assert(!pasted_mover.document().floating_selection.active);
    assert(r(pasted_mover.document().active_cel().pixels[static_cast<std::size_t>(pasted_mover.document().pixel_index(5, 5))]) == 255);
    assert(g(pasted_mover.document().active_cel().pixels[static_cast<std::size_t>(pasted_mover.document().pixel_index(6, 5))]) == 255);

    EditorController onion;
    Document onion_document = Document::create(4, 4);
    onion_document.active_cel().pixels[static_cast<std::size_t>(onion_document.pixel_index(1, 1))] = rgba(220, 30, 20, 255);
    onion_document.add_frame(false);
    onion.replace_document(std::move(onion_document));
    assert(onion.document().active_frame == 1);
    const auto& previous = onion.onion_skin_pixels();
    assert(previous.size() == 16);
    assert(r(previous[static_cast<std::size_t>(onion.document().pixel_index(1, 1))]) == 220);
    assert(a(previous[static_cast<std::size_t>(onion.document().pixel_index(1, 1))]) == 255);
    onion.document().active_frame = 0;
    onion.invalidate_display();
    assert(onion.onion_skin_pixels().empty());

    EditorController model_editor;
    const std::size_t initial_cuboids = model_editor.model().cuboids.size();
    model_editor.add_cuboid();
    assert(model_editor.model().cuboids.size() == initial_cuboids + 1);
    assert(model_editor.undo());
    assert(model_editor.model().cuboids.size() == initial_cuboids);
    assert(model_editor.redo());
    assert(model_editor.model().cuboids.size() == initial_cuboids + 1);
    assert(model_editor.remove_selected_cuboid());
    assert(model_editor.model().cuboids.size() == initial_cuboids);
    assert(model_editor.undo());
    assert(model_editor.model().cuboids.size() == initial_cuboids + 1);

    std::cout << "Qt editor controller tests passed\n";
    return 0;
}
