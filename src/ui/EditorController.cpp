// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/EditorController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
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
    pasted_selection_before_.clear();
    pasted_selection_active_ = document_.floating_selection.active;
    if (pasted_selection_active_ && document_.has_active_cel()) {
        pasted_selection_before_ = document_.snapshot_active_cel();
    }
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

void EditorController::begin_stroke(int x, int y, bool secondary, SelectionCombineMode selection_mode,
                                    float pressure) {
    if (!document_.in_bounds(x, y)) return;
    if (!document_.ensure_active_cel()) return;
    if (pasted_selection_active_ && tool_.tool != ToolType::MovePixels) (void)commit_pasted_selection();
    interaction_active_ = true;
    secondary_interaction_ = secondary;
    selection_mode_ = selection_mode;
    interaction_start_x_ = interaction_last_x_ = x;
    interaction_start_y_ = interaction_last_y_ = y;
    interaction_floating_start_offset_x_ = document_.floating_selection.offset_x;
    interaction_floating_start_offset_y_ = document_.floating_selection.offset_y;
    interaction_before_ = document_.snapshot_active_cel();
    selection_before_ = document_.selection;
    lasso_points_.clear();
    stroke_tool_active_ = tool_.tool == ToolType::Pencil || tool_.tool == ToolType::Brush || tool_.tool == ToolType::Eraser || tool_.tool == ToolType::CloneStamp;
    brush_path_x_ = static_cast<float>(x);
    brush_path_y_ = static_cast<float>(y);
    brush_distance_since_stamp_ = 0.0f;
    brush_last_pressure_ = std::clamp(pressure, 0.0f, 1.0f);
    apply_immediate_tool(x, y, secondary, selection_mode);
}

void EditorController::stamp_active_brush(float x, float y, float pressure) {
    pressure = std::clamp(pressure, 0.0f, 1.0f);
    const bool pencil = tool_.tool == ToolType::Pencil;
    const bool eraser = tool_.tool == ToolType::Eraser;
    const float size_pressure = !pencil && tool_.pressure_controls_size
                                    ? 0.15f + pressure * 0.85f : 1.0f;
    const int size = pencil ? 1 : std::max(1, static_cast<int>(std::lround(
        static_cast<float>(tool_.brush_size) * size_pressure)));
    const float opacity_pressure = !pencil && tool_.pressure_controls_opacity ? pressure : 1.0f;
    const float opacity = pencil ? 1.0f : std::clamp(tool_.brush_opacity * opacity_pressure,
                                                     0.0f, 1.0f);
    plot_brush_raw(document_, static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
                   interaction_color(secondary_interaction_), size, eraser, opacity,
                   pencil ? 1.0f : tool_.brush_hardness);
}

void EditorController::continue_active_brush(float x, float y, float pressure) {
    pressure = std::clamp(pressure, 0.0f, 1.0f);
    const float smoothing = std::clamp(tool_.brush_smoothing, 0.0f, 1.0f);
    const float smoothing_factor = 1.0f - smoothing * 0.9f;
    const float target_x = brush_path_x_ + (x - brush_path_x_) * smoothing_factor;
    const float target_y = brush_path_y_ + (y - brush_path_y_) * smoothing_factor;
    const float delta_x = target_x - brush_path_x_;
    const float delta_y = target_y - brush_path_y_;
    const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    if (distance < 0.0001f) {
        brush_last_pressure_ = pressure;
        return;
    }
    const float spacing = std::max(1.0f, static_cast<float>(std::max(1, tool_.brush_size)) *
                                           std::clamp(tool_.brush_spacing, 0.01f, 2.0f));
    float next = spacing - brush_distance_since_stamp_;
    while (next <= distance + 0.0001f) {
        const float t = std::clamp(next / distance, 0.0f, 1.0f);
        const float interpolated_pressure = brush_last_pressure_ +
                                            (pressure - brush_last_pressure_) * t;
        stamp_active_brush(brush_path_x_ + delta_x * t, brush_path_y_ + delta_y * t,
                           interpolated_pressure);
        next += spacing;
    }
    brush_distance_since_stamp_ = std::fmod(brush_distance_since_stamp_ + distance, spacing);
    brush_path_x_ = target_x;
    brush_path_y_ = target_y;
    brush_last_pressure_ = pressure;
}

