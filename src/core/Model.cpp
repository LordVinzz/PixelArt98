#include "core/Model.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace px {

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

void rotate_cuboid(Cuboid& cuboid, int axis, float angle_degrees, bool snap_to_45_degrees) {
    cuboid.rotation_axis = std::clamp(axis, 0, 2);
    cuboid.rotation_angle = snap_to_45_degrees ? std::round(angle_degrees / 45.0f) * 45.0f
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
            model.cuboids.push_back(cuboid);
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

} // namespace px
