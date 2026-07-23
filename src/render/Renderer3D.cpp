// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/Renderer3D.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace px {

namespace {

// Scene-geometry pipeline: transforms model data into renderable vertices.
#include "detail/Renderer3DSceneGeometry.inc"

std::vector<RenderVertex> make_reference_grid_vertices() {
    constexpr int min_line = -32;
    constexpr int max_line = 32;
    constexpr int step = 4;
    constexpr float y = 0.0f;
    std::vector<RenderVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(((max_line - min_line) / step + 1) * 4));
    for (int value = min_line; value <= max_line; value += step) {
        if (value == 0) {
            continue;
        }
        const float coordinate = static_cast<float>(value);
        vertices.push_back({static_cast<float>(min_line), y, coordinate, 0.0f, 0.0f, 0.0f, 0.0f});
        vertices.push_back({static_cast<float>(max_line), y, coordinate, 0.0f, 0.0f, 0.0f, 0.0f});
        vertices.push_back({coordinate, y, static_cast<float>(min_line), 0.0f, 0.0f, 0.0f, 0.0f});
        vertices.push_back({coordinate, y, static_cast<float>(max_line), 0.0f, 0.0f, 0.0f, 0.0f});
    }
    return vertices;
}

std::array<RenderVertex, 2> reference_axis_vertices(int axis) {
    constexpr float min_line = -34.0f;
    constexpr float max_line = 34.0f;
    constexpr float y = 0.0f;
    switch (std::clamp(axis, 0, 2)) {
        case 0:
            return {{{min_line, y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {max_line, y, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}}};
        case 1:
            return {{{0.0f, min_line, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, max_line, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}}};
        default:
            return {{{0.0f, y, min_line, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, y, max_line, 0.0f, 0.0f, 0.0f, 0.0f}}};
    }
}

void draw_line_vertices(unsigned int shader_program,
                        unsigned int vertex_array,
                        unsigned int vertex_buffer,
                        const Mat4& mvp,
                        const RenderVertex* vertices,
                        std::size_t vertex_count,
                        float red,
                        float green,
                        float blue,
                        float alpha,
                        float line_width) {
    if (vertex_count == 0U || vertices == nullptr) {
        return;
    }
    glUseProgram(shader_program);
    glUniformMatrix4fv(glGetUniformLocation(shader_program, "u_mvp"), 1, GL_FALSE, mvp.v.data());
    glUniform1i(glGetUniformLocation(shader_program, "u_wireframe"), 1);
    glUniform4f(glGetUniformLocation(shader_program, "u_wire_color"), red, green, blue, alpha);
    glBindVertexArray(vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertex_count * sizeof(RenderVertex)),
                 vertices,
                 GL_DYNAMIC_DRAW);
    glLineWidth(line_width);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertex_count));
    glLineWidth(1.0f);
    glBindVertexArray(0);
}

const char* shader_type_name(unsigned int type) {
    if (type == GL_VERTEX_SHADER) {
        return "vertex";
    }
    if (type == GL_FRAGMENT_SHADER) {
        return "fragment";
    }
    return "unknown";
}

void set_error(std::string& target, const std::string& value) {
    target = value;
}

void configure_render_vertex_layout(unsigned int vertex_array, unsigned int vertex_buffer) {
    glBindVertexArray(vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(sizeof(float) * 5));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(sizeof(float) * 6));
    glBindVertexArray(0);
}

