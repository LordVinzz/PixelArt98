// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/EditorController.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace px {

namespace {

std::size_t environment_limit(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr) return fallback;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    return end == value ? fallback : static_cast<std::size_t>(parsed);
}

} // namespace

EditorController::EditorController() = default;

void EditorController::new_document(int width, int height) {
    replace_project(Document::create(width, height), ModelDocument::create_default());
    modified_ = false;
    status_ = "New document";
}

void EditorController::replace_document(Document document) {
    replace_project(std::move(document), ModelDocument::create_default());
}

void EditorController::replace_project(Document document, ModelDocument model) {
    document_ = std::move(document);
    model_ = std::move(model);
    model_.texture_width = document_.width;
    model_.texture_height = document_.height;
    clamp_model_uvs(model_);
    display_pixels_.clear();
    display_pixels_.shrink_to_fit();
    onion_skin_pixels_.clear();
    onion_skin_pixels_.shrink_to_fit();
    onion_skin_frame_ = -1;
    interaction_before_.clear();
    display_dirty_ = true;
    onion_skin_dirty_ = true;
    histogram_dirty_ = true;
    modified_ = false;
}

void EditorController::mark_changed(std::string status) {
    status_ = std::move(status);
    modified_ = true;
    display_dirty_ = true;
    onion_skin_dirty_ = true;
    histogram_dirty_ = true;
}

void EditorController::invalidate_display() noexcept {
    display_dirty_ = true;
}

const std::vector<Pixel>& EditorController::display_pixels() {
    if (huge_document_history_mode() && document_.layers.size() == 1U && document_.active_frame >= 0) {
        return document_.active_cel().pixels;
    }
    if (display_dirty_) {
        display_pixels_ = document_.composite_active();
        display_dirty_ = false;
    }
    return display_pixels_;
}

const std::vector<Pixel>& EditorController::onion_skin_pixels() {
    const int previous_frame = document_.active_frame - 1;
    if (previous_frame < 0 || previous_frame >= static_cast<int>(document_.frames.size())) {
        onion_skin_pixels_.clear();
        onion_skin_frame_ = -1;
        onion_skin_dirty_ = false;
        return onion_skin_pixels_;
    }

    if (huge_document_history_mode() && document_.layers.size() == 1U &&
        !document_.frames[static_cast<std::size_t>(previous_frame)].cels.empty()) {
        return document_.frames[static_cast<std::size_t>(previous_frame)].cels[0].pixels;
    }

    if (onion_skin_dirty_ || onion_skin_frame_ != previous_frame) {
        onion_skin_pixels_ = document_.composite_frame(previous_frame);
        onion_skin_frame_ = previous_frame;
        onion_skin_dirty_ = false;
    }
    return onion_skin_pixels_;
}

const std::array<int, 256>& EditorController::histogram_luma() {
    if (histogram_dirty_) rebuild_histogram();
    return histogram_luma_;
}

void EditorController::rebuild_histogram() {
    histogram_luma_.fill(0);
    const auto& pixels = display_pixels();
    const std::size_t limit = environment_limit("PIXELART_HISTOGRAM_SAMPLE_LIMIT", 4U * 1024U * 1024U);
    const std::size_t stride = pixels.size() > limit ? std::max<std::size_t>(1U, pixels.size() / limit) : 1U;
    histogram_approximate_ = stride > 1U;
    for (std::size_t index = 0; index < pixels.size(); index += stride) {
        const Pixel pixel = pixels[index];
        if (a(pixel) == 0) continue;
        const int bin = std::clamp(static_cast<int>(luminance(pixel) + 0.5f), 0, 255);
        ++histogram_luma_[static_cast<std::size_t>(bin)];
    }
    histogram_dirty_ = false;
}

Pixel EditorController::interaction_color(bool secondary) const {
    return secondary ? tool_.secondary : tool_.primary;
}

void EditorController::clear_tiny_selection() {
    if (document_.selection.selected_count() <= 1) {
        document_.selection.clear();
    }
}

