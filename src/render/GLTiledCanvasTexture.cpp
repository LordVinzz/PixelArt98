// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/GLTiledCanvasTexture.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>

namespace px {

namespace {

ImTextureID gl_imgui_texture_id(unsigned int id) {
    return static_cast<ImTextureID>(static_cast<unsigned long long>(id));
}

} // namespace

GLTiledCanvasTexture::~GLTiledCanvasTexture() {
    destroy();
}

void GLTiledCanvasTexture::invalidate() {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
        for (Tile& tile : tiles_) {
            tile.generation = 0;
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
    if (draw_list == nullptr || document_width <= 0 || document_height <= 0 || zoom <= 0.0f ||
        pixels.size() != static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height)) {
        return;
    }
    if (document_width != document_width_ || document_height != document_height_) {
        reset_grid(document_width, document_height);
    }

    if (should_draw_lod(zoom)) {
        draw_lod(draw_list, document_width, document_height, pixels, canvas_origin, zoom, tint);
        return;
    }

    const int min_x = std::clamp(static_cast<int>(std::floor((viewport_min.x - canvas_origin.x) / zoom)) - 1,
                                 0,
                                 document_width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor((viewport_min.y - canvas_origin.y) / zoom)) - 1,
                                 0,
                                 document_height - 1);
    const int max_x = std::clamp(static_cast<int>(std::ceil((viewport_max.x - canvas_origin.x) / zoom)) + 1,
                                 0,
                                 document_width);
    const int max_y = std::clamp(static_cast<int>(std::ceil((viewport_max.y - canvas_origin.y) / zoom)) + 1,
                                 0,
                                 document_height);
    if (max_x <= min_x || max_y <= min_y) {
        return;
    }

    const int first_tile_x = min_x / kTileSize;
    const int first_tile_y = min_y / kTileSize;
    const int last_tile_x = (max_x - 1) / kTileSize;
    const int last_tile_y = (max_y - 1) / kTileSize;

    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            Tile& tile = tile_at(tile_x, tile_y);
            if (tile.generation != generation_) {
                update_tile(tile, pixels, document_width);
            }
            if (tile.texture_id == 0) {
                continue;
            }

            const ImVec2 min_pos(canvas_origin.x + static_cast<float>(tile.x) * zoom,
                                 canvas_origin.y + static_cast<float>(tile.y) * zoom);
            const ImVec2 max_pos(canvas_origin.x + static_cast<float>(tile.x + tile.width) * zoom,
                                 canvas_origin.y + static_cast<float>(tile.y + tile.height) * zoom);
            draw_list->AddImage(gl_imgui_texture_id(tile.texture_id), min_pos, max_pos, ImVec2(0, 0), ImVec2(1, 1), tint);
        }
    }
}

void GLTiledCanvasTexture::destroy() {
    for (Tile& tile : tiles_) {
        if (tile.texture_id != 0) {
            glDeleteTextures(1, &tile.texture_id);
            tile.texture_id = 0;
        }
    }
    tiles_.clear();
    scratch_.clear();
    lod_pixels_.clear();
    if (lod_texture_id_ != 0) {
        glDeleteTextures(1, &lod_texture_id_);
        lod_texture_id_ = 0;
    }
    document_width_ = 0;
    document_height_ = 0;
    lod_width_ = 0;
    lod_height_ = 0;
    columns_ = 0;
    rows_ = 0;
    generation_ = 1;
    lod_generation_ = 0;
}

GLTiledCanvasTexture::Tile& GLTiledCanvasTexture::tile_at(int tile_x, int tile_y) {
    return tiles_[static_cast<std::size_t>(tile_y * columns_ + tile_x)];
}

void GLTiledCanvasTexture::reset_grid(int document_width, int document_height) {
    destroy();
    document_width_ = document_width;
    document_height_ = document_height;
    columns_ = (document_width_ + kTileSize - 1) / kTileSize;
    rows_ = (document_height_ + kTileSize - 1) / kTileSize;
    tiles_.resize(static_cast<std::size_t>(columns_ * rows_));
    for (int tile_y = 0; tile_y < rows_; ++tile_y) {
        for (int tile_x = 0; tile_x < columns_; ++tile_x) {
            Tile& tile = tile_at(tile_x, tile_y);
            tile.x = tile_x * kTileSize;
            tile.y = tile_y * kTileSize;
            tile.width = std::min(kTileSize, document_width_ - tile.x);
            tile.height = std::min(kTileSize, document_height_ - tile.y);
        }
    }
}