unsigned int compile_shader(unsigned int type, const char* source, std::string& error) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        set_error(error, std::string("Renderer3D ") + shader_type_name(type) + " shader compile failed: " + log);
        std::fprintf(stderr, "%s\n", error.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool point_in_triangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    auto sign = [](Vec2 p1, Vec2 p2, Vec2 p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    float d1 = sign(p, a, b);
    float d2 = sign(p, b, c);
    float d3 = sign(p, c, a);
    bool has_neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool has_pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(has_neg && has_pos);
}

float distance_squared(Vec2 a, Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace

Renderer3D::~Renderer3D() {
    destroy();
}

const char* Renderer3D::vertex_shader_source() {
    return kRenderer3DVertexShader;
}

const char* Renderer3D::fragment_shader_source() {
    return kRenderer3DFragmentShader;
}

bool Renderer3D::init() {
    if (initialized_) {
        return true;
    }
    if (!ensure_program()) {
        return false;
    }
    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glGenVertexArrays(1, &model_vertex_array_);
    glGenBuffers(1, &model_vertex_buffer_);
    configure_render_vertex_layout(vertex_array_, vertex_buffer_);
    configure_render_vertex_layout(model_vertex_array_, model_vertex_buffer_);
    initialized_ = true;
    return true;
}

void Renderer3D::destroy() {
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) glDeleteVertexArrays(1, &vertex_array_);
    if (model_vertex_buffer_ != 0) glDeleteBuffers(1, &model_vertex_buffer_);
    if (model_vertex_array_ != 0) glDeleteVertexArrays(1, &model_vertex_array_);
    if (depth_buffer_ != 0) glDeleteRenderbuffers(1, &depth_buffer_);
    if (color_texture_ != 0) glDeleteTextures(1, &color_texture_);
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    vertex_buffer_ = 0;
    vertex_array_ = 0;
    model_vertex_buffer_ = 0;
    model_vertex_array_ = 0;
    depth_buffer_ = 0;
    color_texture_ = 0;
    framebuffer_ = 0;
    shader_program_ = 0;
    model_vertex_count_ = 0;
    model_texture_width_ = 0;
    model_texture_height_ = 0;
    model_lod_stride_ = 1;
    model_vertices_dirty_ = true;
    width_ = 0;
    height_ = 0;
    initialized_ = false;
}

void Renderer3D::invalidate_model_cache() {
    model_vertices_dirty_ = true;
}

bool Renderer3D::ensure_program() {
    if (shader_program_ != 0) {
        return true;
    }
    last_error_.clear();
    unsigned int vs = compile_shader(GL_VERTEX_SHADER, kRenderer3DVertexShader, last_error_);
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, kRenderer3DFragmentShader, last_error_);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vs);
    glAttachShader(shader_program_, fs);
    glLinkProgram(shader_program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetProgramInfoLog(shader_program_, sizeof(log), nullptr, log);
        set_error(last_error_, std::string("Renderer3D shader link failed: ") + log);
        std::fprintf(stderr, "%s\n", last_error_.c_str());
        glDeleteProgram(shader_program_);
        shader_program_ = 0;
        return false;
    }
    return true;
}

bool Renderer3D::ensure_target(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (framebuffer_ == 0) {
        glGenFramebuffers(1, &framebuffer_);
    }
    if (color_texture_ == 0) {
        glGenTextures(1, &color_texture_);
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (depth_buffer_ == 0) {
        glGenRenderbuffers(1, &depth_buffer_);
    }
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_buffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer_);
    const GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
        set_error(last_error_, "Renderer3D framebuffer is incomplete: status " + std::to_string(framebuffer_status));
        return false;
    }
    return true;
}