void EditorController::begin_stroke(int x, int y, bool secondary, SelectionCombineMode selection_mode) {
    if (!document_.in_bounds(x, y)) return;
    if (!document_.ensure_active_cel()) return;
    interaction_active_ = true;
    secondary_interaction_ = secondary;
    selection_mode_ = selection_mode;
    interaction_start_x_ = interaction_last_x_ = x;
    interaction_start_y_ = interaction_last_y_ = y;
    interaction_before_ = document_.snapshot_active_cel();
    selection_before_ = document_.selection;
    lasso_points_.clear();
    stroke_tool_active_ = tool_.tool == ToolType::Pencil || tool_.tool == ToolType::Brush || tool_.tool == ToolType::Eraser || tool_.tool == ToolType::CloneStamp;
    apply_immediate_tool(x, y, secondary, selection_mode);
}

void EditorController::apply_immediate_tool(int x, int y, bool secondary, SelectionCombineMode selection_mode) {
    const Pixel color = interaction_color(secondary);
    switch (tool_.tool) {
        case ToolType::Pencil:
        case ToolType::Brush:
        case ToolType::Eraser:
            plot_brush_raw(document_, x, y, color, tool_.tool == ToolType::Pencil ? 1 : tool_.brush_size, tool_.tool == ToolType::Eraser);
            break;
        case ToolType::Bucket:
            fill_bucket(document_, x, y, color, tool_.tolerance, tool_.contiguous);
            interaction_active_ = false;
            document_.commit_active_cel_edit("Bucket", std::move(interaction_before_));
            break;
        case ToolType::Eyedropper: {
            const auto picked = pick_color(document_, x, y);
            if (picked.has_value()) (secondary ? tool_.secondary : tool_.primary) = *picked;
            interaction_active_ = false;
            break;
        }
        case ToolType::MagicWand:
            magic_wand(document_, x, y, tool_.tolerance, tool_.contiguous, selection_mode);
            clear_tiny_selection();
            document_.commit_selection_edit("Magic Wand", selection_before_);
            interaction_active_ = false;
            break;
        case ToolType::CloneStamp:
            if (secondary) {
                tool_.clone_source_x = x;
                tool_.clone_source_y = y;
                interaction_active_ = false;
            } else if (tool_.clone_source_x >= 0) {
                clone_stamp_raw(document_, interaction_before_, tool_.clone_source_x, tool_.clone_source_y, x, y, tool_.brush_size);
            }
            break;
        case ToolType::LassoSelect:
            lasso_points_.push_back({x, y});
            break;
        case ToolType::MovePixels:
            if (!document_.selection.active) document_.selection.select_all();
            document_.begin_floating_selection();
            break;
        case ToolType::Text:
            stamp_text(document_, x, y, "TEXT", color);
            document_.commit_active_cel_edit("Text", std::move(interaction_before_));
            interaction_active_ = false;
            break;
        case ToolType::Line:
        case ToolType::Rectangle:
        case ToolType::Ellipse:
        case ToolType::Gradient:
        case ToolType::RectSelect:
            break;
    }
    invalidate_display();
}

void EditorController::update_stroke(int x, int y, bool) {
    if (!interaction_active_ || !document_.in_bounds(x, y)) return;
    bool display_changed = false;
    if (stroke_tool_active_) {
        if (tool_.tool == ToolType::CloneStamp && tool_.clone_source_x >= 0) {
            clone_stamp_raw(document_, interaction_before_, tool_.clone_source_x, tool_.clone_source_y, x, y, tool_.brush_size);
        } else {
            const int size = tool_.tool == ToolType::Pencil ? 1 : tool_.brush_size;
            draw_line_raw(document_, interaction_last_x_, interaction_last_y_, x, y, interaction_color(secondary_interaction_), size, tool_.tool == ToolType::Eraser);
        }
        display_changed = true;
    } else if (tool_.tool == ToolType::LassoSelect) {
        if (lasso_points_.empty() || lasso_points_.back() != std::array<int, 2>{x, y}) lasso_points_.push_back({x, y});
    } else if (tool_.tool == ToolType::MovePixels) {
        document_.move_floating_selection(x - interaction_start_x_, y - interaction_start_y_);
    }
    interaction_last_x_ = x;
    interaction_last_y_ = y;
    if (display_changed) invalidate_display();
}

void EditorController::end_stroke(int x, int y, bool constrain) {
    if (!interaction_active_) return;
    commit_drag_tool(x, y, constrain);
    interaction_active_ = false;
    stroke_tool_active_ = false;
    mark_changed(tool_name(tool_.tool));
}

