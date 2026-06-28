// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace px {

namespace {

void normalize_mesh_selection(MeshObject& mesh) {
    mesh.selected_vertices.resize(mesh.vertices.size(), 0);
    mesh.selected_faces.resize(mesh.triangles.size(), 0);
    for (MeshTriangle& triangle : mesh.triangles) {
        for (int& index : triangle.indices) {
            if (mesh.vertices.empty()) {
                index = 0;
            } else {
                index = std::clamp(index, 0, static_cast<int>(mesh.vertices.size()) - 1);
            }
        }
    }
}

std::set<int> selected_mesh_vertex_indices(const MeshObject& mesh) {
    std::set<int> indices;
    for (std::size_t i = 0; i < mesh.selected_vertices.size() && i < mesh.vertices.size(); ++i) {
        if (mesh.selected_vertices[i] != 0) {
            indices.insert(static_cast<int>(i));
        }
    }
    for (std::size_t face = 0; face < mesh.selected_faces.size() && face < mesh.triangles.size(); ++face) {
        if (mesh.selected_faces[face] == 0) {
            continue;
        }
        for (int index : mesh.triangles[face].indices) {
            if (index >= 0 && index < static_cast<int>(mesh.vertices.size())) {
                indices.insert(index);
            }
        }
    }
    if (indices.empty()) {
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            indices.insert(static_cast<int>(i));
        }
    }
    return indices;
}

std::array<float, 3> mesh_vertex_center(const MeshObject& mesh, const std::set<int>& indices) {
    if (mesh.vertices.empty() || indices.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }
    std::array<float, 3> center = {0.0f, 0.0f, 0.0f};
    int count = 0;
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(mesh.vertices.size())) {
            continue;
        }
        const auto& position = mesh.vertices[static_cast<std::size_t>(index)].position;
        center[0] += position[0];
        center[1] += position[1];
        center[2] += position[2];
        ++count;
    }
    if (count == 0) {
        return {0.0f, 0.0f, 0.0f};
    }
    center[0] /= static_cast<float>(count);
    center[1] /= static_cast<float>(count);
    center[2] /= static_cast<float>(count);
    return center;
}

std::array<float, 3> rotate_position_around_axis(std::array<float, 3> position,
                                                 const std::array<float, 3>& origin,
                                                 int axis,
                                                 float angle_degrees) {
    const float angle = angle_degrees * 3.14159265358979323846f / 180.0f;
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    std::array<float, 3> p = {
        position[0] - origin[0],
        position[1] - origin[1],
        position[2] - origin[2]
    };
    std::array<float, 3> rotated = p;
    switch (std::clamp(axis, 0, 2)) {
        case 0:
            rotated = {p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c};
            break;
        case 2:
            rotated = {p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2]};
            break;
        default:
            rotated = {p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c};
            break;
    }
    return {rotated[0] + origin[0], rotated[1] + origin[1], rotated[2] + origin[2]};
}

} // namespace

const char* model_face_name(int face) {
    static const char* names[] = {"north", "south", "east", "west", "up", "down"};
    return names[std::clamp(face, 0, 5)];
}

int model_face_index_from_name(const std::string& name) {
    for (int i = 0; i < 6; ++i) {
        if (name == model_face_name(i)) {
            return i;
        }
    }
    return -1;
}

const char* model_rotation_axis_name(int axis) {
    static const char* names[] = {"x", "y", "z"};
    return names[std::clamp(axis, 0, 2)];
}

int model_rotation_axis_from_name(const std::string& name) {
    for (int i = 0; i < 3; ++i) {
        if (name == model_rotation_axis_name(i)) {
            return i;
        }
    }
    return 1;
}

bool uv_rect_valid(const UvRect& uv, int texture_width, int texture_height) {
    return texture_width > 0 && texture_height > 0 &&
           uv.w > 0 && uv.h > 0 &&
           uv.x >= 0 && uv.y >= 0 &&
           uv.x + uv.w <= texture_width &&
           uv.y + uv.h <= texture_height;
}

