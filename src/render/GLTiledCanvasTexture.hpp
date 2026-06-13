// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Pixel.hpp"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace px {

class GLTiledCanvasTexture {
public:
    GLTiledCanvasTexture() = default;
    ~GLTiledCanvasTexture();

    GLTiledCanvasTexture(const GLTiledCanvasTexture&) = delete;
    GLTiledCanvasTexture& operator=(const GLTiledCanvasTexture&) = delete;

    void invalidate();
    void draw_visible(ImDrawList* draw_list,
                      int document_width,
                      int document_height,
                      const std::vector<Pixel>& pixels,
                      ImVec2 canvas_origin,
                      float zoom,
                      ImVec2 viewport_min,
                      ImVec2 viewport_max,
                      ImU32 tint = IM_COL32_WHITE);
    void destroy();

private:
    struct Tile {
        unsigned int texture_id = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::uint64_t generation = 0;
    };

    Tile& tile_at(int tile_x, int tile_y);
    void reset_grid(int document_width, int document_height);
    bool should_draw_lod(float zoom) const;
    void draw_lod(ImDrawList* draw_list,
                  int document_width,
                  int document_height,
                  const std::vector<Pixel>& pixels,
                  ImVec2 canvas_origin,
                  float zoom,
                  ImU32 tint);
    void update_lod(int document_width, int document_height, const std::vector<Pixel>& pixels);
    void update_tile(Tile& tile, const std::vector<Pixel>& pixels, int document_width);

    static constexpr int kTileSize = 512;
    static constexpr int kLodMaxDimension = 2048;
    static constexpr float kLodZoomThreshold = 1.0f;

    std::vector<Tile> tiles_;
    std::vector<Pixel> scratch_;
    std::vector<Pixel> lod_pixels_;
    unsigned int lod_texture_id_ = 0;
    int document_width_ = 0;
    int document_height_ = 0;
    int lod_width_ = 0;
    int lod_height_ = 0;
    int columns_ = 0;
    int rows_ = 0;
    std::uint64_t generation_ = 1;
    std::uint64_t lod_generation_ = 0;
};

} // namespace px
