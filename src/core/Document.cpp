// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"

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
    select_rect(x0, y0, x1, y1, replace ? SelectionCombineMode::Replace : SelectionCombineMode::Add);
}

void SelectionMask::select_rect(int x0, int y0, int x1, int y1, SelectionCombineMode mode) {
    if (width <= 0 || height <= 0) {
        active = false;
        return;
    }
    std::vector<std::uint8_t> source(mask.size(), 0);
    const int requested_min_x = std::min(x0, x1);
    const int requested_max_x = std::max(x0, x1);
    const int requested_min_y = std::min(y0, y1);
    const int requested_max_y = std::max(y0, y1);
    if (requested_max_x < 0 || requested_min_x >= width ||
        requested_max_y < 0 || requested_min_y >= height) {
        combine_with_mask(source, mode);
        return;
    }
    const int min_x = std::clamp(requested_min_x, 0, width - 1);
    const int max_x = std::clamp(requested_max_x, 0, width - 1);
    const int min_y = std::clamp(requested_min_y, 0, height - 1);
    const int max_y = std::clamp(requested_max_y, 0, height - 1);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            source[static_cast<std::size_t>(y * width + x)] = 1;
        }
    }
    combine_with_mask(source, mode);
}

void SelectionMask::select_ellipse(int x0, int y0, int x1, int y1,
                                   SelectionCombineMode mode) {
    if (width <= 0 || height <= 0) {
        active = false;
        return;
    }
    std::vector<std::uint8_t> source(mask.size(), 0);
    const int min_x = std::min(x0, x1);
    const int max_x = std::max(x0, x1);
    const int min_y = std::min(y0, y1);
    const int max_y = std::max(y0, y1);
    const double center_x = (static_cast<double>(min_x) + static_cast<double>(max_x) + 1.0) * 0.5;
    const double center_y = (static_cast<double>(min_y) + static_cast<double>(max_y) + 1.0) * 0.5;
    const double radius_x = std::max(0.5, static_cast<double>(max_x - min_x + 1) * 0.5);
    const double radius_y = std::max(0.5, static_cast<double>(max_y - min_y + 1) * 0.5);
    for (int y = std::max(0, min_y); y <= std::min(height - 1, max_y); ++y) {
        for (int x = std::max(0, min_x); x <= std::min(width - 1, max_x); ++x) {
            const double normalized_x = (static_cast<double>(x) + 0.5 - center_x) / radius_x;
            const double normalized_y = (static_cast<double>(y) + 0.5 - center_y) / radius_y;
            if (normalized_x * normalized_x + normalized_y * normalized_y <= 1.0) {
                source[static_cast<std::size_t>(y * width + x)] = 1;
            }
        }
    }
    combine_with_mask(source, mode);
}

void SelectionMask::select_polygon(const std::vector<std::array<int, 2>>& points, bool replace) {
    select_polygon(points, replace ? SelectionCombineMode::Replace : SelectionCombineMode::Add);
}

void SelectionMask::select_polygon(const std::vector<std::array<int, 2>>& points, SelectionCombineMode mode) {
    if (points.size() < 3 || width <= 0 || height <= 0) {
        active = selected_count() > 0;
        return;
    }

    std::vector<std::uint8_t> source(mask.size(), 0);
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
                source[static_cast<std::size_t>(y * width + x)] = 1;
            }
        }
    }
    combine_with_mask(source, mode);
}

void SelectionMask::combine_with_mask(const std::vector<std::uint8_t>& source, SelectionCombineMode mode) {
    if (source.size() != mask.size()) {
        return;
    }
    switch (mode) {
        case SelectionCombineMode::Replace:
            mask = source;
            break;
        case SelectionCombineMode::Add:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                mask[i] = static_cast<std::uint8_t>(mask[i] != 0 || source[i] != 0);
            }
            break;
        case SelectionCombineMode::Subtract:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                if (source[i] != 0) {
                    mask[i] = 0;
                }
            }
            break;
        case SelectionCombineMode::Intersect:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                mask[i] = static_cast<std::uint8_t>(mask[i] != 0 && source[i] != 0);
            }
            break;
        case SelectionCombineMode::Invert:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                if (source[i] != 0) {
                    mask[i] = static_cast<std::uint8_t>(mask[i] == 0);
                }
            }
            break;
    }
    active = selected_count() > 0;
}