UvRect clamped_uv_rect(UvRect uv, int texture_width, int texture_height) {
    texture_width = std::max(1, texture_width);
    texture_height = std::max(1, texture_height);
    uv.w = std::clamp(uv.w, 1, texture_width);
    uv.h = std::clamp(uv.h, 1, texture_height);
    uv.x = std::clamp(uv.x, 0, texture_width - uv.w);
    uv.y = std::clamp(uv.y, 0, texture_height - uv.h);
    return uv;
}

void clamp_model_uvs(ModelDocument& model) {
    model.texture_width = std::max(1, model.texture_width);
    model.texture_height = std::max(1, model.texture_height);
    model.selected_face = std::clamp(model.selected_face, 0, 5);
    model.mesh_selection_mode = std::clamp(model.mesh_selection_mode, 0, 1);
    if (model.meshes.empty()) {
        model.selected_mesh = -1;
    } else {
        model.selected_mesh = std::clamp(model.selected_mesh, 0, static_cast<int>(model.meshes.size()) - 1);
    }
    for (MeshObject& mesh : model.meshes) {
        normalize_mesh_selection(mesh);
        for (MeshVertex& vertex : mesh.vertices) {
            vertex.uv[0] = std::clamp(vertex.uv[0], 0.0f, 1.0f);
            vertex.uv[1] = std::clamp(vertex.uv[1], 0.0f, 1.0f);
        }
    }
    if (model.cuboids.empty()) {
        model.selected_cuboid = -1;
        return;
    }
    model.selected_cuboid = std::clamp(model.selected_cuboid, 0, static_cast<int>(model.cuboids.size()) - 1);
    for (auto& cuboid : model.cuboids) {
        for (auto& uv : cuboid.uv) {
            uv = clamped_uv_rect(uv, model.texture_width, model.texture_height);
        }
    }
}

ModelDocument ModelDocument::create_default() {
    ModelDocument model;
    Cuboid cube;
    cube.name = "Block";
    cube.from = {0.0f, 0.0f, 0.0f};
    cube.to = {16.0f, 16.0f, 16.0f};
    cube.rotation_origin = {8.0f, 8.0f, 8.0f};
    cube.uv = {
        UvRect{0, 16, 16, 16},
        UvRect{16, 16, 16, 16},
        UvRect{32, 16, 16, 16},
        UvRect{48, 16, 16, 16},
        UvRect{16, 0, 16, 16},
        UvRect{32, 0, 16, 16}
    };
    cube.selected = true;
    model.cuboids.push_back(cube);
    return model;
}

Cuboid& ModelDocument::selected() {
    if (cuboids.empty()) {
        *this = create_default();
    }
    selected_cuboid = std::clamp(selected_cuboid, 0, static_cast<int>(cuboids.size()) - 1);
    return cuboids[static_cast<std::size_t>(selected_cuboid)];
}

const Cuboid& ModelDocument::selected() const {
    if (cuboids.empty()) {
        static const ModelDocument fallback = ModelDocument::create_default();
        return fallback.cuboids.front();
    }
    int idx = std::clamp(selected_cuboid, 0, static_cast<int>(cuboids.size()) - 1);
    return cuboids[static_cast<std::size_t>(idx)];
}

void ModelDocument::add_cuboid() {
    Cuboid c;
    c.name = "Cuboid " + std::to_string(cuboids.size() + 1);
    float offset = static_cast<float>(cuboids.size() * 2);
    c.from = {offset, 0.0f, offset};
    c.to = {offset + 8.0f, 8.0f, offset + 8.0f};
    c.rotation_origin = {(c.from[0] + c.to[0]) * 0.5f,
                         (c.from[1] + c.to[1]) * 0.5f,
                         (c.from[2] + c.to[2]) * 0.5f};
    for (auto& uv : c.uv) {
        uv = {0, 0, 8, 8};
    }
    cuboids.push_back(c);
    selected_cuboid = static_cast<int>(cuboids.size()) - 1;
    selected_mesh = -1;
}

