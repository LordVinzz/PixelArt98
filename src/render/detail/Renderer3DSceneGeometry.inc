// Internal implementation fragment for Renderer3D scene geometry.
// This file is included by its owning translation unit; do not compile it separately.

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
    float mesh;
};

constexpr const char* kRenderer3DVertexShader = R"GLSL(
    #version 330 core
    layout(location = 0) in vec3 in_pos;
    layout(location = 1) in vec2 in_uv;
    layout(location = 2) in float in_selected;
    layout(location = 3) in float in_mesh;
    uniform mat4 u_mvp;
    out vec2 v_uv;
    flat out float v_selected;
    flat out float v_mesh;
    void main() {
        v_uv = in_uv;
        v_selected = in_selected;
        v_mesh = in_mesh;
        gl_Position = u_mvp * vec4(in_pos, 1.0);
    }
)GLSL";

constexpr const char* kRenderer3DFragmentShader = R"GLSL(
    #version 330 core
    in vec2 v_uv;
    flat in float v_selected;
    flat in float v_mesh;
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
            if (v_mesh > 0.5) {
                color = vec4(0.52, 0.74, 0.45, 1.0);
            } else {
                discard;
            }
        }
        if (v_selected > 0.5) {
            color.rgb = mix(color.rgb, vec3(1.0, 0.86, 0.08), 0.38);
            color.a = max(color.a, 0.95);
        }
        out_color = color;
    }
)GLSL";

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
    std::array<float, 3> minp{};
    std::array<float, 3> maxp{};
    if (!model_bounds(model, minp, maxp)) {
        return {8.0f, 8.0f, 8.0f};
    }
    return {(minp[0] + maxp[0]) * 0.5f, (minp[1] + maxp[1]) * 0.5f, (minp[2] + maxp[2]) * 0.5f};
}

Mat4 view_projection(const ModelDocument& model, const ModelViewportState& viewport, int width, int height) {
    Vec3 center = model_center(model);
    center.x += viewport.pan_x;
    center.y += viewport.pan_y;

    float yaw = radians(viewport.yaw_degrees);
    float pitch = radians(std::clamp(viewport.pitch_degrees, -85.0f, 85.0f));
    const float radius = std::max(0.0001f, model_bounding_radius(model));
    float distance = std::max(radius * 0.05f, viewport.distance);
    Vec3 eye{
        center.x + std::sin(yaw) * std::cos(pitch) * distance,
        center.y + std::sin(pitch) * distance,
        center.z + std::cos(yaw) * std::cos(pitch) * distance
    };
    Mat4 view = look_at(eye, center, {0.0f, 1.0f, 0.0f});
    const float near_plane = std::clamp(distance / 1000.0f, 0.0001f, 0.1f);
    const float far_plane = std::max(512.0f, distance + radius * 8.0f + 1.0f);
    Mat4 proj = perspective(radians(45.0f),
                            static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height)),
                            near_plane,
                            far_plane);
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

bool triangle_has_selected_vertex(const MeshObject& mesh, const MeshTriangle& triangle) {
    for (int vertex_index : triangle.indices) {
        if (vertex_index >= 0 &&
            vertex_index < static_cast<int>(mesh.selected_vertices.size()) &&
            mesh.selected_vertices[static_cast<std::size_t>(vertex_index)] != 0) {
            return true;
        }
    }
    return false;
}

bool triangle_selected(const MeshObject& mesh, int face_index, const MeshTriangle& triangle) {
    const bool selected_face =
        face_index < static_cast<int>(mesh.selected_faces.size()) &&
        mesh.selected_faces[static_cast<std::size_t>(face_index)] != 0;
    return selected_face || triangle_has_selected_vertex(mesh, triangle);
}

bool lod_sample_triangle(int triangle_index, int lod_stride) {
    if (lod_stride <= 1) {
        return true;
    }
    const auto hash = static_cast<unsigned int>(triangle_index) * 2654435761U;
    return (hash % static_cast<unsigned int>(lod_stride)) == 0U;
}

std::size_t mesh_triangle_count(const ModelDocument& model) {
    std::size_t count = 0;
    for (const MeshObject& mesh : model.meshes) {
        count += mesh.triangles.size();
    }
    return count;
}

int render_lod_stride(const ModelDocument& model,
                      const ModelViewportState& viewport,
                      int output_width,
                      int output_height) {
    const std::size_t triangle_count = mesh_triangle_count(model);
    if (triangle_count == 0U) {
        return 1;
    }
    const float radius = std::max(0.0001f, model_bounding_radius(model));
    const float distance = std::max(radius * 0.05f, viewport.distance);
    const float f = 1.0f / std::tan(radians(45.0f) * 0.5f);
    const float projected_diameter = (radius * 2.0f / distance) * f * static_cast<float>(std::max(1, output_height)) * 0.5f;
    const float viewport_min_dimension = static_cast<float>(std::max(1, std::min(output_width, output_height)));
    if (projected_diameter >= viewport_min_dimension * 0.82f) {
        return 1;
    }

    const float screen_radius = std::max(1.0f, projected_diameter * 0.5f);
    const float model_screen_area = std::min(static_cast<float>(std::max(1, output_width) * std::max(1, output_height)),
                                             pi * screen_radius * screen_radius);
    const std::size_t target_triangles = std::max<std::size_t>(900U, static_cast<std::size_t>(model_screen_area / 6.0f));
    int stride = static_cast<int>((triangle_count + target_triangles - 1U) / target_triangles);
    if (projected_diameter < viewport_min_dimension * 0.55f) {
        stride = std::max(stride, 2);
    }
    if (projected_diameter < viewport_min_dimension * 0.35f) {
        stride = std::max(stride, 4);
    }
    if (projected_diameter < viewport_min_dimension * 0.20f) {
        stride = std::max(stride, 8);
    }
    if (projected_diameter < viewport_min_dimension * 0.12f) {
        stride = std::max(stride, 16);
    }
    return std::clamp(stride, 1, 128);
}

std::vector<RenderVertex> make_vertices(const ModelDocument& model, int texture_width, int texture_height, int lod_stride) {
    std::vector<RenderVertex> vertices;
    texture_width = std::max(1, texture_width);
    texture_height = std::max(1, texture_height);
    lod_stride = std::max(1, lod_stride);
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
                                selected,
                                0.0f});
        }
    }
    for (int mesh_index = 0; mesh_index < static_cast<int>(model.meshes.size()); ++mesh_index) {
        const MeshObject& mesh = model.meshes[static_cast<std::size_t>(mesh_index)];
        for (int face_index = 0; face_index < static_cast<int>(mesh.triangles.size()); ++face_index) {
            const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(face_index)];
            const bool selected = triangle_selected(mesh, face_index, triangle);
            if (!selected && !lod_sample_triangle(face_index, lod_stride)) {
                continue;
            }
            for (int vertex_index : triangle.indices) {
                if (vertex_index < 0 || vertex_index >= static_cast<int>(mesh.vertices.size())) {
                    continue;
                }
                const MeshVertex& vertex = mesh.vertices[static_cast<std::size_t>(vertex_index)];
                vertices.push_back({vertex.position[0],
                                    vertex.position[1],
                                    vertex.position[2],
                                    std::clamp(vertex.uv[0], 0.0f, 1.0f),
                                    std::clamp(vertex.uv[1], 0.0f, 1.0f),
                                    selected ? 1.0f : 0.0f,
                                    1.0f});
            }
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
            vertices.push_back({p.x, p.y, p.z, 0.0f, 0.0f, selected, 0.0f});
        }
    }
    return vertices;
}

