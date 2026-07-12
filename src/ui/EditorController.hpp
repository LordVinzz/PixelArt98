// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Document.hpp"
#include "core/Filters.hpp"
#include "core/Model.hpp"
#include "core/Tools.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace px {

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
    void invalidate_display() noexcept { display_dirty_ = true; }
    [[nodiscard]] const std::array<int, 256>& histogram_luma();

    void begin_stroke(int x, int y, bool secondary, SelectionCombineMode selection_mode);
    void update_stroke(int x, int y, bool constrain);
    void end_stroke(int x, int y, bool constrain);
    void cancel_interaction();

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void select_all();
    void clear_selection();
    void invert_selection();
    void delete_selection();
    void nudge_selection(int dx, int dy);

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

    Document document_ = Document::create(64, 64);
    ModelDocument model_ = ModelDocument::create_default();
    ToolContext tool_;
    std::vector<Pixel> display_pixels_;
    std::vector<Pixel> interaction_before_;
    SelectionMask selection_before_;
    std::vector<std::array<int, 2>> lasso_points_;
    std::array<int, 256> histogram_luma_{};
    bool display_dirty_ = true;
    bool histogram_dirty_ = true;
    bool histogram_approximate_ = false;
    bool interaction_active_ = false;
    bool stroke_tool_active_ = false;
    bool secondary_interaction_ = false;
    int interaction_start_x_ = 0;
    int interaction_start_y_ = 0;
    int interaction_last_x_ = 0;
    int interaction_last_y_ = 0;
    SelectionCombineMode selection_mode_ = SelectionCombineMode::Replace;
    bool modified_ = false;
    std::string status_ = "Ready";
};

} // namespace px
