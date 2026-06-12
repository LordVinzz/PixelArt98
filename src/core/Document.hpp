// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Pixel.hpp"

#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace px {

enum class SelectionCombineMode {
    Replace,
    Add,
    Subtract,
    Intersect,
    Invert
};

struct SelectionMask {
    int width = 0;
    int height = 0;
    bool active = false;
    std::vector<std::uint8_t> mask;

    void resize(int w, int h);
    void clear();
    void select_all();
    void select_rect(int x0, int y0, int x1, int y1, bool replace = true);
    void select_rect(int x0, int y0, int x1, int y1, SelectionCombineMode mode);
    void select_polygon(const std::vector<std::array<int, 2>>& points, bool replace = true);
    void select_polygon(const std::vector<std::array<int, 2>>& points, SelectionCombineMode mode);
    void combine_with_mask(const std::vector<std::uint8_t>& source, SelectionCombineMode mode);
    void invert();
    void translate(int dx, int dy);
    bool contains(int x, int y) const;
    int selected_count() const;
    std::optional<std::array<int, 4>> bounds() const;
};

struct Cel {
    int x = 0;
    int y = 0;
    std::vector<Pixel> pixels;
};

enum class LayerBlendMode {
    Normal,
    Multiply,
    Additive,
    Add = Additive,
    ColorBurn,
    ColorDodge,
    Reflect,
    Glow,
    Overlay,
    Difference,
    Negation,
    Lighten,
    Darken,
    Screen,
    Xor
};

struct Layer {
    std::string name = "Layer";
    bool visible = true;
    float opacity = 1.0f;
    LayerBlendMode blend_mode = LayerBlendMode::Normal;
    bool mask_enabled = false;
    bool clip_to_below = false;
    std::vector<std::uint8_t> mask;
};

struct Frame {
    int duration_ms = 100;
    std::vector<Cel> cels;
};

struct Palette {
    std::vector<Pixel> colors;
    int active = 0;
};

struct TileDiff {
    int frame = 0;
    int layer = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    std::vector<Pixel> before;
    std::vector<Pixel> after;
};

struct AnimationTag {
    std::string name = "Tag";
    int from = 0;
    int to = 0;
};

enum class PlaybackMode {
    Loop,
    PingPong
};

struct FloatingSelection {
    bool active = false;
    int source_x = 0;
    int source_y = 0;
    int offset_x = 0;
    int offset_y = 0;
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels;
    std::vector<std::uint8_t> mask;

    void clear();
    bool contains_local(int x, int y) const;
};

struct UndoCommand {
    std::string name;
    int frame = 0;
    int layer = 0;
    std::vector<TileDiff> pixel_diffs;
    std::optional<SelectionMask> before_selection;
    std::optional<SelectionMask> after_selection;
    std::optional<Palette> before_palette;
    std::optional<Palette> after_palette;
    std::optional<std::vector<Layer>> before_layers;
    std::optional<std::vector<Layer>> after_layers;
    std::optional<std::vector<Frame>> before_frames;
    std::optional<std::vector<Frame>> after_frames;
    std::optional<std::vector<AnimationTag>> before_tags;
    std::optional<std::vector<AnimationTag>> after_tags;
    int before_active_layer = 0;
    int before_active_frame = 0;
    int after_active_layer = 0;
    int after_active_frame = 0;
};

class Document {
public:
    int width = 64;
    int height = 64;
    int active_layer = 0;
    int active_frame = 0;
    Palette palette;
    SelectionMask selection;
    FloatingSelection floating_selection;
    std::vector<Layer> layers;
    std::vector<Frame> frames;
    std::vector<AnimationTag> tags;
    PlaybackMode playback_mode = PlaybackMode::Loop;

    static Document create(int w, int h);

    bool valid() const;
    bool in_bounds(int x, int y) const;
    int pixel_index(int x, int y) const;

    Cel& cel(int frame, int layer);
    const Cel& cel(int frame, int layer) const;
    Cel& active_cel();
    const Cel& active_cel() const;

    std::vector<Pixel> snapshot_active_cel() const;
    void commit_active_cel_edit(const std::string& name, std::vector<Pixel> before);
    void commit_selection_edit(const std::string& name, const SelectionMask& before);
    void commit_palette_edit(const std::string& name, const Palette& before);
    void commit_structure_edit(const std::string& name,
                               std::vector<Layer> before_layers,
                               std::vector<Frame> before_frames,
                               std::vector<AnimationTag> before_tags,
                               int before_active_layer,
                               int before_active_frame);
    bool undo();
    bool redo();
    void clear_history();
    std::vector<std::string> consume_recent_commit_names();
    void clear_recent_commit_names();

    void add_layer(const std::string& name);
    void insert_layer(int index, const std::string& name, std::vector<Pixel> pixels, const std::string& undo_name);
    void duplicate_layer(int index);
    bool remove_layer(int index);
    void move_layer(int from, int to);
    bool merge_layer_down(int index);
    void add_frame(bool duplicate_current);
    bool remove_frame(int index);
    void duplicate_frame(int index);
    void move_frame(int from, int to);
    void add_tag(const std::string& name, int from, int to);
    bool remove_tag(int index);

    std::vector<Pixel> composite_frame(int frame) const;
    std::vector<Pixel> composite_active() const;
    std::array<int, 256> histogram_luma() const;
    std::array<int, 256> histogram_channel(int channel) const;

    void replace_active_pixels(std::vector<Pixel> pixels, const std::string& undo_name);
    bool delete_selected_pixels(const std::string& undo_name = "Delete Selection");

    bool begin_floating_selection();
    void move_floating_selection(int dx, int dy);
    void cancel_floating_selection();
    void commit_floating_selection(const std::string& undo_name, std::vector<Pixel> before);

private:
    std::deque<UndoCommand> undo_stack_;
    std::deque<UndoCommand> redo_stack_;
    std::vector<std::string> recent_commit_names_;
};

std::vector<Pixel> default_palette();
std::vector<TileDiff> make_tile_diffs(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int frame,
                                      int layer,
                                      int tile_size = 16);
const char* playback_mode_name(PlaybackMode mode);
const char* layer_blend_mode_name(LayerBlendMode mode);

} // namespace px
