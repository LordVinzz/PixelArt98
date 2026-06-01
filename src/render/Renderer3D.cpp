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

constexpr float pi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Mat4 {
    std::array<float, 16> v{};
};

struct RenderVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float selected;
};

struct FaceQuad {
    std::array<Vec3, 4> p;
    int cuboid = 0;
    int face = 0;
};

float radians(float degrees) {
    return degrees * pi / 180.0f;
}

Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Vec3 normalize(Vec3 a) {
    float len = std::sqrt(std::max(0.000001f, dot(a, a)));
    return {a.x / len, a.y / len, a.z / len};
}

Vec3 rotate_point(Vec3 point, const Cuboid& cuboid) {
    if (std::abs(cuboid.rotation_angle) <= 0.0001f) {
        return point;
    }

    Vec3 origin{cuboid.rotation_origin[0], cuboid.rotation_origin[1], cuboid.rotation_origin[2]};
    Vec3 p = point - origin;
    float angle = radians(cuboid.rotation_angle);
    float s = std::sin(angle);
    float c = std::cos(angle);
    Vec3 rotated = p;
    switch (std::clamp(cuboid.rotation_axis, 0, 2)) {
        case 0:
            rotated = {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
            break;
        case 2:
            rotated = {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
            break;
        default:
            rotated = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
            break;
    }
    return rotated + origin;
}

float& m(Mat4& mat, int row, int col) {
    return mat.v[static_cast<std::size_t>(col * 4 + row)];
}

float m(const Mat4& mat, int row, int col) {
    return mat.v[static_cast<std::size_t>(col * 4 + row)];
}

Mat4 identity() {
    Mat4 out;
    m(out, 0, 0) = 1.0f;
    m(out, 1, 1) = 1.0f;
    m(out, 2, 2) = 1.0f;
    m(out, 3, 3) = 1.0f;
    return out;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m(a, row, k) * m(b, k, col);
            }
            m(out, row, col) = sum;
        }
    }
    return out;
}

Vec4 transform(const Mat4& mat, Vec3 p) {
    Vec4 out;
    out.x = m(mat, 0, 0) * p.x + m(mat, 0, 1) * p.y + m(mat, 0, 2) * p.z + m(mat, 0, 3);
    out.y = m(mat, 1, 0) * p.x + m(mat, 1, 1) * p.y + m(mat, 1, 2) * p.z + m(mat, 1, 3);
    out.z = m(mat, 2, 0) * p.x + m(mat, 2, 1) * p.y + m(mat, 2, 2) * p.z + m(mat, 2, 3);
    out.w = m(mat, 3, 0) * p.x + m(mat, 3, 1) * p.y + m(mat, 3, 2) * p.z + m(mat, 3, 3);
    return out;
}

Mat4 perspective(float fovy, float aspect, float near_plane, float far_plane) {
    Mat4 out;
    float f = 1.0f / std::tan(fovy * 0.5f);
    m(out, 0, 0) = f / std::max(0.001f, aspect);
    m(out, 1, 1) = f;
    m(out, 2, 2) = (far_plane + near_plane) / (near_plane - far_plane);
    m(out, 2, 3) = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    m(out, 3, 2) = -1.0f;
    return out;
}

Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 out = identity();
    m(out, 0, 0) = s.x;
    m(out, 0, 1) = s.y;
    m(out, 0, 2) = s.z;
    m(out, 0, 3) = -dot(s, eye);
    m(out, 1, 0) = u.x;
    m(out, 1, 1) = u.y;
    m(out, 1, 2) = u.z;
    m(out, 1, 3) = -dot(u, eye);
    m(out, 2, 0) = -f.x;
    m(out, 2, 1) = -f.y;
    m(out, 2, 2) = -f.z;
    m(out, 2, 3) = dot(f, eye);
    return out;
}

