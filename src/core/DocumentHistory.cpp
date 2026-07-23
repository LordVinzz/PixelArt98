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

void Document::commit_active_cel_edit(const std::string& name, std::vector<Pixel> before) {
    MemoryTraceScope trace("Document::commit_active_cel_edit", name);
    memory_trace_vector("commit_active_cel_edit.before", before);
    if (!has_active_cel()) {
        return;
    }
    const auto& after = active_cel().pixels;
    memory_trace_vector("commit_active_cel_edit.after", after);
    const TileDiffStats stats = analyze_tile_diff_stats(before, after, width, height);
    if (!stats.any_changed()) {
        return;
    }
    recent_commit_names_.push_back(name);
    UndoCommand command;
    command.name = name;
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
                                                         "commit_active_cel_edit.before");
        trace_dense_diff_storage("commit_active_cel_edit", *command.dense_pixel_diff);
    } else {
        command.pixel_diffs = make_tile_diffs(before, after, width, height, active_frame, active_layer);
        trace_tile_diff_storage("commit_active_cel_edit", command.pixel_diffs);
    }
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128) {
        undo_stack_.pop_front();
    }
    redo_stack_.clear();
}

namespace {

bool tile_diff_target_valid(const Document& doc, const TileDiff& diff) {
    return diff.frame >= 0 &&
           diff.layer >= 0 &&
           diff.frame < static_cast<int>(doc.frames.size()) &&
           diff.layer < static_cast<int>(doc.layers.size());
}

void capture_current_pixels_as_after(Document& doc, std::vector<TileDiff>& diffs) {
    for (TileDiff& diff : diffs) {
        if (!tile_diff_target_valid(doc, diff)) {
            continue;
        }
        auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
        diff.after.clear();
        diff.after.reserve(static_cast<std::size_t>(diff.w * diff.h));
        for (int y = 0; y < diff.h; ++y) {
            for (int x = 0; x < diff.w; ++x) {
                const int dst_x = diff.x + x;
                const int dst_y = diff.y + y;
                if (!doc.in_bounds(dst_x, dst_y)) {
                    diff.after.push_back(0);
                    continue;
                }
                diff.after.push_back(pixels[static_cast<std::size_t>(doc.pixel_index(dst_x, dst_y))]);
            }
        }
    }
}

void release_after_pixels(std::vector<TileDiff>& diffs) {
    for (TileDiff& diff : diffs) {
        std::vector<Pixel>().swap(diff.after);
    }
}

} // namespace

void Document::commit_selection_edit(const std::string& name, const SelectionMask& before) {
    if (before.mask == selection.mask && before.active == selection.active) {
        return;
    }
    recent_commit_names_.push_back(name);
    UndoCommand command;
    command.name = name;
    command.before_selection = before;
    command.after_selection = selection;
    command.before_active_frame = command.after_active_frame = active_frame;
    command.before_active_layer = command.after_active_layer = active_layer;
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
}

void Document::commit_palette_edit(const std::string& name, const Palette& before) {
    if (before.colors == palette.colors && before.active == palette.active) {
        return;
    }
    recent_commit_names_.push_back(name);
    UndoCommand command;
    command.name = name;
    command.before_palette = before;
    command.after_palette = palette;
    command.before_active_frame = command.after_active_frame = active_frame;
    command.before_active_layer = command.after_active_layer = active_layer;
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
}

void Document::commit_structure_edit(const std::string& name,
                                     std::vector<Layer> before_layers,
                                     std::vector<Frame> before_frames,
                                     std::vector<AnimationTag> before_tags,
                                     int before_active_layer,
                                     int before_active_frame) {
    recent_commit_names_.push_back(name);
    UndoCommand command;
    command.name = name;
    command.before_layers = std::move(before_layers);
    command.after_layers = layers;
    command.before_frames = std::move(before_frames);
    command.after_frames = frames;
    command.before_tags = std::move(before_tags);
    command.after_tags = tags;
    command.before_active_layer = before_active_layer;
    command.before_active_frame = before_active_frame;
    command.after_active_layer = active_layer;
    command.after_active_frame = active_frame;
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
}

void Document::commit_document_edit(const std::string& name,
                                    int old_width,
                                    int old_height,
                                    std::vector<Layer> before_layers,
                                    std::vector<Frame> before_frames,
                                    SelectionMask before_selection,
                                    FloatingSelection before_floating_selection,
                                    std::optional<ModelDocument> before_model,
                                    std::optional<ModelDocument> after_model) {
    UndoCommand command;
    command.name = name;
    command.before_width = old_width;
    command.before_height = old_height;
    command.after_width = width;
    command.after_height = height;
    command.before_layers = std::move(before_layers);
    command.after_layers = layers;
    command.before_frames = std::move(before_frames);
    command.after_frames = frames;
    command.before_selection = std::move(before_selection);
    command.after_selection = selection;
    command.before_floating_selection = std::move(before_floating_selection);
    command.after_floating_selection = floating_selection;
    command.before_model = std::move(before_model);
    command.after_model = std::move(after_model);
    command.before_active_layer = command.after_active_layer = active_layer;
    command.before_active_frame = command.after_active_frame = active_frame;
    recent_commit_names_.push_back(name);
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
}

