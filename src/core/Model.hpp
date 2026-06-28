// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace px {

enum class FaceIndex {
    North = 0,
    South = 1,
    East = 2,
    West = 3,
    Up = 4,
    Down = 5
};

struct UvRect {
    int x = 0;
    int y = 0;
    int w = 16;
    int h = 16;
};

struct FaceHit {
    bool hit = false;
    int cuboid = -1;
    int face = -1;
    int mesh = -1;
    int mesh_face = -1;
    int mesh_vertex = -1;
    float depth = 1.0f;
};

struct ModelViewportState {
    float yaw_degrees = 35.0f;
    float pitch_degrees = 24.0f;
    float distance = 36.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
};

struct Cuboid {
    std::string name = "Cuboid";
    std::array<float, 3> from = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> to = {16.0f, 16.0f, 16.0f};
    float rotation_angle = 0.0f;
    int rotation_axis = 1;
    std::array<float, 3> rotation_origin = {8.0f, 8.0f, 8.0f};
    bool rotation_rescale = false;
    std::array<UvRect, 6> uv;
    bool selected = false;
};

struct MeshVertex {
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    std::array<float, 2> uv = {0.0f, 0.0f};
};

struct MeshTriangle {
    std::array<int, 3> indices = {0, 0, 0};
    std::array<float, 3> normal = {0.0f, 1.0f, 0.0f};
};

struct MeshObject {
    std::string name = "Mesh";
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
    std::vector<std::uint8_t> selected_vertices;
    std::vector<std::uint8_t> selected_faces;
};

struct MeshUvUnwrapResult {
    bool changed = false;
    int mesh_count = 0;
    int island_count = 0;
    int triangle_count = 0;
    int recommended_width = 1;
    int recommended_height = 1;
};

struct ModelDocument {
    int texture_width = 64;
    int texture_height = 64;
    int selected_cuboid = 0;
    int selected_face = 0;
    int selected_mesh = -1;
    int mesh_selection_mode = 0; // 0 = faces, 1 = vertices
    std::vector<Cuboid> cuboids;
    std::vector<MeshObject> meshes;

    static ModelDocument create_default();
    Cuboid& selected();
    const Cuboid& selected() const;
    void add_cuboid();
    bool remove_selected();
};

std::string model_to_json(const ModelDocument& model);
bool model_from_json(const std::string& text, ModelDocument& out_model, std::string* error = nullptr);
bool uv_rect_valid(const UvRect& uv, int texture_width, int texture_height);
UvRect clamped_uv_rect(UvRect uv, int texture_width, int texture_height);
void clamp_model_uvs(ModelDocument& model);
const char* model_face_name(int face);
int model_face_index_from_name(const std::string& name);
const char* model_rotation_axis_name(int axis);
int model_rotation_axis_from_name(const std::string& name);
float cuboid_axis_size(const Cuboid& cuboid, int axis);
void translate_cuboid(Cuboid& cuboid, int axis, float delta, bool snap_to_axis_size);
void scale_cuboid(Cuboid& cuboid, int axis, float factor, bool snap_to_double);
void rotate_cuboid(Cuboid& cuboid, int axis, float angle_degrees, bool snap_to_15_degrees);
bool model_has_geometry(const ModelDocument& model);
bool model_has_mesh_selection(const ModelDocument& model);
bool model_bounds(const ModelDocument& model, std::array<float, 3>& out_min, std::array<float, 3>& out_max);
float model_bounding_radius(const ModelDocument& model);
std::array<float, 3> selected_mesh_component_center(const ModelDocument& model);
void clear_mesh_selections(ModelDocument& model);
void translate_selected_mesh_components(ModelDocument& model, int axis, float delta, bool snap_to_unit);
void scale_selected_mesh_components(ModelDocument& model, int axis, float factor, bool snap_to_double);
void rotate_selected_mesh_components(ModelDocument& model, int axis, float angle_degrees, bool snap_to_15_degrees);
MeshUvUnwrapResult unwrap_model_mesh_uvs(ModelDocument& model, int texture_width, int texture_height);

} // namespace px
