// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/GLTiledCanvasTexture.hpp"

#include "core/MemoryTrace.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace px {

namespace {

ImTextureID gl_imgui_texture_id(unsigned int id) {
    return static_cast<ImTextureID>(static_cast<unsigned long long>(id));
}

int ceil_div(int value, int divisor) {
    return (value + divisor - 1) / divisor;
}

Pixel average_pixels(const std::vector<Pixel>& pixels, int width, int height, int x, int y) {
    const int x0 = std::min(width - 1, x * 2);
    const int y0 = std::min(height - 1, y * 2);
    const int x1 = std::min(width - 1, x0 + 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const Pixel p0 = pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x0)];
    const Pixel p1 = pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x1)];
    const Pixel p2 = pixels[static_cast<std::size_t>(y1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x0)];
    const Pixel p3 = pixels[static_cast<std::size_t>(y1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x1)];
    return rgba(static_cast<std::uint8_t>((static_cast<unsigned int>(r(p0)) + r(p1) + r(p2) + r(p3) + 2U) / 4U),
                static_cast<std::uint8_t>((static_cast<unsigned int>(g(p0)) + g(p1) + g(p2) + g(p3) + 2U) / 4U),
                static_cast<std::uint8_t>((static_cast<unsigned int>(b(p0)) + b(p1) + b(p2) + b(p3) + 2U) / 4U),
                static_cast<std::uint8_t>((static_cast<unsigned int>(a(p0)) + a(p1) + a(p2) + a(p3) + 2U) / 4U));
}

int pyramid_level_count(int width, int height) {
    constexpr int kMinimumLargestDimension = 512;
    int levels = 0;
    while (std::max(width, height) > kMinimumLargestDimension) {
        width = std::max(1, ceil_div(width, 2));
        height = std::max(1, ceil_div(height, 2));
        ++levels;
    }
    return levels;
}

} // namespace

std::size_t GLTiledCanvasTexture::CpuPyramid::pixel_count() const {
    std::size_t total = 0;
    for (const CpuPyramidLevel& level : levels) {
        total += level.pixels.size();
    }
    return total;
}

GLTiledCanvasTexture::~GLTiledCanvasTexture() {
    destroy();
}

bool GLTiledCanvasTexture::should_use_pyramid(int document_width, int document_height) {
    if (document_width <= 0 || document_height <= 0) {
        return false;
    }
    return static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height) >= kPyramidMinPixels;
}

GLTiledCanvasTexture::CpuPyramid GLTiledCanvasTexture::build_cpu_pyramid(int document_width,
                                                                         int document_height,
                                                                         const std::vector<Pixel>& pixels,
                                                                         const PyramidProgressCallback& progress) {
    return build_cpu_pyramid_with_min_pixels(document_width, document_height, pixels, kPyramidMinPixels, progress);
}

GLTiledCanvasTexture::CpuPyramid GLTiledCanvasTexture::build_cpu_pyramid_with_min_pixels(int document_width,
                                                                                         int document_height,
                                                                                         const std::vector<Pixel>& pixels,
                                                                                         std::size_t min_pixels,
                                                                                         const PyramidProgressCallback& progress) {
    MemoryTraceScope trace("GLTiledCanvasTexture::build_cpu_pyramid");
    memory_trace_vector("build_cpu_pyramid.source", pixels);
    CpuPyramid pyramid;
    pyramid.width = document_width;
    pyramid.height = document_height;
    const std::size_t expected_size = static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height);
    if (document_width <= 0 || document_height <= 0 || expected_size < min_pixels || pixels.size() != expected_size) {
        return pyramid;
    }

    const int total = pyramid_level_count(document_width, document_height);
    int previous_width = document_width;
    int previous_height = document_height;
    const std::vector<Pixel>* previous_pixels = &pixels;
    pyramid.levels.reserve(static_cast<std::size_t>(total));

    for (int level = 1; level <= total; ++level) {
        const int next_width = std::max(1, ceil_div(previous_width, 2));
        const int next_height = std::max(1, ceil_div(previous_height, 2));
        CpuPyramidLevel next;
        next.width = next_width;
        next.height = next_height;
        next.pixels.resize(static_cast<std::size_t>(next_width) * static_cast<std::size_t>(next_height));
        memory_trace_vector("build_cpu_pyramid.next_level_allocated", next.pixels);
        if (progress) {
            progress(level - 1, total, "Building image pyramid");
        }
        for (int y = 0; y < next_height; ++y) {
            for (int x = 0; x < next_width; ++x) {
                next.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(next_width) + static_cast<std::size_t>(x)] =
                    average_pixels(*previous_pixels, previous_width, previous_height, x, y);
            }
        }
        pyramid.levels.push_back(std::move(next));
        memory_trace_vector("build_cpu_pyramid.stored_level", pyramid.levels.back().pixels);
        previous_width = pyramid.levels.back().width;
        previous_height = pyramid.levels.back().height;
        previous_pixels = &pyramid.levels.back().pixels;
        if (progress) {
            progress(level, total, "Building image pyramid");
        }
    }

    memory_trace_event("counter",
                       {},
                       "build_cpu_pyramid.total_pixels",
                       nullptr,
                       pyramid.pixel_count(),
                       pyramid.pixel_count(),
                       sizeof(Pixel));
    return pyramid;
}