void Document::commit_model_edit(const std::string& name,
                                 ModelDocument before_model,
                                 ModelDocument after_model) {
    UndoCommand command;
    command.name = name;
    command.before_model = std::move(before_model);
    command.after_model = std::move(after_model);
    command.before_active_layer = command.after_active_layer = active_layer;
    command.before_active_frame = command.after_active_frame = active_frame;
    recent_commit_names_.push_back(name);
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
}

void Document::commit_layer_edit(const std::string& name, std::vector<Layer> before_layers) {
    UndoCommand command;
    command.name = name;
    command.before_layers = std::move(before_layers);
    command.after_layers = layers;
    command.before_active_layer = command.after_active_layer = active_layer;
    command.before_active_frame = command.after_active_frame = active_frame;
    recent_commit_names_.push_back(name);
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128U) undo_stack_.pop_front();
    redo_stack_.clear();
}

static void apply_tile_diffs(Document& doc, const std::vector<TileDiff>& diffs, bool use_after) {
    for (const TileDiff& diff : diffs) {
        if (!tile_diff_target_valid(doc, diff)) {
            continue;
        }
        auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
        const auto& source = use_after ? diff.after : diff.before;
        if (source.size() != static_cast<std::size_t>(diff.w * diff.h)) {
            continue;
        }
        for (int y = 0; y < diff.h; ++y) {
            for (int x = 0; x < diff.w; ++x) {
                int dst_x = diff.x + x;
                int dst_y = diff.y + y;
                if (!doc.in_bounds(dst_x, dst_y)) {
                    continue;
                }
                std::size_t src_i = static_cast<std::size_t>(y * diff.w + x);
                std::size_t dst_i = static_cast<std::size_t>(doc.pixel_index(dst_x, dst_y));
                if (src_i < source.size()) {
                    pixels[dst_i] = source[src_i];
                }
            }
        }
    }
}

bool Document::undo(ModelDocument* model) {
    if (undo_stack_.empty()) {
        return false;
    }
    if (undo_stack_.back().before_model && model == nullptr) return false;
    UndoCommand cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    capture_current_pixels_as_after(*this, cmd.pixel_diffs);
    if (cmd.dense_pixel_diff) {
        static_cast<void>(capture_dense_after_pixels(*this, *cmd.dense_pixel_diff));
    }
    if (cmd.before_width) width = *cmd.before_width;
    if (cmd.before_height) height = *cmd.before_height;
    if (cmd.before_layers) layers = *cmd.before_layers;
    if (cmd.before_frames) frames = *cmd.before_frames;
    if (cmd.before_tags) tags = *cmd.before_tags;
    if (cmd.before_selection) selection = *cmd.before_selection;
    if (cmd.before_floating_selection) floating_selection = *cmd.before_floating_selection;
    if (cmd.before_palette) palette = *cmd.before_palette;
    if (cmd.before_model) *model = *cmd.before_model;
    if (cmd.before_frame_duration_ms && cmd.frame >= 0 && cmd.frame < static_cast<int>(frames.size())) {
        frames[static_cast<std::size_t>(cmd.frame)].duration_ms = *cmd.before_frame_duration_ms;
    }
    apply_tile_diffs(*this, cmd.pixel_diffs, false);
    if (cmd.dense_pixel_diff) {
        static_cast<void>(apply_history_payload(*this, *cmd.dense_pixel_diff, cmd.dense_pixel_diff->before));
    }
    active_frame = frames.empty() ? 0 : std::clamp(cmd.before_active_frame, 0, static_cast<int>(frames.size()) - 1);
    active_layer = layers.empty() ? 0 : std::clamp(cmd.before_active_layer, 0, static_cast<int>(layers.size()) - 1);
    redo_stack_.push_back(std::move(cmd));
    return true;
}