Vec3 model_center(const ModelDocument& model) {
    if (model.cuboids.empty()) {
        return {8.0f, 8.0f, 8.0f};
    }
    Vec3 minp{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 maxp{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& c : model.cuboids) {
        float x0 = std::min(c.from[0], c.to[0]);
        float y0 = std::min(c.from[1], c.to[1]);
        float z0 = std::min(c.from[2], c.to[2]);
        float x1 = std::max(c.from[0], c.to[0]);
        float y1 = std::max(c.from[1], c.to[1]);
        float z1 = std::max(c.from[2], c.to[2]);
        std::array<Vec3, 8> points = {{
            {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}
        }};
        for (Vec3 point : points) {
            point = rotate_point(point, c);
            minp.x = std::min(minp.x, point.x);
            minp.y = std::min(minp.y, point.y);
            minp.z = std::min(minp.z, point.z);
            maxp.x = std::max(maxp.x, point.x);
            maxp.y = std::max(maxp.y, point.y);
            maxp.z = std::max(maxp.z, point.z);
        }
    }
    return {(minp.x + maxp.x) * 0.5f, (minp.y + maxp.y) * 0.5f, (minp.z + maxp.z) * 0.5f};
}

Mat4 view_projection(const ModelDocument& model, const ModelViewportState& viewport, int width, int height) {
    Vec3 center = model_center(model);
    center.x += viewport.pan_x;
    center.y += viewport.pan_y;

    float yaw = radians(viewport.yaw_degrees);
    float pitch = radians(std::clamp(viewport.pitch_degrees, -85.0f, 85.0f));
    float distance = std::max(4.0f, viewport.distance);
    Vec3 eye{
        center.x + std::sin(yaw) * std::cos(pitch) * distance,
        center.y + std::sin(pitch) * distance,
        center.z + std::cos(yaw) * std::cos(pitch) * distance
    };
    Mat4 view = look_at(eye, center, {0.0f, 1.0f, 0.0f});
    Mat4 proj = perspective(radians(45.0f), static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height)), 0.1f, 512.0f);
    return multiply(proj, view);
}

void add_face_quad(std::vector<FaceQuad>& out, const Cuboid& c, int cuboid, int face) {
    float x0 = std::min(c.from[0], c.to[0]);
    float y0 = std::min(c.from[1], c.to[1]);
    float z0 = std::min(c.from[2], c.to[2]);
    float x1 = std::max(c.from[0], c.to[0]);
    float y1 = std::max(c.from[1], c.to[1]);
    float z1 = std::max(c.from[2], c.to[2]);

    FaceQuad quad;
    quad.cuboid = cuboid;
    quad.face = face;
    switch (face) {
        case 0: quad.p = {{{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}}; break;
        case 1: quad.p = {{{x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}}}; break;
        case 2: quad.p = {{{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}}}; break;
        case 3: quad.p = {{{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}}}; break;
        case 4: quad.p = {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}}; break;
        default: quad.p = {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}}; break;
    }
    for (auto& point : quad.p) {
        point = rotate_point(point, c);
    }
    out.push_back(quad);
}

std::vector<FaceQuad> face_quads(const ModelDocument& model) {
    std::vector<FaceQuad> out;
    out.reserve(model.cuboids.size() * 6);
    for (int ci = 0; ci < static_cast<int>(model.cuboids.size()); ++ci) {
        for (int face = 0; face < 6; ++face) {
            add_face_quad(out, model.cuboids[static_cast<std::size_t>(ci)], ci, face);
        }
    }
    return out;
}