bool GLTiledCanvasTexture::should_draw_lod(float zoom) const {
    return zoom < kLodZoomThreshold &&
           (document_width_ > kLodMaxDimension || document_height_ > kLodMaxDimension);
}

void GLTiledCanvasTexture::draw_lod(ImDrawList* draw_list,
                                    int document_width,
                                    int document_height,
                                    const std::vector<Pixel>& pixels,
                                    ImVec2 canvas_origin,
                                    float zoom,
                                    ImU32 tint) {
    if (lod_generation_ != generation_) {
        update_lod(document_width, document_height, pixels);
    }
    if (lod_texture_id_ == 0) {
        return;
    }
    const ImVec2 max_pos(canvas_origin.x + static_cast<float>(document_width) * zoom,
                         canvas_origin.y + static_cast<float>(document_height) * zoom);
    draw_list->AddImage(gl_imgui_texture_id(lod_texture_id_),
                        canvas_origin,
                        max_pos,
                        ImVec2(0, 0),
                        ImVec2(1, 1),
                        tint);
}

void GLTiledCanvasTexture::update_lod(int document_width, int document_height, const std::vector<Pixel>& pixels) {
    const int largest_dimension = std::max(document_width, document_height);
    const float scale = std::min(1.0f, static_cast<float>(kLodMaxDimension) / static_cast<float>(largest_dimension));
    const int next_width = std::max(1, static_cast<int>(std::floor(static_cast<float>(document_width) * scale)));
    const int next_height = std::max(1, static_cast<int>(std::floor(static_cast<float>(document_height) * scale)));

    lod_pixels_.resize(static_cast<std::size_t>(next_width) * static_cast<std::size_t>(next_height));
    for (int y = 0; y < next_height; ++y) {
        const int src_y = std::clamp(static_cast<int>((static_cast<float>(y) + 0.5f) *
                                                      static_cast<float>(document_height) /
                                                      static_cast<float>(next_height)),
                                     0,
                                     document_height - 1);
        for (int x = 0; x < next_width; ++x) {
            const int src_x = std::clamp(static_cast<int>((static_cast<float>(x) + 0.5f) *
                                                          static_cast<float>(document_width) /
                                                          static_cast<float>(next_width)),
                                         0,
                                         document_width - 1);
            lod_pixels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(next_width) +
                        static_cast<std::size_t>(x)] =
                pixels[static_cast<std::size_t>(src_y) * static_cast<std::size_t>(document_width) +
                       static_cast<std::size_t>(src_x)];
        }
    }

    if (lod_texture_id_ == 0) {
        glGenTextures(1, &lod_texture_id_);
        glBindTexture(GL_TEXTURE_2D, lod_texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, lod_texture_id_);
    }

    if (lod_width_ != next_width || lod_height_ != next_height) {
        lod_width_ = next_width;
        lod_height_ = next_height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, lod_width_, lod_height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, lod_pixels_.data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, lod_width_, lod_height_, GL_RGBA, GL_UNSIGNED_BYTE, lod_pixels_.data());
    }
    lod_generation_ = generation_;
}

void GLTiledCanvasTexture::update_tile(Tile& tile, const std::vector<Pixel>& pixels, int document_width) {
    if (tile.width <= 0 || tile.height <= 0) {
        return;
    }
    scratch_.resize(static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height));
    for (int row = 0; row < tile.height; ++row) {
        const std::size_t src = static_cast<std::size_t>(tile.y + row) * static_cast<std::size_t>(document_width) +
                                static_cast<std::size_t>(tile.x);
        const std::size_t dst = static_cast<std::size_t>(row) * static_cast<std::size_t>(tile.width);
        std::copy_n(pixels.data() + src, static_cast<std::size_t>(tile.width), scratch_.data() + dst);
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
    tile.generation = generation_;
}

} // namespace px