bool Document::redo(ModelDocument* model) {
    if (redo_stack_.empty()) {
        return false;
    }
    if (redo_stack_.back().after_model && model == nullptr) return false;
    UndoCommand cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    if (cmd.after_width) width = *cmd.after_width;
    if (cmd.after_height) height = *cmd.after_height;
    if (cmd.after_layers) layers = *cmd.after_layers;
    if (cmd.after_frames) frames = *cmd.after_frames;
    if (cmd.after_tags) tags = *cmd.after_tags;
    if (cmd.after_selection) selection = *cmd.after_selection;
    if (cmd.after_floating_selection) floating_selection = *cmd.after_floating_selection;
    if (cmd.after_palette) palette = *cmd.after_palette;
    if (cmd.after_model) *model = *cmd.after_model;
    if (cmd.after_frame_duration_ms && cmd.frame >= 0 && cmd.frame < static_cast<int>(frames.size())) {
        frames[static_cast<std::size_t>(cmd.frame)].duration_ms = *cmd.after_frame_duration_ms;
    }
    apply_tile_diffs(*this, cmd.pixel_diffs, true);
    release_after_pixels(cmd.pixel_diffs);
    if (cmd.dense_pixel_diff) {
        static_cast<void>(apply_history_payload(*this, *cmd.dense_pixel_diff, cmd.dense_pixel_diff->after));
        release_dense_after_pixels(*cmd.dense_pixel_diff);
    }
    active_frame = frames.empty() ? 0 : std::clamp(cmd.after_active_frame, 0, static_cast<int>(frames.size()) - 1);
    active_layer = layers.empty() ? 0 : std::clamp(cmd.after_active_layer, 0, static_cast<int>(layers.size()) - 1);
    undo_stack_.push_back(std::move(cmd));
    return true;
}

void Document::clear_history() {
    undo_stack_.clear();
    redo_stack_.clear();
    recent_commit_names_.clear();
}

const std::deque<UndoCommand>& Document::undo_history_for_recovery() const noexcept {
    return undo_stack_;
}

const std::deque<UndoCommand>& Document::redo_history_for_recovery() const noexcept {
    return redo_stack_;
}

void Document::restore_history_for_recovery(std::deque<UndoCommand> undo,
                                            std::deque<UndoCommand> redo) {
    while (undo.size() > 128U) undo.pop_front();
    while (redo.size() > 128U) redo.pop_front();
    undo_stack_ = std::move(undo);
    redo_stack_ = std::move(redo);
    recent_commit_names_.clear();
}
bool Document::has_recent_commit_names() const {
    return !recent_commit_names_.empty();
}

std::vector<std::string> Document::consume_recent_commit_names() {
    std::vector<std::string> names = std::move(recent_commit_names_);
    recent_commit_names_.clear();
    return names;
}

void Document::clear_recent_commit_names() {
    recent_commit_names_.clear();
}

namespace {

std::size_t frames_pixel_capacity(const std::vector<Frame>& frames) {
    std::size_t total = 0;
    for (const Frame& frame : frames) {
        for (const Cel& cel : frame.cels) {
            total += cel.pixels.capacity();
        }
    }
    return total;
}

std::size_t command_pixel_diff_capacity(const UndoCommand& command) {
    std::size_t total = 0;
    for (const TileDiff& diff : command.pixel_diffs) {
        total += diff.before.capacity();
        total += diff.after.capacity();
    }
    if (command.dense_pixel_diff) {
        total += command.dense_pixel_diff->before.pixels.capacity();
        total += command.dense_pixel_diff->after.pixels.capacity();
    }
    return total;
}

std::size_t payload_disk_pixel_count(const HistoryPixelPayload& payload) {
    return payload.ref.valid() ? static_cast<std::size_t>(payload.ref.pixel_count) : 0U;
}

std::size_t command_disk_history_pixel_count(const UndoCommand& command) {
    if (!command.dense_pixel_diff) {
        return 0;
    }
    return payload_disk_pixel_count(command.dense_pixel_diff->before) +
           payload_disk_pixel_count(command.dense_pixel_diff->after);
}

std::size_t command_full_frame_pixel_capacity(const UndoCommand& command) {
    std::size_t total = 0;
    if (command.before_frames) {
        total += frames_pixel_capacity(*command.before_frames);
    }
    if (command.after_frames) {
        total += frames_pixel_capacity(*command.after_frames);
    }
    return total;
}

} // namespace

std::size_t Document::undo_stack_pixel_diff_capacity() const {
    std::size_t total = 0;
    for (const UndoCommand& command : undo_stack_) {
        total += command_pixel_diff_capacity(command);
    }
    for (const UndoCommand& command : redo_stack_) {
        total += command_pixel_diff_capacity(command);
    }
    return total;
}

std::size_t Document::undo_stack_full_frame_pixel_capacity() const {
    std::size_t total = 0;
    for (const UndoCommand& command : undo_stack_) {
        total += command_full_frame_pixel_capacity(command);
    }
    for (const UndoCommand& command : redo_stack_) {
        total += command_full_frame_pixel_capacity(command);
    }
    return total;
}

std::size_t Document::undo_stack_disk_history_pixel_count() const {
    std::size_t total = 0;
    for (const UndoCommand& command : undo_stack_) {
        total += command_disk_history_pixel_count(command);
    }
    for (const UndoCommand& command : redo_stack_) {
        total += command_disk_history_pixel_count(command);
    }
    return total;
}
} // namespace px