void GLTiledCanvasTexture::invalidate() {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
        for (Level& level : levels_) {
            for (Tile& tile : level.tiles) {
                tile.generation = 0;
            }
            if (level.downsample == 1) {
                level.source_generation = generation_;
            }
        }
    }
    if (!levels_.empty()) {
        levels_.front().source_generation = generation_;
    }
}

void GLTiledCanvasTexture::upload_region_now(int document_width,
                                             int document_height,
                                             const std::vector<Pixel>& pixels,
                                             int x,
                                             int y,
                                             int width,
                                             int height) {
    MemoryTraceScope trace("GLTiledCanvasTexture::upload_region_now");
    memory_trace_vector("upload_region_now.pixels", pixels);
    const std::size_t expected_size = static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height);
    if (document_width <= 0 || document_height <= 0 || width <= 0 || height <= 0 || pixels.size() != expected_size) {
        return;
    }
    if (document_width != document_width_ || document_height != document_height_ || levels_.empty()) {
        reset_levels(document_width, document_height);
    }
    if (levels_.empty()) {
        return;
    }

    Level& level = levels_.front();
    if (level.downsample != 1 || level.columns <= 0 || level.rows <= 0) {
        return;
    }
    const int min_x = std::clamp(x, 0, document_width);
    const int min_y = std::clamp(y, 0, document_height);
    const int max_x = std::clamp(x + width, 0, document_width);
    const int max_y = std::clamp(y + height, 0, document_height);
    if (max_x <= min_x || max_y <= min_y) {
        return;
    }

    const int first_tile_x = std::clamp(min_x / kTileSize, 0, level.columns - 1);
    const int first_tile_y = std::clamp(min_y / kTileSize, 0, level.rows - 1);
    const int last_tile_x = std::clamp((max_x - 1) / kTileSize, 0, level.columns - 1);
    const int last_tile_y = std::clamp((max_y - 1) / kTileSize, 0, level.rows - 1);
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            Tile& tile = tile_at(level, tile_x, tile_y);
            upload_tile(level, tile, pixels, document_width, generation_);
        }
    }
}