void EditorController::apply_immediate_tool(int x, int y, bool secondary, SelectionCombineMode selection_mode) {
    const Pixel color = interaction_color(secondary);
    switch (tool_.tool) {
        case ToolType::Pencil:
        case ToolType::Brush:
        case ToolType::Eraser:
            secondary_interaction_ = secondary;
            stamp_active_brush(static_cast<float>(x), static_cast<float>(y), brush_last_pressure_);
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
            if (!document_.floating_selection.active) {
                if (!document_.selection.active) document_.selection.select_all();
                document_.begin_floating_selection();
            }
            break;
        case ToolType::Text:
            interaction_active_ = false;
            break;
        case ToolType::Line:
        case ToolType::Rectangle:
        case ToolType::Ellipse:
        case ToolType::Gradient:
        case ToolType::RectSelect:
        case ToolType::EllipseSelect:
            break;
    }
    invalidate_display();
}

void EditorController::update_stroke(int x, int y, bool, float pressure) {
    if (!interaction_active_ || !document_.in_bounds(x, y)) return;
    bool display_changed = false;
    if (stroke_tool_active_) {
        if (tool_.tool == ToolType::Pencil || tool_.tool == ToolType::Brush ||
            tool_.tool == ToolType::Eraser) {
            continue_active_brush(static_cast<float>(x), static_cast<float>(y), pressure);
        } else if (tool_.tool == ToolType::CloneStamp && tool_.clone_source_x >= 0) {
            clone_stamp_raw(document_, interaction_before_, tool_.clone_source_x, tool_.clone_source_y, x, y, tool_.brush_size);
        }
        display_changed = true;
    } else if (tool_.tool == ToolType::LassoSelect) {
        if (lasso_points_.empty() || lasso_points_.back() != std::array<int, 2>{x, y}) lasso_points_.push_back({x, y});
    } else if (tool_.tool == ToolType::MovePixels) {
        document_.move_floating_selection(interaction_floating_start_offset_x_ + x - interaction_start_x_,
                                          interaction_floating_start_offset_y_ + y - interaction_start_y_);
        if (pasted_selection_active_) sync_selection_to_floating();
    }
    interaction_last_x_ = x;
    interaction_last_y_ = y;
    if (display_changed) invalidate_display();
}

void EditorController::end_stroke(int x, int y, bool constrain, float pressure) {
    if (!interaction_active_) return;
    if (tool_.tool == ToolType::Pencil || tool_.tool == ToolType::Brush ||
        tool_.tool == ToolType::Eraser) {
        // TabletRelease commonly reports zero pressure after the pen has left
        // the surface. Preserve the last real sample for the tail of the stroke.
        if (pressure <= 0.0f) pressure = brush_last_pressure_;
        continue_active_brush(static_cast<float>(x), static_cast<float>(y), pressure);
    }
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
        case ToolType::EllipseSelect:
            if (interaction_start_x_ == x && interaction_start_y_ == y) {
                document_.selection.clear();
            } else {
                document_.selection.select_ellipse(interaction_start_x_, interaction_start_y_, x, y,
                                                   selection_mode_);
                clear_tiny_selection();
            }
            document_.commit_selection_edit("Ellipse Selection", selection_before_);
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
            if (!pasted_selection_active_) {
                document_.commit_floating_selection("Move Pixels", std::move(interaction_before_));
            }
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
    if (selection_transform_active_) {
        if (pasted_selection_active_) {
            document_.floating_selection = selection_transform_before_floating_;
        } else {
            document_.active_cel().pixels = std::move(interaction_before_);
            document_.floating_selection.clear();
        }
        document_.selection = selection_before_;
        selection_transform_active_ = false;
        selection_transform_handle_ = SelectionTransformHandle::None;
        interaction_active_ = false;
        invalidate_display();
        return;
    }
    if (pasted_selection_active_ && document_.floating_selection.active) {
        document_.move_floating_selection(interaction_floating_start_offset_x_,
                                          interaction_floating_start_offset_y_);
        sync_selection_to_floating();
        interaction_active_ = false;
        invalidate_display();
        return;
    }
    document_.active_cel().pixels = std::move(interaction_before_);
    document_.selection = selection_before_;
    document_.cancel_floating_selection();
    interaction_active_ = false;
    invalidate_display();
}