void SelectionMask::invert() {
    for (auto& value : mask) {
        value = static_cast<std::uint8_t>(value == 0);
    }
    active = selected_count() > 0;
}

void SelectionMask::translate(int dx, int dy) {
    if (!active || (dx == 0 && dy == 0) || width <= 0 || height <= 0) {
        return;
    }
    std::vector<std::uint8_t> translated(mask.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int src_x = x - dx;
            const int src_y = y - dy;
            if (src_x < 0 || src_y < 0 || src_x >= width || src_y >= height) {
                continue;
            }
            translated[static_cast<std::size_t>(y * width + x)] =
                mask[static_cast<std::size_t>(src_y * width + src_x)];
        }
    }
    mask = std::move(translated);
    active = selected_count() > 0;
}

void SelectionMask::expand(int radius) {
    radius = std::max(0, radius);
    if (!active || radius == 0 || width <= 0 || height <= 0) return;
    const std::vector<std::uint8_t> source = mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool selected = false;
            for (int dy = -radius; dy <= radius && !selected; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sample_x = x + dx;
                    const int sample_y = y + dy;
                    if (sample_x >= 0 && sample_y >= 0 && sample_x < width && sample_y < height &&
                        source[static_cast<std::size_t>(sample_y * width + sample_x)] != 0) {
                        selected = true;
                        break;
                    }
                }
            }
            mask[static_cast<std::size_t>(y * width + x)] = selected ? 1 : 0;
        }
    }
    active = selected_count() > 0;
}

void SelectionMask::contract(int radius) {
    radius = std::max(0, radius);
    if (!active || radius == 0 || width <= 0 || height <= 0) return;
    const std::vector<std::uint8_t> source = mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool selected = true;
            for (int dy = -radius; dy <= radius && selected; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sample_x = x + dx;
                    const int sample_y = y + dy;
                    if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height ||
                        source[static_cast<std::size_t>(sample_y * width + sample_x)] == 0) {
                        selected = false;
                        break;
                    }
                }
            }
            mask[static_cast<std::size_t>(y * width + x)] = selected ? 1 : 0;
        }
    }
    active = selected_count() > 0;
}

void SelectionMask::select_border(int radius) {
    radius = std::max(1, radius);
    if (!active) return;
    SelectionMask expanded = *this;
    SelectionMask contracted = *this;
    expanded.expand(radius);
    contracted.contract(radius);
    for (std::size_t index = 0; index < mask.size(); ++index) {
        mask[index] = expanded.mask[index] != 0 && contracted.mask[index] == 0 ? 1 : 0;
    }
    active = selected_count() > 0;
}

void SelectionMask::smooth(int radius) {
    radius = std::max(0, radius);
    if (!active || radius == 0 || width <= 0 || height <= 0) return;
    const std::vector<std::uint8_t> source = mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int selected = 0;
            int samples = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sample_x = x + dx;
                    const int sample_y = y + dy;
                    if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height)
                        continue;
                    ++samples;
                    if (source[static_cast<std::size_t>(sample_y * width + sample_x)] != 0) ++selected;
                }
            }
            mask[static_cast<std::size_t>(y * width + x)] = selected * 2 >= samples ? 1 : 0;
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
    return static_cast<int>(std::count_if(mask.begin(), mask.end(),
                                         [](std::uint8_t value) { return value != 0; }));
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

