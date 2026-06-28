// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/MeshUvOverlayRenderer.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace px {

namespace {

struct UvLineVertex {
    float u = 0.0f;
    float v = 0.0f;
    float selected = 0.0f;
};

struct TriangleDrawState {
    bool drawable = false;
    bool selected = false;
    bool critical = false;
};

constexpr const char* kMeshUvOverlayVertexShader = R"GLSL(
    #version 330 core
    layout(location = 0) in vec2 in_uv;
    layout(location = 1) in float in_selected;
    uniform vec4 u_canvas_params0;
    uniform vec4 u_canvas_params1;
    flat out float v_selected;
    void main() {
        vec2 viewport_size = u_canvas_params0.xy;
        vec2 texture_size = u_canvas_params0.zw;
        vec2 canvas_origin = u_canvas_params1.xy;
        float zoom = u_canvas_params1.z;
        vec2 pixel = canvas_origin + in_uv * texture_size * zoom;
        vec2 ndc = vec2((pixel.x / max(1.0, viewport_size.x)) * 2.0 - 1.0,
                        1.0 - (pixel.y / max(1.0, viewport_size.y)) * 2.0);
        v_selected = in_selected;
        gl_Position = vec4(ndc, 0.0, 1.0);
    }
)GLSL";

constexpr const char* kMeshUvOverlayFragmentShader = R"GLSL(
    #version 330 core
    flat in float v_selected;
    out vec4 out_color;
    void main() {
        out_color = v_selected > 0.5 ? vec4(1.0, 0.88, 0.08, 0.96)
                                     : vec4(0.12, 0.72, 1.0, 0.58);
    }
)GLSL";

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

unsigned int compile_shader(unsigned int type, const char* source, std::string& error) {
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        set_error(error, std::string("MeshUvOverlayRenderer ") + shader_type_name(type) +
                             " shader compile failed: " + log);
        std::fprintf(stderr, "%s\n", error.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool mesh_has_selected_components(const MeshObject& mesh) {
    for (std::uint8_t selected : mesh.selected_faces) {
        if (selected != 0) {
            return true;
        }
    }
    for (std::uint8_t selected : mesh.selected_vertices) {
        if (selected != 0) {
            return true;
        }
    }
    return false;
}

bool triangle_has_selected_vertex(const MeshObject& mesh, const MeshTriangle& triangle) {
    for (int index : triangle.indices) {
        if (index >= 0 &&
            index < static_cast<int>(mesh.selected_vertices.size()) &&
            mesh.selected_vertices[static_cast<std::size_t>(index)] != 0) {
            return true;
        }
    }
    return false;
}

TriangleDrawState triangle_draw_state(const MeshObject& mesh,
                                      bool selected_mesh,
                                      bool has_selection,
                                      int triangle_index,
                                      const MeshTriangle& triangle,
                                      bool show_all_mesh_uvs) {
    const bool selected_face =
        selected_mesh &&
        triangle_index < static_cast<int>(mesh.selected_faces.size()) &&
        mesh.selected_faces[static_cast<std::size_t>(triangle_index)] != 0;
    const bool selected_vertex = selected_mesh && triangle_has_selected_vertex(mesh, triangle);
    TriangleDrawState state;
    state.critical = selected_face || selected_vertex;
    state.selected = state.critical || (selected_mesh && !has_selection && !show_all_mesh_uvs);
    state.drawable = show_all_mesh_uvs || state.selected;
    return state;
}

bool lod_sample_triangle(int triangle_index, int lod_stride) {
    if (lod_stride <= 1) {
        return true;
    }
    const auto hash = static_cast<unsigned int>(triangle_index) * 2654435761U;
    return (hash % static_cast<unsigned int>(lod_stride)) == 0U;
}

std::size_t drawable_triangle_count(const ModelDocument& model, bool show_all_mesh_uvs) {
    std::size_t count = 0;
    for (int mesh_index = 0; mesh_index < static_cast<int>(model.meshes.size()); ++mesh_index) {
        const MeshObject& mesh = model.meshes[static_cast<std::size_t>(mesh_index)];
        const bool selected_mesh = mesh_index == model.selected_mesh;
        const bool has_selection = mesh_has_selected_components(mesh);
        for (int triangle_index = 0; triangle_index < static_cast<int>(mesh.triangles.size()); ++triangle_index) {
            const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(triangle_index)];
            if (triangle_draw_state(mesh, selected_mesh, has_selection, triangle_index, triangle, show_all_mesh_uvs).drawable) {
                ++count;
            }
        }
    }
    return count;
}

int canvas_uv_lod_stride(const ModelDocument& model, bool show_all_mesh_uvs, float zoom, int output_width, int output_height) {
    const std::size_t triangle_count = drawable_triangle_count(model, show_all_mesh_uvs);
    if (triangle_count == 0U || zoom >= 0.75f) {
        return 1;
    }
    const int output_area = std::max(1, output_width) * std::max(1, output_height);
    const std::size_t target_triangles = std::max<std::size_t>(1200U, static_cast<std::size_t>(output_area / 24));
    int stride = static_cast<int>((triangle_count + target_triangles - 1U) / target_triangles);
    stride = std::max(stride, static_cast<int>(std::ceil(0.75f / std::max(0.0001f, zoom))));
    return std::clamp(stride, 1, 128);
}

void add_triangle_edges(std::vector<UvLineVertex>& vertices,
                        const MeshObject& mesh,
                        const MeshTriangle& triangle,
                        float selected) {
    constexpr std::array<int, 6> edges = {0, 1, 1, 2, 2, 0};
    for (int corner : edges) {
        const int index = triangle.indices[static_cast<std::size_t>(corner)];
        if (index < 0 || index >= static_cast<int>(mesh.vertices.size())) {
            continue;
        }
        const MeshVertex& vertex = mesh.vertices[static_cast<std::size_t>(index)];
        vertices.push_back({std::clamp(vertex.uv[0], 0.0f, 1.0f),
                            std::clamp(vertex.uv[1], 0.0f, 1.0f),
                            selected});
    }
}

} // namespace