bool ModelDocument::remove_selected() {
    if (cuboids.empty()) {
        return false;
    }
    selected_cuboid = std::clamp(selected_cuboid, 0, static_cast<int>(cuboids.size()) - 1);
    cuboids.erase(cuboids.begin() + selected_cuboid);
    selected_cuboid = cuboids.empty() ? -1 : std::clamp(selected_cuboid, 0, static_cast<int>(cuboids.size()) - 1);
    return true;
}

float cuboid_axis_size(const Cuboid& cuboid, int axis) {
    const int index = std::clamp(axis, 0, 2);
    return std::abs(cuboid.to[static_cast<std::size_t>(index)] - cuboid.from[static_cast<std::size_t>(index)]);
}

void translate_cuboid(Cuboid& cuboid, int axis, float delta, bool snap_to_axis_size) {
    const int index = std::clamp(axis, 0, 2);
    float next_delta = delta;
    if (snap_to_axis_size) {
        const float step = std::max(0.001f, cuboid_axis_size(cuboid, index));
        next_delta = std::round(delta / step) * step;
    }
    cuboid.from[static_cast<std::size_t>(index)] += next_delta;
    cuboid.to[static_cast<std::size_t>(index)] += next_delta;
    cuboid.rotation_origin[static_cast<std::size_t>(index)] += next_delta;
}

void scale_cuboid(Cuboid& cuboid, int axis, float factor, bool snap_to_double) {
    const int index = std::clamp(axis, 0, 2);
    const float next_factor = snap_to_double ? (factor >= 1.0f ? 2.0f : 0.5f)
                                             : std::clamp(factor, 0.05f, 64.0f);
    const float center = (cuboid.from[static_cast<std::size_t>(index)] + cuboid.to[static_cast<std::size_t>(index)]) * 0.5f;
    cuboid.from[static_cast<std::size_t>(index)] =
        center + (cuboid.from[static_cast<std::size_t>(index)] - center) * next_factor;
    cuboid.to[static_cast<std::size_t>(index)] =
        center + (cuboid.to[static_cast<std::size_t>(index)] - center) * next_factor;
    cuboid.rotation_origin = {(cuboid.from[0] + cuboid.to[0]) * 0.5f,
                              (cuboid.from[1] + cuboid.to[1]) * 0.5f,
                              (cuboid.from[2] + cuboid.to[2]) * 0.5f};
}

void rotate_cuboid(Cuboid& cuboid, int axis, float angle_degrees, bool snap_to_15_degrees) {
    cuboid.rotation_axis = std::clamp(axis, 0, 2);
    cuboid.rotation_angle = snap_to_15_degrees ? std::round(angle_degrees / 15.0f) * 15.0f
                                               : angle_degrees;
    cuboid.rotation_origin = {(cuboid.from[0] + cuboid.to[0]) * 0.5f,
                              (cuboid.from[1] + cuboid.to[1]) * 0.5f,
                              (cuboid.from[2] + cuboid.to[2]) * 0.5f};
}

std::string model_to_json(const ModelDocument& model) {
    nlohmann::json root;
    root["format"] = "pixelart98-cuboid-model";
    root["texture_size"] = {model.texture_width, model.texture_height};
    root["selected_cuboid"] = model.selected_cuboid;
    root["selected_face"] = model.selected_face;
    root["selected_mesh"] = model.selected_mesh;
    root["mesh_selection_mode"] = model.mesh_selection_mode;
    root["cuboids"] = nlohmann::json::array();
    for (const auto& cuboid : model.cuboids) {
        nlohmann::json c;
        c["name"] = cuboid.name;
        c["from"] = cuboid.from;
        c["to"] = cuboid.to;
        c["rotation"] = {
            {"angle", cuboid.rotation_angle},
            {"axis", model_rotation_axis_name(cuboid.rotation_axis)},
            {"origin", cuboid.rotation_origin},
            {"rescale", cuboid.rotation_rescale}
        };
        c["uv"] = nlohmann::json::array();
        for (const auto& uv : cuboid.uv) {
            c["uv"].push_back({{"x", uv.x}, {"y", uv.y}, {"w", uv.w}, {"h", uv.h}});
        }
        root["cuboids"].push_back(c);
    }
    root["meshes"] = nlohmann::json::array();
    for (const MeshObject& mesh : model.meshes) {
        nlohmann::json m;
        m["name"] = mesh.name;
        m["vertices"] = nlohmann::json::array();
        for (const MeshVertex& vertex : mesh.vertices) {
            m["vertices"].push_back({{"position", vertex.position}, {"uv", vertex.uv}});
        }
        m["triangles"] = nlohmann::json::array();
        for (const MeshTriangle& triangle : mesh.triangles) {
            m["triangles"].push_back({{"indices", triangle.indices}, {"normal", triangle.normal}});
        }
        m["selected_vertices"] = mesh.selected_vertices;
        m["selected_faces"] = mesh.selected_faces;
        root["meshes"].push_back(std::move(m));
    }
    return root.dump(2);
}

