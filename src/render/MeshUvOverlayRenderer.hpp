// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Model.hpp"

#include <cstddef>
#include <string>

namespace px {

class MeshUvOverlayRenderer {
public:
    MeshUvOverlayRenderer() = default;
    ~MeshUvOverlayRenderer();

    MeshUvOverlayRenderer(const MeshUvOverlayRenderer&) = delete;
    MeshUvOverlayRenderer& operator=(const MeshUvOverlayRenderer&) = delete;

    bool render_to_texture(const ModelDocument& model,
                           int texture_width,
                           int texture_height,
                           float canvas_x,
                           float canvas_y,
                           float zoom,
                           int output_width,
                           int output_height,
                           bool show_all_mesh_uvs);
    void invalidate_cache();
    void destroy();

    unsigned int texture_id() const { return color_texture_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    [[nodiscard]] static const char* vertex_shader_source();
    [[nodiscard]] static const char* fragment_shader_source();

private:
    bool init();
    bool ensure_program();
    bool ensure_target(int width, int height);
    bool rebuild_cache(const ModelDocument& model, bool show_all_mesh_uvs, int lod_stride);

    unsigned int framebuffer_ = 0;
    unsigned int color_texture_ = 0;
    unsigned int vertex_array_ = 0;
    unsigned int vertex_buffer_ = 0;
    unsigned int shader_program_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::size_t vertex_count_ = 0;
    bool initialized_ = false;
    bool cache_dirty_ = true;
    bool cached_show_all_mesh_uvs_ = false;
    int cached_lod_stride_ = 1;
    std::string last_error_;
};

} // namespace px