std::vector<RenderVertex> make_vertices(const ModelDocument& model, int texture_width, int texture_height) {
    std::vector<RenderVertex> vertices;
    texture_width = std::max(1, texture_width);
    texture_height = std::max(1, texture_height);
    auto quads = face_quads(model);
    vertices.reserve(quads.size() * 6);
    for (const auto& quad : quads) {
        const auto& uv = model.cuboids[static_cast<std::size_t>(quad.cuboid)].uv[static_cast<std::size_t>(quad.face)];
        float u0 = static_cast<float>(uv.x) / static_cast<float>(texture_width);
        float v0 = static_cast<float>(uv.y) / static_cast<float>(texture_height);
        float u1 = static_cast<float>(uv.x + uv.w) / static_cast<float>(texture_width);
        float v1 = static_cast<float>(uv.y + uv.h) / static_cast<float>(texture_height);
        std::array<Vec2, 4> uvp = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
        float selected = (quad.cuboid == model.selected_cuboid && quad.face == model.selected_face) ? 1.0f : 0.0f;
        int index[6] = {0, 1, 2, 0, 2, 3};
        for (int i : index) {
            vertices.push_back({quad.p[static_cast<std::size_t>(i)].x,
                                quad.p[static_cast<std::size_t>(i)].y,
                                quad.p[static_cast<std::size_t>(i)].z,
                                uvp[static_cast<std::size_t>(i)].x,
                                uvp[static_cast<std::size_t>(i)].y,
                                selected});
        }
    }
    return vertices;
}

bool face_pixels_fully_transparent(const ModelDocument& model,
                                   const FaceQuad& quad,
                                   int texture_width,
                                   int texture_height,
                                   const std::vector<Pixel>& texture_pixels) {
    if (texture_width <= 0 || texture_height <= 0 ||
        texture_pixels.size() < static_cast<std::size_t>(texture_width * texture_height)) {
        return false;
    }
    const auto& source = model.cuboids[static_cast<std::size_t>(quad.cuboid)].uv[static_cast<std::size_t>(quad.face)];
    UvRect uv = clamped_uv_rect(source, texture_width, texture_height);
    for (int y = uv.y; y < uv.y + uv.h; ++y) {
        for (int x = uv.x; x < uv.x + uv.w; ++x) {
            Pixel pixel = texture_pixels[static_cast<std::size_t>(y * texture_width + x)];
            if (a(pixel) != 0) {
                return false;
            }
        }
    }
    return true;
}

std::vector<RenderVertex> make_transparent_wire_vertices(const ModelDocument& model,
                                                         int texture_width,
                                                         int texture_height,
                                                         const std::vector<Pixel>& texture_pixels) {
    std::vector<RenderVertex> vertices;
    auto quads = face_quads(model);
    vertices.reserve(quads.size() * 8);
    constexpr int edges[8] = {0, 1, 1, 2, 2, 3, 3, 0};
    for (const auto& quad : quads) {
        if (!face_pixels_fully_transparent(model, quad, texture_width, texture_height, texture_pixels)) {
            continue;
        }
        float selected = (quad.cuboid == model.selected_cuboid && quad.face == model.selected_face) ? 1.0f : 0.0f;
        for (int index : edges) {
            Vec3 p = quad.p[static_cast<std::size_t>(index)];
            vertices.push_back({p.x, p.y, p.z, 0.0f, 0.0f, selected});
        }
    }
    return vertices;
}

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
        vertices.push_back({static_cast<float>(min_line), y, coordinate, 0.0f, 0.0f, 0.0f});
        vertices.push_back({static_cast<float>(max_line), y, coordinate, 0.0f, 0.0f, 0.0f});
        vertices.push_back({coordinate, y, static_cast<float>(min_line), 0.0f, 0.0f, 0.0f});
        vertices.push_back({coordinate, y, static_cast<float>(max_line), 0.0f, 0.0f, 0.0f});
    }
    return vertices;
}

