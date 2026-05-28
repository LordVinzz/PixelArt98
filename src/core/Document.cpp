#include "core/Document.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace px {

void SelectionMask::resize(int w, int h) {
    width = w;
    height = h;
    mask.assign(static_cast<std::size_t>(w * h), 0);
    active = false;
}

void SelectionMask::clear() {
    std::fill(mask.begin(), mask.end(), 0);
    active = false;
}

void SelectionMask::select_all() {
    std::fill(mask.begin(), mask.end(), 1);
    active = true;
}

void SelectionMask::select_rect(int x0, int y0, int x1, int y1, bool replace) {
    if (replace) {
        std::fill(mask.begin(), mask.end(), 0);
    }
    if (width <= 0 || height <= 0) {
        active = false;
        return;
    }
    int min_x = std::clamp(std::min(x0, x1), 0, width - 1);
    int max_x = std::clamp(std::max(x0, x1), 0, width - 1);
    int min_y = std::clamp(std::min(y0, y1), 0, height - 1);
    int max_y = std::clamp(std::max(y0, y1), 0, height - 1);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            mask[static_cast<std::size_t>(y * width + x)] = 1;
        }
    }
    active = selected_count() > 0;
}

void SelectionMask::select_polygon(const std::vector<std::array<int, 2>>& points, bool replace) {
    if (replace) {
        std::fill(mask.begin(), mask.end(), 0);
    }
    if (points.size() < 3 || width <= 0 || height <= 0) {
        active = selected_count() > 0;
        return;
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float py = static_cast<float>(y) + 0.5f;
            bool inside = false;
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                float xi = static_cast<float>(points[i][0]);
                float yi = static_cast<float>(points[i][1]);
                float xj = static_cast<float>(points[j][0]);
                float yj = static_cast<float>(points[j][1]);
                bool crosses = ((yi > py) != (yj > py)) &&
                               (px < (xj - xi) * (py - yi) / ((yj - yi) + 0.00001f) + xi);
                if (crosses) {
                    inside = !inside;
                }
            }
            if (inside) {
                mask[static_cast<std::size_t>(y * width + x)] = 1;
            }
        }
    }
    active = selected_count() > 0;
}