bool EditorController::begin_selection_transform(SelectionTransformHandle handle, int x, int y) {
    if (handle == SelectionTransformHandle::None || !document_.has_active_cel()) return false;
    if (!document_.floating_selection.active && !document_.selection.active) return false;
    if (pasted_selection_active_ && !document_.floating_selection.active) return false;
    interaction_before_ = document_.snapshot_active_cel();
    selection_before_ = document_.selection;
    selection_transform_before_floating_ = document_.floating_selection;
    if (!document_.floating_selection.active && !document_.begin_floating_selection()) return false;
    selection_transform_source_ = document_.floating_selection;
    selection_transform_handle_ = handle;
    selection_transform_active_ = true;
    interaction_active_ = true;
    const double left = static_cast<double>(selection_transform_source_.source_x +
                                            selection_transform_source_.offset_x);
    const double top = static_cast<double>(selection_transform_source_.source_y +
                                           selection_transform_source_.offset_y);
    const double center_x = left + static_cast<double>(selection_transform_source_.width) * 0.5;
    const double center_y = top + static_cast<double>(selection_transform_source_.height) * 0.5;
    selection_transform_start_angle_ = std::atan2(static_cast<double>(y) + 0.5 - center_y,
                                                   static_cast<double>(x) + 0.5 - center_x);
    invalidate_display();
    return true;
}

void EditorController::update_selection_transform(int x, int y, bool constrain) {
    if (!selection_transform_active_) return;
    const FloatingSelection& source = selection_transform_source_;
    const int original_left = source.source_x + source.offset_x;
    const int original_top = source.source_y + source.offset_y;
    const int original_right = original_left + source.width - 1;
    const int original_bottom = original_top + source.height - 1;
    if (selection_transform_handle_ == SelectionTransformHandle::Rotate) {
        const double center_x = static_cast<double>(original_left) + static_cast<double>(source.width) * 0.5;
        const double center_y = static_cast<double>(original_top) + static_cast<double>(source.height) * 0.5;
        const double current = std::atan2(static_cast<double>(y) + 0.5 - center_y,
                                          static_cast<double>(x) + 0.5 - center_x);
        double angle = (current - selection_transform_start_angle_) * 180.0 / std::numbers::pi;
        if (constrain) angle = std::round(angle / 15.0) * 15.0;
        document_.floating_selection = rotate_floating_selection(source, static_cast<float>(angle));
        invalidate_display();
        return;
    }

    int left = original_left;
    int top = original_top;
    int right = original_right;
    int bottom = original_bottom;
    const bool west = selection_transform_handle_ == SelectionTransformHandle::West ||
                      selection_transform_handle_ == SelectionTransformHandle::NorthWest ||
                      selection_transform_handle_ == SelectionTransformHandle::SouthWest;
    const bool east = selection_transform_handle_ == SelectionTransformHandle::East ||
                      selection_transform_handle_ == SelectionTransformHandle::NorthEast ||
                      selection_transform_handle_ == SelectionTransformHandle::SouthEast;
    const bool north = selection_transform_handle_ == SelectionTransformHandle::North ||
                       selection_transform_handle_ == SelectionTransformHandle::NorthWest ||
                       selection_transform_handle_ == SelectionTransformHandle::NorthEast;
    const bool south = selection_transform_handle_ == SelectionTransformHandle::South ||
                       selection_transform_handle_ == SelectionTransformHandle::SouthWest ||
                       selection_transform_handle_ == SelectionTransformHandle::SouthEast;
    if (west) left = std::min(x, right);
    if (east) right = std::max(x, left);
    if (north) top = std::min(y, bottom);
    if (south) bottom = std::max(y, top);
    int width = std::max(1, right - left + 1);
    int height = std::max(1, bottom - top + 1);
    if (constrain && (west || east) && (north || south)) {
        const double aspect = static_cast<double>(source.width) / static_cast<double>(source.height);
        if (static_cast<double>(width) / static_cast<double>(height) > aspect) {
            height = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) / aspect)));
            if (north) top = bottom - height + 1;
            else bottom = top + height - 1;
        } else {
            width = std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * aspect)));
            if (west) left = right - width + 1;
            else right = left + width - 1;
        }
    }
    document_.floating_selection = scale_floating_selection(source, left, top, width, height);
    invalidate_display();
}