void GLTiledCanvasTexture::draw_visible(ImDrawList* draw_list,
                                        int document_width,
                                        int document_height,
                                        const std::vector<Pixel>& pixels,
                                        ImVec2 canvas_origin,
                                        float zoom,
                                        ImVec2 viewport_min,
                                        ImVec2 viewport_max,
                                        ImU32 tint) {
    MemoryTraceScope trace("GLTiledCanvasTexture::draw_visible");
    memory_trace_vector("draw_visible.base_pixels", pixels);
    last_draw_stats_ = {};
    if (draw_list == nullptr || document_width <= 0 || document_height <= 0 || zoom <= 0.0f ||
        pixels.size() != static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height)) {
        return;
    }
    if (document_width != document_width_ || document_height != document_height_ || levels_.empty()) {
        reset_levels(document_width, document_height);
    }

    const int selected_level = choose_level(zoom);
    last_draw_stats_.selected_level = selected_level;
    last_draw_stats_.drew_lod = selected_level > 0;

    upload_queue_.clear();
    for (int level = static_cast<int>(levels_.size()) - 1; level >= selected_level; --level) {
        enqueue_visible_tiles(level, canvas_origin, zoom, viewport_min, viewport_max, level == selected_level ? 1 : 0);
    }
    process_upload_queue(pixels, document_width);

    for (int level = static_cast<int>(levels_.size()) - 1; level >= selected_level; --level) {
        draw_ready_level(draw_list, level, canvas_origin, zoom, viewport_min, viewport_max, tint);
    }
    last_draw_stats_.pending_tiles = static_cast<int>(std::min<std::size_t>(upload_queue_.size(),
                                                                            static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

void GLTiledCanvasTexture::set_prepared_pyramid(CpuPyramid&& pyramid) {
    MemoryTraceScope trace("GLTiledCanvasTexture::set_prepared_pyramid");
    memory_trace_event("counter",
                       {},
                       "set_prepared_pyramid.source_total_pixels",
                       nullptr,
                       pyramid.pixel_count(),
                       pyramid.pixel_count(),
                       sizeof(Pixel));
    destroy();
    if (pyramid.width <= 0 || pyramid.height <= 0) {
        return;
    }
    document_width_ = pyramid.width;
    document_height_ = pyramid.height;
    add_level(document_width_, document_height_, 1, {}, generation_);
    int downsample = 2;
    for (CpuPyramidLevel& level : pyramid.levels) {
        memory_trace_vector("set_prepared_pyramid.source_level_before_move", level.pixels);
        add_level(level.width, level.height, downsample, std::move(level.pixels), generation_);
        downsample = std::min(downsample * 2, 1 << 30);
    }
    pyramid.levels.clear();
    memory_trace_event("counter",
                       {},
                       "set_prepared_pyramid.stored_total_pixels",
                       nullptr,
                       prepared_pyramid_pixel_count(),
                       prepared_pyramid_pixel_count(),
                       sizeof(Pixel));
}

void GLTiledCanvasTexture::clear_prepared_pyramid() {
    const int width = document_width_;
    const int height = document_height_;
    destroy();
    if (width > 0 && height > 0) {
        reset_levels(width, height);
    }
}

bool GLTiledCanvasTexture::has_prepared_pyramid() const {
    return levels_.size() > 1U;
}

std::size_t GLTiledCanvasTexture::prepared_pyramid_pixel_count() const {
    std::size_t total = 0;
    for (std::size_t i = 1; i < levels_.size(); ++i) {
        total += levels_[i].pixels.size();
    }
    return total;
}

std::size_t GLTiledCanvasTexture::scratch_capacity_pixels() const {
    return scratch_.capacity();
}

void GLTiledCanvasTexture::destroy() {
    for (Level& level : levels_) {
        for (Tile& tile : level.tiles) {
            if (tile.texture_id != 0) {
                glDeleteTextures(1, &tile.texture_id);
                tile.texture_id = 0;
            }
        }
    }
    levels_.clear();
    upload_queue_.clear();
    scratch_.clear();
    document_width_ = 0;
    document_height_ = 0;
    generation_ = 1;
}

GLTiledCanvasTexture::Tile& GLTiledCanvasTexture::tile_at(Level& level, int tile_x, int tile_y) {
    return level.tiles[static_cast<std::size_t>(tile_y * level.columns + tile_x)];
}

const GLTiledCanvasTexture::Tile& GLTiledCanvasTexture::tile_at(const Level& level, int tile_x, int tile_y) const {
    return level.tiles[static_cast<std::size_t>(tile_y * level.columns + tile_x)];
}

void GLTiledCanvasTexture::reset_levels(int document_width, int document_height) {
    destroy();
    document_width_ = document_width;
    document_height_ = document_height;
    add_level(document_width_, document_height_, 1, {}, generation_);
}

void GLTiledCanvasTexture::add_level(int width,
                                     int height,
                                     int downsample,
                                     std::vector<Pixel> pixels,
                                     std::uint64_t source_generation) {
    Level level;
    level.width = width;
    level.height = height;
    level.downsample = std::max(1, downsample);
    level.columns = ceil_div(level.width, kTileSize);
    level.rows = ceil_div(level.height, kTileSize);
    level.source_generation = source_generation;
    level.pixels = std::move(pixels);
    level.tiles.resize(static_cast<std::size_t>(level.columns * level.rows));
    for (int tile_y = 0; tile_y < level.rows; ++tile_y) {
        for (int tile_x = 0; tile_x < level.columns; ++tile_x) {
            Tile& tile = tile_at(level, tile_x, tile_y);
            tile.x = tile_x * kTileSize;
            tile.y = tile_y * kTileSize;
            tile.width = std::min(kTileSize, level.width - tile.x);
            tile.height = std::min(kTileSize, level.height - tile.y);
        }
    }
    levels_.push_back(std::move(level));
}

int GLTiledCanvasTexture::choose_level(float zoom) const {
    if (levels_.empty()) {
        return 0;
    }
    int best = 0;
    float best_score = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(levels_.size()); ++i) {
        const float screen_pixels_per_level_pixel = zoom * static_cast<float>(levels_[static_cast<std::size_t>(i)].downsample);
        const float score = std::abs(std::log2(std::max(0.0001f, screen_pixels_per_level_pixel)));
        if (score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

std::uint64_t GLTiledCanvasTexture::target_generation_for_level(int level_index) const {
    if (level_index < 0 || level_index >= static_cast<int>(levels_.size())) {
        return 0;
    }
    const Level& level = levels_[static_cast<std::size_t>(level_index)];
    return level.downsample == 1 ? generation_ : level.source_generation;
}

void GLTiledCanvasTexture::enqueue_visible_tiles(int level_index,
                                                 ImVec2 canvas_origin,
                                                 float zoom,
                                                 ImVec2 viewport_min,
                                                 ImVec2 viewport_max,
                                                 int margin_tiles) {
    if (level_index < 0 || level_index >= static_cast<int>(levels_.size())) {
        return;
    }
    Level& level = levels_[static_cast<std::size_t>(level_index)];
    const float level_zoom = zoom * static_cast<float>(level.downsample);
    if (level_zoom <= 0.0f) {
        return;
    }
    const int min_x = std::clamp(static_cast<int>(std::floor((viewport_min.x - canvas_origin.x) / level_zoom)) - 1,
                                 0,
                                 level.width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor((viewport_min.y - canvas_origin.y) / level_zoom)) - 1,
                                 0,
                                 level.height - 1);
    const int max_x = std::clamp(static_cast<int>(std::ceil((viewport_max.x - canvas_origin.x) / level_zoom)) + 1,
                                 0,
                                 level.width);
    const int max_y = std::clamp(static_cast<int>(std::ceil((viewport_max.y - canvas_origin.y) / level_zoom)) + 1,
                                 0,
                                 level.height);
    if (max_x <= min_x || max_y <= min_y) {
        return;
    }

    const int first_tile_x = std::clamp(min_x / kTileSize - margin_tiles, 0, level.columns - 1);
    const int first_tile_y = std::clamp(min_y / kTileSize - margin_tiles, 0, level.rows - 1);
    const int last_tile_x = std::clamp((max_x - 1) / kTileSize + margin_tiles, 0, level.columns - 1);
    const int last_tile_y = std::clamp((max_y - 1) / kTileSize + margin_tiles, 0, level.rows - 1);
    const std::uint64_t target_generation = target_generation_for_level(level_index);
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            const int tile_index = tile_y * level.columns + tile_x;
            Tile& tile = level.tiles[static_cast<std::size_t>(tile_index)];
            if (margin_tiles == 0) {
                ++last_draw_stats_.visible_tiles;
            }
            if (tile.generation != target_generation) {
                enqueue_tile(level_index, tile_index);
            }
        }
    }
}

void GLTiledCanvasTexture::enqueue_tile(int level_index, int tile_index) {
    for (const TileKey& key : upload_queue_) {
        if (key.level == level_index && key.tile == tile_index) {
            return;
        }
    }
    upload_queue_.push_back({level_index, tile_index});
}

bool GLTiledCanvasTexture::tile_ready(int level_index, int tile_index) const {
    if (level_index < 0 || level_index >= static_cast<int>(levels_.size())) {
        return false;
    }
    const Level& level = levels_[static_cast<std::size_t>(level_index)];
    if (tile_index < 0 || tile_index >= static_cast<int>(level.tiles.size())) {
        return false;
    }
    const Tile& tile = level.tiles[static_cast<std::size_t>(tile_index)];
    return tile.texture_id != 0 && tile.generation == target_generation_for_level(level_index);
}

void GLTiledCanvasTexture::process_upload_queue(const std::vector<Pixel>& base_pixels, int document_width) {
    MemoryTraceScope trace("GLTiledCanvasTexture::process_upload_queue");
    memory_trace_vector("process_upload_queue.base_pixels", base_pixels);
    const auto start = std::chrono::steady_clock::now();
    int uploaded = 0;
    auto it = upload_queue_.begin();
    while (it != upload_queue_.end() && uploaded < kMaxUploadsPerFrame) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (uploaded > 0 && elapsed >= kUploadBudgetSeconds) {
            break;
        }
        if (it->level < 0 || it->level >= static_cast<int>(levels_.size())) {
            it = upload_queue_.erase(it);
            continue;
        }
        Level& level = levels_[static_cast<std::size_t>(it->level)];
        if (it->tile < 0 || it->tile >= static_cast<int>(level.tiles.size())) {
            it = upload_queue_.erase(it);
            continue;
        }
        Tile& tile = level.tiles[static_cast<std::size_t>(it->tile)];
        const std::uint64_t target_generation = target_generation_for_level(it->level);
        if (tile.generation == target_generation) {
            it = upload_queue_.erase(it);
            continue;
        }
        const std::vector<Pixel>& source = level.downsample == 1 ? base_pixels : level.pixels;
        const int source_width = level.downsample == 1 ? document_width : level.width;
        if (source.empty() || !upload_tile(level, tile, source, source_width, target_generation)) {
            it = upload_queue_.erase(it);
            continue;
        }
        ++uploaded;
        ++last_draw_stats_.tiles_uploaded;
        if (it->level > 0) {
            last_draw_stats_.lod_uploaded = true;
        }
        it = upload_queue_.erase(it);
    }
}