namespace {

constexpr int kHistoryTileSize = 16;
constexpr std::size_t kDefaultDenseHistoryMinBytes = 64U * 1024U * 1024U;
constexpr std::size_t kDefaultHistorySpillBytes = 64U * 1024U * 1024U;
constexpr std::size_t kHistoryIoChunkBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint64_t kHistoryBlockVersion = 1;
constexpr std::uint64_t kHistoryBlockFixedHeaderBytes = 8U + 9U * 8U;
constexpr char kHistoryJournalMagic[8] = {'P', 'X', 'H', 'I', 'S', 'T', '2', '\n'};
constexpr char kHistoryBlockMagic[8] = {'P', 'X', 'H', 'B', 'L', 'K', '2', '\n'};

struct TileDiffStats {
    std::size_t total_tiles = 0;
    std::size_t changed_tiles = 0;
    std::size_t payload_pixels = 0;

    bool any_changed() const {
        return changed_tiles > 0;
    }
};

std::size_t pixel_byte_count(std::size_t pixel_count) {
    if (pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(Pixel)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return pixel_count * sizeof(Pixel);
}

std::size_t env_size_bytes(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    std::istringstream in(value);
    unsigned long long parsed = 0;
    if (!(in >> parsed)) {
        return fallback;
    }
    const unsigned long long max_value =
        static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max());
    if (parsed > max_value) {
        return std::numeric_limits<std::size_t>::max();
    }
    return static_cast<std::size_t>(parsed);
}

std::size_t dense_history_min_bytes() {
    return env_size_bytes("PIXELART_DENSE_HISTORY_MIN_BYTES", kDefaultDenseHistoryMinBytes);
}

std::size_t history_spill_bytes() {
    return env_size_bytes("PIXELART_HISTORY_SPILL_BYTES", kDefaultHistorySpillBytes);
}

TileDiffStats analyze_tile_diff_stats(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int tile_size = kHistoryTileSize) {
    TileDiffStats stats;
    if (before.size() != after.size() || width <= 0 || height <= 0 || tile_size <= 0) {
        return stats;
    }
    for (int ty = 0; ty < height; ty += tile_size) {
        for (int tx = 0; tx < width; tx += tile_size) {
            ++stats.total_tiles;
            const int tw = std::min(tile_size, width - tx);
            const int th = std::min(tile_size, height - ty);
            bool changed = false;
            for (int y = 0; y < th && !changed; ++y) {
                for (int x = 0; x < tw; ++x) {
                    const std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    if (before[i] != after[i]) {
                        changed = true;
                        break;
                    }
                }
            }
            if (changed) {
                ++stats.changed_tiles;
                stats.payload_pixels += static_cast<std::size_t>(tw) * static_cast<std::size_t>(th);
            }
        }
    }
    return stats;
}

bool should_use_dense_pixel_history(const TileDiffStats& stats, std::size_t full_pixel_count) {
    if (!stats.any_changed() || full_pixel_count == 0) {
        return false;
    }
    const std::size_t payload_bytes = pixel_byte_count(stats.payload_pixels);
    if (payload_bytes < dense_history_min_bytes()) {
        return false;
    }
    return stats.payload_pixels >= (full_pixel_count / 4U);
}

std::uint64_t checksum_update(std::uint64_t checksum, const char* data, std::size_t byte_count) {
    for (std::size_t i = 0; i < byte_count; ++i) {
        checksum ^= static_cast<unsigned char>(data[i]);
        checksum *= kFnvPrime;
    }
    return checksum;
}

void write_u64(std::ostream& out, std::uint64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_u64_at(std::fstream& file, std::uint64_t offset, std::uint64_t value) {
    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    write_u64(file, value);
}

std::uint64_t encode_i32(int value) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
}

int current_process_id() {
#if defined(_WIN32)
    return _getpid();
#else
    return getpid();
#endif
}

std::filesystem::path history_journal_directory() {
    if (const char* path = std::getenv("PIXELART_HISTORY_DIR"); path != nullptr && path[0] != '\0') {
        return std::filesystem::path(path);
    }
    return std::filesystem::temp_directory_path() / "pixelart_history";
}

std::filesystem::path history_journal_path() {
    if (const char* path = std::getenv("PIXELART_HISTORY_JOURNAL_FILE"); path != nullptr && path[0] != '\0') {
        return std::filesystem::path(path);
    }
    static const std::filesystem::path path = [] {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::ostringstream name;
        name << "pixelart_history_" << current_process_id() << '_' << millis << ".pxhist";
        return history_journal_directory() / name.str();
    }();
    return path;
}

void apply_private_permissions(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    const auto permissions = directory
                                 ? std::filesystem::perms::owner_all
                                 : (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::filesystem::permissions(path, permissions, std::filesystem::perm_options::replace, error);
}

void ensure_history_journal_lock(const std::filesystem::path& journal_path) {
#if !defined(_WIN32)
    static int lock_fd = -1;
    if (lock_fd >= 0) {
        return;
    }
    std::filesystem::path lock_path = journal_path;
    lock_path += ".lock";
    lock_fd = open(lock_path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (lock_fd >= 0) {
        static_cast<void>(flock(lock_fd, LOCK_EX));
        apply_private_permissions(lock_path, false);
    }
#else
    (void)journal_path;
#endif
}

bool ensure_history_journal_file_locked(const std::filesystem::path& journal_path) {
    const std::filesystem::path parent = journal_path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        apply_private_permissions(parent, true);
    }
    ensure_history_journal_lock(journal_path);

    std::error_code error;
    const bool has_existing_header =
        std::filesystem::exists(journal_path, error) && std::filesystem::file_size(journal_path, error) > 0U;
    if (has_existing_header) {
        apply_private_permissions(journal_path, false);
        return true;
    }

    std::ofstream out(journal_path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(kHistoryJournalMagic, sizeof(kHistoryJournalMagic));
    out.flush();
    apply_private_permissions(journal_path, false);
    return static_cast<bool>(out);
}

std::mutex& history_journal_mutex() {
    static std::mutex mutex;
    return mutex;
}

HistoryPayloadRef append_pixels_to_history_journal(const Pixel* pixels,
                                                   std::size_t pixel_count,
                                                   int frame,
                                                   int layer,
                                                   int width,
                                                   int height,
                                                   std::string_view label) {
    HistoryPayloadRef ref;
    if (pixels == nullptr || pixel_count == 0) {
        return ref;
    }

    std::lock_guard lock(history_journal_mutex());
    const std::filesystem::path journal_path = history_journal_path();
    if (!ensure_history_journal_file_locked(journal_path)) {
        return ref;
    }

    std::error_code error;
    const std::uint64_t block_offset =
        static_cast<std::uint64_t>(std::filesystem::file_size(journal_path, error));
    if (error) {
        return ref;
    }
    const std::uint64_t label_size = static_cast<std::uint64_t>(label.size());
    const std::uint64_t data_offset = block_offset + kHistoryBlockFixedHeaderBytes + label_size;
    const std::size_t total_bytes = pixel_byte_count(pixel_count);
    const std::uint64_t byte_count = static_cast<std::uint64_t>(total_bytes);
    const std::uint64_t checksum_offset = block_offset + 8U + 7U * 8U;

    std::fstream file(journal_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        return ref;
    }
    file.seekp(static_cast<std::streamoff>(block_offset), std::ios::beg);
    file.write(kHistoryBlockMagic, sizeof(kHistoryBlockMagic));
    write_u64(file, kHistoryBlockVersion);
    write_u64(file, encode_i32(frame));
    write_u64(file, encode_i32(layer));
    write_u64(file, encode_i32(width));
    write_u64(file, encode_i32(height));
    write_u64(file, static_cast<std::uint64_t>(pixel_count));
    write_u64(file, byte_count);
    write_u64(file, 0);
    write_u64(file, label_size);
    file.write(label.data(), static_cast<std::streamsize>(label.size()));

    const char* bytes = reinterpret_cast<const char*>(pixels);
    std::size_t written = 0;
    std::uint64_t checksum = kFnvOffset;
    while (written < total_bytes) {
        const std::size_t chunk = std::min(kHistoryIoChunkBytes, total_bytes - written);
        file.write(bytes + written, static_cast<std::streamsize>(chunk));
        if (!file) {
            return {};
        }
        checksum = checksum_update(checksum, bytes + written, chunk);
        written += chunk;
    }
    write_u64_at(file, checksum_offset, checksum);
    file.flush();
    if (!file) {
        return {};
    }

    ref.journal_path = journal_path.string();
    ref.data_offset = data_offset;
    ref.pixel_count = static_cast<std::uint64_t>(pixel_count);
    ref.checksum = checksum;
    memory_trace_event("counter",
                       {},
                       "history_journal.spilled_pixels",
                       nullptr,
                       pixel_count,
                       pixel_count,
                       sizeof(Pixel),
                       label);
    return ref;
}

bool read_pixels_from_history_journal(const HistoryPayloadRef& ref, Pixel* pixels, std::size_t pixel_count) {
    if (!ref.valid() || pixels == nullptr || ref.pixel_count != static_cast<std::uint64_t>(pixel_count)) {
        return false;
    }
    std::ifstream file(ref.journal_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.seekg(static_cast<std::streamoff>(ref.data_offset), std::ios::beg);
    char* bytes = reinterpret_cast<char*>(pixels);
    const std::size_t total_bytes = pixel_byte_count(pixel_count);
    std::size_t read = 0;
    std::uint64_t checksum = kFnvOffset;
    while (read < total_bytes) {
        const std::size_t chunk = std::min(kHistoryIoChunkBytes, total_bytes - read);
        file.read(bytes + read, static_cast<std::streamsize>(chunk));
        if (file.gcount() != static_cast<std::streamsize>(chunk)) {
            return false;
        }
        checksum = checksum_update(checksum, bytes + read, chunk);
        read += chunk;
    }
    return checksum == ref.checksum;
}

bool should_spill_history_payload(std::size_t pixel_count) {
    return pixel_count > 0 && pixel_byte_count(pixel_count) >= history_spill_bytes();
}

void clear_payload_memory(HistoryPixelPayload& payload) {
    std::vector<Pixel>().swap(payload.pixels);
}

bool store_history_payload(HistoryPixelPayload& payload,
                           const Pixel* pixels,
                           std::size_t pixel_count,
                           int frame,
                           int layer,
                           int width,
                           int height,
                           std::string_view label) {
    payload = {};
    payload.pixel_count = static_cast<std::uint64_t>(pixel_count);
    if (pixel_count == 0) {
        return true;
    }
    if (should_spill_history_payload(pixel_count)) {
        payload.ref = append_pixels_to_history_journal(pixels, pixel_count, frame, layer, width, height, label);
        if (payload.ref.valid()) {
            return true;
        }
        memory_trace_note("history_journal.spill_failed", label);
    }
    payload.pixels.assign(pixels, pixels + pixel_count);
    return payload.pixels.size() == pixel_count;
}

bool dense_diff_target_valid(const Document& doc, const DensePixelDiff& diff) {
    return diff.frame >= 0 &&
           diff.layer >= 0 &&
           diff.frame < static_cast<int>(doc.frames.size()) &&
           diff.layer < static_cast<int>(doc.layers.size());
}

bool apply_history_payload(Document& doc, const DensePixelDiff& diff, const HistoryPixelPayload& payload) {
    if (!dense_diff_target_valid(doc, diff)) {
        return false;
    }
    const std::size_t expected_pixels = static_cast<std::size_t>(std::max(0, diff.width)) *
                                        static_cast<std::size_t>(std::max(0, diff.height));
    if (payload.pixel_count != static_cast<std::uint64_t>(expected_pixels)) {
        return false;
    }
    auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
    if (pixels.size() != expected_pixels) {
        pixels.assign(expected_pixels, 0);
    }
    if (payload.ref.valid()) {
        return read_pixels_from_history_journal(payload.ref, pixels.data(), expected_pixels);
    }
    if (payload.pixels.size() != expected_pixels) {
        return false;
    }
    std::copy(payload.pixels.begin(), payload.pixels.end(), pixels.begin());
    return true;
}

bool capture_dense_after_pixels(Document& doc, DensePixelDiff& diff) {
    if (!dense_diff_target_valid(doc, diff)) {
        return false;
    }
    const auto& pixels = doc.cel(diff.frame, diff.layer).pixels;
    return store_history_payload(diff.after,
                                 pixels.data(),
                                 pixels.size(),
                                 diff.frame,
                                 diff.layer,
                                 diff.width,
                                 diff.height,
                                 "dense_after");
}

void release_dense_after_pixels(DensePixelDiff& diff) {
    clear_payload_memory(diff.after);
    diff.after = {};
}

DensePixelDiff make_dense_pixel_diff(const std::vector<Pixel>& before,
                                     int frame,
                                     int layer,
                                     int width,
                                     int height,
                                     std::string_view label) {
    DensePixelDiff diff;
    diff.frame = frame;
    diff.layer = layer;
    diff.width = width;
    diff.height = height;
    static_cast<void>(store_history_payload(diff.before,
                                            before.data(),
                                            before.size(),
                                            frame,
                                            layer,
                                            width,
                                            height,
                                            label));
    return diff;
}

void trace_tile_diff_storage(std::string_view prefix, const std::vector<TileDiff>& diffs) {
    memory_trace_event("vector",
                       {},
                       std::string(prefix) + ".pixel_diffs",
                       diffs.empty() ? nullptr : diffs.data(),
                       diffs.size(),
                       diffs.capacity(),
                       sizeof(TileDiff));
    std::size_t diff_payload_pixels = 0;
    for (const TileDiff& diff : diffs) {
        diff_payload_pixels += diff.before.capacity() + diff.after.capacity();
    }
    memory_trace_event("counter",
                       {},
                       std::string(prefix) + ".pixel_diff_payload_pixels",
                       nullptr,
                       diff_payload_pixels,
                       diff_payload_pixels,
                       sizeof(Pixel));
}

void trace_dense_diff_storage(std::string_view prefix, const DensePixelDiff& diff) {
    memory_trace_event("counter",
                       {},
                       std::string(prefix) + ".dense_before_pixels",
                       nullptr,
                       static_cast<std::size_t>(diff.before.pixel_count),
                       static_cast<std::size_t>(diff.before.pixel_count),
                       sizeof(Pixel),
                       diff.before.ref.valid() ? "disk" : "memory");
}

} // namespace

std::vector<TileDiff> make_tile_diffs(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int frame,
                                      int layer,
                                      int tile_size,
                                      bool store_after_pixels) {
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
            if (store_after_pixels) {
                diff.after.reserve(static_cast<std::size_t>(tw * th));
            }
            for (int y = 0; y < th; ++y) {
                for (int x = 0; x < tw; ++x) {
                    std::size_t i = static_cast<std::size_t>((ty + y) * width + tx + x);
                    diff.before.push_back(before[i]);
                    if (store_after_pixels) {
                        diff.after.push_back(after[i]);
                    }
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

bool Document::materialize_history_payload(const HistoryPixelPayload& payload,
                                           std::vector<Pixel>& pixels) const {
    if (!payload.pixels.empty()) {
        pixels = payload.pixels;
        return payload.pixel_count == 0U ||
               payload.pixel_count == static_cast<std::uint64_t>(pixels.size());
    }
    if (!payload.ref.valid()) {
        pixels.clear();
        return payload.pixel_count == 0U;
    }
    if (payload.pixel_count > static_cast<std::uint64_t>(
                                  std::numeric_limits<std::size_t>::max())) {
        pixels.clear();
        return false;
    }
    pixels.resize(static_cast<std::size_t>(payload.pixel_count));
    if (!read_pixels_from_history_journal(payload.ref, pixels.data(), pixels.size())) {
        pixels.clear();
        return false;
    }
    return true;
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