bool EditorController::end_selection_transform() {
    if (!selection_transform_active_) return false;
    selection_transform_active_ = false;
    selection_transform_handle_ = SelectionTransformHandle::None;
    interaction_active_ = false;
    if (pasted_selection_active_) {
        sync_selection_to_floating();
    } else {
        document_.commit_floating_selection("Transform Selection", std::move(interaction_before_));
    }
    mark_changed("Transform Selection");
    return true;
}

bool EditorController::apply_selection_transform(float scale_x, float scale_y,
                                                 float angle_degrees) {
    scale_x = std::max(0.01f, scale_x);
    scale_y = std::max(0.01f, scale_y);
    if (std::abs(scale_x - 1.0f) < 0.0001f && std::abs(scale_y - 1.0f) < 0.0001f &&
        std::abs(angle_degrees) < 0.0001f) return false;
    if (!document_.floating_selection.active && !document_.selection.active) return false;
    interaction_before_ = document_.snapshot_active_cel();
    selection_before_ = document_.selection;
    if (!document_.floating_selection.active && !document_.begin_floating_selection()) return false;
    const FloatingSelection source = document_.floating_selection;
    const int left = source.source_x + source.offset_x;
    const int top = source.source_y + source.offset_y;
    const int width = std::max(1, static_cast<int>(std::lround(source.width * scale_x)));
    const int height = std::max(1, static_cast<int>(std::lround(source.height * scale_y)));
    FloatingSelection transformed = scale_floating_selection(source, left, top, width, height);
    if (std::abs(angle_degrees) >= 0.0001f) {
        transformed = rotate_floating_selection(transformed, angle_degrees);
    }
    document_.floating_selection = std::move(transformed);
    if (pasted_selection_active_) sync_selection_to_floating();
    else document_.commit_floating_selection("Transform Selection", std::move(interaction_before_));
    mark_changed("Transform Selection");
    return true;
}

