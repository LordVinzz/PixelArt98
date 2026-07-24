// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Document.hpp"
#include "core/Filters.hpp"
#include "core/Model.hpp"
#include "core/Tools.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace px {

enum class SelectionTransformHandle {
    None,
    NorthWest,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    Rotate
};

// Toolkit-independent editor state shared by the Qt widgets and headless tests.
class EditorController {
public:
    EditorController();

    [[nodiscard]] Document& document() noexcept { return document_; }
    [[nodiscard]] const Document& document() const noexcept { return document_; }
    [[nodiscard]] ModelDocument& model() noexcept { return model_; }
    [[nodiscard]] const ModelDocument& model() const noexcept { return model_; }
    [[nodiscard]] ToolContext& tool() noexcept { return tool_; }
    [[nodiscard]] const ToolContext& tool() const noexcept { return tool_; }

    void new_document(int width, int height);
    void replace_document(Document document);
    void replace_project(Document document, ModelDocument model);
    void mark_changed(std::string status);
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    [[nodiscard]] bool modified() const noexcept { return modified_; }
    void set_modified(bool modified) noexcept { modified_ = modified; }

    [[nodiscard]] const std::vector<Pixel>& display_pixels();
    [[nodiscard]] const std::vector<Pixel>& onion_skin_pixels();
    void invalidate_display() noexcept;
    [[nodiscard]] std::uint64_t display_revision() const noexcept {
        return display_revision_;
    }
    [[nodiscard]] const std::array<int, 256>& histogram_luma();

    void begin_stroke(int x, int y, bool secondary, SelectionCombineMode selection_mode,
                      float pressure = 1.0f);
    void update_stroke(int x, int y, bool constrain, float pressure = 1.0f);
    void end_stroke(int x, int y, bool constrain, float pressure = 1.0f);
    void cancel_interaction();
    [[nodiscard]] bool begin_selection_transform(SelectionTransformHandle handle, int x, int y);
    void update_selection_transform(int x, int y, bool constrain);
    [[nodiscard]] bool end_selection_transform();
    [[nodiscard]] bool apply_selection_transform(float scale_x, float scale_y,
                                                 float angle_degrees);
    [[nodiscard]] bool selection_transform_active() const noexcept {
        return selection_transform_active_;
    }

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void add_cuboid();
    [[nodiscard]] bool remove_selected_cuboid();
    void commit_model_edit(const std::string& name, ModelDocument before_model);
    void select_all();
    void clear_selection();
    void invert_selection();
    void delete_selection();
    void nudge_selection(int dx, int dy);
    [[nodiscard]] bool expand_selection(int radius);
    [[nodiscard]] bool contract_selection(int radius);
    [[nodiscard]] bool border_selection(int radius);
    [[nodiscard]] bool smooth_selection(int radius);
    void begin_pasted_selection(FloatingSelection selection);
    [[nodiscard]] bool commit_pasted_selection();
    void discard_pasted_selection();

    // Memory-test seam retained from the previous UI owner.
    [[nodiscard]] bool huge_document_history_mode() const;
    [[nodiscard]] bool display_uses_active_cel() const;
    [[nodiscard]] std::size_t display_pixel_capacity() const;
    [[nodiscard]] bool histogram_approximate() const noexcept { return histogram_approximate_; }
    [[nodiscard]] const std::vector<std::array<int, 2>>& lasso_points() const noexcept { return lasso_points_; }

private:
    [[nodiscard]] Pixel interaction_color(bool secondary) const;
    void apply_immediate_tool(int x, int y, bool secondary, SelectionCombineMode selection_mode);
    void commit_drag_tool(int x, int y, bool constrain);
    void clear_tiny_selection();
    void rebuild_histogram();
    void sync_selection_to_floating();
    void stamp_active_brush(float x, float y, float pressure);
    void continue_active_brush(float x, float y, float pressure);

    Document document_ = Document::create(64, 64);
    ModelDocument model_ = ModelDocument::create_default();
    ToolContext tool_;
    std::vector<Pixel> display_pixels_;
    std::vector<Pixel> onion_skin_pixels_;
    std::vector<Pixel> interaction_before_;
    SelectionMask selection_before_;
    std::vector<std::array<int, 2>> lasso_points_;
    std::array<int, 256> histogram_luma_{};
    bool display_dirty_ = true;
    bool onion_skin_dirty_ = true;
    int onion_skin_frame_ = -1;
    bool histogram_dirty_ = true;
    bool histogram_approximate_ = false;
    bool interaction_active_ = false;
    std::uint64_t display_revision_ = 1;
    bool selection_transform_active_ = false;
    SelectionTransformHandle selection_transform_handle_ = SelectionTransformHandle::None;
    FloatingSelection selection_transform_source_;
    FloatingSelection selection_transform_before_floating_;
    double selection_transform_start_angle_ = 0.0;
    bool stroke_tool_active_ = false;
    bool secondary_interaction_ = false;
    int interaction_start_x_ = 0;
    int interaction_start_y_ = 0;
    int interaction_last_x_ = 0;
    int interaction_last_y_ = 0;
    int interaction_floating_start_offset_x_ = 0;
    int interaction_floating_start_offset_y_ = 0;
    float brush_path_x_ = 0.0f;
    float brush_path_y_ = 0.0f;
    float brush_distance_since_stamp_ = 0.0f;
    float brush_last_pressure_ = 1.0f;
    SelectionCombineMode selection_mode_ = SelectionCombineMode::Replace;
    std::vector<Pixel> pasted_selection_before_;
    bool pasted_selection_active_ = false;
    bool modified_ = false;
    std::string status_ = "Ready";
};

} // namespace px
