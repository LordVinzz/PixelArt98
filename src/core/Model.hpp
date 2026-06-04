#pragma once

#include <array>
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

struct ModelDocument {
    int texture_width = 64;
    int texture_height = 64;
    int selected_cuboid = 0;
    int selected_face = 0;
    std::vector<Cuboid> cuboids;

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

} // namespace px