bool Renderer3D::render_model_to_texture(const ModelDocument& model,
                                         unsigned int canvas_texture,
                                         int texture_width,
                                         int texture_height,
                                         const ModelViewportState& viewport,
                                         int output_width,
                                         int output_height,
                                         const std::vector<Pixel>& texture_pixels,
                                         bool force_wireframe) {
    GLint previous_fbo = 0;
    GLint previous_viewport[4] = {};
    GLint previous_program = 0;
    GLint previous_polygon_mode[2] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glGetIntegerv(GL_POLYGON_MODE, previous_polygon_mode);

    if (!init()) {
        return false;
    }
    if (!ensure_target(output_width, output_height)) {
        glUseProgram(static_cast<GLuint>(previous_program));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
        glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
        return false;
    }

    const int lod_stride = force_wireframe ? render_lod_stride(model, viewport, output_width, output_height) : 1;
    if (model_vertices_dirty_ ||
        model_texture_width_ != texture_width ||
        model_texture_height_ != texture_height ||
        model_lod_stride_ != lod_stride) {
        auto vertices = make_vertices(model, texture_width, texture_height, lod_stride);
        glBindVertexArray(model_vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, model_vertex_buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(RenderVertex)),
                     vertices.empty() ? nullptr : vertices.data(),
                     GL_DYNAMIC_DRAW);
        glBindVertexArray(0);
        model_vertex_count_ = vertices.size();
        model_texture_width_ = texture_width;
        model_texture_height_ = texture_height;
        model_lod_stride_ = lod_stride;
        model_vertices_dirty_ = false;
    }
    auto wire_vertices = make_transparent_wire_vertices(model, texture_width, texture_height, texture_pixels);
    auto grid_vertices = make_reference_grid_vertices();
    Mat4 mvp = view_projection(model, viewport, output_width, output_height);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glClearColor(0.18f, 0.19f, 0.20f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_line_vertices(shader_program_,
                       vertex_array_,
                       vertex_buffer_,
                       mvp,
                       grid_vertices.data(),
                       grid_vertices.size(),
                       0.48f,
                       0.50f,
                       0.53f,
                       0.26f,
                       1.0f);
    const auto x_axis = reference_axis_vertices(0);
    draw_line_vertices(shader_program_,
                       vertex_array_,
                       vertex_buffer_,
                       mvp,
                       x_axis.data(),
                       x_axis.size(),
                       1.0f,
                       0.24f,
                       0.24f,
                       0.52f,
                       1.8f);
    const auto y_axis = reference_axis_vertices(1);
    draw_line_vertices(shader_program_,
                       vertex_array_,
                       vertex_buffer_,
                       mvp,
                       y_axis.data(),
                       y_axis.size(),
                       0.30f,
                       0.86f,
                       0.36f,
                       0.52f,
                       1.8f);
    const auto z_axis = reference_axis_vertices(2);
    draw_line_vertices(shader_program_,
                       vertex_array_,
                       vertex_buffer_,
                       mvp,
                       z_axis.data(),
                       z_axis.size(),
                       0.28f,
                       0.52f,
                       1.0f,
                       0.52f,
                       1.8f);

    if (model_vertex_count_ > 0U && canvas_texture != 0) {
        glUseProgram(shader_program_);
        glUniformMatrix4fv(glGetUniformLocation(shader_program_, "u_mvp"), 1, GL_FALSE, mvp.v.data());
        glUniform1i(glGetUniformLocation(shader_program_, "u_texture"), 0);
        glUniform1i(glGetUniformLocation(shader_program_, "u_wireframe"), force_wireframe ? 1 : 0);
        glUniform4f(glGetUniformLocation(shader_program_, "u_wire_color"),
                    force_wireframe ? 0.70f : 0.28f,
                    force_wireframe ? 0.94f : 0.78f,
                    force_wireframe ? 0.48f : 1.0f,
                    1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, canvas_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (force_wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(1.0f);
        }
        glBindVertexArray(model_vertex_array_);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(model_vertex_count_));
        glBindVertexArray(0);
        if (force_wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }
    if (!wire_vertices.empty()) {
        glUseProgram(shader_program_);
        glUniformMatrix4fv(glGetUniformLocation(shader_program_, "u_mvp"), 1, GL_FALSE, mvp.v.data());
        glUniform1i(glGetUniformLocation(shader_program_, "u_wireframe"), 1);
        glUniform4f(glGetUniformLocation(shader_program_, "u_wire_color"), 0.28f, 0.78f, 1.0f, 0.92f);
        glBindVertexArray(vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(wire_vertices.size() * sizeof(RenderVertex)),
                     wire_vertices.data(),
                     GL_DYNAMIC_DRAW);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(wire_vertices.size()));
        glLineWidth(1.0f);
        glBindVertexArray(0);
        glUniform1i(glGetUniformLocation(shader_program_, "u_wireframe"), 0);
    }

    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT, static_cast<GLenum>(previous_polygon_mode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(previous_polygon_mode[1]));
    glUseProgram(static_cast<GLuint>(previous_program));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
    return true;
}

