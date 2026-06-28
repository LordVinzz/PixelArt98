// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Model.hpp"
#include "core/Pixel.hpp"

#include <string>
#include <vector>

namespace px {

class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    bool init();
    void destroy();

    bool render_model_to_texture(const ModelDocument& model,
                                 unsigned int canvas_texture,
                                 int texture_width,
                                 int texture_height,
                                 const ModelViewportState& viewport,
                                 int output_width,
                                 int output_height,
                                 const std::vector<Pixel>& texture_pixels,
                                 bool force_wireframe = false);
    void invalidate_model_cache();

    FaceHit pick_face(const ModelDocument& model,
                      const ModelViewportState& viewport,
                      int output_width,
                      int output_height,
                      float mouse_x,
                      float mouse_y) const;

    bool project_first_cuboid_corner(const ModelDocument& model,
                                     const ModelViewportState& viewport,
                                     int output_width,
                                     int output_height,
                                     float& out_x,
                                     float& out_y) const;

    bool project_world_point(const ModelDocument& model,
                             const ModelViewportState& viewport,
                             int output_width,
                             int output_height,
                             float world_x,
                             float world_y,
                             float world_z,
                             float& out_x,
                             float& out_y) const;

    unsigned int texture_id() const { return color_texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    [[nodiscard]] static const char* vertex_shader_source();
    [[nodiscard]] static const char* fragment_shader_source();

private:
    bool ensure_target(int width, int height);
    bool ensure_program();

    unsigned int framebuffer_ = 0;
    unsigned int color_texture_ = 0;
    unsigned int depth_buffer_ = 0;
    unsigned int vertex_array_ = 0;
    unsigned int vertex_buffer_ = 0;
    unsigned int model_vertex_array_ = 0;
    unsigned int model_vertex_buffer_ = 0;
    unsigned int shader_program_ = 0;
    std::size_t model_vertex_count_ = 0;
    int model_texture_width_ = 0;
    int model_texture_height_ = 0;
    int model_lod_stride_ = 1;
    bool model_vertices_dirty_ = true;
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    std::string last_error_;
};

} // namespace px