bool EditorController::undo() { if (pasted_selection_active_) (void)commit_pasted_selection(); const bool changed = document_.undo(&model_); if (changed) mark_changed("Undo"); return changed; }
bool EditorController::redo() { if (pasted_selection_active_) (void)commit_pasted_selection(); const bool changed = document_.redo(&model_); if (changed) mark_changed("Redo"); return changed; }
void EditorController::add_cuboid() { ModelDocument before = model_; model_.add_cuboid(); document_.commit_model_edit("Add Cuboid", std::move(before), model_); mark_changed("Add Cuboid"); }
bool EditorController::remove_selected_cuboid() { ModelDocument before = model_; if (!model_.remove_selected()) return false; document_.commit_model_edit("Remove Cuboid", std::move(before), model_); mark_changed("Remove Cuboid"); return true; }
void EditorController::commit_model_edit(const std::string& name, ModelDocument before_model) { document_.commit_model_edit(name, std::move(before_model), model_); mark_changed(name); }
void EditorController::select_all() { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.select_all(); document_.commit_selection_edit("Select All", before); mark_changed("Select All"); }
void EditorController::clear_selection() { if (pasted_selection_active_) (void)commit_pasted_selection(); if (!document_.selection.active && document_.selection.selected_count() == 0) return; const auto before = document_.selection; document_.selection.clear(); document_.commit_selection_edit("Clear Selection", before); mark_changed("Clear Selection"); }
void EditorController::invert_selection() { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.invert(); document_.commit_selection_edit("Invert Selection", before); mark_changed("Invert Selection"); }
void EditorController::delete_selection() { if (pasted_selection_active_) (void)commit_pasted_selection(); if (document_.delete_selected_pixels()) mark_changed("Delete Selection"); }
void EditorController::nudge_selection(int dx, int dy) { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.translate(dx, dy); document_.commit_selection_edit("Nudge Selection", before); mark_changed("Nudge Selection"); }
bool EditorController::expand_selection(int radius) { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.expand(radius); if (before.mask == document_.selection.mask) return false; document_.commit_selection_edit("Expand Selection", before); mark_changed("Expand Selection"); return true; }
bool EditorController::contract_selection(int radius) { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.contract(radius); if (before.mask == document_.selection.mask) return false; document_.commit_selection_edit("Contract Selection", before); mark_changed("Contract Selection"); return true; }
bool EditorController::border_selection(int radius) { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.select_border(radius); if (before.mask == document_.selection.mask) return false; document_.commit_selection_edit("Border Selection", before); mark_changed("Border Selection"); return true; }
bool EditorController::smooth_selection(int radius) { if (pasted_selection_active_) (void)commit_pasted_selection(); const auto before = document_.selection; document_.selection.smooth(radius); if (before.mask == document_.selection.mask) return false; document_.commit_selection_edit("Smooth Selection", before); mark_changed("Smooth Selection"); return true; }

void EditorController::begin_pasted_selection(FloatingSelection selection) {
    if (pasted_selection_active_) (void)commit_pasted_selection();
    if (!document_.ensure_active_cel() || !selection.active || selection.width <= 0 || selection.height <= 0) return;
    pasted_selection_before_ = document_.snapshot_active_cel();
    document_.floating_selection = std::move(selection);
    sync_selection_to_floating();
    pasted_selection_active_ = true;
    mark_changed("Paste");
}

void EditorController::sync_selection_to_floating() {
    document_.selection.clear();
    const FloatingSelection& floating = document_.floating_selection;
    bool any_selected = false;
    for (int y = 0; y < floating.height; ++y) {
        for (int x = 0; x < floating.width; ++x) {
            const std::size_t local = static_cast<std::size_t>(y * floating.width + x);
            if (local >= floating.mask.size() || floating.mask[local] == 0) continue;
            const int destination_x = floating.source_x + floating.offset_x + x;
            const int destination_y = floating.source_y + floating.offset_y + y;
            if (!document_.in_bounds(destination_x, destination_y)) continue;
            document_.selection.mask[static_cast<std::size_t>(document_.pixel_index(destination_x, destination_y))] = 1;
            any_selected = true;
        }
    }
    document_.selection.active = any_selected;
}

bool EditorController::commit_pasted_selection() {
    if (!pasted_selection_active_) return false;
    if (document_.floating_selection.active) {
        document_.commit_floating_selection("Paste", std::move(pasted_selection_before_));
    }
    pasted_selection_before_.clear();
    pasted_selection_active_ = false;
    invalidate_display();
    return true;
}

void EditorController::discard_pasted_selection() {
    if (!pasted_selection_active_) return;
    document_.floating_selection.clear();
    document_.selection.clear();
    pasted_selection_before_.clear();
    pasted_selection_active_ = false;
    mark_changed("Cut");
}

bool EditorController::huge_document_history_mode() const {
    const std::size_t threshold = environment_limit("PIXELART_HUGE_DOCUMENT_PIXEL_THRESHOLD", 64U * 1024U * 1024U);
    return static_cast<std::size_t>(document_.width) * static_cast<std::size_t>(document_.height) >= threshold;
}
bool EditorController::display_uses_active_cel() const { return huge_document_history_mode() && document_.layers.size() == 1U; }
std::size_t EditorController::display_pixel_capacity() const { return display_pixels_.capacity(); }

} // namespace px