bool model_from_json(const std::string& text, ModelDocument& out_model, std::string* error) {
    try {
        auto root = nlohmann::json::parse(text);
        ModelDocument model;
        auto size = root.value("texture_size", std::vector<int>{64, 64});
        if (size.size() >= 2) {
            model.texture_width = std::max(1, size[0]);
            model.texture_height = std::max(1, size[1]);
        }
        model.selected_cuboid = root.value("selected_cuboid", 0);
        model.selected_face = root.value("selected_face", 0);
        model.selected_mesh = root.value("selected_mesh", -1);
        model.mesh_selection_mode = root.value("mesh_selection_mode", 0);
        if (root.contains("cuboids")) {
        for (const auto& c : root.at("cuboids")) {
            Cuboid cuboid;
            cuboid.name = c.value("name", "Cuboid");
            auto from = c.value("from", std::vector<float>{0, 0, 0});
            auto to = c.value("to", std::vector<float>{16, 16, 16});
            for (int i = 0; i < 3 && i < static_cast<int>(from.size()); ++i) cuboid.from[static_cast<std::size_t>(i)] = from[static_cast<std::size_t>(i)];
            for (int i = 0; i < 3 && i < static_cast<int>(to.size()); ++i) cuboid.to[static_cast<std::size_t>(i)] = to[static_cast<std::size_t>(i)];
            cuboid.rotation_origin = {(cuboid.from[0] + cuboid.to[0]) * 0.5f,
                                      (cuboid.from[1] + cuboid.to[1]) * 0.5f,
                                      (cuboid.from[2] + cuboid.to[2]) * 0.5f};
            if (c.contains("rotation")) {
                const auto& rotation = c.at("rotation");
                cuboid.rotation_angle = rotation.value("angle", 0.0f);
                cuboid.rotation_axis = model_rotation_axis_from_name(rotation.value("axis", "y"));
                cuboid.rotation_rescale = rotation.value("rescale", false);
                auto origin = rotation.value("origin", std::vector<float>{cuboid.rotation_origin[0], cuboid.rotation_origin[1], cuboid.rotation_origin[2]});
                for (int i = 0; i < 3 && i < static_cast<int>(origin.size()); ++i) {
                    cuboid.rotation_origin[static_cast<std::size_t>(i)] = origin[static_cast<std::size_t>(i)];
                }
            }
            int face = 0;
            if (c.contains("uv")) {
            for (const auto& uv_json : c.at("uv")) {
                if (face >= 6) break;
                cuboid.uv[static_cast<std::size_t>(face)] = {
                    uv_json.value("x", 0),
                    uv_json.value("y", 0),
                    uv_json.value("w", 16),
                    uv_json.value("h", 16)
                };
                ++face;
            }
            }
            model.cuboids.push_back(cuboid);
        }
        }
        if (root.contains("meshes")) {
            for (const auto& mesh_json : root.at("meshes")) {
                MeshObject mesh;
                mesh.name = mesh_json.value("name", "Mesh");
                if (mesh_json.contains("vertices")) {
                    for (const auto& vertex_json : mesh_json.at("vertices")) {
                        MeshVertex vertex;
                        auto position = vertex_json.value("position", std::vector<float>{0.0f, 0.0f, 0.0f});
                        auto uv = vertex_json.value("uv", std::vector<float>{0.0f, 0.0f});
                        for (int i = 0; i < 3 && i < static_cast<int>(position.size()); ++i) {
                            vertex.position[static_cast<std::size_t>(i)] = position[static_cast<std::size_t>(i)];
                        }
                        for (int i = 0; i < 2 && i < static_cast<int>(uv.size()); ++i) {
                            vertex.uv[static_cast<std::size_t>(i)] = uv[static_cast<std::size_t>(i)];
                        }
                        mesh.vertices.push_back(vertex);
                    }
                }
                if (mesh_json.contains("triangles")) {
                    for (const auto& triangle_json : mesh_json.at("triangles")) {
                        MeshTriangle triangle;
                        auto indices = triangle_json.value("indices", std::vector<int>{0, 0, 0});
                        auto normal = triangle_json.value("normal", std::vector<float>{0.0f, 1.0f, 0.0f});
                        for (int i = 0; i < 3 && i < static_cast<int>(indices.size()); ++i) {
                            triangle.indices[static_cast<std::size_t>(i)] = indices[static_cast<std::size_t>(i)];
                        }
                        for (int i = 0; i < 3 && i < static_cast<int>(normal.size()); ++i) {
                            triangle.normal[static_cast<std::size_t>(i)] = normal[static_cast<std::size_t>(i)];
                        }
                        mesh.triangles.push_back(triangle);
                    }
                }
                mesh.selected_vertices = mesh_json.value("selected_vertices", std::vector<std::uint8_t>{});
                mesh.selected_faces = mesh_json.value("selected_faces", std::vector<std::uint8_t>{});
                normalize_mesh_selection(mesh);
                model.meshes.push_back(std::move(mesh));
            }
        }
        clamp_model_uvs(model);
        out_model = std::move(model);
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
}

bool model_has_geometry(const ModelDocument& model) {
    if (!model.cuboids.empty()) {
        return true;
    }
    for (const MeshObject& mesh : model.meshes) {
        if (!mesh.vertices.empty() && !mesh.triangles.empty()) {
            return true;
        }
    }
    return false;
}

bool model_has_mesh_selection(const ModelDocument& model) {
    if (model.selected_mesh < 0 || model.selected_mesh >= static_cast<int>(model.meshes.size())) {
        return false;
    }
    const MeshObject& mesh = model.meshes[static_cast<std::size_t>(model.selected_mesh)];
    return !selected_mesh_vertex_indices(mesh).empty();
}

bool model_bounds(const ModelDocument& model, std::array<float, 3>& out_min, std::array<float, 3>& out_max) {
    out_min = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    out_max = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };
    bool found = false;
    auto include_point = [&](const std::array<float, 3>& point) {
        for (int axis = 0; axis < 3; ++axis) {
            out_min[static_cast<std::size_t>(axis)] =
                std::min(out_min[static_cast<std::size_t>(axis)], point[static_cast<std::size_t>(axis)]);
            out_max[static_cast<std::size_t>(axis)] =
                std::max(out_max[static_cast<std::size_t>(axis)], point[static_cast<std::size_t>(axis)]);
        }
        found = true;
    };

    for (const Cuboid& cuboid : model.cuboids) {
        const float x0 = std::min(cuboid.from[0], cuboid.to[0]);
        const float y0 = std::min(cuboid.from[1], cuboid.to[1]);
        const float z0 = std::min(cuboid.from[2], cuboid.to[2]);
        const float x1 = std::max(cuboid.from[0], cuboid.to[0]);
        const float y1 = std::max(cuboid.from[1], cuboid.to[1]);
        const float z1 = std::max(cuboid.from[2], cuboid.to[2]);
        const std::array<std::array<float, 3>, 8> corners = {{
            {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
            {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}
        }};
        for (const auto& corner : corners) {
            include_point(rotate_position_around_axis(corner,
                                                      cuboid.rotation_origin,
                                                      cuboid.rotation_axis,
                                                      cuboid.rotation_angle));
        }
    }

    for (const MeshObject& mesh : model.meshes) {
        for (const MeshVertex& vertex : mesh.vertices) {
            include_point(vertex.position);
        }
    }
    if (!found) {
        out_min = {0.0f, 0.0f, 0.0f};
        out_max = {0.0f, 0.0f, 0.0f};
    }
    return found;
}

float model_bounding_radius(const ModelDocument& model) {
    std::array<float, 3> minp{};
    std::array<float, 3> maxp{};
    if (!model_bounds(model, minp, maxp)) {
        return 1.0f;
    }
    const float dx = maxp[0] - minp[0];
    const float dy = maxp[1] - minp[1];
    const float dz = maxp[2] - minp[2];
    return std::max(0.0001f, std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f);
}

std::array<float, 3> selected_mesh_component_center(const ModelDocument& model) {
    if (model.selected_mesh < 0 || model.selected_mesh >= static_cast<int>(model.meshes.size())) {
        return {0.0f, 0.0f, 0.0f};
    }
    const MeshObject& mesh = model.meshes[static_cast<std::size_t>(model.selected_mesh)];
    return mesh_vertex_center(mesh, selected_mesh_vertex_indices(mesh));
}

void clear_mesh_selections(ModelDocument& model) {
    for (MeshObject& mesh : model.meshes) {
        std::fill(mesh.selected_vertices.begin(), mesh.selected_vertices.end(), 0);
        std::fill(mesh.selected_faces.begin(), mesh.selected_faces.end(), 0);
    }
}

void translate_selected_mesh_components(ModelDocument& model, int axis, float delta, bool snap_to_unit) {
    if (model.selected_mesh < 0 || model.selected_mesh >= static_cast<int>(model.meshes.size())) {
        return;
    }
    MeshObject& mesh = model.meshes[static_cast<std::size_t>(model.selected_mesh)];
    const int index = std::clamp(axis, 0, 2);
    const float next_delta = snap_to_unit ? std::round(delta) : delta;
    for (int vertex_index : selected_mesh_vertex_indices(mesh)) {
        mesh.vertices[static_cast<std::size_t>(vertex_index)].position[static_cast<std::size_t>(index)] += next_delta;
    }
}

void scale_selected_mesh_components(ModelDocument& model, int axis, float factor, bool snap_to_double) {
    if (model.selected_mesh < 0 || model.selected_mesh >= static_cast<int>(model.meshes.size())) {
        return;
    }
    MeshObject& mesh = model.meshes[static_cast<std::size_t>(model.selected_mesh)];
    const int index = std::clamp(axis, 0, 2);
    const float next_factor = snap_to_double ? (factor >= 1.0f ? 2.0f : 0.5f)
                                             : std::clamp(factor, 0.05f, 64.0f);
    const auto indices = selected_mesh_vertex_indices(mesh);
    const auto center = mesh_vertex_center(mesh, indices);
    for (int vertex_index : indices) {
        auto& position = mesh.vertices[static_cast<std::size_t>(vertex_index)].position;
        position[static_cast<std::size_t>(index)] =
            center[static_cast<std::size_t>(index)] +
            (position[static_cast<std::size_t>(index)] - center[static_cast<std::size_t>(index)]) * next_factor;
    }
}

void rotate_selected_mesh_components(ModelDocument& model, int axis, float angle_degrees, bool snap_to_15_degrees) {
    if (model.selected_mesh < 0 || model.selected_mesh >= static_cast<int>(model.meshes.size())) {
        return;
    }
    MeshObject& mesh = model.meshes[static_cast<std::size_t>(model.selected_mesh)];
    const float next_angle = snap_to_15_degrees ? std::round(angle_degrees / 15.0f) * 15.0f
                                                : angle_degrees;
    const auto indices = selected_mesh_vertex_indices(mesh);
    const auto center = mesh_vertex_center(mesh, indices);
    for (int vertex_index : indices) {
        auto& position = mesh.vertices[static_cast<std::size_t>(vertex_index)].position;
        position = rotate_position_around_axis(position, center, axis, next_angle);
    }
}

} // namespace px
