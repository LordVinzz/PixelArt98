// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "core/DocumentInternal.hpp"

#include "core/MemoryTrace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace px {

using namespace history_detail;

void Document::add_layer(const std::string& name) {
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    if (frames.empty()) {
        frames.push_back(Frame{});
        active_frame = 0;
    }
    const std::size_t expected_size = static_cast<std::size_t>(std::max(0, width)) *
                                      static_cast<std::size_t>(std::max(0, height));
    Layer layer;
    layer.name = name.empty() ? "Layer" : name;
    layers.push_back(std::move(layer));
    for (auto& frame : frames) {
        Cel cel;
        cel.pixels.assign(expected_size, 0);
        frame.cels.push_back(std::move(cel));
    }
    active_layer = static_cast<int>(layers.size()) - 1;
    commit_structure_edit("Add Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

void Document::insert_layer(int index, const std::string& name, std::vector<Pixel> pixels, const std::string& undo_name) {
    if (frames.empty()) {
        return;
    }
    const std::size_t expected_size = static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    if (pixels.size() != expected_size) {
        return;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    const int clamped_index = std::clamp(index, 0, static_cast<int>(layers.size()));

    Layer layer;
    layer.name = name.empty() ? "Layer" : name;
    layers.insert(layers.begin() + clamped_index, std::move(layer));

    for (int frame_index = 0; frame_index < static_cast<int>(frames.size()); ++frame_index) {
        Cel cel;
        cel.pixels.assign(expected_size, 0);
        if (frame_index == active_frame) {
            cel.pixels = pixels;
        }
        frames[static_cast<std::size_t>(frame_index)].cels.insert(
            frames[static_cast<std::size_t>(frame_index)].cels.begin() + clamped_index,
            std::move(cel));
    }
    active_layer = clamped_index;
    commit_structure_edit(undo_name.empty() ? "Insert Layer" : undo_name,
                          std::move(before_layers),
                          std::move(before_frames),
                          std::move(before_tags),
                          before_active_layer,
                          before_active_frame);
}

void Document::duplicate_layer(int index) {
    if (index < 0 || index >= static_cast<int>(layers.size())) {
        return;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    Layer layer = layers[static_cast<std::size_t>(index)];
    layer.name += " Copy";
    layers.insert(layers.begin() + index + 1, layer);
    for (auto& frame : frames) {
        frame.cels.insert(frame.cels.begin() + index + 1, frame.cels[static_cast<std::size_t>(index)]);
    }
    active_layer = index + 1;
    commit_structure_edit("Duplicate Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

bool Document::remove_layer(int index) {
    if (layers.size() <= 1 || index < 0 || index >= static_cast<int>(layers.size())) {
        return false;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    floating_selection.clear();
    layers.erase(layers.begin() + index);
    for (auto& frame : frames) {
        if (index < static_cast<int>(frame.cels.size())) {
            frame.cels.erase(frame.cels.begin() + index);
        }
    }
    active_layer = layers.empty() ? 0 : std::clamp(active_layer, 0, static_cast<int>(layers.size()) - 1);
    commit_structure_edit("Remove Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
    return true;
}

void Document::move_layer(int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(layers.size()) || to >= static_cast<int>(layers.size()) || from == to) {
        return;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    auto layer = layers[static_cast<std::size_t>(from)];
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, layer);
    for (auto& frame : frames) {
        auto cel_copy = frame.cels[static_cast<std::size_t>(from)];
        frame.cels.erase(frame.cels.begin() + from);
        frame.cels.insert(frame.cels.begin() + to, std::move(cel_copy));
    }
    active_layer = to;
    commit_structure_edit("Move Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

bool Document::merge_layer_down(int index) {
    if (index <= 0 || index >= static_cast<int>(layers.size())) {
        return false;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    for (auto& frame : frames) {
        auto& below = frame.cels[static_cast<std::size_t>(index - 1)].pixels;
        const auto& top = frame.cels[static_cast<std::size_t>(index)].pixels;
        float opacity = layers[static_cast<std::size_t>(index)].opacity;
        for (std::size_t i = 0; i < below.size() && i < top.size(); ++i) {
            below[i] = blend_over(below[i], top[i], opacity);
        }
        frame.cels.erase(frame.cels.begin() + index);
    }
    layers[static_cast<std::size_t>(index - 1)].name += " + " + layers[static_cast<std::size_t>(index)].name;
    layers.erase(layers.begin() + index);
    active_layer = index - 1;
    commit_structure_edit("Merge Layer Down", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
    return true;
}

bool Document::set_layer_name(int index, const std::string& name) {
    if (index < 0 || index >= static_cast<int>(layers.size()) || name.empty() ||
        layers[static_cast<std::size_t>(index)].name == name) return false;
    auto before = layers;
    layers[static_cast<std::size_t>(index)].name = name;
    commit_layer_edit("Rename Layer", std::move(before));
    return true;
}

bool Document::set_layer_visible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(layers.size()) ||
        layers[static_cast<std::size_t>(index)].visible == visible) return false;
    auto before = layers;
    layers[static_cast<std::size_t>(index)].visible = visible;
    commit_layer_edit("Layer Visibility", std::move(before));
    return true;
}

bool Document::set_layer_opacity(int index, float opacity) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return false;
    const float value = std::clamp(opacity, 0.0f, 1.0f);
    if (std::abs(layers[static_cast<std::size_t>(index)].opacity - value) < 0.0001f) return false;
    auto before = layers;
    layers[static_cast<std::size_t>(index)].opacity = value;
    commit_layer_edit("Layer Opacity", std::move(before));
    return true;
}

bool Document::set_layer_blend_mode(int index, LayerBlendMode blend_mode) {
    if (index < 0 || index >= static_cast<int>(layers.size()) ||
        layers[static_cast<std::size_t>(index)].blend_mode == blend_mode) return false;
    auto before = layers;
    layers[static_cast<std::size_t>(index)].blend_mode = blend_mode;
    commit_layer_edit("Layer Blend Mode", std::move(before));
    return true;
}

bool Document::set_layer_clipped(int index, bool clipped) {
    if (index < 0 || index >= static_cast<int>(layers.size()) ||
        layers[static_cast<std::size_t>(index)].clip_to_below == clipped) return false;
    auto before = layers;
    layers[static_cast<std::size_t>(index)].clip_to_below = clipped;
    commit_layer_edit("Layer Clipping", std::move(before));
    return true;
}

bool Document::set_layer_mask(int index, std::vector<std::uint8_t> mask, bool enabled,
                              const std::string& undo_name) {
    if (index < 0 || index >= static_cast<int>(layers.size())) return false;
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!mask.empty() && mask.size() != expected) return false;
    Layer& layer = layers[static_cast<std::size_t>(index)];
    if (layer.mask == mask && layer.mask_enabled == enabled) return false;
    auto before = layers;
    layer.mask = std::move(mask);
    layer.mask_enabled = enabled;
    commit_layer_edit(undo_name.empty() ? "Layer Mask" : undo_name, std::move(before));
    return true;
}

void Document::add_frame(bool duplicate_current) {
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    Frame frame;
    frame.duration_ms = frames[static_cast<std::size_t>(active_frame)].duration_ms;
    if (duplicate_current) {
        frame.cels = frames[static_cast<std::size_t>(active_frame)].cels;
    } else {
        for (std::size_t i = 0; i < layers.size(); ++i) {
            Cel cel;
            cel.pixels.assign(static_cast<std::size_t>(width * height), 0);
            frame.cels.push_back(std::move(cel));
        }
    }
    frames.insert(frames.begin() + active_frame + 1, std::move(frame));
    active_frame += 1;
    commit_structure_edit(duplicate_current ? "Duplicate Frame" : "Add Frame", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

bool Document::remove_frame(int index) {
    if (frames.size() <= 1 || index < 0 || index >= static_cast<int>(frames.size())) {
        return false;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    frames.erase(frames.begin() + index);
    active_frame = std::clamp(active_frame, 0, static_cast<int>(frames.size()) - 1);
    for (auto& tag : tags) {
        tag.from = std::clamp(tag.from, 0, static_cast<int>(frames.size()) - 1);
        tag.to = std::clamp(tag.to, tag.from, static_cast<int>(frames.size()) - 1);
    }
    commit_structure_edit("Remove Frame", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
    return true;
}

void Document::duplicate_frame(int index) {
    if (index < 0 || index >= static_cast<int>(frames.size())) {
        return;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    frames.insert(frames.begin() + index + 1, frames[static_cast<std::size_t>(index)]);
    active_frame = index + 1;
    commit_structure_edit("Duplicate Frame", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

void Document::move_frame(int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(frames.size()) || to >= static_cast<int>(frames.size()) || from == to) {
        return;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    auto frame = frames[static_cast<std::size_t>(from)];
    frames.erase(frames.begin() + from);
    frames.insert(frames.begin() + to, std::move(frame));
    active_frame = to;
    commit_structure_edit("Move Frame", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

bool Document::set_frame_duration(int index, int duration_ms) {
    if (index < 0 || index >= static_cast<int>(frames.size())) {
        return false;
    }
    const int clamped_duration = std::clamp(duration_ms, kMinimumFrameDurationMs, kMaximumFrameDurationMs);
    Frame& frame = frames[static_cast<std::size_t>(index)];
    if (frame.duration_ms == clamped_duration) {
        return false;
    }

    UndoCommand command;
    command.name = "Change Frame Duration";
    command.frame = index;
    command.before_frame_duration_ms = frame.duration_ms;
    command.after_frame_duration_ms = clamped_duration;
    command.before_active_layer = command.after_active_layer = active_layer;
    command.before_active_frame = command.after_active_frame = active_frame;
    frame.duration_ms = clamped_duration;
    recent_commit_names_.push_back(command.name);
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
    return true;
}

void Document::add_tag(const std::string& name, int from, int to) {
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    int last = std::max(0, static_cast<int>(frames.size()) - 1);
    AnimationTag tag;
    tag.name = name.empty() ? "Tag" : name;
    tag.from = std::clamp(std::min(from, to), 0, last);
    tag.to = std::clamp(std::max(from, to), 0, last);
    tags.push_back(std::move(tag));
    commit_structure_edit("Add Animation Tag", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
}

bool Document::remove_tag(int index) {
    if (index < 0 || index >= static_cast<int>(tags.size())) {
        return false;
    }
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    tags.erase(tags.begin() + index);
    commit_structure_edit("Remove Animation Tag", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
    return true;
}

std::vector<Pixel> Document::composite_frame(int frame_index) const {
    std::vector<Pixel> out(static_cast<std::size_t>(width * height), 0);
    if (frame_index < 0 || frame_index >= static_cast<int>(frames.size())) {
        return out;
    }
    const auto& frame = frames[static_cast<std::size_t>(frame_index)];
    for (std::size_t layer_index = 0; layer_index < layers.size() && layer_index < frame.cels.size(); ++layer_index) {
        const Layer& layer = layers[layer_index];
        if (!layer.visible || layer.opacity <= 0.0f) {
            continue;
        }
        const Cel& c = frame.cels[layer_index];
        if (c.pixels.size() != out.size()) {
            continue;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int sx = x - c.x;
                int sy = y - c.y;
                if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
                    continue;
                }
                std::size_t dst_i = static_cast<std::size_t>(y * width + x);
                std::size_t src_i = static_cast<std::size_t>(sy * width + sx);
                Pixel src = c.pixels[src_i];
                float effective_opacity = layer.opacity;
                if (layer.mask_enabled && layer.mask.size() == static_cast<std::size_t>(width * height)) {
                    effective_opacity *= static_cast<float>(layer.mask[dst_i]) / 255.0f;
                }
                if (layer.clip_to_below) {
                    effective_opacity *= static_cast<float>(a(out[dst_i])) / 255.0f;
                }
                if (effective_opacity <= 0.0f) {
                    continue;
                }
                src = apply_layer_blend_mode(out[dst_i], src, layer.blend_mode);
                out[dst_i] = blend_over(out[dst_i], src, effective_opacity);
            }
        }
    }
    return out;
}

std::vector<Pixel> Document::composite_active() const {
    MemoryTraceScope trace("Document::composite_active");
    std::vector<Pixel> pixels = composite_frame(active_frame);
    memory_trace_vector("composite_active.result", pixels);
    return pixels;
}

std::array<int, 256> Document::histogram_luma() const {
    std::array<int, 256> hist{};
    auto pixels = composite_active();
    for (Pixel p : pixels) {
        if (a(p) == 0) {
            continue;
        }
        int v = std::clamp(static_cast<int>(luminance(p) + 0.5f), 0, 255);
        hist[static_cast<std::size_t>(v)] += 1;
    }
    return hist;
}

std::array<int, 256> Document::histogram_channel(int channel) const {
    std::array<int, 256> hist{};
    auto pixels = composite_active();
    for (Pixel p : pixels) {
        if (a(p) == 0) {
            continue;
        }
        int v = channel == 0 ? r(p) : channel == 1 ? g(p) : b(p);
        hist[static_cast<std::size_t>(v)] += 1;
    }
    return hist;
}

void Document::replace_active_pixels(std::vector<Pixel> pixels, const std::string& undo_name) {
    if (!has_active_cel()) {
        return;
    }
    if (pixels.size() != active_cel().pixels.size()) {
        return;
    }
    MemoryTraceScope trace("Document::replace_active_pixels", undo_name);
    const auto& before = active_cel().pixels;
    memory_trace_vector("replace_active_pixels.before", before);
    memory_trace_vector("replace_active_pixels.after_input", pixels);
    const TileDiffStats stats = analyze_tile_diff_stats(before, pixels, width, height);
    if (!stats.any_changed()) {
        return;
    }
    recent_commit_names_.push_back(undo_name);
    UndoCommand command;
    command.name = undo_name;
    command.frame = active_frame;
    command.layer = active_layer;
    command.before_active_frame = active_frame;
    command.before_active_layer = active_layer;
    command.after_active_frame = active_frame;
    command.after_active_layer = active_layer;
    if (should_use_dense_pixel_history(stats, before.size())) {
        command.dense_pixel_diff = make_dense_pixel_diff(before,
                                                         active_frame,
                                                         active_layer,
                                                         width,
                                                         height,
                                                         "replace_active_pixels.before");
        trace_dense_diff_storage("replace_active_pixels", *command.dense_pixel_diff);
    } else {
        command.pixel_diffs = make_tile_diffs(before, pixels, width, height, active_frame, active_layer);
        trace_tile_diff_storage("replace_active_pixels", command.pixel_diffs);
    }
    active_cel().pixels = std::move(pixels);
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128) {
        undo_stack_.pop_front();
    }
    redo_stack_.clear();
}

bool Document::delete_selected_pixels(const std::string& undo_name) {
    if (!has_active_cel()) {
        return false;
    }
    if (!selection.active || selection.selected_count() == 0) {
        return false;
    }

    auto before = snapshot_active_cel();
    auto& pixels = active_cel().pixels;
    bool changed = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!selection.contains(x, y)) {
                continue;
            }
            std::size_t i = static_cast<std::size_t>(pixel_index(x, y));
            if (pixels[i] != 0) {
                pixels[i] = 0;
                changed = true;
            }
        }
    }
    if (!changed) {
        return false;
    }

    commit_active_cel_edit(undo_name, std::move(before));
    return true;
}

bool Document::begin_floating_selection() {
    if (!has_active_cel()) {
        return false;
    }
    auto selection_bounds = selection.bounds();
    if (!selection_bounds) {
        return false;
    }
    const auto [min_x, min_y, max_x, max_y] = *selection_bounds;
    floating_selection.clear();
    floating_selection.active = true;
    floating_selection.source_x = min_x;
    floating_selection.source_y = min_y;
    floating_selection.offset_x = 0;
    floating_selection.offset_y = 0;
    floating_selection.width = max_x - min_x + 1;
    floating_selection.height = max_y - min_y + 1;
    floating_selection.pixels.assign(static_cast<std::size_t>(floating_selection.width * floating_selection.height), 0);
    floating_selection.mask.assign(static_cast<std::size_t>(floating_selection.width * floating_selection.height), 0);
    auto& pixels = active_cel().pixels;
    for (int y = 0; y < floating_selection.height; ++y) {
        for (int x = 0; x < floating_selection.width; ++x) {
            int sx = min_x + x;
            int sy = min_y + y;
            std::size_t local = static_cast<std::size_t>(y * floating_selection.width + x);
            if (!selection.contains(sx, sy)) {
                continue;
            }
            std::size_t source_i = static_cast<std::size_t>(pixel_index(sx, sy));
            floating_selection.mask[local] = 1;
            floating_selection.pixels[local] = pixels[source_i];
            pixels[source_i] = 0;
        }
    }
    return true;
}

void Document::move_floating_selection(int dx, int dy) {
    if (!floating_selection.active) {
        return;
    }
    floating_selection.offset_x = dx;
    floating_selection.offset_y = dy;
}

void Document::cancel_floating_selection() {
    if (!floating_selection.active || !has_active_cel()) {
        return;
    }
    auto& pixels = active_cel().pixels;
    for (int y = 0; y < floating_selection.height; ++y) {
        for (int x = 0; x < floating_selection.width; ++x) {
            if (!floating_selection.contains_local(x, y)) {
                continue;
            }
            int dx = floating_selection.source_x + x;
            int dy = floating_selection.source_y + y;
            if (in_bounds(dx, dy)) {
                pixels[static_cast<std::size_t>(pixel_index(dx, dy))] = floating_selection.pixels[static_cast<std::size_t>(y * floating_selection.width + x)];
            }
        }
    }
    floating_selection.clear();
}

void Document::commit_floating_selection(const std::string& undo_name, std::vector<Pixel> before) {
    if (!floating_selection.active || !has_active_cel()) {
        return;
    }
    const SelectionMask before_selection = selection;
    auto& pixels = active_cel().pixels;
    selection.clear();
    bool any_selected = false;
    for (int y = 0; y < floating_selection.height; ++y) {
        for (int x = 0; x < floating_selection.width; ++x) {
            if (!floating_selection.contains_local(x, y)) {
                continue;
            }
            int dx = floating_selection.source_x + floating_selection.offset_x + x;
            int dy = floating_selection.source_y + floating_selection.offset_y + y;
            if (in_bounds(dx, dy)) {
                pixels[static_cast<std::size_t>(pixel_index(dx, dy))] = floating_selection.pixels[static_cast<std::size_t>(y * floating_selection.width + x)];
                selection.mask[static_cast<std::size_t>(pixel_index(dx, dy))] = 1;
                any_selected = true;
            }
        }
    }
    selection.active = any_selected;
    floating_selection.clear();
    const bool pixels_changed = before != pixels;
    if (pixels_changed) {
        commit_active_cel_edit(undo_name, std::move(before));
        if (!undo_stack_.empty()) {
            // Moving or transforming pixels also changes the selection shape.
            // Keep both state changes in one operation instead of requiring a
            // second undo just to restore the selection outline.
            undo_stack_.back().before_selection = before_selection;
            undo_stack_.back().after_selection = selection;
        }
    } else {
        commit_selection_edit(undo_name, before_selection);
    }
}
} // namespace px