FaceHit Renderer3D::pick_face(const ModelDocument& model,
                              const ModelViewportState& viewport,
                              int output_width,
                              int output_height,
                              float mouse_x,
                              float mouse_y) const {
    FaceHit best;
    if (output_width <= 0 || output_height <= 0) {
        return best;
    }
    Mat4 mvp = view_projection(model, viewport, output_width, output_height);
    for (const auto& quad : face_quads(model)) {
        std::array<Vec2, 4> screen{};
        std::array<float, 4> depth{};
        bool clipped = false;
        for (int i = 0; i < 4; ++i) {
            Vec4 clip = transform(mvp, quad.p[static_cast<std::size_t>(i)]);
            if (clip.w <= 0.0001f) {
                clipped = true;
                break;
            }
            float ndc_x = clip.x / clip.w;
            float ndc_y = clip.y / clip.w;
            float ndc_z = clip.z / clip.w;
            screen[static_cast<std::size_t>(i)] = {
                (ndc_x * 0.5f + 0.5f) * static_cast<float>(output_width),
                (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(output_height)
            };
            depth[static_cast<std::size_t>(i)] = ndc_z;
        }
        if (clipped) {
            continue;
        }
        Vec2 p{mouse_x, mouse_y};
        if (!point_in_triangle(p, screen[0], screen[1], screen[2]) &&
            !point_in_triangle(p, screen[0], screen[2], screen[3])) {
            continue;
        }
        float z = (depth[0] + depth[1] + depth[2] + depth[3]) * 0.25f;
        if (!best.hit || z < best.depth) {
            best.hit = true;
            best.cuboid = quad.cuboid;
            best.face = quad.face;
            best.mesh = -1;
            best.mesh_face = -1;
            best.mesh_vertex = -1;
            best.depth = z;
        }
    }
    for (int mesh_index = 0; mesh_index < static_cast<int>(model.meshes.size()); ++mesh_index) {
        const MeshObject& mesh = model.meshes[static_cast<std::size_t>(mesh_index)];
        for (int face_index = 0; face_index < static_cast<int>(mesh.triangles.size()); ++face_index) {
            const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(face_index)];
            std::array<Vec2, 3> screen{};
            std::array<float, 3> depth{};
            bool clipped = false;
            for (int i = 0; i < 3; ++i) {
                const int vertex_index = triangle.indices[static_cast<std::size_t>(i)];
                if (vertex_index < 0 || vertex_index >= static_cast<int>(mesh.vertices.size())) {
                    clipped = true;
                    break;
                }
                const auto& position = mesh.vertices[static_cast<std::size_t>(vertex_index)].position;
                Vec4 clip = transform(mvp, {position[0], position[1], position[2]});
                if (clip.w <= 0.0001f) {
                    clipped = true;
                    break;
                }
                const float ndc_x = clip.x / clip.w;
                const float ndc_y = clip.y / clip.w;
                const float ndc_z = clip.z / clip.w;
                screen[static_cast<std::size_t>(i)] = {
                    (ndc_x * 0.5f + 0.5f) * static_cast<float>(output_width),
                    (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(output_height)
                };
                depth[static_cast<std::size_t>(i)] = ndc_z;
            }
            if (clipped) {
                continue;
            }
            const Vec2 p{mouse_x, mouse_y};
            if (!point_in_triangle(p, screen[0], screen[1], screen[2])) {
                continue;
            }
            const float z = (depth[0] + depth[1] + depth[2]) / 3.0f;
            if (best.hit && z >= best.depth) {
                continue;
            }
            int nearest_vertex = triangle.indices[0];
            float nearest_distance = distance_squared(p, screen[0]);
            for (int i = 1; i < 3; ++i) {
                const float candidate = distance_squared(p, screen[static_cast<std::size_t>(i)]);
                if (candidate < nearest_distance) {
                    nearest_distance = candidate;
                    nearest_vertex = triangle.indices[static_cast<std::size_t>(i)];
                }
            }
            best.hit = true;
            best.cuboid = -1;
            best.face = -1;
            best.mesh = mesh_index;
            best.mesh_face = face_index;
            best.mesh_vertex = nearest_vertex;
            best.depth = z;
        }
    }
    return best;
}

bool Renderer3D::project_first_cuboid_corner(const ModelDocument& model,
                                             const ModelViewportState& viewport,
                                             int output_width,
                                             int output_height,
                                             float& out_x,
                                             float& out_y) const {
    if (model.cuboids.empty() || output_width <= 0 || output_height <= 0) {
        return false;
    }
    const Cuboid& cuboid = model.cuboids.front();
    const float x0 = std::min(cuboid.from[0], cuboid.to[0]);
    const float y0 = std::min(cuboid.from[1], cuboid.to[1]);
    const float z0 = std::min(cuboid.from[2], cuboid.to[2]);
    const float x1 = std::max(cuboid.from[0], cuboid.to[0]);
    const float y1 = std::max(cuboid.from[1], cuboid.to[1]);
    const float z1 = std::max(cuboid.from[2], cuboid.to[2]);
    const std::array<Vec3, 8> corners = {{
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}
    }};
    const Mat4 mvp = view_projection(model, viewport, output_width, output_height);
    bool found = false;
    float best_depth = std::numeric_limits<float>::lowest();
    float best_x = 0.0f;
    float best_y = 0.0f;
    for (Vec3 corner : corners) {
        const Vec4 clip = transform(mvp, rotate_point(corner, cuboid));
        if (clip.w <= 0.0001f) {
            continue;
        }
        const float ndc_x = clip.x / clip.w;
        const float ndc_y = clip.y / clip.w;
        const float ndc_z = clip.z / clip.w;
        if (ndc_z > best_depth) {
            best_depth = ndc_z;
            best_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(output_width);
            best_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(output_height);
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    out_x = best_x;
    out_y = best_y;
    return true;
}

bool Renderer3D::project_world_point(const ModelDocument& model,
                                     const ModelViewportState& viewport,
                                     int output_width,
                                     int output_height,
                                     float world_x,
                                     float world_y,
                                     float world_z,
                                     float& out_x,
                                     float& out_y) const {
    if (output_width <= 0 || output_height <= 0) {
        return false;
    }
    const Mat4 mvp = view_projection(model, viewport, output_width, output_height);
    const Vec4 clip = transform(mvp, {world_x, world_y, world_z});
    if (clip.w <= 0.0001f) {
        return false;
    }
    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    out_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(output_width);
    out_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(output_height);
    return true;
}

} // namespace px