std::array<RenderVertex, 2> reference_axis_vertices(int axis) {
    constexpr float min_line = -34.0f;
    constexpr float max_line = 34.0f;
    constexpr float y = 0.0f;
    switch (std::clamp(axis, 0, 2)) {
        case 0:
            return {{{min_line, y, 0.0f, 0.0f, 0.0f, 0.0f}, {max_line, y, 0.0f, 0.0f, 0.0f, 0.0f}}};
        case 1:
            return {{{0.0f, min_line, 0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, max_line, 0.0f, 0.0f, 0.0f, 0.0f}}};
        default:
            return {{{0.0f, y, min_line, 0.0f, 0.0f, 0.0f}, {0.0f, y, max_line, 0.0f, 0.0f, 0.0f}}};
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

} // namespace

Renderer3D::~Renderer3D() {
    destroy();
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
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<void*>(sizeof(float) * 5));
    glBindVertexArray(0);
    initialized_ = true;
    return true;
}

void Renderer3D::destroy() {
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) glDeleteVertexArrays(1, &vertex_array_);
    if (depth_buffer_ != 0) glDeleteRenderbuffers(1, &depth_buffer_);
    if (color_texture_ != 0) glDeleteTextures(1, &color_texture_);
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    vertex_buffer_ = 0;
    vertex_array_ = 0;
    depth_buffer_ = 0;
    color_texture_ = 0;
    framebuffer_ = 0;
    shader_program_ = 0;
    width_ = 0;
    height_ = 0;
    initialized_ = false;
}

bool Renderer3D::ensure_program() {
    if (shader_program_ != 0) {
        return true;
    }
    last_error_.clear();
    const char* vertex_source = R"GLSL(
        #version 330 core
        layout(location = 0) in vec3 in_pos;
        layout(location = 1) in vec2 in_uv;
        layout(location = 2) in float in_selected;
        uniform mat4 u_mvp;
        out vec2 v_uv;
        flat out float v_selected;
        void main() {
            v_uv = in_uv;
            v_selected = in_selected;
            gl_Position = u_mvp * vec4(in_pos, 1.0);
        }
    )GLSL";
    const char* fragment_source = R"GLSL(
        #version 330 core
        in vec2 v_uv;
        flat in float v_selected;
        uniform sampler2D u_texture;
        uniform bool u_wireframe;
        uniform vec4 u_wire_color;
        out vec4 out_color;
        void main() {
            if (u_wireframe) {
                out_color = v_selected > 0.5 ? vec4(1.0, 0.86, 0.08, 1.0) : u_wire_color;
                return;
            }
            vec4 color = texture(u_texture, v_uv);
            if (color.a <= 0.001) {
                discard;
            }
            if (v_selected > 0.5) {
                color.rgb = mix(color.rgb, vec3(1.0, 0.86, 0.08), 0.38);
                color.a = max(color.a, 0.95);
            }
            out_color = color;
        }
    )GLSL";
    unsigned int vs = compile_shader(GL_VERTEX_SHADER, vertex_source, last_error_);
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, fragment_source, last_error_);
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
                                         const std::vector<Pixel>& texture_pixels) {
    GLint previous_fbo = 0;
    GLint previous_viewport[4] = {};
    GLint previous_program = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);

    if (!init()) {
        return false;
    }
    if (!ensure_target(output_width, output_height)) {
        glUseProgram(static_cast<GLuint>(previous_program));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
        glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
        return false;
    }

    auto vertices = make_vertices(model, texture_width, texture_height);
    auto wire_vertices = make_transparent_wire_vertices(model, texture_width, texture_height, texture_pixels);
    auto grid_vertices = make_reference_grid_vertices();
    Mat4 mvp = view_projection(model, viewport, output_width, output_height);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glClearColor(0.18f, 0.19f, 0.20f, 1.0f);
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

    if (!vertices.empty() && canvas_texture != 0) {
        glUseProgram(shader_program_);
        glUniformMatrix4fv(glGetUniformLocation(shader_program_, "u_mvp"), 1, GL_FALSE, mvp.v.data());
        glUniform1i(glGetUniformLocation(shader_program_, "u_texture"), 0);
        glUniform1i(glGetUniformLocation(shader_program_, "u_wireframe"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, canvas_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindVertexArray(vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(RenderVertex)),
                     vertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        glBindVertexArray(0);
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
