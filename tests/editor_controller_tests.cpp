// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/EditorController.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>

using namespace px;

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

    std::cout << "Qt editor controller tests passed\n";
    return 0;
}
