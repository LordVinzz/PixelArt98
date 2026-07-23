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
    Layer background;
    background.name = "Background";
    doc.layers.push_back(std::move(background));
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
bool Document::valid() const {
    return width > 0 && height > 0 && !layers.empty() && !frames.empty();
}

bool Document::has_active_cel() const {
    if (active_frame < 0 || active_frame >= static_cast<int>(frames.size()) ||
        active_layer < 0 || active_layer >= static_cast<int>(layers.size())) {
        return false;
    }
    const Frame& frame = frames[static_cast<std::size_t>(active_frame)];
    if (active_layer >= static_cast<int>(frame.cels.size())) {
        return false;
    }
    const std::size_t expected_size = static_cast<std::size_t>(std::max(0, width)) *
                                      static_cast<std::size_t>(std::max(0, height));
    return frame.cels[static_cast<std::size_t>(active_layer)].pixels.size() == expected_size;
}

bool Document::ensure_active_cel() {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (frames.empty()) {
        frames.push_back(Frame{});
        active_frame = 0;
    } else {
        active_frame = std::clamp(active_frame, 0, static_cast<int>(frames.size()) - 1);
    }
    if (layers.empty()) {
        Layer layer;
        layer.name = "Layer";
        layers.push_back(std::move(layer));
        active_layer = 0;
    } else {
        active_layer = std::clamp(active_layer, 0, static_cast<int>(layers.size()) - 1);
    }

    Frame& frame = frames[static_cast<std::size_t>(active_frame)];
    if (frame.cels.size() < layers.size()) {
        frame.cels.resize(layers.size());
    }
    Cel& active = frame.cels[static_cast<std::size_t>(active_layer)];
    const std::size_t expected_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (active.pixels.size() != expected_size) {
        active.pixels.assign(expected_size, 0);
    }
    return true;
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
    MemoryTraceScope trace("Document::snapshot_active_cel");
    if (!has_active_cel()) {
        return {};
    }
    memory_trace_vector("snapshot_active_cel.source", active_cel().pixels);
    std::vector<Pixel> snapshot = active_cel().pixels;
    memory_trace_vector("snapshot_active_cel.result", snapshot);
    return snapshot;
}
} // namespace px