bool SelectionMask::contains(int x, int y) const {
    if (!active) {
        return true;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return mask[static_cast<std::size_t>(y * width + x)] != 0;
}

int SelectionMask::selected_count() const {
    return static_cast<int>(std::count(mask.begin(), mask.end(), static_cast<std::uint8_t>(1)));
}

std::optional<std::array<int, 4>> SelectionMask::bounds() const {
    if (!active || selected_count() == 0) {
        return std::nullopt;
    }
    int min_x = width;
    int min_y = height;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (mask[static_cast<std::size_t>(y * width + x)] == 0) {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    return std::array<int, 4>{min_x, min_y, max_x, max_y};
}

void FloatingSelection::clear() {
    active = false;
    source_x = source_y = offset_x = offset_y = 0;
    width = height = 0;
    pixels.clear();
    mask.clear();
}

bool FloatingSelection::contains_local(int x, int y) const {
    if (!active || x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return mask[static_cast<std::size_t>(y * width + x)] != 0;
}

std::vector<Pixel> default_palette() {
    return {
        rgba(0, 0, 0), rgba(128, 128, 128), rgba(192, 192, 192), rgba(255, 255, 255),
        rgba(128, 0, 0), rgba(255, 0, 0), rgba(255, 128, 128), rgba(255, 192, 192),
        rgba(128, 64, 0), rgba(255, 128, 0), rgba(255, 192, 128), rgba(255, 224, 192),
        rgba(128, 128, 0), rgba(255, 255, 0), rgba(255, 255, 128), rgba(255, 255, 192),
        rgba(0, 128, 0), rgba(0, 255, 0), rgba(128, 255, 128), rgba(192, 255, 192),
        rgba(0, 128, 128), rgba(0, 255, 255), rgba(128, 255, 255), rgba(192, 255, 255),
        rgba(0, 0, 128), rgba(0, 0, 255), rgba(128, 128, 255), rgba(192, 192, 255),
        rgba(128, 0, 128), rgba(255, 0, 255), rgba(255, 128, 255), rgba(255, 192, 255)
    };
}

Document Document::create(int w, int h) {
    Document doc;
    doc.width = std::max(1, w);
    doc.height = std::max(1, h);
    doc.layers.push_back({"Background", true, 1.0f, LayerBlendMode::Normal});
    Frame frame;
    Cel cel;
    cel.pixels.assign(static_cast<std::size_t>(doc.width * doc.height), 0);
    frame.cels.push_back(std::move(cel));
    doc.frames.push_back(std::move(frame));
    doc.palette.colors = default_palette();
    doc.selection.resize(doc.width, doc.height);
    return doc;
}

const char* playback_mode_name(PlaybackMode mode) {
    switch (mode) {
        case PlaybackMode::Loop: return "Loop";
        case PlaybackMode::PingPong: return "Ping-Pong";
    }
    return "Loop";
}

const char* layer_blend_mode_name(LayerBlendMode mode) {
    switch (mode) {
        case LayerBlendMode::Normal: return "Normal";
        case LayerBlendMode::Multiply: return "Multiply";
        case LayerBlendMode::Additive: return "Additive";
        case LayerBlendMode::ColorBurn: return "Color Burn";
        case LayerBlendMode::ColorDodge: return "Color Dodge";
        case LayerBlendMode::Reflect: return "Reflect";
        case LayerBlendMode::Glow: return "Glow";
        case LayerBlendMode::Overlay: return "Overlay";
        case LayerBlendMode::Difference: return "Difference";
        case LayerBlendMode::Negation: return "Negation";
        case LayerBlendMode::Lighten: return "Lighten";
        case LayerBlendMode::Darken: return "Darken";
        case LayerBlendMode::Screen: return "Screen";
        case LayerBlendMode::Xor: return "Xor";
    }
    return "Normal";
}

std::uint8_t clamp_channel(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

int multiply_channel(int base, int blend) {
    return (base * blend) / 255;
}

int screen_channel(int base, int blend) {
    return 255 - ((255 - base) * (255 - blend)) / 255;
}

int color_burn_channel(int base, int blend) {
    return blend == 0 ? 0 : std::max(0, 255 - ((255 - base) * 255) / blend);
}

int color_dodge_channel(int base, int blend) {
    return blend == 255 ? 255 : std::min(255, (base * 255) / (255 - blend));
}

int reflect_channel(int base, int blend) {
    if (blend == 0) {
        return base;
    }
    if (blend == 255) {
        return 255;
    }
    return std::min(255, (base * base) / (255 - blend));
}

int glow_channel(int base, int blend) {
    if (blend == 0) {
        return base;
    }
    if (base == 255) {
        return 255;
    }
    return std::min(255, (blend * blend) / (255 - base));
}

int overlay_channel(int base, int blend) {
    return blend < 128 ? (2 * base * blend) / 255
                       : 255 - (2 * (255 - base) * (255 - blend)) / 255;
}

Pixel apply_layer_blend_mode(Pixel base, Pixel blend, LayerBlendMode mode) {
    auto apply = [&](auto fn) {
        return rgba(clamp_channel(fn(r(base), r(blend))),
                    clamp_channel(fn(g(base), g(blend))),
                    clamp_channel(fn(b(base), b(blend))),
                    a(blend));
    };

    switch (mode) {
        case LayerBlendMode::Normal:
            return blend;
        case LayerBlendMode::Multiply:
            return apply(multiply_channel);
        case LayerBlendMode::Additive:
            return apply([](int base_channel, int blend_channel) { return base_channel + blend_channel; });
        case LayerBlendMode::ColorBurn:
            return apply(color_burn_channel);
        case LayerBlendMode::ColorDodge:
            return apply(color_dodge_channel);
        case LayerBlendMode::Reflect:
            return apply(reflect_channel);
        case LayerBlendMode::Glow:
            return apply(glow_channel);
        case LayerBlendMode::Overlay:
            return apply(overlay_channel);
        case LayerBlendMode::Difference:
            return apply([](int base_channel, int blend_channel) { return std::abs(base_channel - blend_channel); });
        case LayerBlendMode::Negation:
            return apply([](int base_channel, int blend_channel) { return 255 - std::abs(255 - base_channel - blend_channel); });
        case LayerBlendMode::Lighten:
            return apply([](int base_channel, int blend_channel) { return std::max(base_channel, blend_channel); });
        case LayerBlendMode::Darken:
            return apply([](int base_channel, int blend_channel) { return std::min(base_channel, blend_channel); });
        case LayerBlendMode::Screen:
            return apply(screen_channel);
        case LayerBlendMode::Xor:
            return apply([](int base_channel, int blend_channel) { return base_channel ^ blend_channel; });
    }
    return blend;
}

std::vector<TileDiff> make_tile_diffs(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int frame,
                                      int layer,
                                      int tile_size) {
    std::vector<TileDiff> diffs;
    if (before.size() != after.size() || width <= 0 || height <= 0) {
        return diffs;
    }
    for (int ty = 0; ty < height; ty += tile_size) {
        for (int tx = 0; tx < width; tx += tile_size) {
            int tw = std::min(tile_size, width - tx);
            int th = std::min(tile_size, height - ty);
            bool changed = false;
            for (int y = 0; y < th && !changed; ++y) {
                for (int x = 0; x < tw; ++x) {
                    std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    if (before[i] != after[i]) {
                        changed = true;
                        break;
                    }
                }
            }
            if (!changed) {
                continue;
            }
            TileDiff diff;
            diff.frame = frame;
            diff.layer = layer;
            diff.x = tx;
            diff.y = ty;
            diff.w = tw;
            diff.h = th;
            diff.before.reserve(static_cast<std::size_t>(tw * th));
            diff.after.reserve(static_cast<std::size_t>(tw * th));
            for (int y = 0; y < th; ++y) {
                for (int x = 0; x < tw; ++x) {
                    std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    diff.before.push_back(before[i]);
                    diff.after.push_back(after[i]);
                }
            }
            diffs.push_back(std::move(diff));
        }
    }
    return diffs;
}

bool Document::valid() const {
    return width > 0 && height > 0 && !layers.empty() && !frames.empty();
}

bool Document::in_bounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}

int Document::pixel_index(int x, int y) const {
    return y * width + x;
}

Cel& Document::cel(int frame, int layer) {
    return frames.at(static_cast<std::size_t>(frame)).cels.at(static_cast<std::size_t>(layer));
}

const Cel& Document::cel(int frame, int layer) const {
    return frames.at(static_cast<std::size_t>(frame)).cels.at(static_cast<std::size_t>(layer));
}

Cel& Document::active_cel() {
    return cel(active_frame, active_layer);
}

const Cel& Document::active_cel() const {
    return cel(active_frame, active_layer);
}

std::vector<Pixel> Document::snapshot_active_cel() const {
    return active_cel().pixels;
}

void Document::commit_active_cel_edit(const std::string& name, std::vector<Pixel> before) {
    const auto& after = active_cel().pixels;
    if (before == after) {
        return;
    }
    UndoCommand command;
    command.name = name;
    command.frame = active_frame;
    command.layer = active_layer;
    command.before_active_frame = active_frame;
    command.before_active_layer = active_layer;
    command.after_active_frame = active_frame;
    command.after_active_layer = active_layer;
    command.pixel_diffs = make_tile_diffs(before, after, width, height, active_frame, active_layer);
    undo_stack_.push_back(std::move(command));
    if (undo_stack_.size() > 128) {
        undo_stack_.pop_front();
    }
    redo_stack_.clear();
}

void Document::commit_selection_edit(const std::string& name, const SelectionMask& before) {
    if (before.mask == selection.mask && before.active == selection.active) {
        return;
    }
    UndoCommand command;
    command.name = name;
    command.before_selection = before;
    command.after_selection = selection;
    command.before_active_frame = command.after_active_frame = active_frame;
    command.before_active_layer = command.after_active_layer = active_layer;
    undo_stack_.push_back(std::move(command));
    redo_stack_.clear();
}

void Document::commit_palette_edit(const std::string& name, const Palette& before) {
    if (before.colors == palette.colors && before.active == palette.active) {
        return;
    }
    UndoCommand command;
    command.name = name;
    command.before_palette = before;
    command.after_palette = palette;
    command.before_active_frame = command.after_active_frame = active_frame;
    command.before_active_layer = command.after_active_layer = active_layer;
    undo_stack_.push_back(std::move(command));
    redo_stack_.clear();
}

void Document::commit_structure_edit(const std::string& name,
                                     std::vector<Layer> before_layers,
                                     std::vector<Frame> before_frames,
                                     std::vector<AnimationTag> before_tags,
                                     int before_active_layer,
                                     int before_active_frame) {
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
    redo_stack_.clear();
}

static void apply_tile_diffs(Document& doc, const std::vector<TileDiff>& diffs, bool use_after) {
    for (const TileDiff& diff : diffs) {
        if (diff.frame < 0 || diff.layer < 0 ||
            diff.frame >= static_cast<int>(doc.frames.size()) ||
            diff.layer >= static_cast<int>(doc.layers.size())) {
            continue;
        }
        auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
        const auto& source = use_after ? diff.after : diff.before;
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

bool Document::undo() {
    if (undo_stack_.empty()) {
        return false;
    }
    UndoCommand cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    if (cmd.before_layers) layers = *cmd.before_layers;
    if (cmd.before_frames) frames = *cmd.before_frames;
    if (cmd.before_tags) tags = *cmd.before_tags;
    if (cmd.before_selection) selection = *cmd.before_selection;
    if (cmd.before_palette) palette = *cmd.before_palette;
    apply_tile_diffs(*this, cmd.pixel_diffs, false);
    active_frame = std::clamp(cmd.before_active_frame, 0, static_cast<int>(frames.size()) - 1);
    active_layer = std::clamp(cmd.before_active_layer, 0, static_cast<int>(layers.size()) - 1);
    redo_stack_.push_back(std::move(cmd));
    return true;
}

bool Document::redo() {
    if (redo_stack_.empty()) {
        return false;
    }
    UndoCommand cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    if (cmd.after_layers) layers = *cmd.after_layers;
    if (cmd.after_frames) frames = *cmd.after_frames;
    if (cmd.after_tags) tags = *cmd.after_tags;
    if (cmd.after_selection) selection = *cmd.after_selection;
    if (cmd.after_palette) palette = *cmd.after_palette;
    apply_tile_diffs(*this, cmd.pixel_diffs, true);
    active_frame = std::clamp(cmd.after_active_frame, 0, static_cast<int>(frames.size()) - 1);
    active_layer = std::clamp(cmd.after_active_layer, 0, static_cast<int>(layers.size()) - 1);
    undo_stack_.push_back(std::move(cmd));
    return true;
}

void Document::clear_history() {
    undo_stack_.clear();
    redo_stack_.clear();
}

void Document::add_layer(const std::string& name) {
    auto before_layers = layers;
    auto before_frames = frames;
    auto before_tags = tags;
    int before_active_layer = active_layer;
    int before_active_frame = active_frame;
    layers.push_back({name.empty() ? "Layer" : name, true, 1.0f, LayerBlendMode::Normal});
    for (auto& frame : frames) {
        Cel cel;
        cel.pixels.assign(static_cast<std::size_t>(width * height), 0);
        frame.cels.push_back(std::move(cel));
    }
    active_layer = static_cast<int>(layers.size()) - 1;
    commit_structure_edit("Add Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_active_layer, before_active_frame);
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
    layers.erase(layers.begin() + index);
    for (auto& frame : frames) {
        frame.cels.erase(frame.cels.begin() + index);
    }
    active_layer = std::clamp(active_layer, 0, static_cast<int>(layers.size()) - 1);
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
    for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
        const Layer& layer = layers[layer_index];
        if (!layer.visible || layer.opacity <= 0.0f) {
            continue;
        }
        const Cel& c = frame.cels[layer_index];
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
                src = apply_layer_blend_mode(out[dst_i], src, layer.blend_mode);
                out[dst_i] = blend_over(out[dst_i], src, layer.opacity);
            }
        }
    }
    return out;
}

std::vector<Pixel> Document::composite_active() const {
    return composite_frame(active_frame);
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
    if (pixels.size() != active_cel().pixels.size()) {
        return;
    }
    auto before = snapshot_active_cel();
    active_cel().pixels = std::move(pixels);
    commit_active_cel_edit(undo_name, std::move(before));
}

bool Document::delete_selected_pixels(const std::string& undo_name) {
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
    if (!floating_selection.active) {
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
    if (!floating_selection.active) {
        return;
    }
    auto& pixels = active_cel().pixels;
    selection.clear();
    for (int y = 0; y < floating_selection.height; ++y) {
        for (int x = 0; x < floating_selection.width; ++x) {
            if (!floating_selection.contains_local(x, y)) {
                continue;
            }
            int dx = floating_selection.source_x + floating_selection.offset_x + x;
            int dy = floating_selection.source_y + floating_selection.offset_y + y;
            if (in_bounds(dx, dy)) {
                pixels[static_cast<std::size_t>(pixel_index(dx, dy))] = floating_selection.pixels[static_cast<std::size_t>(y * floating_selection.width + x)];
                selection.select_rect(dx, dy, dx, dy, false);
            }
        }
    }
    floating_selection.clear();
    commit_active_cel_edit(undo_name, std::move(before));
}

} // namespace px
