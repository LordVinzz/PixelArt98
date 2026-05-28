#pragma once

#include "core/Model.hpp"
#include "core/Pixel.hpp"

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
                                 const std::vector<Pixel>& texture_pixels);

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

private:
    bool ensure_target(int width, int height);
    bool ensure_program();

    unsigned int framebuffer_ = 0;
    unsigned int color_texture_ = 0;
    unsigned int depth_buffer_ = 0;
    unsigned int vertex_array_ = 0;
    unsigned int vertex_buffer_ = 0;
    unsigned int shader_program_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
};

} // namespace px