MeshUvOverlayRenderer::~MeshUvOverlayRenderer() {
    destroy();
}

const char* MeshUvOverlayRenderer::vertex_shader_source() {
    return kMeshUvOverlayVertexShader;
}

const char* MeshUvOverlayRenderer::fragment_shader_source() {
    return kMeshUvOverlayFragmentShader;
}

void MeshUvOverlayRenderer::destroy() {
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) glDeleteVertexArrays(1, &vertex_array_);
    if (color_texture_ != 0) glDeleteTextures(1, &color_texture_);
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    vertex_buffer_ = 0;
    vertex_array_ = 0;
    color_texture_ = 0;
    framebuffer_ = 0;
    shader_program_ = 0;
    width_ = 0;
    height_ = 0;
    vertex_count_ = 0;
    initialized_ = false;
    cache_dirty_ = true;
    cached_lod_stride_ = 1;
}

void MeshUvOverlayRenderer::invalidate_cache() {
    cache_dirty_ = true;
}

bool MeshUvOverlayRenderer::init() {
    if (initialized_) {
        return true;
    }
    if (!ensure_program()) {
        return false;
    }
    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UvLineVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(UvLineVertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glBindVertexArray(0);
    initialized_ = true;
    return true;
}

bool MeshUvOverlayRenderer::ensure_program() {
    if (shader_program_ != 0) {
        return true;
    }
    last_error_.clear();
    const unsigned int vs = compile_shader(GL_VERTEX_SHADER, kMeshUvOverlayVertexShader, last_error_);
    const unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, kMeshUvOverlayFragmentShader, last_error_);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
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
        set_error(last_error_, std::string("MeshUvOverlayRenderer shader link failed: ") + log);
        std::fprintf(stderr, "%s\n", last_error_.c_str());
        glDeleteProgram(shader_program_);
        shader_program_ = 0;
        return false;
    }
    return true;
}