bool GLTiledCanvasTexture::upload_tile(Level& level,
                                       Tile& tile,
                                       const std::vector<Pixel>& source,
                                       int source_width,
                                       std::uint64_t generation) {
    MemoryTraceScope trace("GLTiledCanvasTexture::upload_tile");
    memory_trace_vector("upload_tile.source", source);
    if (tile.width <= 0 || tile.height <= 0 || source_width <= 0) {
        return false;
    }
    scratch_.resize(static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height));
    memory_trace_vector("upload_tile.scratch", scratch_);
    for (int row = 0; row < tile.height; ++row) {
        const std::size_t src = static_cast<std::size_t>(tile.y + row) * static_cast<std::size_t>(source_width) +
                                static_cast<std::size_t>(tile.x);
        const std::size_t dst = static_cast<std::size_t>(row) * static_cast<std::size_t>(tile.width);
        if (src + static_cast<std::size_t>(tile.width) > source.size()) {
            return false;
        }
        std::copy_n(source.data() + src, static_cast<std::size_t>(tile.width), scratch_.data() + dst);
    }

    if (tile.texture_id == 0) {
        glGenTextures(1, &tile.texture_id);
        glBindTexture(GL_TEXTURE_2D, tile.texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile.width, tile.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, tile.texture_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tile.width, tile.height, GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
    }
    tile.generation = generation;
    (void)level;
    return true;
}