void EditorController::commit_drag_tool(int x, int y, bool constrain) {
    const auto endpoint = constrained_tool_endpoint(tool_.tool, interaction_start_x_, interaction_start_y_, x, y, constrain);
    x = endpoint[0];
    y = endpoint[1];
    const Pixel color = interaction_color(secondary_interaction_);
    switch (tool_.tool) {
        case ToolType::Pencil:
        case ToolType::Brush:
        case ToolType::Eraser:
        case ToolType::CloneStamp:
            document_.commit_active_cel_edit(tool_name(tool_.tool), std::move(interaction_before_));
            break;
        case ToolType::Line:
            draw_line_raw(document_, interaction_start_x_, interaction_start_y_, x, y, color, tool_.brush_size, false);
            document_.commit_active_cel_edit("Line", std::move(interaction_before_));
            break;
        case ToolType::Rectangle:
            draw_rect_raw(document_, interaction_start_x_, interaction_start_y_, x, y, color, tool_.brush_size, false);
            document_.commit_active_cel_edit("Rectangle", std::move(interaction_before_));
            break;
        case ToolType::Ellipse:
            draw_ellipse_raw(document_, interaction_start_x_, interaction_start_y_, x, y, color, tool_.brush_size, false);
            document_.commit_active_cel_edit("Ellipse", std::move(interaction_before_));
            break;
        case ToolType::Gradient:
            fill_gradient_raw(document_, interaction_start_x_, interaction_start_y_, x, y, tool_.primary, tool_.secondary);
            document_.commit_active_cel_edit("Gradient", std::move(interaction_before_));
            break;
        case ToolType::RectSelect:
            if (interaction_start_x_ == x && interaction_start_y_ == y) {
                document_.selection.clear();
            } else {
                document_.selection.select_rect(interaction_start_x_, interaction_start_y_, x, y, selection_mode_);
                clear_tiny_selection();
            }
            document_.commit_selection_edit("Rectangle Selection", selection_before_);
            break;
        case ToolType::LassoSelect:
            if (lasso_points_.size() < 3U) {
                document_.selection.clear();
            } else {
                document_.selection.select_polygon(lasso_points_, selection_mode_);
                clear_tiny_selection();
            }
            document_.commit_selection_edit("Lasso Selection", selection_before_);
            break;
        case ToolType::MovePixels:
            document_.commit_floating_selection("Move Pixels", std::move(interaction_before_));
            break;
        case ToolType::Bucket:
        case ToolType::Eyedropper:
        case ToolType::MagicWand:
        case ToolType::Text:
            break;
    }
}

void EditorController::cancel_interaction() {
    if (!interaction_active_) return;
    document_.active_cel().pixels = std::move(interaction_before_);
    document_.selection = selection_before_;
    document_.cancel_floating_selection();
    interaction_active_ = false;
    invalidate_display();
}

bool EditorController::undo() { const bool changed = document_.undo(); if (changed) mark_changed("Undo"); return changed; }
bool EditorController::redo() { const bool changed = document_.redo(); if (changed) mark_changed("Redo"); return changed; }
void EditorController::select_all() { const auto before = document_.selection; document_.selection.select_all(); document_.commit_selection_edit("Select All", before); mark_changed("Select All"); }
void EditorController::clear_selection() { if (!document_.selection.active && document_.selection.selected_count() == 0) return; const auto before = document_.selection; document_.selection.clear(); document_.commit_selection_edit("Clear Selection", before); mark_changed("Clear Selection"); }
void EditorController::invert_selection() { const auto before = document_.selection; document_.selection.invert(); document_.commit_selection_edit("Invert Selection", before); mark_changed("Invert Selection"); }
void EditorController::delete_selection() { if (document_.delete_selected_pixels()) mark_changed("Delete Selection"); }
void EditorController::nudge_selection(int dx, int dy) { const auto before = document_.selection; document_.selection.translate(dx, dy); document_.commit_selection_edit("Nudge Selection", before); mark_changed("Nudge Selection"); }

bool EditorController::huge_document_history_mode() const {
    const std::size_t threshold = environment_limit("PIXELART_HUGE_DOCUMENT_PIXEL_THRESHOLD", 64U * 1024U * 1024U);
    return static_cast<std::size_t>(document_.width) * static_cast<std::size_t>(document_.height) >= threshold;
}
bool EditorController::display_uses_active_cel() const { return huge_document_history_mode() && document_.layers.size() == 1U; }
std::size_t EditorController::display_pixel_capacity() const { return display_pixels_.capacity(); }

} // namespace px
