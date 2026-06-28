// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Pixel.hpp"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace px {

class GLTiledCanvasTexture {
public:
    struct DrawStats {
        int tiles_uploaded = 0;
        int visible_tiles = 0;
        int pending_tiles = 0;
        int selected_level = 0;
        bool drew_lod = false;
        bool lod_uploaded = false;
    };

    struct CpuPyramidLevel {
        int width = 0;
        int height = 0;
        std::vector<Pixel> pixels;

        CpuPyramidLevel() = default;
        CpuPyramidLevel(const CpuPyramidLevel&) = delete;
        CpuPyramidLevel& operator=(const CpuPyramidLevel&) = delete;
        CpuPyramidLevel(CpuPyramidLevel&&) noexcept = default;
        CpuPyramidLevel& operator=(CpuPyramidLevel&&) noexcept = default;
    };

    struct CpuPyramid {
        int width = 0;
        int height = 0;
        std::vector<CpuPyramidLevel> levels;

        CpuPyramid() = default;
        CpuPyramid(const CpuPyramid&) = delete;
        CpuPyramid& operator=(const CpuPyramid&) = delete;
        CpuPyramid(CpuPyramid&&) noexcept = default;
        CpuPyramid& operator=(CpuPyramid&&) noexcept = default;

        bool empty() const { return levels.empty(); }
        std::size_t pixel_count() const;
    };

    using PyramidProgressCallback = std::function<void(int done, int total, const char* status)>;

    GLTiledCanvasTexture() = default;
    ~GLTiledCanvasTexture();

    GLTiledCanvasTexture(const GLTiledCanvasTexture&) = delete;
    GLTiledCanvasTexture& operator=(const GLTiledCanvasTexture&) = delete;

    void invalidate();
    void upload_region_now(int document_width,
                           int document_height,
                           const std::vector<Pixel>& pixels,
                           int x,
                           int y,
                           int width,
                           int height);
    void draw_visible(ImDrawList* draw_list,
                      int document_width,
                      int document_height,
                      const std::vector<Pixel>& pixels,
                      ImVec2 canvas_origin,
                      float zoom,
                      ImVec2 viewport_min,
                      ImVec2 viewport_max,
                      ImU32 tint = IM_COL32_WHITE);
    const DrawStats& last_draw_stats() const { return last_draw_stats_; }
    void set_prepared_pyramid(CpuPyramid&& pyramid);
    void clear_prepared_pyramid();
    bool has_prepared_pyramid() const;
    std::size_t prepared_pyramid_pixel_count() const;
    std::size_t scratch_capacity_pixels() const;
    static constexpr std::size_t max_tile_scratch_pixels() {
        return static_cast<std::size_t>(kTileSize) * static_cast<std::size_t>(kTileSize);
    }
    void destroy();

    static bool should_use_pyramid(int document_width, int document_height);
    static CpuPyramid build_cpu_pyramid(int document_width,
                                        int document_height,
                                        const std::vector<Pixel>& pixels,
                                        const PyramidProgressCallback& progress = {});
    static CpuPyramid build_cpu_pyramid_with_min_pixels(int document_width,
                                                        int document_height,
                                                        const std::vector<Pixel>& pixels,
                                                        std::size_t min_pixels,
                                                        const PyramidProgressCallback& progress = {});

private:
    struct Tile {
        unsigned int texture_id = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::uint64_t generation = 0;
    };

    struct Level {
        int width = 0;
        int height = 0;
        int downsample = 1;
        int columns = 0;
        int rows = 0;
        std::uint64_t source_generation = 0;
        std::vector<Pixel> pixels;
        std::vector<Tile> tiles;
    };

    struct TileKey {
        int level = 0;
        int tile = 0;
    };

    Tile& tile_at(Level& level, int tile_x, int tile_y);
    const Tile& tile_at(const Level& level, int tile_x, int tile_y) const;
    void reset_levels(int document_width, int document_height);
    void add_level(int width, int height, int downsample, std::vector<Pixel> pixels, std::uint64_t source_generation);
    int choose_level(float zoom) const;
    std::uint64_t target_generation_for_level(int level_index) const;
    void enqueue_visible_tiles(int level_index,
                               ImVec2 canvas_origin,
                               float zoom,
                               ImVec2 viewport_min,
                               ImVec2 viewport_max,
                               int margin_tiles);
    void enqueue_tile(int level_index, int tile_index);
    bool tile_ready(int level_index, int tile_index) const;
    void process_upload_queue(const std::vector<Pixel>& base_pixels, int document_width);
    bool upload_tile(Level& level, Tile& tile, const std::vector<Pixel>& source, int source_width, std::uint64_t generation);
    void draw_ready_level(ImDrawList* draw_list,
                          int level_index,
                          ImVec2 canvas_origin,
                          float zoom,
                          ImVec2 viewport_min,
                          ImVec2 viewport_max,
                          ImU32 tint);

    static constexpr int kTileSize = 512;
    static constexpr int kPyramidMinDimension = 512;
    static constexpr std::size_t kPyramidMinPixels = 8192U * 8192U;
    static constexpr int kMaxUploadsPerFrame = 3;
    static constexpr double kUploadBudgetSeconds = 0.002;

    std::vector<Level> levels_;
    std::vector<TileKey> upload_queue_;
    std::vector<Pixel> scratch_;
    int document_width_ = 0;
    int document_height_ = 0;
    std::uint64_t generation_ = 1;
    DrawStats last_draw_stats_;
};

} // namespace px