void GLTiledCanvasTexture::draw_ready_level(ImDrawList* draw_list,
                                            int level_index,
                                            ImVec2 canvas_origin,
                                            float zoom,
                                            ImVec2 viewport_min,
                                            ImVec2 viewport_max,
                                            ImU32 tint) {
    if (level_index < 0 || level_index >= static_cast<int>(levels_.size())) {
        return;
    }
    const Level& level = levels_[static_cast<std::size_t>(level_index)];
    const float level_zoom = zoom * static_cast<float>(level.downsample);
    const int min_x = std::clamp(static_cast<int>(std::floor((viewport_min.x - canvas_origin.x) / level_zoom)) - 1,
                                 0,
                                 level.width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor((viewport_min.y - canvas_origin.y) / level_zoom)) - 1,
                                 0,
                                 level.height - 1);
    const int max_x = std::clamp(static_cast<int>(std::ceil((viewport_max.x - canvas_origin.x) / level_zoom)) + 1,
                                 0,
                                 level.width);
    const int max_y = std::clamp(static_cast<int>(std::ceil((viewport_max.y - canvas_origin.y) / level_zoom)) + 1,
                                 0,
                                 level.height);
    if (max_x <= min_x || max_y <= min_y) {
        return;
    }
    const int first_tile_x = min_x / kTileSize;
    const int first_tile_y = min_y / kTileSize;
    const int last_tile_x = (max_x - 1) / kTileSize;
    const int last_tile_y = (max_y - 1) / kTileSize;
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            const int tile_index = tile_y * level.columns + tile_x;
            if (tile_index < 0 || tile_index >= static_cast<int>(level.tiles.size())) {
                continue;
            }
            const Tile& tile = tile_at(level, tile_x, tile_y);
            if (tile.texture_id == 0) {
                continue;
            }

            const float source_min_x = static_cast<float>(tile.x * level.downsample);
            const float source_min_y = static_cast<float>(tile.y * level.downsample);
            const float source_max_x = static_cast<float>(std::min(document_width_, (tile.x + tile.width) * level.downsample));
            const float source_max_y = static_cast<float>(std::min(document_height_, (tile.y + tile.height) * level.downsample));
            const ImVec2 min_pos(canvas_origin.x + source_min_x * zoom,
                                 canvas_origin.y + source_min_y * zoom);
            const ImVec2 max_pos(canvas_origin.x + source_max_x * zoom,
                                 canvas_origin.y + source_max_y * zoom);
            draw_list->AddImage(gl_imgui_texture_id(tile.texture_id), min_pos, max_pos, ImVec2(0, 0), ImVec2(1, 1), tint);
        }
    }
}

} // namespace px