bool MeshUvOverlayRenderer::ensure_target(int width, int height) {
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
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glBindTexture(GL_TEXTURE_2D, color_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture_, 0);
    const GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
        set_error(last_error_, "MeshUvOverlayRenderer framebuffer is incomplete: status " +
                                   std::to_string(framebuffer_status));
        return false;
    }
    return true;
}

bool MeshUvOverlayRenderer::rebuild_cache(const ModelDocument& model, bool show_all_mesh_uvs, int lod_stride) {
    std::vector<UvLineVertex> vertices;
    lod_stride = std::max(1, lod_stride);
    for (int mesh_index = 0; mesh_index < static_cast<int>(model.meshes.size()); ++mesh_index) {
        const MeshObject& mesh = model.meshes[static_cast<std::size_t>(mesh_index)];
        const bool selected_mesh = mesh_index == model.selected_mesh;
        const bool has_selection = mesh_has_selected_components(mesh);
        for (int triangle_index = 0; triangle_index < static_cast<int>(mesh.triangles.size()); ++triangle_index) {
            const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(triangle_index)];
            const TriangleDrawState state =
                triangle_draw_state(mesh, selected_mesh, has_selection, triangle_index, triangle, show_all_mesh_uvs);
            if (!state.drawable) {
                continue;
            }
            if (!state.critical && !lod_sample_triangle(triangle_index, lod_stride)) {
                continue;
            }
            add_triangle_edges(vertices, mesh, triangle, state.selected ? 1.0f : 0.0f);
        }
    }
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(UvLineVertex)),
                 vertices.empty() ? nullptr : vertices.data(),
                 GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
    vertex_count_ = vertices.size();
    cached_show_all_mesh_uvs_ = show_all_mesh_uvs;
    cached_lod_stride_ = lod_stride;
    cache_dirty_ = false;
    return true;
}

bool MeshUvOverlayRenderer::render_to_texture(const ModelDocument& model,
                                              int texture_width,
                                              int texture_height,
                                              float canvas_x,
                                              float canvas_y,
                                              float zoom,
                                              int output_width,
                                              int output_height,
                                              bool show_all_mesh_uvs) {
    GLint previous_fbo = 0;
    GLint previous_viewport[4] = {};
    GLint previous_program = 0;
    GLfloat previous_line_width = 1.0f;
    GLboolean previous_blend = GL_FALSE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glGetFloatv(GL_LINE_WIDTH, &previous_line_width);
    previous_blend = glIsEnabled(GL_BLEND);

    if (!init()) {
        return false;
    }
    if (!ensure_target(output_width, output_height)) {
        glUseProgram(static_cast<GLuint>(previous_program));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
        glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
        return false;
    }
    const int lod_stride = canvas_uv_lod_stride(model, show_all_mesh_uvs, zoom, output_width, output_height);
    if (cache_dirty_ || cached_show_all_mesh_uvs_ != show_all_mesh_uvs || cached_lod_stride_ != lod_stride) {
        rebuild_cache(model, show_all_mesh_uvs, lod_stride);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (vertex_count_ > 0U) {
        glUseProgram(shader_program_);
        glUniform4f(glGetUniformLocation(shader_program_, "u_canvas_params0"),
                    static_cast<float>(width_),
                    static_cast<float>(height_),
                    static_cast<float>(std::max(1, texture_width)),
                    static_cast<float>(std::max(1, texture_height)));
        glUniform4f(glGetUniformLocation(shader_program_, "u_canvas_params1"), canvas_x, canvas_y, std::max(0.0001f, zoom), 0.0f);
        glBindVertexArray(vertex_array_);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertex_count_));
        glBindVertexArray(0);
    }

    if (previous_blend == GL_TRUE) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glLineWidth(previous_line_width);
    glUseProgram(static_cast<GLuint>(previous_program));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
    return true;
}

} // namespace px
