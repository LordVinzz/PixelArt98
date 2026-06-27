// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "ui/EditorApp.hpp"

#include "core/MemoryTrace.hpp"
#include "io/ProjectIO.hpp"
#include "ui/EmbeddedTransformIcons.h"

#include <stb_image.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace px {

namespace {

constexpr int kMaxDocumentSize = 4096;
constexpr float kCentimetersPerInch = 2.54f;
constexpr const char* kResamplingModeNames[] = {"Nearest Neighbor", "Bilinear", "Bicubic"};

ResamplingMode resampling_mode_from_index(int index) {
    switch (std::clamp(index, 0, 2)) {
        case 0:
            return ResamplingMode::Nearest;
        case 1:
            return ResamplingMode::Bilinear;
        case 2:
            return ResamplingMode::Bicubic;
    }
    return ResamplingMode::Bicubic;
}

ImVec4 to_imvec4(Pixel p) {
    return ImVec4(r(p) / 255.0f, g(p) / 255.0f, b(p) / 255.0f, a(p) / 255.0f);
}

Pixel from_float4(const float color[4]) {
    return rgba(static_cast<std::uint8_t>(std::clamp(color[0], 0.0f, 1.0f) * 255.0f + 0.5f),
                static_cast<std::uint8_t>(std::clamp(color[1], 0.0f, 1.0f) * 255.0f + 0.5f),
                static_cast<std::uint8_t>(std::clamp(color[2], 0.0f, 1.0f) * 255.0f + 0.5f),
                static_cast<std::uint8_t>(std::clamp(color[3], 0.0f, 1.0f) * 255.0f + 0.5f));
}

void copy_path(char* dst, std::size_t dst_size, const std::string& src) {
    std::snprintf(dst, dst_size, "%s", src.c_str());
}

std::u8string utf8_to_u8string(std::string_view text) {
    std::u8string out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string u8string_to_utf8(std::u8string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char8_t ch : text) {
        out.push_back(static_cast<char>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(utf8_to_u8string(text));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    return u8string_to_utf8(path.u8string());
}

int clamped_document_size(int value) {
    return std::clamp(value, 1, kMaxDocumentSize);
}

float resolution_pixels_per_inch(float resolution, int resolution_unit) {
    const float safe_resolution = std::max(0.01f, resolution);
    return resolution_unit == 0 ? safe_resolution : safe_resolution * kCentimetersPerInch;
}

float pixels_per_document_unit(float resolution, int resolution_unit, int size_unit) {
    const float pixels_per_inch = resolution_pixels_per_inch(resolution, resolution_unit);
    if (size_unit == 1) {
        return pixels_per_inch;
    }
    if (size_unit == 2) {
        return pixels_per_inch / kCentimetersPerInch;
    }
    return 1.0f;
}

int document_size_value_to_pixels(float value, int size_unit, float resolution, int resolution_unit) {
    const float pixels = std::max(1.0f, value) * pixels_per_document_unit(resolution, resolution_unit, size_unit);
    return clamped_document_size(static_cast<int>(std::round(pixels)));
}

float document_pixels_to_size_value(int pixels, int size_unit, float resolution, int resolution_unit) {
    const float safe_pixels = static_cast<float>(clamped_document_size(pixels));
    return safe_pixels / pixels_per_document_unit(resolution, resolution_unit, size_unit);
}

const char* face_name(int face) {
    static const char* names[] = {"North", "South", "East", "West", "Up", "Down"};
    return names[std::clamp(face, 0, 5)];
}

ImU32 color_u32(Pixel p) {
    return IM_COL32(r(p), g(p), b(p), a(p));
}

ImTextureID gl_texture_id(unsigned int id) {
    return static_cast<ImTextureID>(static_cast<unsigned long long>(id));
}

bool action_modifier_down(const ImGuiIO& io) {
    return io.KeyCtrl || io.KeySuper;
}

bool shortcut_ctrl_or_super(ImGuiKey key) {
    return ImGui::Shortcut(ImGuiMod_Ctrl | key) || ImGui::Shortcut(ImGuiMod_Super | key);
}

SelectionCombineMode selection_mode_from_input(const ImGuiIO& io, bool right_button) {
    const bool action_modifier = action_modifier_down(io);
    if (right_button && action_modifier) {
        return SelectionCombineMode::Invert;
    }
    if (right_button && io.KeyAlt) {
        return SelectionCombineMode::Intersect;
    }
    if (action_modifier) {
        return SelectionCombineMode::Add;
    }
    if (io.KeyAlt) {
        return SelectionCombineMode::Subtract;
    }
    return SelectionCombineMode::Replace;
}

const char* selection_undo_name(const char* base, SelectionCombineMode mode) {
    switch (mode) {
        case SelectionCombineMode::Replace:
            return base;
        case SelectionCombineMode::Add:
            return "Add to Selection";
        case SelectionCombineMode::Subtract:
            return "Subtract from Selection";
        case SelectionCombineMode::Intersect:
            return "Intersect Selection";
        case SelectionCombineMode::Invert:
            return "Invert Selection";
    }
    return base;
}

constexpr float pi = 3.14159265358979323846f;

float radians(float degrees) {
    return degrees * pi / 180.0f;
}

struct AxisVec3 {
    float x;
    float y;
    float z;
};

struct ModelGizmoGeometry {
    bool visible = false;
    ImVec2 center = ImVec2(0.0f, 0.0f);
    std::array<ImVec2, 3> axis_dirs = {};
    std::array<ImVec2, 3> axis_ends = {};
    float handle_length = 72.0f;
    float ring_radius = 54.0f;
};

struct AxisGizmoGeometry {
    bool visible = false;
    ImVec2 center = ImVec2(0.0f, 0.0f);
    std::array<ImVec2, 3> axis_dirs = {};
    std::array<ImVec2, 3> positive_ends = {};
    std::array<ImVec2, 3> negative_ends = {};
    float radius = 32.0f;
};

AxisVec3 cross(AxisVec3 a, AxisVec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(AxisVec3 a, AxisVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

AxisVec3 normalize(AxisVec3 value) {
    float length = std::sqrt(std::max(0.000001f, dot(value, value)));
    return {value.x / length, value.y / length, value.z / length};
}

ImVec2 normalized_screen_axis(AxisVec3 axis, const ModelViewportState& viewport) {
    float yaw = radians(viewport.yaw_degrees);
    float pitch = radians(std::clamp(viewport.pitch_degrees, -85.0f, 85.0f));
    AxisVec3 eye_direction{
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)
    };
    AxisVec3 forward{-eye_direction.x, -eye_direction.y, -eye_direction.z};
    AxisVec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    AxisVec3 up = cross(right, forward);
    ImVec2 projected(dot(right, axis), -dot(up, axis));
    float length = std::sqrt(std::max(0.000001f, projected.x * projected.x + projected.y * projected.y));
    return ImVec2(projected.x / length, projected.y / length);
}

AxisVec3 axis_vector(int axis) {
    switch (std::clamp(axis, 0, 2)) {
        case 0:
            return {1.0f, 0.0f, 0.0f};
        case 1:
            return {0.0f, 1.0f, 0.0f};
        default:
            return {0.0f, 0.0f, 1.0f};
    }
}

AxisVec3 selected_model_pivot(const ModelDocument& model) {
    if (model.cuboids.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }
    const Cuboid& cuboid = model.selected();
    return {cuboid.rotation_origin[0], cuboid.rotation_origin[1], cuboid.rotation_origin[2]};
}

ImU32 axis_color(int axis, int alpha = 255) {
    switch (std::clamp(axis, 0, 2)) {
        case 0:
            return IM_COL32(255, 92, 92, alpha);
        case 1:
            return IM_COL32(96, 220, 120, alpha);
        default:
            return IM_COL32(92, 156, 255, alpha);
    }
}

float distance(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float distance_to_segment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const float length_squared = ab.x * ab.x + ab.y * ab.y;
    if (length_squared <= 0.0001f) {
        return distance(p, a);
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / length_squared, 0.0f, 1.0f);
    const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
    return distance(p, closest);
}

ModelGizmoGeometry build_model_gizmo_geometry(Renderer3D& renderer,
                                              const ModelDocument& model,
                                              const ModelViewportState& viewport,
                                              const ImVec2& origin,
                                              const ImVec2& size) {
    ModelGizmoGeometry geometry;
    if (model.cuboids.empty() || size.x <= 1.0f || size.y <= 1.0f) {
        return geometry;
    }

    const AxisVec3 pivot = selected_model_pivot(model);
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    if (!renderer.project_world_point(model,
                                      viewport,
                                      static_cast<int>(size.x),
                                      static_cast<int>(size.y),
                                      pivot.x,
                                      pivot.y,
                                      pivot.z,
                                      screen_x,
                                      screen_y)) {
        return geometry;
    }

    geometry.visible = true;
    geometry.center = ImVec2(origin.x + screen_x, origin.y + screen_y);
    geometry.handle_length = std::clamp(std::min(size.x, size.y) * 0.20f, 46.0f, 88.0f);
    geometry.ring_radius = std::clamp(std::min(size.x, size.y) * 0.17f, 34.0f, 72.0f);
    for (int axis = 0; axis < 3; ++axis) {
        const ImVec2 direction = normalized_screen_axis(axis_vector(axis), viewport);
        geometry.axis_dirs[static_cast<std::size_t>(axis)] = direction;
        geometry.axis_ends[static_cast<std::size_t>(axis)] =
            ImVec2(geometry.center.x + direction.x * geometry.handle_length,
                   geometry.center.y + direction.y * geometry.handle_length);
    }
    return geometry;
}

std::array<ImVec2, 65> rotation_ring_points(const ModelGizmoGeometry& geometry,
                                            const ModelViewportState& viewport,
                                            int axis) {
    std::array<ImVec2, 65> points{};
    const ImVec2 first = normalized_screen_axis(axis_vector((axis + 1) % 3), viewport);
    const ImVec2 second = normalized_screen_axis(axis_vector((axis + 2) % 3), viewport);
    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
        const float t = (static_cast<float>(index) / static_cast<float>(points.size() - 1U)) * 6.28318530717958647692f;
        const float x = std::cos(t);
        const float y = std::sin(t);
        points[static_cast<std::size_t>(index)] =
            ImVec2(geometry.center.x + (first.x * x + second.x * y) * geometry.ring_radius,
                   geometry.center.y + (first.y * x + second.y * y) * geometry.ring_radius);
    }
    return points;
}

std::array<ImVec2, 2> rotation_ring_basis(const ModelViewportState& viewport, int axis) {
    return {
        normalized_screen_axis(axis_vector((axis + 1) % 3), viewport),
        normalized_screen_axis(axis_vector((axis + 2) % 3), viewport)
    };
}

float rotation_ring_angle_at_mouse(const ModelGizmoGeometry& geometry,
                                   const ModelViewportState& viewport,
                                   int axis,
                                   ImVec2 mouse) {
    const auto basis = rotation_ring_basis(viewport, axis);
    const ImVec2 delta(mouse.x - geometry.center.x, mouse.y - geometry.center.y);
    const float x = delta.x * basis[0].x + delta.y * basis[0].y;
    const float y = delta.x * basis[1].x + delta.y * basis[1].y;
    return std::atan2(y, x);
}

float unwrap_angle_delta(float current, float start) {
    float delta = current - start;
    while (delta > pi) {
        delta -= 2.0f * pi;
    }
    while (delta < -pi) {
        delta += 2.0f * pi;
    }
    return delta;
}

float distance_to_polyline(ImVec2 point, const std::array<ImVec2, 65>& points) {
    float best = 100000.0f;
    for (std::size_t index = 1; index < points.size(); ++index) {
        best = std::min(best, distance_to_segment(point, points[index - 1U], points[index]));
    }
    return best;
}

int hit_model_gizmo(const ModelGizmoGeometry& geometry,
                    const ModelViewportState& viewport,
                    int mode,
                    ImVec2 mouse) {
    if (!geometry.visible || mode == 0) {
        return -1;
    }

    float best_distance = mode == 2 ? 18.0f : 14.0f;
    int best_axis = -1;
    for (int axis = 0; axis < 3; ++axis) {
        float candidate = 100000.0f;
        if (mode == 2) {
            candidate = distance_to_polyline(mouse, rotation_ring_points(geometry, viewport, axis));
        } else {
            const ImVec2 end = geometry.axis_ends[static_cast<std::size_t>(axis)];
            candidate = std::min(distance_to_segment(mouse, geometry.center, end), distance(mouse, end));
        }
        if (candidate < best_distance) {
            best_distance = candidate;
            best_axis = axis;
        }
    }
    return best_axis;
}

void draw_transform_axis_arrow(ImDrawList* draw_list,
                               const ModelGizmoGeometry& geometry,
                               int axis,
                               bool highlighted,
                               bool scale_mode) {
    const ImVec2 direction = geometry.axis_dirs[static_cast<std::size_t>(axis)];
    const ImVec2 end = geometry.axis_ends[static_cast<std::size_t>(axis)];
    const ImVec2 perpendicular(-direction.y, direction.x);
    const float thickness = highlighted ? 4.0f : 2.6f;
    const ImU32 color = highlighted ? axis_color(axis, 255) : axis_color(axis, 220);

    draw_list->AddLine(geometry.center, end, IM_COL32(0, 0, 0, 165), thickness + 2.0f);
    draw_list->AddLine(geometry.center, end, color, thickness);

    if (scale_mode) {
        const float radius = highlighted ? 6.5f : 5.0f;
        draw_list->AddRectFilled(ImVec2(end.x - radius, end.y - radius),
                                 ImVec2(end.x + radius, end.y + radius),
                                 color);
        draw_list->AddRect(ImVec2(end.x - radius, end.y - radius),
                           ImVec2(end.x + radius, end.y + radius),
                           IM_COL32(0, 0, 0, 190));
        return;
    }

    const float head_length = highlighted ? 12.0f : 10.0f;
    const float head_width = highlighted ? 7.0f : 5.5f;
    const ImVec2 head_base(end.x - direction.x * head_length, end.y - direction.y * head_length);
    const ImVec2 side_a(head_base.x + perpendicular.x * head_width, head_base.y + perpendicular.y * head_width);
    const ImVec2 side_b(head_base.x - perpendicular.x * head_width, head_base.y - perpendicular.y * head_width);
    draw_list->AddTriangleFilled(end, side_a, side_b, color);
    draw_list->AddTriangle(end, side_a, side_b, IM_COL32(0, 0, 0, 190), 1.0f);
}

void draw_transform_gizmo(ImDrawList* draw_list,
                          const ModelGizmoGeometry& geometry,
                          const ModelViewportState& viewport,
                          int mode,
                          int active_axis,
                          int hover_axis,
                          float active_rotation_start_radians,
                          float active_rotation_delta_degrees) {
    if (!geometry.visible || mode == 0) {
        return;
    }

    if (mode == 2) {
        for (int axis = 0; axis < 3; ++axis) {
            const auto points = rotation_ring_points(geometry, viewport, axis);
            const bool highlighted = axis == active_axis || axis == hover_axis;
            const float thickness = highlighted ? 3.6f : 2.1f;
            const ImU32 color = highlighted ? axis_color(axis, 255) : axis_color(axis, 205);
            for (std::size_t index = 1; index < points.size(); ++index) {
                draw_list->AddLine(points[index - 1U], points[index], IM_COL32(0, 0, 0, 150), thickness + 2.0f);
                draw_list->AddLine(points[index - 1U], points[index], color, thickness);
            }
        }
        if (active_axis >= 0) {
            const auto basis = rotation_ring_basis(viewport, active_axis);
            const float delta_radians = radians(active_rotation_delta_degrees);
            const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(delta_radians) / (pi / 32.0f))));
            auto point_at_angle = [&](float angle) {
                return ImVec2(geometry.center.x + (basis[0].x * std::cos(angle) + basis[1].x * std::sin(angle)) * geometry.ring_radius,
                              geometry.center.y + (basis[0].y * std::cos(angle) + basis[1].y * std::sin(angle)) * geometry.ring_radius);
            };
            ImVec2 previous = point_at_angle(active_rotation_start_radians);
            for (int step = 1; step <= steps; ++step) {
                const float t = active_rotation_start_radians +
                                delta_radians * (static_cast<float>(step) / static_cast<float>(steps));
                const ImVec2 current = point_at_angle(t);
                draw_list->AddLine(previous, current, IM_COL32(255, 255, 255, 235), 4.2f);
                previous = current;
            }
            draw_list->AddCircleFilled(point_at_angle(active_rotation_start_radians), 4.5f, IM_COL32(255, 255, 255, 240), 12);
            draw_list->AddCircleFilled(previous, 5.5f, axis_color(active_axis, 255), 12);
            char label[64];
            std::snprintf(label,
                          sizeof(label),
                          "%s %.1f deg",
                          model_rotation_axis_name(active_axis),
                          static_cast<double>(active_rotation_delta_degrees));
            draw_list->AddText(ImVec2(geometry.center.x + 10.0f, geometry.center.y + 10.0f),
                               IM_COL32(255, 255, 255, 245),
                               label);
        }
    } else {
        for (int axis = 0; axis < 3; ++axis) {
            const bool highlighted = axis == active_axis || axis == hover_axis;
            draw_transform_axis_arrow(draw_list, geometry, axis, highlighted, mode == 3);
        }
    }

    draw_list->AddCircleFilled(geometry.center, 5.0f, IM_COL32(245, 248, 252, 255), 16);
    draw_list->AddCircle(geometry.center, 7.0f, IM_COL32(0, 0, 0, 180), 16, 1.0f);
}

AxisGizmoGeometry build_axis_gizmo_geometry(const ModelViewportState& viewport,
                                            const ImVec2& origin,
                                            const ImVec2& size) {
    AxisGizmoGeometry geometry;
    const float radius = std::clamp(std::min(size.x, size.y) * 0.16f, 18.0f, 42.0f);
    const float margin = radius + 14.0f;
    geometry.visible = true;
    geometry.radius = radius;
    geometry.center = ImVec2(origin.x + size.x - margin, origin.y + margin);
    geometry.center.x = std::clamp(geometry.center.x,
                                   origin.x + margin,
                                   origin.x + std::max(margin, size.x - margin));
    geometry.center.y = std::clamp(geometry.center.y,
                                   origin.y + margin,
                                   origin.y + std::max(margin, size.y - margin));
    for (int axis = 0; axis < 3; ++axis) {
        const ImVec2 direction = normalized_screen_axis(axis_vector(axis), viewport);
        geometry.axis_dirs[static_cast<std::size_t>(axis)] = direction;
        geometry.positive_ends[static_cast<std::size_t>(axis)] =
            ImVec2(geometry.center.x + direction.x * radius, geometry.center.y + direction.y * radius);
        geometry.negative_ends[static_cast<std::size_t>(axis)] =
            ImVec2(geometry.center.x - direction.x * radius * 0.58f,
                   geometry.center.y - direction.y * radius * 0.58f);
    }
    return geometry;
}

int hit_axis_gizmo(const AxisGizmoGeometry& geometry, ImVec2 mouse) {
    if (!geometry.visible) {
        return -1;
    }
    int best = -1;
    float best_distance = 12.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float positive = distance(mouse, geometry.positive_ends[static_cast<std::size_t>(axis)]);
        if (positive < best_distance) {
            best_distance = positive;
            best = axis;
        }
        const float negative = distance(mouse, geometry.negative_ends[static_cast<std::size_t>(axis)]);
        if (negative < best_distance) {
            best_distance = negative;
            best = axis + 3;
        }
    }
    if (best < 0 && distance(mouse, geometry.center) <= 10.0f) {
        return 6;
    }
    return best;
}

void apply_axis_gizmo_view(ModelViewportState& viewport, int hit) {
    const int axis = std::clamp(hit, 0, 5) % 3;
    const bool positive = hit < 3;
    if (axis == 0) {
        viewport.yaw_degrees = positive ? 90.0f : -90.0f;
        viewport.pitch_degrees = 0.0f;
    } else if (axis == 1) {
        viewport.pitch_degrees = positive ? 85.0f : -85.0f;
    } else {
        viewport.yaw_degrees = positive ? 0.0f : 180.0f;
        viewport.pitch_degrees = 0.0f;
    }
}

const char* axis_gizmo_status(int hit) {
    static const char* names[] = {"+X view", "+Y view", "+Z view", "-X view", "-Y view", "-Z view"};
    return names[static_cast<std::size_t>(std::clamp(hit, 0, 5))];
}

void draw_axis_gizmo(ImDrawList* draw_list, const AxisGizmoGeometry& geometry, int hover_hit) {
    if (!geometry.visible) {
        return;
    }
    for (int axis = 0; axis < 3; ++axis) {
        const ImVec2 positive = geometry.positive_ends[static_cast<std::size_t>(axis)];
        const ImVec2 negative = geometry.negative_ends[static_cast<std::size_t>(axis)];
        const ImU32 line = axis_color(axis, 175);
        draw_list->AddLine(negative, positive, IM_COL32(0, 0, 0, 140), 4.0f);
        draw_list->AddLine(negative, positive, line, 2.2f);
    }

    static const char* labels[] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis) {
        const bool positive_hovered = hover_hit == axis;
        const bool negative_hovered = hover_hit == axis + 3;
        const ImVec2 positive = geometry.positive_ends[static_cast<std::size_t>(axis)];
        const ImVec2 negative = geometry.negative_ends[static_cast<std::size_t>(axis)];
        const float positive_radius = positive_hovered ? 8.0f : 6.4f;
        const float negative_radius = negative_hovered ? 6.2f : 4.3f;
        draw_list->AddCircleFilled(negative, negative_radius + 1.5f, IM_COL32(0, 0, 0, 155), 16);
        draw_list->AddCircleFilled(negative, negative_radius, axis_color(axis, negative_hovered ? 210 : 115), 16);
        draw_list->AddCircleFilled(positive, positive_radius + 1.8f, IM_COL32(0, 0, 0, 160), 16);
        draw_list->AddCircleFilled(positive, positive_radius, axis_color(axis, 255), 16);
        draw_list->AddText(ImVec2(positive.x - 3.5f, positive.y - 7.5f), IM_COL32(255, 255, 255, 245), labels[axis]);
    }
    draw_list->AddCircleFilled(geometry.center, hover_hit == 6 ? 6.0f : 4.5f, IM_COL32(236, 240, 244, 245), 16);
    draw_list->AddCircle(geometry.center, hover_hit == 6 ? 8.0f : 6.0f, IM_COL32(0, 0, 0, 165), 16, 1.0f);
}

const char* effect_preview_name(EffectPreviewKind kind) {
    switch (kind) {
        case EffectPreviewKind::InkSketch: return "Ink Sketch";
        case EffectPreviewKind::OilPainting: return "Oil Painting";
        case EffectPreviewKind::PencilSketch: return "Pencil Sketch";
        case EffectPreviewKind::GaussianBlur: return "Gaussian Blur";
        case EffectPreviewKind::MotionBlur: return "Motion Blur";
        case EffectPreviewKind::RadialBlur: return "Radial Blur";
        case EffectPreviewKind::ZoomBlur: return "Zoom Blur";
        case EffectPreviewKind::MedianBlur: return "Median Blur";
        case EffectPreviewKind::SurfaceBlur: return "Surface Blur";
        case EffectPreviewKind::BrightnessContrast: return "Brightness / Contrast";
        case EffectPreviewKind::Hsv: return "HSV";
        case EffectPreviewKind::Temperature: return "Warmth / Coolness";
        case EffectPreviewKind::Levels: return "Levels";
        case EffectPreviewKind::TonalRange: return "Tonal Range";
        case EffectPreviewKind::Curves: return "Curves";
        case EffectPreviewKind::PaletteQuantize: return "Quantize to Palette";
        case EffectPreviewKind::PaletteDither: return "Dither to Palette";
        case EffectPreviewKind::AutoLevel: return "Auto-Level";
        case EffectPreviewKind::Grayscale: return "Black and White";
        case EffectPreviewKind::Sepia: return "Sepia";
        case EffectPreviewKind::InvertColors: return "Invert Colors";
        case EffectPreviewKind::InvertAlpha: return "Invert Alpha";
        case EffectPreviewKind::Posterize: return "Posterize";
        case EffectPreviewKind::Bulge: return "Bulge";
        case EffectPreviewKind::Crystalize: return "Crystalize";
        case EffectPreviewKind::Dents: return "Dents";
        case EffectPreviewKind::FrostedGlass: return "Frosted Glass";
        case EffectPreviewKind::Pixelate: return "Pixelate";
        case EffectPreviewKind::PolarInversion: return "Polar Inversion";
        case EffectPreviewKind::TileReflection: return "Tile Reflection";
        case EffectPreviewKind::Twist: return "Twist";
        case EffectPreviewKind::AddNoise: return "Add Noise";
        case EffectPreviewKind::ReduceNoise: return "Reduce Noise";
        case EffectPreviewKind::Feather: return "Feather";
        case EffectPreviewKind::Outline: return "Outline";
        case EffectPreviewKind::Glow: return "Glow";
        case EffectPreviewKind::RedEyeRemoval: return "Red Eye Removal";
        case EffectPreviewKind::Sharpen: return "Sharpen";
        case EffectPreviewKind::SoftenPortrait: return "Soften Portrait";
        case EffectPreviewKind::Vignette: return "Vignette";
        case EffectPreviewKind::Clouds: return "Clouds";
        case EffectPreviewKind::JuliaFractal: return "Julia Fractal";
        case EffectPreviewKind::MandelbrotFractal: return "Mandelbrot Fractal";
        case EffectPreviewKind::Turbulence: return "Turbulence";
        case EffectPreviewKind::EdgeDetect: return "Edge Detect";
        case EffectPreviewKind::Emboss: return "Emboss";
        case EffectPreviewKind::Relief: return "Relief";
        case EffectPreviewKind::DepthOfField: return "Depth of Field";
        case EffectPreviewKind::None: return "Effect";
    }
    return "Effect";
}

ImVec2 floor_screen_pos(ImVec2 value) {
    return ImVec2(std::floor(value.x), std::floor(value.y));
}

float pixel_scale(int value, float scale) {
    return static_cast<float>(value) * scale;
}

constexpr float kCanvasZoomStep = 1.18f;

float fit_canvas_zoom(int image_width, int image_height, ImVec2 viewport_size) {
    if (image_width <= 0 || image_height <= 0 || viewport_size.x <= 1.0f || viewport_size.y <= 1.0f) {
        return 1.0f;
    }
    constexpr float padding = 24.0f;
    const float available_width = std::max(1.0f, viewport_size.x - padding);
    const float available_height = std::max(1.0f, viewport_size.y - padding);
    return std::max(0.01f,
                    std::min(available_width / static_cast<float>(image_width),
                             available_height / static_cast<float>(image_height)));
}

float min_canvas_zoom(int image_width, int image_height, ImVec2 viewport_size) {
    const float fit_zoom = fit_canvas_zoom(image_width, image_height, viewport_size);
    return std::max(0.02f, std::min(1.0f, fit_zoom * 0.20f));
}

float max_canvas_zoom(int image_width, int image_height, ImVec2 viewport_size) {
    const float fit_zoom = fit_canvas_zoom(image_width, image_height, viewport_size);
    const int largest_dimension = std::max(1, std::max(image_width, image_height));
    const float image_bound_zoom = 16384.0f / static_cast<float>(largest_dimension);
    return std::clamp(std::max(fit_zoom * 32.0f, image_bound_zoom), 32.0f, 256.0f);
}

float clamped_canvas_zoom(float zoom, int image_width, int image_height, ImVec2 viewport_size) {
    return std::clamp(zoom,
                      min_canvas_zoom(image_width, image_height, viewport_size),
                      max_canvas_zoom(image_width, image_height, viewport_size));
}

ImVec2 canvas_center_offset(ImVec2 viewport_size, ImVec2 image_size) {
    return ImVec2((viewport_size.x - image_size.x) * 0.5f,
                  (viewport_size.y - image_size.y) * 0.5f);
}

bool point_in_rect(ImVec2 point, ImVec2 min, ImVec2 max) {
    return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
}

void push_nearest_sampler(ImDrawList* draw_list) {
    ImDrawCallback callback = ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest;
    if (callback != nullptr) {
        draw_list->AddCallback(callback, nullptr);
    }
}

void push_linear_sampler(ImDrawList* draw_list) {
    ImDrawCallback callback = ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear;
    if (callback != nullptr) {
        draw_list->AddCallback(callback, nullptr);
    }
}

bool selection_equal(const SelectionMask& a_value, const SelectionMask& b_value) {
    return a_value.width == b_value.width &&
           a_value.height == b_value.height &&
           a_value.active == b_value.active &&
           a_value.mask == b_value.mask;
}

bool floating_selection_equal(const FloatingSelection& a_value, const FloatingSelection& b_value) {
    return a_value.active == b_value.active &&
           a_value.source_x == b_value.source_x &&
           a_value.source_y == b_value.source_y &&
           a_value.offset_x == b_value.offset_x &&
           a_value.offset_y == b_value.offset_y &&
           a_value.width == b_value.width &&
           a_value.height == b_value.height &&
           a_value.pixels == b_value.pixels &&
           a_value.mask == b_value.mask;
}

bool layers_equal(const std::vector<Layer>& a_value, const std::vector<Layer>& b_value) {
    if (a_value.size() != b_value.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a_value.size(); ++i) {
        const Layer& a_layer = a_value[i];
        const Layer& b_layer = b_value[i];
        if (a_layer.name != b_layer.name ||
            a_layer.visible != b_layer.visible ||
            a_layer.opacity != b_layer.opacity ||
            a_layer.blend_mode != b_layer.blend_mode ||
            a_layer.mask_enabled != b_layer.mask_enabled ||
            a_layer.clip_to_below != b_layer.clip_to_below ||
            a_layer.mask != b_layer.mask) {
            return false;
        }
    }
    return true;
}

bool frames_equal(const std::vector<Frame>& a_value, const std::vector<Frame>& b_value) {
    if (a_value.size() != b_value.size()) {
        return false;
    }
    for (std::size_t frame_index = 0; frame_index < a_value.size(); ++frame_index) {
        const Frame& a_frame = a_value[frame_index];
        const Frame& b_frame = b_value[frame_index];
        if (a_frame.duration_ms != b_frame.duration_ms || a_frame.cels.size() != b_frame.cels.size()) {
            return false;
        }
        for (std::size_t cel_index = 0; cel_index < a_frame.cels.size(); ++cel_index) {
            const Cel& a_cel = a_frame.cels[cel_index];
            const Cel& b_cel = b_frame.cels[cel_index];
            if (a_cel.x != b_cel.x || a_cel.y != b_cel.y || a_cel.pixels != b_cel.pixels) {
                return false;
            }
        }
    }
    return true;
}

bool tags_equal(const std::vector<AnimationTag>& a_value, const std::vector<AnimationTag>& b_value) {
    if (a_value.size() != b_value.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a_value.size(); ++i) {
        if (a_value[i].name != b_value[i].name ||
            a_value[i].from != b_value[i].from ||
            a_value[i].to != b_value[i].to) {
            return false;
        }
    }
    return true;
}

bool document_state_equal(const Document& a_value, const Document& b_value) {
    return a_value.width == b_value.width &&
           a_value.height == b_value.height &&
           a_value.active_layer == b_value.active_layer &&
           a_value.active_frame == b_value.active_frame &&
           a_value.palette.colors == b_value.palette.colors &&
           a_value.palette.active == b_value.palette.active &&
           selection_equal(a_value.selection, b_value.selection) &&
           floating_selection_equal(a_value.floating_selection, b_value.floating_selection) &&
           layers_equal(a_value.layers, b_value.layers) &&
           frames_equal(a_value.frames, b_value.frames) &&
           tags_equal(a_value.tags, b_value.tags) &&
           a_value.playback_mode == b_value.playback_mode;
}

bool document_metadata_equal(const Document& a_value, const Document& b_value) {
    if (a_value.width != b_value.width ||
        a_value.height != b_value.height ||
        a_value.active_layer != b_value.active_layer ||
        a_value.active_frame != b_value.active_frame ||
        a_value.palette.colors != b_value.palette.colors ||
        a_value.palette.active != b_value.palette.active ||
        a_value.layers.size() != b_value.layers.size() ||
        a_value.frames.size() != b_value.frames.size() ||
        !tags_equal(a_value.tags, b_value.tags) ||
        a_value.playback_mode != b_value.playback_mode) {
        return false;
    }
    for (std::size_t i = 0; i < a_value.layers.size(); ++i) {
        const Layer& a_layer = a_value.layers[i];
        const Layer& b_layer = b_value.layers[i];
        if (a_layer.name != b_layer.name ||
            a_layer.visible != b_layer.visible ||
            a_layer.opacity != b_layer.opacity ||
            a_layer.blend_mode != b_layer.blend_mode ||
            a_layer.mask_enabled != b_layer.mask_enabled ||
            a_layer.clip_to_below != b_layer.clip_to_below ||
            a_layer.mask.size() != b_layer.mask.size()) {
            return false;
        }
    }
    for (std::size_t frame_index = 0; frame_index < a_value.frames.size(); ++frame_index) {
        const Frame& a_frame = a_value.frames[frame_index];
        const Frame& b_frame = b_value.frames[frame_index];
        if (a_frame.duration_ms != b_frame.duration_ms ||
            a_frame.cels.size() != b_frame.cels.size()) {
            return false;
        }
        for (std::size_t cel_index = 0; cel_index < a_frame.cels.size(); ++cel_index) {
            const Cel& a_cel = a_frame.cels[cel_index];
            const Cel& b_cel = b_frame.cels[cel_index];
            if (a_cel.x != b_cel.x ||
                a_cel.y != b_cel.y ||
                a_cel.pixels.size() != b_cel.pixels.size()) {
                return false;
            }
        }
    }
    return true;
}

bool can_use_active_cel_as_composite(const Document& document) {
    if (document.layers.size() != 1U || document.frames.empty() ||
        document.active_frame < 0 || document.active_frame >= static_cast<int>(document.frames.size())) {
        return false;
    }
    const Layer& layer = document.layers.front();
    if (!layer.visible || layer.opacity < 0.999f || layer.blend_mode != LayerBlendMode::Normal ||
        layer.mask_enabled || layer.clip_to_below) {
        return false;
    }
    const Frame& frame = document.frames[static_cast<std::size_t>(document.active_frame)];
    if (frame.cels.size() != 1U) {
        return false;
    }
    const Cel& cel = frame.cels.front();
    return cel.x == 0 && cel.y == 0 &&
           cel.pixels.size() == static_cast<std::size_t>(document.width) * static_cast<std::size_t>(document.height);
}

bool full_canvas_texture_fits_budget(int width, int height) {
    constexpr std::size_t kMaxFullCanvasTexturePixels = 8192U * 8192U;
    if (width <= 0 || height <= 0) {
        return false;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) <= kMaxFullCanvasTexturePixels;
}

std::size_t huge_document_pixel_threshold() {
    constexpr std::size_t kDefaultHugeDocumentPixels = (256U * 1024U * 1024U) / sizeof(Pixel);
    const char* threshold = std::getenv("PIXELART_HUGE_DOCUMENT_PIXEL_THRESHOLD");
    if (threshold == nullptr || threshold[0] == '\0') {
        return kDefaultHugeDocumentPixels;
    }
    const char* end = threshold + std::strlen(threshold);
    std::size_t parsed = 0;
    const auto result = std::from_chars(threshold, end, parsed);
    if (result.ec == std::errc{} && result.ptr == end && parsed > 0U) {
        return parsed;
    }
    return kDefaultHugeDocumentPixels;
}

std::size_t env_size_or_default(const char* name, std::size_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    const char* end = value + std::strlen(value);
    std::size_t parsed = 0;
    const auto result = std::from_chars(value, end, parsed);
    if (result.ec == std::errc{} && result.ptr == end && parsed > 0U) {
        return parsed;
    }
    return fallback;
}

bool is_huge_document_dimensions(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) >= huge_document_pixel_threshold();
}

bool is_huge_document(const Document& document) {
    return is_huge_document_dimensions(document.width, document.height);
}

void trace_document_pixel_buffers(std::string_view label, const Document& document) {
    if (!memory_trace_enabled()) {
        return;
    }
    const std::size_t expected_pixels = document.width > 0 && document.height > 0
                                            ? static_cast<std::size_t>(document.width) * static_cast<std::size_t>(document.height)
                                            : 0U;
    memory_trace_event("document",
                       {},
                       label,
                       nullptr,
                       expected_pixels,
                       expected_pixels,
                       sizeof(Pixel),
                       "frames=" + std::to_string(document.frames.size()) + ";layers=" + std::to_string(document.layers.size()));
    for (std::size_t frame_index = 0; frame_index < document.frames.size(); ++frame_index) {
        const Frame& frame = document.frames[frame_index];
        for (std::size_t cel_index = 0; cel_index < frame.cels.size(); ++cel_index) {
            memory_trace_vector(std::string(label) + ".frame" + std::to_string(frame_index) +
                                    ".cel" + std::to_string(cel_index),
                                frame.cels[cel_index].pixels);
        }
    }
}

std::size_t document_pixel_capacity(const Document& document) {
    std::size_t total = 0;
    for (const Frame& frame : document.frames) {
        for (const Cel& cel : frame.cels) {
            total += cel.pixels.capacity();
        }
    }
    return total;
}

struct HistogramBins {
    std::array<int, 256> luma{};
    std::array<int, 256> red{};
    std::array<int, 256> green{};
    std::array<int, 256> blue{};
};

void add_pixel_to_histogram(HistogramBins& bins, Pixel pixel) {
    if (a(pixel) == 0) {
        return;
    }
    const int red = r(pixel);
    const int green = g(pixel);
    const int blue = b(pixel);
    const int luma = std::clamp(static_cast<int>(0.2126f * static_cast<float>(red) +
                                                 0.7152f * static_cast<float>(green) +
                                                 0.0722f * static_cast<float>(blue) +
                                                 0.5f),
                                0,
                                255);
    bins.luma[static_cast<std::size_t>(luma)] += 1;
    bins.red[static_cast<std::size_t>(red)] += 1;
    bins.green[static_cast<std::size_t>(green)] += 1;
    bins.blue[static_cast<std::size_t>(blue)] += 1;
}

HistogramBins build_histogram_bins(const std::vector<Pixel>& pixels) {
    constexpr std::size_t kMinPixelsPerWorker = 262144;
    const std::size_t pixel_count = pixels.size();
    const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const unsigned max_workers = static_cast<unsigned>(std::max<std::size_t>(1, pixel_count / kMinPixelsPerWorker));
    const unsigned worker_count = std::min({hardware_threads, max_workers, 8u});
    if (worker_count <= 1 || pixel_count < kMinPixelsPerWorker * 2) {
        HistogramBins bins;
        for (Pixel pixel : pixels) {
            add_pixel_to_histogram(bins, pixel);
        }
        return bins;
    }

    std::vector<HistogramBins> local_bins(worker_count);
    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    auto process_range = [&](unsigned worker_index) {
        const std::size_t begin = pixel_count * static_cast<std::size_t>(worker_index) /
                                  static_cast<std::size_t>(worker_count);
        const std::size_t end = pixel_count * static_cast<std::size_t>(worker_index + 1) /
                                static_cast<std::size_t>(worker_count);
        HistogramBins& bins = local_bins[worker_index];
        for (std::size_t i = begin; i < end; ++i) {
            add_pixel_to_histogram(bins, pixels[i]);
        }
    };

    for (unsigned worker_index = 1; worker_index < worker_count; ++worker_index) {
        workers.emplace_back(process_range, worker_index);
    }
    process_range(0);
    for (std::thread& worker : workers) {
        worker.join();
    }

    HistogramBins merged;
    for (const HistogramBins& bins : local_bins) {
        for (std::size_t i = 0; i < merged.luma.size(); ++i) {
            merged.luma[i] += bins.luma[i];
            merged.red[i] += bins.red[i];
            merged.green[i] += bins.green[i];
            merged.blue[i] += bins.blue[i];
        }
    }
    return merged;
}

HistogramBins build_histogram_bins_sampled(const std::vector<Pixel>& pixels, std::size_t max_samples) {
    if (pixels.size() <= max_samples || max_samples == 0U) {
        return build_histogram_bins(pixels);
    }
    HistogramBins bins;
    const std::size_t stride = std::max<std::size_t>(1U, pixels.size() / max_samples);
    for (std::size_t i = 0; i < pixels.size(); i += stride) {
        add_pixel_to_histogram(bins, pixels[i]);
    }
    return bins;
}

std::array<float, 256> normalize_histogram(const std::array<int, 256>& hist) {
    std::array<float, 256> values{};
    int max_value = 1;
    for (int value : hist) {
        max_value = std::max(max_value, value);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<float>(hist[i]) / static_cast<float>(max_value);
    }
    return values;
}

bool uv_equal(const UvRect& a_value, const UvRect& b_value) {
    return a_value.x == b_value.x &&
           a_value.y == b_value.y &&
           a_value.w == b_value.w &&
           a_value.h == b_value.h;
}

bool cuboid_equal(const Cuboid& a_value, const Cuboid& b_value) {
    if (a_value.name != b_value.name ||
        a_value.from != b_value.from ||
        a_value.to != b_value.to ||
        a_value.rotation_angle != b_value.rotation_angle ||
        a_value.rotation_axis != b_value.rotation_axis ||
        a_value.rotation_origin != b_value.rotation_origin ||
        a_value.rotation_rescale != b_value.rotation_rescale ||
        a_value.selected != b_value.selected) {
        return false;
    }
    for (std::size_t i = 0; i < a_value.uv.size(); ++i) {
        if (!uv_equal(a_value.uv[i], b_value.uv[i])) {
            return false;
        }
    }
    return true;
}

bool model_state_equal(const ModelDocument& a_value, const ModelDocument& b_value) {
    if (a_value.texture_width != b_value.texture_width ||
        a_value.texture_height != b_value.texture_height ||
        a_value.selected_cuboid != b_value.selected_cuboid ||
        a_value.selected_face != b_value.selected_face ||
        a_value.cuboids.size() != b_value.cuboids.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a_value.cuboids.size(); ++i) {
        if (!cuboid_equal(a_value.cuboids[i], b_value.cuboids[i])) {
            return false;
        }
    }
    return true;
}

std::vector<Pixel> extracted_palette_from_document(const Document& document, int max_colors) {
    std::unordered_map<Pixel, int> counts;
    for (const Pixel pixel : document.composite_active()) {
        if (a(pixel) == 0) {
            continue;
        }
        ++counts[pixel];
    }
    std::vector<std::pair<Pixel, int>> ranked;
    ranked.reserve(counts.size());
    for (const auto& item : counts) {
        ranked.push_back(item);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.second != rhs.second) {
            return lhs.second > rhs.second;
        }
        return lhs.first < rhs.first;
    });
    std::vector<Pixel> colors;
    colors.reserve(static_cast<std::size_t>(std::max(0, max_colors)));
    for (const auto& item : ranked) {
        colors.push_back(item.first);
        if (static_cast<int>(colors.size()) >= max_colors) {
            break;
        }
    }
    return colors;
}

float pixel_luma(Pixel pixel) {
    return 0.2126f * static_cast<float>(r(pixel)) +
           0.7152f * static_cast<float>(g(pixel)) +
           0.0722f * static_cast<float>(b(pixel));
}

float pixel_hue(Pixel pixel) {
    const float red = static_cast<float>(r(pixel)) / 255.0f;
    const float green = static_cast<float>(g(pixel)) / 255.0f;
    const float blue = static_cast<float>(b(pixel)) / 255.0f;
    const float max_channel = std::max(red, std::max(green, blue));
    const float min_channel = std::min(red, std::min(green, blue));
    const float delta = max_channel - min_channel;
    if (delta <= 0.0f) {
        return 0.0f;
    }
    float hue = 0.0f;
    if (max_channel == red) {
        hue = 60.0f * std::fmod(((green - blue) / delta), 6.0f);
    } else if (max_channel == green) {
        hue = 60.0f * (((blue - red) / delta) + 2.0f);
    } else {
        hue = 60.0f * (((red - green) / delta) + 4.0f);
    }
    return hue < 0.0f ? hue + 360.0f : hue;
}

std::vector<Pixel> palette_ramp(Pixel first, Pixel second, int steps) {
    std::vector<Pixel> colors;
    const int count = std::max(2, steps);
    colors.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        const float inv = 1.0f - t;
        colors.push_back(rgba(static_cast<std::uint8_t>(static_cast<float>(r(first)) * inv + static_cast<float>(r(second)) * t + 0.5f),
                              static_cast<std::uint8_t>(static_cast<float>(g(first)) * inv + static_cast<float>(g(second)) * t + 0.5f),
                              static_cast<std::uint8_t>(static_cast<float>(b(first)) * inv + static_cast<float>(b(second)) * t + 0.5f),
                              static_cast<std::uint8_t>(static_cast<float>(a(first)) * inv + static_cast<float>(a(second)) * t + 0.5f)));
    }
    return colors;
}

void ensure_layer_mask(Layer& layer, int width, int height, std::uint8_t value) {
    const std::size_t size = static_cast<std::size_t>(std::max(1, width) * std::max(1, height));
    if (layer.mask.size() != size) {
        layer.mask.assign(size, value);
    }
}

void fill_layer_mask_from_selection(Layer& layer, const SelectionMask& selection, int width, int height) {
    const std::size_t size = static_cast<std::size_t>(std::max(1, width) * std::max(1, height));
    layer.mask.assign(size, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (selection.contains(x, y)) {
                layer.mask[static_cast<std::size_t>(y * width + x)] = 255;
            }
        }
    }
    layer.mask_enabled = true;
}

void fill_layer_mask_from_alpha(Layer& layer, const Cel& cel, int width, int height) {
    const std::size_t size = static_cast<std::size_t>(std::max(1, width) * std::max(1, height));
    layer.mask.assign(size, 0);
    for (std::size_t i = 0; i < layer.mask.size() && i < cel.pixels.size(); ++i) {
        layer.mask[i] = a(cel.pixels[i]);
    }
    layer.mask_enabled = true;
}

void load_selection_from_layer_mask(SelectionMask& selection, const Layer& layer, int width, int height) {
    if (layer.mask.size() != static_cast<std::size_t>(width * height)) {
        return;
    }
    selection.resize(width, height);
    for (std::size_t i = 0; i < layer.mask.size(); ++i) {
        selection.mask[i] = static_cast<std::uint8_t>(layer.mask[i] > 0);
    }
    selection.active = selection.selected_count() > 0;
}

void invert_layer_mask(Layer& layer, int width, int height) {
    ensure_layer_mask(layer, width, height, 255);
    for (std::uint8_t& value : layer.mask) {
        value = static_cast<std::uint8_t>(255 - value);
    }
    layer.mask_enabled = true;
}

void draw_layer_thumbnail(const Document& document, int layer_index, ImVec2 size) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    draw_list->AddRectFilled(origin, end, IM_COL32(42, 42, 42, 255));

    constexpr float cell = 6.0f;
    for (float y = origin.y; y < end.y; y += cell) {
        for (float x = origin.x; x < end.x; x += cell) {
            const bool light =
                (static_cast<int>((x - origin.x) / cell) + static_cast<int>((y - origin.y) / cell)) % 2 == 0;
            draw_list->AddRectFilled(ImVec2(x, y),
                                     ImVec2(std::min(x + cell, end.x), std::min(y + cell, end.y)),
                                     light ? IM_COL32(86, 86, 86, 255) : IM_COL32(58, 58, 58, 255));
        }
    }

    const Cel& cel = document.cel(document.active_frame, layer_index);
    const int samples_x = std::max(1, std::min(document.width, static_cast<int>(size.x)));
    const int samples_y = std::max(1, std::min(document.height, static_cast<int>(size.y)));
    for (int sy = 0; sy < samples_y; ++sy) {
        for (int sx = 0; sx < samples_x; ++sx) {
            const int px = std::min(document.width - 1, sx * document.width / samples_x);
            const int py = std::min(document.height - 1, sy * document.height / samples_y);
            const Pixel pixel = cel.pixels[static_cast<std::size_t>(document.pixel_index(px, py))];
            if (a(pixel) == 0) {
                continue;
            }
            const float x0 = origin.x + static_cast<float>(sx) * size.x / static_cast<float>(samples_x);
            const float y0 = origin.y + static_cast<float>(sy) * size.y / static_cast<float>(samples_y);
            const float x1 = origin.x + static_cast<float>(sx + 1) * size.x / static_cast<float>(samples_x);
            const float y1 = origin.y + static_cast<float>(sy + 1) * size.y / static_cast<float>(samples_y);
            draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color_u32(pixel));
        }
    }
    draw_list->AddRect(origin, end, IM_COL32(20, 20, 20, 220));
    ImGui::InvisibleButton("thumbnail", size);
}

constexpr FileFilter kProjectFilters[] = {{"PixelArt project", "pixart"}};
constexpr FileFilter kImageFilters[] = {{"Image files", "png,jpg,jpeg,bmp,tga"}};
constexpr FileFilter kPngFilters[] = {{"PNG image", "png"}};
constexpr FileFilter kGifFilters[] = {{"GIF animation", "gif"}};
constexpr FileFilter kAsepriteFilters[] = {{"Aseprite sprite", "aseprite,ase"}};
constexpr FileFilter kJsonFilters[] = {{"JSON", "json"}};
constexpr FileFilter kGltfFilters[] = {{"glTF model", "gltf"}};
constexpr FileFilter kThreeJSPackFilters[] = {{"ThreeJSPack", "threejspack"}};

struct SkyboxOption {
    const char* name;
    const char* directory;
};

constexpr SkyboxOption kSkyboxes[] = {
    {"Solid", ""},
    {"Kiara Dawn", "res/skyboxes/kiara_1_dawn"},
    {"Venice Sunset", "res/skyboxes/venice_sunset"},
    {"Snowy Field", "res/skyboxes/snowy_field"},
};

constexpr const char* kSkyboxFaceNames[] = {"front", "right", "back", "left", "top", "bottom"};

struct TransformIconAsset {
    const unsigned char* bytes;
    unsigned int byte_count;
    const char* tooltip;
};

const TransformIconAsset kTransformIconAssets[] = {
    {px_transform_icon_select_png, px_transform_icon_select_png_len, "Select"},
    {px_transform_icon_translate_png, px_transform_icon_translate_png_len, "Move"},
    {px_transform_icon_rotate_png, px_transform_icon_rotate_png_len, "Rotate"},
    {px_transform_icon_scale_png, px_transform_icon_scale_png_len, "Stretch"},
};

constexpr float kTransformToolbarPadding = 10.0f;
constexpr float kTransformToolbarButtonSize = 38.0f;
constexpr float kTransformToolbarGap = 6.0f;
constexpr float kTransformToolbarIconInset = 8.0f;

template <std::size_t N>
FileFilterList filter_list(const FileFilter (&items)[N]) {
    return {items, N};
}

std::string default_folder_for(const char* remembered_path) {
    if (remembered_path == nullptr || remembered_path[0] == '\0') {
        return {};
    }
    std::filesystem::path path = path_from_utf8(remembered_path);
    std::filesystem::path parent = path.parent_path();
    return parent.empty() ? std::string{} : path_to_utf8(parent);
}

std::string default_name_for(const char* remembered_path) {
    if (remembered_path == nullptr || remembered_path[0] == '\0') {
        return {};
    }
    std::filesystem::path path = path_from_utf8(remembered_path);
    std::filesystem::path filename = path.filename();
    return filename.empty() ? std::string(remembered_path) : path_to_utf8(filename);
}

std::string with_default_extension(const std::string& path, const char* extension) {
    std::filesystem::path fs_path = path_from_utf8(path);
    if (fs_path.extension().empty()) {
        fs_path += extension;
        return path_to_utf8(fs_path);
    }
    return path;
}

std::string replace_extension(const std::string& path, const char* extension) {
    std::filesystem::path fs_path = path_from_utf8(path);
    fs_path.replace_extension(extension);
    return path_to_utf8(fs_path);
}

std::string minecraft_texture_path_for(const std::string& model_path) {
    std::filesystem::path fs_path = path_from_utf8(model_path);
    std::string texture_name = path_to_utf8(fs_path.stem()) + "_texture.png";
    fs_path.replace_filename(path_from_utf8(texture_name));
    return path_to_utf8(fs_path);
}

std::string gltf_texture_path_for(const std::string& model_path) {
    std::filesystem::path fs_path = path_from_utf8(model_path);
    std::string texture_name = path_to_utf8(fs_path.stem()) + "_texture.png";
    fs_path.replace_filename(path_from_utf8(texture_name));
    return path_to_utf8(fs_path);
}

std::string filename_for_reference(const std::string& path) {
    std::filesystem::path fs_path = path_from_utf8(path);
    std::filesystem::path filename = fs_path.filename();
    return filename.empty() ? path : path_to_utf8(filename);
}

bool load_jpeg_pixels(const char* path, int& width, int& height, std::vector<Pixel>& pixels) {
    int channels = 0;
    unsigned char* decoded = stbi_load(path, &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return false;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels.resize(pixel_count);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::size_t offset = i * 4;
        pixels[i] = rgba(decoded[offset], decoded[offset + 1], decoded[offset + 2], decoded[offset + 3]);
    }
    stbi_image_free(decoded);
    return true;
}

bool load_png_pixels_from_memory(const unsigned char* bytes,
                                 unsigned int byte_count,
                                 int& width,
                                 int& height,
                                 std::vector<Pixel>& pixels) {
    if (bytes == nullptr || byte_count == 0U) {
        return false;
    }
    int channels = 0;
    unsigned char* decoded =
        stbi_load_from_memory(bytes, static_cast<int>(byte_count), &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return false;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels.resize(pixel_count);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::size_t offset = i * 4;
        pixels[i] = rgba(decoded[offset], decoded[offset + 1], decoded[offset + 2], decoded[offset + 3]);
    }
    stbi_image_free(decoded);
    return true;
}

} // namespace

EditorApp::EditorApp(FileDialogProvider* dialogs, AppSettings settings)
    : document_(Document::create(64, 64)),
      model_(ModelDocument::create_default()),
      settings_(settings) {
    dialogs_ = dialogs;
    if (!settings_.heavy_gpu_optimization) {
        settings_.mps_backend = false;
    }
    depth_tile_size_ = std::clamp(settings_.depth_tile_size, 64, 4096);
    depth_tile_overlap_ = std::clamp(settings_.depth_tile_overlap, 0, depth_tile_size_ / 2);
    tool_.primary = rgba(0, 0, 0);
    tool_.secondary = rgba(255, 255, 255);
    reset_history_tree();
}

EditorApp::~EditorApp() {
    depth_cancel_requested_.store(true);
    if (depth_thread_.joinable()) {
        depth_thread_.join();
    }
    if (image_import_thread_.joinable()) {
        image_import_thread_.join();
    }
    save_settings();
}

void EditorApp::debug_replace_document_for_memory_test(Document document) {
    document_ = std::move(document);
    sync_model_texture_metadata();
    reset_history_tree();
    texture_dirty_ = true;
    full_canvas_texture_dirty_ = true;
    refresh_texture();
}

bool EditorApp::debug_huge_document_history_mode() const {
    return huge_document_history_mode();
}

bool EditorApp::debug_canvas_uses_active_cel() const {
    return composite_uses_active_cel_;
}

std::size_t EditorApp::debug_composite_pixel_capacity() const {
    return composite_.capacity();
}

std::size_t EditorApp::debug_history_document_pixel_capacity() const {
    std::size_t total = document_pixel_capacity(history_before_document_);
    for (const EditorHistoryNode& node : history_nodes_) {
        total += document_pixel_capacity(node.before_document);
        total += document_pixel_capacity(node.after_document);
    }
    return total;
}

void EditorApp::debug_update_histogram_cache_for_memory_test() {
    invalidate_histogram_cache();
    update_histogram_cache();
}

bool EditorApp::debug_histogram_cache_approximate() const {
    return histogram_cache_approximate_;
}

void EditorApp::render() {
    finish_image_import_job_if_ready();
    update_playback();
    refresh_texture();
    begin_history_frame();
    handle_global_shortcuts();
    draw_dockspace();
    draw_main_menu();
    draw_new_document_dialog();
    if (settings_.show_tools_panel) {
        draw_toolbar();
    }
    draw_canvas();
    if (settings_.show_colors_panel) {
        draw_color_panel();
    }
    if (settings_.show_layers_panel) {
        draw_layers_panel();
    }
    if (settings_.show_animation_panel) {
        draw_timeline_panel();
    }
    if (settings_.show_adjustments_panel) {
        draw_adjustments_panel();
    }
    draw_model_panel();
    draw_model_preview_window();
    draw_tile_preview_window();
    draw_effect_preview_popup();
    draw_rotate_zoom_popup();
    draw_depth_map_popup();
    draw_image_import_popup();
    draw_undo_tree_window();
    draw_error_console();
    draw_status_bar();
    end_history_frame();
}

void EditorApp::import_image_document(const std::string& path) {
    if (path.empty()) {
        return;
    }
    copy_path(image_path_, sizeof(image_path_), path);
    start_image_document_import(path);
}

void EditorApp::update_playback() {
    if (!playing_ || document_.frames.empty()) {
        return;
    }
    playback_accum_ += ImGui::GetIO().DeltaTime * 1000.0f;
    int duration = std::max(1, document_.frames[static_cast<std::size_t>(document_.active_frame)].duration_ms);
    if (playback_accum_ >= static_cast<float>(duration)) {
        playback_accum_ = 0.0f;
        if (document_.playback_mode == PlaybackMode::PingPong && document_.frames.size() > 1) {
            document_.active_frame += playback_direction_;
            if (document_.active_frame >= static_cast<int>(document_.frames.size())) {
                playback_direction_ = -1;
                document_.active_frame = static_cast<int>(document_.frames.size()) - 2;
            } else if (document_.active_frame < 0) {
                playback_direction_ = 1;
                document_.active_frame = 1;
            }
        } else {
            document_.active_frame = (document_.active_frame + 1) % static_cast<int>(document_.frames.size());
        }
        texture_dirty_ = true;
        history_playback_frame_change_ = true;
        if (!history_pending_ &&
            history_before_document_.frames.size() == document_.frames.size() &&
            history_before_document_.width == document_.width &&
            history_before_document_.height == document_.height) {
            history_before_document_.active_frame = document_.active_frame;
        }
    }
}

void EditorApp::refresh_texture() {
    MemoryTraceScope trace("refresh_texture");
    if (effect_preview_active_) {
        return;
    }
    if (!texture_dirty_) {
        return;
    }
    trace_document_pixel_buffers("refresh_texture.document_before", document_);
    memory_trace_vector("refresh_texture.composite_before", composite_);
    composite_uses_active_cel_ = can_use_active_cel_as_composite(document_);
    if (composite_uses_active_cel_) {
        composite_.clear();
        composite_.shrink_to_fit();
        memory_trace_note("refresh_texture.using_active_cel");
    } else {
        composite_ = document_.composite_active();
    }
    memory_trace_vector("refresh_texture.composite_after", composite_);
    if (is_huge_document(document_) && tiled_canvas_texture_.has_prepared_pyramid()) {
        memory_trace_note("refresh_texture.clear_stale_pyramid", "huge_document_edit");
        tiled_canvas_texture_.clear_prepared_pyramid();
    }
    tiled_canvas_texture_.invalidate();
    tiled_onion_texture_.invalidate();
    full_canvas_texture_dirty_ = true;
    texture_dirty_ = false;
    history_playback_frame_change_ = false;
    invalidate_histogram_cache();
}

const std::vector<Pixel>& EditorApp::canvas_pixels() const {
    if (composite_uses_active_cel_ && document_.has_active_cel()) {
        return document_.active_cel().pixels;
    }
    return composite_;
}

bool EditorApp::ensure_full_canvas_texture() {
    MemoryTraceScope trace("ensure_full_canvas_texture");
    const std::size_t expected_size = static_cast<std::size_t>(document_.width) * static_cast<std::size_t>(document_.height);
    if (!full_canvas_texture_fits_budget(document_.width, document_.height)) {
        canvas_texture_.destroy();
        full_canvas_texture_dirty_ = true;
        return false;
    }
    const std::vector<Pixel>* source = &canvas_pixels();
    memory_trace_vector("ensure_full_canvas_texture.source", *source);
    if (source->size() != expected_size) {
        composite_ = document_.composite_active();
        composite_uses_active_cel_ = false;
        source = &composite_;
        memory_trace_vector("ensure_full_canvas_texture.composite_fallback", composite_);
        tiled_canvas_texture_.invalidate();
        full_canvas_texture_dirty_ = true;
    }
    if (full_canvas_texture_dirty_ ||
        canvas_texture_.width() != document_.width ||
        canvas_texture_.height() != document_.height) {
        canvas_texture_.update(document_.width, document_.height, *source);
        full_canvas_texture_dirty_ = false;
    }
    return canvas_texture_.id() != 0;
}

void EditorApp::invalidate_histogram_cache() {
    histogram_cache_valid_ = false;
    histogram_cache_approximate_ = false;
}

void EditorApp::update_histogram_cache() {
    MemoryTraceScope trace("update_histogram_cache");
    if (histogram_cache_valid_ &&
        histogram_cache_width_ == document_.width &&
        histogram_cache_height_ == document_.height) {
        return;
    }

    const std::vector<Pixel>* source = &canvas_pixels();
    memory_trace_vector("update_histogram_cache.source_initial", *source);
    const std::size_t expected_size = static_cast<std::size_t>(document_.width) * static_cast<std::size_t>(document_.height);
    if (source->size() != expected_size) {
        composite_ = document_.composite_active();
        composite_uses_active_cel_ = false;
        source = &composite_;
        memory_trace_vector("update_histogram_cache.composite_fallback", composite_);
    }

    const std::size_t max_huge_display_histogram_samples =
        env_size_or_default("PIXELART_HISTOGRAM_SAMPLE_LIMIT", 1024U * 1024U);
    const bool approximate_display = is_huge_document(document_) && source->size() > max_huge_display_histogram_samples;
    const HistogramBins bins = approximate_display
                                   ? build_histogram_bins_sampled(*source, max_huge_display_histogram_samples)
                                   : build_histogram_bins(*source);
    memory_trace_note("update_histogram_cache.mode", approximate_display ? "sampled" : "full");
    histogram_luma_values_ = normalize_histogram(bins.luma);
    histogram_red_values_ = normalize_histogram(bins.red);
    histogram_green_values_ = normalize_histogram(bins.green);
    histogram_blue_values_ = normalize_histogram(bins.blue);
    histogram_cache_width_ = document_.width;
    histogram_cache_height_ = document_.height;
    histogram_cache_approximate_ = approximate_display;
    histogram_cache_valid_ = true;
}

void EditorApp::set_status(const std::string& status) {
    std::snprintf(status_, sizeof(status_), "%s", status.c_str());
}

void EditorApp::report_error(std::string_view context, std::string_view details) {
    constexpr std::size_t kMaxErrorEntries = 200;
    const std::string status = details.empty() ? std::string(context) : std::string(details);
    set_status(status);
    error_console_entries_.push_back({++error_console_sequence_, std::string(context), std::string(details)});
    if (error_console_entries_.size() > kMaxErrorEntries) {
        const std::size_t extra_count = error_console_entries_.size() - kMaxErrorEntries;
        error_console_entries_.erase(error_console_entries_.begin(),
                                     error_console_entries_.begin() + static_cast<std::ptrdiff_t>(extra_count));
    }
    if (settings_.auto_open_error_console) {
        error_console_open_ = true;
        error_console_scroll_to_bottom_ = true;
    }
}

void EditorApp::save_settings() {
    std::string error;
    if (!save_app_settings(settings_, &error)) {
        report_error("Save settings", error);
    }
}

void EditorApp::handle_global_shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    const bool action_modifier = io.KeyCtrl || io.KeySuper;
    const bool shift = io.KeyShift;
    const bool z_pressed = ImGui::IsKeyPressed(ImGuiKey_Z, false);
    const bool y_pressed = ImGui::IsKeyPressed(ImGuiKey_Y, false);
    const bool undo_requested = action_modifier && z_pressed && !shift;
    const bool redo_requested = action_modifier && (y_pressed || (z_pressed && shift));
    if (undo_requested) {
        texture_dirty_ = undo_editor() || texture_dirty_;
    } else if (redo_requested) {
        texture_dirty_ = redo_editor() || texture_dirty_;
    }
}

void EditorApp::sync_model_texture_metadata() {
    model_.texture_width = document_.width;
    model_.texture_height = document_.height;
    clamp_model_uvs(model_);
}

void EditorApp::begin_history_frame() {
    if (huge_document_history_mode()) {
        return;
    }
    if (history_suppress_frame_ && !history_pending_) {
        history_before_document_ = document_;
        history_before_model_ = model_;
    }
}

bool EditorApp::huge_document_history_mode() const {
    return history_lightweight_mode_ || is_huge_document(document_);
}

bool EditorApp::editor_state_changed_from_history_baseline() const {
    if (huge_document_history_mode()) {
        return !model_state_equal(history_before_model_, model_);
    }
    return !document_state_equal(history_before_document_, document_) ||
           !model_state_equal(history_before_model_, model_);
}

bool EditorApp::history_interaction_in_progress() const {
    return drag_active_ ||
           stroke_active_ ||
           clone_drag_active_ ||
           move_drag_active_ ||
           pixel_drag_preview_active_ ||
           lasso_active_ ||
           uv_drag_active_ ||
           model_transform_drag_active_ ||
           model_view_gizmo_drag_active_ ||
           ImGui::IsAnyItemActive();
}

std::string EditorApp::history_label_from_changes(const Document& before_document,
                                                  const ModelDocument& before_model,
                                                  const Document& after_document,
                                                  const ModelDocument& after_model,
                                                  const std::vector<std::string>& document_labels) const {
    if (!document_labels.empty()) {
        if (document_labels.size() == 1) {
            return document_labels.front();
        }
        bool same = true;
        for (std::size_t i = 1; i < document_labels.size(); ++i) {
            if (document_labels[i] != document_labels.front()) {
                same = false;
                break;
            }
        }
        return same ? document_labels.front()
                    : document_labels.front() + " +" + std::to_string(document_labels.size() - 1);
    }
    if (!model_state_equal(before_model, after_model)) {
        if (before_model.cuboids.size() != after_model.cuboids.size()) {
            return before_model.cuboids.size() < after_model.cuboids.size() ? "Add Cuboid" : "Remove Cuboid";
        }
        if (before_model.selected_cuboid != after_model.selected_cuboid ||
            before_model.selected_face != after_model.selected_face) {
            return "Select Model Part";
        }
        return "Edit Model";
    }
    if (!document_state_equal(before_document, after_document)) {
        return "Edit";
    }
    return "Edit";
}

void EditorApp::reset_history_tree() {
    MemoryTraceScope trace("reset_history_tree");
    trace_document_pixel_buffers("reset_history_tree.document", document_);
    document_.clear_recent_commit_names();
    history_nodes_.clear();
    history_lightweight_mode_ = is_huge_document(document_);
    memory_trace_note("reset_history_tree.mode", history_lightweight_mode_ ? "lightweight" : "full");
    EditorHistoryNode root;
    root.id = 0;
    root.parent = -1;
    root.name = history_lightweight_mode_ ? "Initial State (lightweight)" : "Initial State";
    if (!history_lightweight_mode_) {
        root.before_document = document_;
        root.after_document = document_;
    }
    root.before_model = model_;
    root.after_model = model_;
    history_nodes_.push_back(std::move(root));
    history_current_node_ = 0;
    next_history_node_id_ = 1;
    history_before_document_ = history_lightweight_mode_ ? Document{} : document_;
    trace_document_pixel_buffers("reset_history_tree.history_before_document", history_before_document_);
    if (!history_nodes_.empty()) {
        trace_document_pixel_buffers("reset_history_tree.root_before_document", history_nodes_.front().before_document);
        trace_document_pixel_buffers("reset_history_tree.root_after_document", history_nodes_.front().after_document);
    }
    history_before_model_ = model_;
    history_pending_ = false;
    history_suppress_frame_ = true;
    history_playback_frame_change_ = false;
}

EditorHistoryNode* EditorApp::history_node_by_id(int id) {
    for (EditorHistoryNode& node : history_nodes_) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

const EditorHistoryNode* EditorApp::history_node_by_id(int id) const {
    for (const EditorHistoryNode& node : history_nodes_) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

void EditorApp::restore_history_node(int node_id) {
    const EditorHistoryNode* node = history_node_by_id(node_id);
    if (node == nullptr) {
        return;
    }
    if (!huge_document_history_mode()) {
        document_ = node->after_document;
    }
    model_ = node->after_model;
    document_.clear_recent_commit_names();
    history_current_node_ = node->id;
    sync_model_texture_metadata();
    texture_dirty_ = true;
    invalidate_histogram_cache();
    history_pending_ = false;
    history_suppress_frame_ = true;
    history_playback_frame_change_ = false;
}

void EditorApp::prune_history_tree() {
    constexpr std::size_t kMaxHistoryNodes = 129;
    while (history_nodes_.size() > kMaxHistoryNodes) {
        int prune_id = -1;
        for (const EditorHistoryNode& node : history_nodes_) {
            if (node.id != 0 && node.id != history_current_node_ && node.children.empty()) {
                prune_id = node.id;
                break;
            }
        }
        if (prune_id < 0) {
            return;
        }
        EditorHistoryNode* parent = nullptr;
        if (const EditorHistoryNode* node = history_node_by_id(prune_id)) {
            parent = history_node_by_id(node->parent);
        }
        if (parent != nullptr) {
            parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), prune_id),
                                   parent->children.end());
            if (parent->preferred_child == prune_id) {
                parent->preferred_child = parent->children.empty() ? -1 : parent->children.back();
            }
        }
        history_nodes_.erase(std::remove_if(history_nodes_.begin(),
                                            history_nodes_.end(),
                                            [prune_id](const EditorHistoryNode& node) {
                                                return node.id == prune_id;
                                            }),
                             history_nodes_.end());
    }
}

void EditorApp::push_history_entry(const std::string& name,
                                   const Document& before_document,
                                   const ModelDocument& before_model,
                                   const Document& after_document,
                                   const ModelDocument& after_model) {
    MemoryTraceScope trace("push_history_entry", name);
    trace_document_pixel_buffers("push_history_entry.before_document", before_document);
    trace_document_pixel_buffers("push_history_entry.after_document", after_document);
    if (is_huge_document(before_document) || is_huge_document(after_document)) {
        memory_trace_note("push_history_entry.skipped", "huge_document");
        history_before_model_ = after_model;
        return;
    }
    if (document_state_equal(before_document, after_document) && model_state_equal(before_model, after_model)) {
        return;
    }
    EditorHistoryNode node;
    node.id = next_history_node_id_++;
    node.parent = history_current_node_;
    node.name = name;
    node.before_document = before_document;
    node.before_model = before_model;
    node.after_document = after_document;
    node.after_model = after_model;
    history_nodes_.push_back(std::move(node));
    if (EditorHistoryNode* parent = history_node_by_id(history_current_node_)) {
        parent->children.push_back(history_nodes_.back().id);
        parent->preferred_child = history_nodes_.back().id;
    }
    history_current_node_ = history_nodes_.back().id;
    prune_history_tree();
}

void EditorApp::end_history_frame() {
    if (huge_document_history_mode()) {
        document_.clear_recent_commit_names();
        history_pending_ = false;
        history_suppress_frame_ = false;
        history_playback_frame_change_ = false;
        history_before_model_ = model_;
        return;
    }
    if (history_suppress_frame_) {
        history_pending_ = false;
        history_before_document_ = document_;
        history_before_model_ = model_;
        history_suppress_frame_ = false;
        history_playback_frame_change_ = false;
        return;
    }

    const bool document_commit_changed = document_.has_recent_commit_names();
    const bool visual_document_changed = texture_dirty_ && !history_playback_frame_change_;
    const bool document_metadata_changed = !document_metadata_equal(history_before_document_, document_);
    const bool model_changed = !model_state_equal(history_before_model_, model_);
    if (!history_pending_ &&
        !document_commit_changed &&
        !visual_document_changed &&
        !document_metadata_changed &&
        !model_changed) {
        history_playback_frame_change_ = false;
        return;
    }

    if (history_interaction_in_progress()) {
        history_pending_ = true;
        history_playback_frame_change_ = false;
        return;
    }

    if (!editor_state_changed_from_history_baseline()) {
        history_pending_ = false;
        document_.clear_recent_commit_names();
        history_playback_frame_change_ = false;
        return;
    }
    std::vector<std::string> document_labels = document_.consume_recent_commit_names();
    const std::string label = history_label_from_changes(history_before_document_,
                                                         history_before_model_,
                                                         document_,
                                                         model_,
                                                         document_labels);
    push_history_entry(label, history_before_document_, history_before_model_, document_, model_);
    history_pending_ = false;
    history_before_document_ = document_;
    history_before_model_ = model_;
    history_playback_frame_change_ = false;
}

bool EditorApp::undo_editor() {
    if (huge_document_history_mode()) {
        const bool changed = document_.undo();
        if (changed) {
            sync_model_texture_metadata();
            texture_dirty_ = true;
            history_pending_ = false;
            history_suppress_frame_ = true;
            set_status("Undo");
        }
        return changed;
    }
    const EditorHistoryNode* current = history_node_by_id(history_current_node_);
    if (current != nullptr && current->parent >= 0) {
        restore_history_node(current->parent);
        set_status("Undo: " + current->name);
        return true;
    }
    const bool changed = document_.undo();
    if (changed) {
        sync_model_texture_metadata();
        texture_dirty_ = true;
        history_pending_ = false;
        history_suppress_frame_ = true;
        set_status("Undo");
    }
    return changed;
}

bool EditorApp::redo_editor() {
    if (huge_document_history_mode()) {
        const bool changed = document_.redo();
        if (changed) {
            sync_model_texture_metadata();
            texture_dirty_ = true;
            history_pending_ = false;
            history_suppress_frame_ = true;
            set_status("Redo");
        }
        return changed;
    }
    const EditorHistoryNode* current = history_node_by_id(history_current_node_);
    if (current != nullptr && !current->children.empty()) {
        int next_id = current->preferred_child;
        if (history_node_by_id(next_id) == nullptr) {
            next_id = current->children.back();
        }
        const EditorHistoryNode* next = history_node_by_id(next_id);
        restore_history_node(next_id);
        set_status(next != nullptr ? "Redo: " + next->name : "Redo");
        return true;
    }
    const bool changed = document_.redo();
    if (changed) {
        sync_model_texture_metadata();
        texture_dirty_ = true;
        history_pending_ = false;
        history_suppress_frame_ = true;
        set_status("Redo");
    }
    return changed;
}

DialogResult EditorApp::open_file_dialog(FileFilterList filters, const char* remembered_path) {
    if (dialogs_ == nullptr) {
        return {false, {}, "Native file dialogs unavailable"};
    }
    return dialogs_->open_file(filters, default_folder_for(remembered_path));
}

DialogResult EditorApp::save_file_dialog(FileFilterList filters, const char* remembered_path) {
    if (dialogs_ == nullptr) {
        return {false, {}, "Native file dialogs unavailable"};
    }
    return dialogs_->save_file(filters, default_folder_for(remembered_path), default_name_for(remembered_path));
}

bool EditorApp::accept_dialog_result(const DialogResult& result, std::string& out_path) {
    if (!result.accepted) {
        if (result.error.empty()) {
            set_status("File dialog canceled");
        } else {
            report_error("File dialog", result.error);
        }
        return false;
    }
    if (result.path.empty()) {
        report_error("File dialog", "No file selected");
        return false;
    }
    out_path = result.path;
    return true;
}

void EditorApp::clear_selection(const char* undo_name) {
    if (!document_.selection.active) {
        return;
    }
    const SelectionMask before = document_.selection;
    document_.selection.clear();
    document_.commit_selection_edit(undo_name, before);
}

void EditorApp::nudge_canvas_selection(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    if (tool_.tool == ToolType::MovePixels && document_.selection.active && !document_.floating_selection.active) {
        std::vector<Pixel> before = document_.snapshot_active_cel();
        if (document_.begin_floating_selection()) {
            document_.move_floating_selection(dx, dy);
            document_.commit_floating_selection("Nudge Selected Pixels", std::move(before));
            texture_dirty_ = true;
        }
        return;
    }
    if (document_.selection.active && !document_.floating_selection.active) {
        const SelectionMask before = document_.selection;
        document_.selection.translate(dx, dy);
        document_.commit_selection_edit("Nudge Selection", before);
    }
}

void EditorApp::draw_dockspace() {
    ImGuiID dockspace_id = ImGui::GetID("PixelArt98Dockspace");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
}

void EditorApp::draw_main_menu() {
    MemoryTraceScope trace("draw_main_menu");
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Document...")) {
            const int current_width = document_size_value_to_pixels(new_document_width_,
                                                                    new_document_size_unit_,
                                                                    new_document_resolution_,
                                                                    new_document_resolution_unit_);
            const int current_height = document_size_value_to_pixels(new_document_height_,
                                                                     new_document_size_unit_,
                                                                     new_document_resolution_,
                                                                     new_document_resolution_unit_);
            new_document_size_preset_ = 0;
            new_document_size_unit_ = 0;
            new_document_width_ = static_cast<float>(current_width);
            new_document_height_ = static_cast<float>(current_height);
            new_document_popup_requested_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Project...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kProjectFilters), project_path_), path)) {
                path = with_default_extension(path, ".pixart");
                copy_path(project_path_, sizeof(project_path_), path);
                std::string error;
                if (save_project(path, document_, model_, &error)) set_status(std::string("Saved ") + path);
                else report_error("Save project: " + path, error);
            }
        }
        if (ImGui::MenuItem("Load Project...")) {
            std::string path;
            if (accept_dialog_result(open_file_dialog(filter_list(kProjectFilters), project_path_), path)) {
                copy_path(project_path_, sizeof(project_path_), path);
                ProjectBundle bundle;
                std::string error;
                if (load_project(path, bundle, &error)) {
                    document_ = std::move(bundle.document);
                    model_ = std::move(bundle.model);
                    reset_history_tree();
                    texture_dirty_ = true;
                    set_status(std::string("Loaded ") + path);
                } else {
                    report_error("Load project: " + path, error);
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Image as Document...")) {
            std::string path;
            if (accept_dialog_result(open_file_dialog(filter_list(kImageFilters), image_path_), path)) {
                copy_path(image_path_, sizeof(image_path_), path);
                start_image_document_import(path);
            }
        }
        if (ImGui::MenuItem("Import Image as Layer...")) {
            std::string path;
            if (accept_dialog_result(open_file_dialog(filter_list(kImageFilters), image_path_), path)) {
                copy_path(image_path_, sizeof(image_path_), path);
                std::string error;
                if (import_image_as_layer(path, document_, "", &error)) {
                    texture_dirty_ = true;
                    set_status(std::string("Imported image layer ") + path);
                } else {
                    report_error("Import image as layer: " + path, error);
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Export Current PNG...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kPngFilters), png_path_), path)) {
                path = with_default_extension(path, ".png");
                copy_path(png_path_, sizeof(png_path_), path);
                std::string error;
                if (export_png(path, document_, document_.active_frame, &error)) set_status(std::string("Exported ") + path);
                else report_error("Export current PNG: " + path, error);
            }
        }
        if (ImGui::MenuItem("Export Spritesheet...")) {
            std::string png_path;
            if (accept_dialog_result(save_file_dialog(filter_list(kPngFilters), spritesheet_path_), png_path)) {
                png_path = with_default_extension(png_path, ".png");
                std::string json_path = replace_extension(png_path, ".json");
                copy_path(spritesheet_path_, sizeof(spritesheet_path_), png_path);
                copy_path(spritesheet_json_path_, sizeof(spritesheet_json_path_), json_path);
                std::string error;
                if (export_spritesheet(png_path, json_path, document_, &error)) set_status("Exported spritesheet");
                else report_error("Export spritesheet: " + png_path, error);
            }
        }
        if (ImGui::MenuItem("Export GIF...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kGifFilters), gif_path_), path)) {
                path = with_default_extension(path, ".gif");
                copy_path(gif_path_, sizeof(gif_path_), path);
                std::string error;
                if (export_gif(path, document_, &error)) set_status(std::string("Exported ") + path);
                else report_error("Export GIF: " + path, error);
            }
        }
        if (ImGui::MenuItem("Export APNG...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kPngFilters), apng_path_), path)) {
                path = with_default_extension(path, ".png");
                copy_path(apng_path_, sizeof(apng_path_), path);
                std::string error;
                if (export_apng(path, document_, &error)) set_status(std::string("Exported ") + path);
                else report_error("Export APNG: " + path, error);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Aseprite...")) {
            std::string path;
            if (accept_dialog_result(open_file_dialog(filter_list(kAsepriteFilters), aseprite_path_), path)) {
                copy_path(aseprite_path_, sizeof(aseprite_path_), path);
                std::string error;
                Document imported;
                if (import_aseprite(path, imported, &error)) {
                    document_ = std::move(imported);
                    sync_model_texture_metadata();
                    reset_history_tree();
                    texture_dirty_ = true;
                    set_status(std::string("Imported ") + path);
                } else {
                    report_error("Import Aseprite: " + path, error);
                }
            }
        }
        if (ImGui::MenuItem("Export Aseprite...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kAsepriteFilters), aseprite_path_), path)) {
                path = with_default_extension(path, ".aseprite");
                copy_path(aseprite_path_, sizeof(aseprite_path_), path);
                std::string error;
                if (export_aseprite(path, document_, &error)) set_status(std::string("Exported ") + path);
                else report_error("Export Aseprite: " + path, error);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Import Model JSON...")) {
            std::string path;
            if (accept_dialog_result(open_file_dialog(filter_list(kJsonFilters), model_path_), path)) {
                copy_path(model_path_, sizeof(model_path_), path);
                std::string error;
                if (import_model_json(path, model_, &error)) set_status(std::string("Imported ") + path);
                else report_error("Import model JSON: " + path, error);
            }
        }
        if (ImGui::MenuItem("Export Model JSON...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kJsonFilters), model_path_), path)) {
                path = with_default_extension(path, ".json");
                copy_path(model_path_, sizeof(model_path_), path);
                std::string error;
                if (export_model_json(path, model_, &error)) set_status(std::string("Exported ") + path);
                else report_error("Export model JSON: " + path, error);
            }
        }
        if (ImGui::MenuItem("Export glTF Model...")) {
            std::string model_path;
            if (accept_dialog_result(save_file_dialog(filter_list(kGltfFilters), gltf_model_path_), model_path)) {
                model_path = with_default_extension(model_path, ".gltf");
                std::string texture_path = gltf_texture_path_for(model_path);
                std::string texture_reference = filename_for_reference(texture_path);
                copy_path(gltf_model_path_, sizeof(gltf_model_path_), model_path);
                copy_path(gltf_texture_path_, sizeof(gltf_texture_path_), texture_path);
                std::string error;
                bool ok = export_gltf_model(model_path, model_, texture_reference, &error);
                if (ok) {
                    ok = export_png(texture_path, document_, document_.active_frame, &error);
                }
                if (ok) set_status("Exported glTF model and texture");
                else report_error("Export glTF model: " + model_path, error);
            }
        }
        if (ImGui::MenuItem("Export ThreeJSPack...")) {
            std::string path;
            if (accept_dialog_result(save_file_dialog(filter_list(kThreeJSPackFilters), threejs_pack_path_), path)) {
                path = with_default_extension(path, ".threejspack");
                copy_path(threejs_pack_path_, sizeof(threejs_pack_path_), path);
                std::string error;
                if (export_threejs_pack(path, document_, model_, &error)) set_status(std::string("Exported ") + path);
                else report_error("Export ThreeJSPack: " + path, error);
            }
        }
        if (ImGui::MenuItem("Import Minecraft Model...")) {
            std::string path;
            if (accept_dialog_result(open_file_dialog(filter_list(kJsonFilters), minecraft_model_path_), path)) {
                copy_path(minecraft_model_path_, sizeof(minecraft_model_path_), path);
                std::string error;
                if (import_minecraft_model(path, model_, &error)) set_status(std::string("Imported ") + path);
                else report_error("Import Minecraft model: " + path, error);
            }
        }
        if (ImGui::MenuItem("Export Minecraft Model...")) {
            std::string model_path;
            if (accept_dialog_result(save_file_dialog(filter_list(kJsonFilters), minecraft_model_path_), model_path)) {
                model_path = with_default_extension(model_path, ".json");
                std::string texture_path = minecraft_texture_path_for(model_path);
                std::string texture_reference = filename_for_reference(texture_path);
                copy_path(minecraft_model_path_, sizeof(minecraft_model_path_), model_path);
                copy_path(minecraft_texture_path_, sizeof(minecraft_texture_path_), texture_path);
                std::string error;
                bool ok = export_minecraft_model(model_path, model_, texture_reference, &error);
                if (ok) {
                    ok = export_png(texture_path, document_, document_.active_frame, &error);
                }
                if (ok) set_status("Exported Minecraft model and texture");
                else report_error("Export Minecraft model: " + model_path, error);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            wants_quit_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z")) texture_dirty_ = undo_editor() || texture_dirty_;
        if (ImGui::MenuItem("Redo", "Ctrl+Y")) texture_dirty_ = redo_editor() || texture_dirty_;
        if (ImGui::MenuItem("Select All", "Ctrl+A")) {
            auto before = document_.selection;
            document_.selection.select_all();
            document_.commit_selection_edit("Select All", before);
        }
        if (ImGui::MenuItem("Clear Selection", "Esc")) {
            auto before = document_.selection;
            document_.selection.clear();
            document_.commit_selection_edit("Clear Selection", before);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Image")) {
        const bool can_edit_pixels = document_.has_active_cel();
        if (ImGui::MenuItem("Flip Horizontal", nullptr, false, can_edit_pixels)) {
            apply_flip_horizontal(document_);
            texture_dirty_ = true;
            set_status("Applied Flip Horizontal");
        }
        if (ImGui::MenuItem("Flip Vertical", nullptr, false, can_edit_pixels)) {
            apply_flip_vertical(document_);
            texture_dirty_ = true;
            set_status("Applied Flip Vertical");
        }
        if (ImGui::MenuItem("Rotate 90 Clockwise", nullptr, false, can_edit_pixels)) {
            apply_rotate_90_clockwise(document_);
            texture_dirty_ = true;
            set_status("Applied Rotate 90 Clockwise");
        }
        if (ImGui::MenuItem("Rotate 90 Counter-Clockwise", nullptr, false, can_edit_pixels)) {
            apply_rotate_90_counter_clockwise(document_);
            texture_dirty_ = true;
            set_status("Applied Rotate 90 Counter-Clockwise");
        }
        if (ImGui::MenuItem("Rotate 180", nullptr, false, can_edit_pixels)) {
            apply_rotate_180(document_);
            texture_dirty_ = true;
            set_status("Applied Rotate 180");
        }
        if (ImGui::MenuItem("Straighten", nullptr, false, can_edit_pixels)) {
            start_straighten_tool();
        }
        if (ImGui::MenuItem("Rotate / Zoom", nullptr, false, can_edit_pixels)) {
            rotate_zoom_angle_ = static_cast<float>(effect_angle_);
            rotate_zoom_zoom_ = effect_zoom_;
            rotate_zoom_pan_x_ = 0;
            rotate_zoom_pan_y_ = 0;
            rotate_zoom_preview_dirty_ = true;
            rotate_zoom_popup_requested_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Effects")) {
        auto effect_item = [&](const char* label, EffectPreviewKind kind) {
            if (ImGui::MenuItem(label)) {
                start_effect_preview(kind);
            }
        };
        if (ImGui::BeginMenu("Artistic")) {
            effect_item("Ink Sketch", EffectPreviewKind::InkSketch);
            effect_item("Oil Painting", EffectPreviewKind::OilPainting);
            effect_item("Pencil Sketch", EffectPreviewKind::PencilSketch);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Blurs")) {
            effect_item("Depth of Field", EffectPreviewKind::DepthOfField);
            effect_item("Gaussian Blur", EffectPreviewKind::GaussianBlur);
            effect_item("Motion Blur", EffectPreviewKind::MotionBlur);
            effect_item("Radial Blur", EffectPreviewKind::RadialBlur);
            effect_item("Zoom Blur", EffectPreviewKind::ZoomBlur);
            effect_item("Median Blur", EffectPreviewKind::MedianBlur);
            effect_item("Surface Blur", EffectPreviewKind::SurfaceBlur);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Color")) {
            effect_item("Auto-Level", EffectPreviewKind::AutoLevel);
            effect_item("Black and White", EffectPreviewKind::Grayscale);
            effect_item("Sepia", EffectPreviewKind::Sepia);
            effect_item("Invert Colors", EffectPreviewKind::InvertColors);
            effect_item("Invert Alpha", EffectPreviewKind::InvertAlpha);
            effect_item("Posterize", EffectPreviewKind::Posterize);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Distort")) {
            effect_item("Bulge", EffectPreviewKind::Bulge);
            effect_item("Crystalize", EffectPreviewKind::Crystalize);
            effect_item("Dents", EffectPreviewKind::Dents);
            effect_item("Frosted Glass", EffectPreviewKind::FrostedGlass);
            effect_item("Pixelate", EffectPreviewKind::Pixelate);
            effect_item("Polar Inversion", EffectPreviewKind::PolarInversion);
            effect_item("Tile Reflection", EffectPreviewKind::TileReflection);
            effect_item("Twist", EffectPreviewKind::Twist);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Noise")) {
            effect_item("Add Noise", EffectPreviewKind::AddNoise);
            effect_item("Median", EffectPreviewKind::MedianBlur);
            effect_item("Reduce Noise", EffectPreviewKind::ReduceNoise);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Object")) {
            effect_item("Feather", EffectPreviewKind::Feather);
            effect_item("Outline", EffectPreviewKind::Outline);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Photo")) {
            effect_item("Glow", EffectPreviewKind::Glow);
            effect_item("Red Eye Removal", EffectPreviewKind::RedEyeRemoval);
            effect_item("Sharpen", EffectPreviewKind::Sharpen);
            effect_item("Soften Portrait", EffectPreviewKind::SoftenPortrait);
            effect_item("Vignette", EffectPreviewKind::Vignette);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Render")) {
            effect_item("Clouds", EffectPreviewKind::Clouds);
            effect_item("Julia Fractal", EffectPreviewKind::JuliaFractal);
            effect_item("Mandelbrot Fractal", EffectPreviewKind::MandelbrotFractal);
            effect_item("Turbulence", EffectPreviewKind::Turbulence);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Stylize")) {
            effect_item("Edge Detect", EffectPreviewKind::EdgeDetect);
            effect_item("Emboss", EffectPreviewKind::Emboss);
            effect_item("Outline", EffectPreviewKind::Outline);
            effect_item("Relief", EffectPreviewKind::Relief);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Grid", nullptr, &show_grid_);
        ImGui::MenuItem("Checkerboard", nullptr, &show_checker_);
        if (ImGui::BeginMenu("3D Preview Skybox")) {
            for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kSkyboxes)); ++i) {
                if (ImGui::MenuItem(kSkyboxes[i].name, nullptr, skybox_index_ == i)) {
                    skybox_index_ = i;
                    set_status(std::string("3D skybox: ") + kSkyboxes[i].name);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Zoom In", "Ctrl++")) {
            zoom_ *= kCanvasZoomStep;
        }
        if (ImGui::MenuItem("Zoom Out", "Ctrl+-")) {
            zoom_ /= kCanvasZoomStep;
        }
        if (ImGui::MenuItem("Actual Size", "Ctrl+1")) {
            zoom_ = 1.0f;
        }
        if (ImGui::MenuItem("Fit to Canvas", "Ctrl+0")) {
            canvas_fit_requested_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Tools", nullptr, &settings_.show_tools_panel);
        ImGui::MenuItem("Colors", nullptr, &settings_.show_colors_panel);
        ImGui::MenuItem("Layers", nullptr, &settings_.show_layers_panel);
        ImGui::MenuItem("Adjustments", nullptr, &settings_.show_adjustments_panel);
        ImGui::MenuItem("Animation", nullptr, &settings_.show_animation_panel);
        ImGui::MenuItem("History", nullptr, &settings_.show_history_panel);
        ImGui::MenuItem("Tile Preview", nullptr, &show_tile_preview_);
        ImGui::MenuItem("Error Console", nullptr, &error_console_open_);
        ImGui::SeparatorText("Modeling");
        if (ImGui::MenuItem("Cuboid / UV Editor", nullptr, &settings_.show_model_uv_panel)) {
            save_settings();
        }
        if (ImGui::MenuItem("Canvas Cuboid UV Overlay", nullptr, &settings_.show_canvas_cuboid_uv_overlay)) {
            save_settings();
        }
        if (ImGui::MenuItem("3D Preview", nullptr, &settings_.show_3d_preview)) {
            save_settings();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Options")) {
        if (ImGui::MenuItem("Show Splash Screen", nullptr, &settings_.show_splash_screen)) {
            save_settings();
        }
        if (ImGui::MenuItem("Auto-open Error Console", nullptr, &settings_.auto_open_error_console)) {
            save_settings();
        }
        ImGui::SeparatorText("Performance");
        if (ImGui::MenuItem("Heavy GPU Optimization", nullptr, &settings_.heavy_gpu_optimization)) {
            if (!settings_.heavy_gpu_optimization) {
                settings_.mps_backend = false;
            }
            save_settings();
            texture_dirty_ = true;
        }
#if defined(__APPLE__)
        if (!settings_.heavy_gpu_optimization) {
            ImGui::BeginDisabled();
        }
        if (ImGui::MenuItem("MPS Backend", nullptr, &settings_.mps_backend)) {
            if (!settings_.heavy_gpu_optimization) {
                settings_.mps_backend = false;
            }
            save_settings();
            texture_dirty_ = true;
        }
        if (!settings_.heavy_gpu_optimization) {
            ImGui::EndDisabled();
            settings_.mps_backend = false;
        }
#endif
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("PixelArt98");
    ImGui::EndMainMenuBar();
}

void EditorApp::draw_new_document_dialog() {
    if (new_document_popup_requested_) {
        ImGui::OpenPopup("New Document");
        new_document_popup_requested_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal("New Document", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const char* aspect_presets[] = {
        "Custom", "1:1 Square", "4:3 Standard", "3:2 Photo", "16:9 Widescreen", "9:16 Portrait", "Current Canvas"
    };
    const char* size_presets[] = {
        "Custom",
        "Current Canvas",
        "A2 Portrait (420 x 594 mm)",
        "A2 Landscape (594 x 420 mm)",
        "A3 Portrait (297 x 420 mm)",
        "A3 Landscape (420 x 297 mm)",
        "A4 Portrait (210 x 297 mm)",
        "A4 Landscape (297 x 210 mm)",
        "A5 Portrait (148 x 210 mm)",
        "A5 Landscape (210 x 148 mm)",
        "Postcard (100 x 148 mm)",
        "Postcard (4 x 6 in)",
        "US Letter (8.5 x 11 in)",
        "US Legal (8.5 x 14 in)",
        "Tabloid (11 x 17 in)"
    };
    const char* size_units[] = {"Pixels", "Inches", "Centimeters"};
    const char* resolution_units[] = {"px/inch", "px/cm"};

    auto final_width = [&]() {
        return document_size_value_to_pixels(new_document_width_,
                                             new_document_size_unit_,
                                             new_document_resolution_,
                                             new_document_resolution_unit_);
    };
    auto final_height = [&]() {
        return document_size_value_to_pixels(new_document_height_,
                                             new_document_size_unit_,
                                             new_document_resolution_,
                                             new_document_resolution_unit_);
    };
    auto set_size_unit_from_pixels = [&](int width, int height, int size_unit) {
        new_document_size_unit_ = size_unit;
        new_document_width_ = document_pixels_to_size_value(width,
                                                            new_document_size_unit_,
                                                            new_document_resolution_,
                                                            new_document_resolution_unit_);
        new_document_height_ = document_pixels_to_size_value(height,
                                                             new_document_size_unit_,
                                                             new_document_resolution_,
                                                             new_document_resolution_unit_);
    };
    auto clamp_size_values = [&]() {
        if (new_document_size_unit_ == 0) {
            new_document_width_ = static_cast<float>(clamped_document_size(static_cast<int>(std::round(new_document_width_))));
            new_document_height_ = static_cast<float>(clamped_document_size(static_cast<int>(std::round(new_document_height_))));
            return;
        }
        const float max_value = document_pixels_to_size_value(kMaxDocumentSize,
                                                              new_document_size_unit_,
                                                              new_document_resolution_,
                                                              new_document_resolution_unit_);
        new_document_width_ = std::clamp(new_document_width_, 0.01f, max_value);
        new_document_height_ = std::clamp(new_document_height_, 0.01f, max_value);
    };
    auto apply_aspect_from_width = [&]() {
        const float ratio_width = static_cast<float>(std::max(1, new_document_aspect_width_));
        const float ratio_height = static_cast<float>(std::max(1, new_document_aspect_height_));
        new_document_height_ = new_document_width_ * ratio_height / ratio_width;
        clamp_size_values();
    };
    auto apply_aspect_from_height = [&]() {
        const float ratio_width = static_cast<float>(std::max(1, new_document_aspect_width_));
        const float ratio_height = static_cast<float>(std::max(1, new_document_aspect_height_));
        new_document_width_ = new_document_height_ * ratio_width / ratio_height;
        clamp_size_values();
    };
    auto set_aspect = [&](int width, int height) {
        new_document_aspect_width_ = std::max(1, width);
        new_document_aspect_height_ = std::max(1, height);
        if (new_document_lock_aspect_) {
            new_document_size_preset_ = 0;
            apply_aspect_from_width();
        }
    };
    auto set_size_from_value = [&](float width, float height, int size_unit) {
        new_document_size_unit_ = size_unit;
        new_document_width_ = width;
        new_document_height_ = height;
        clamp_size_values();
        new_document_aspect_preset_ = 0;
        new_document_aspect_width_ = final_width();
        new_document_aspect_height_ = final_height();
    };

    if (!open) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::TextUnformatted("Canvas");
    ImGui::Separator();

    int size_preset = new_document_size_preset_;
    if (ImGui::Combo("Preset", &size_preset, size_presets, IM_ARRAYSIZE(size_presets))) {
        new_document_size_preset_ = size_preset;
        switch (new_document_size_preset_) {
        case 1: set_size_from_value(static_cast<float>(document_.width), static_cast<float>(document_.height), 0); break;
        case 2: set_size_from_value(42.0f, 59.4f, 2); break;
        case 3: set_size_from_value(59.4f, 42.0f, 2); break;
        case 4: set_size_from_value(29.7f, 42.0f, 2); break;
        case 5: set_size_from_value(42.0f, 29.7f, 2); break;
        case 6: set_size_from_value(21.0f, 29.7f, 2); break;
        case 7: set_size_from_value(29.7f, 21.0f, 2); break;
        case 8: set_size_from_value(14.8f, 21.0f, 2); break;
        case 9: set_size_from_value(21.0f, 14.8f, 2); break;
        case 10: set_size_from_value(10.0f, 14.8f, 2); break;
        case 11: set_size_from_value(4.0f, 6.0f, 1); break;
        case 12: set_size_from_value(8.5f, 11.0f, 1); break;
        case 13: set_size_from_value(8.5f, 14.0f, 1); break;
        case 14: set_size_from_value(11.0f, 17.0f, 1); break;
        default: break;
        }
    }

    bool lock_aspect = new_document_lock_aspect_;
    if (ImGui::Checkbox("Lock aspect ratio", &lock_aspect)) {
        new_document_lock_aspect_ = lock_aspect;
        if (new_document_lock_aspect_) {
            new_document_aspect_preset_ = 0;
            new_document_aspect_width_ = final_width();
            new_document_aspect_height_ = final_height();
        }
    }

    int aspect_preset = new_document_aspect_preset_;
    if (ImGui::Combo("Aspect", &aspect_preset, aspect_presets, IM_ARRAYSIZE(aspect_presets))) {
        new_document_aspect_preset_ = aspect_preset;
        switch (new_document_aspect_preset_) {
        case 1: set_aspect(1, 1); break;
        case 2: set_aspect(4, 3); break;
        case 3: set_aspect(3, 2); break;
        case 4: set_aspect(16, 9); break;
        case 5: set_aspect(9, 16); break;
        case 6: set_aspect(document_.width, document_.height); break;
        default: break;
        }
    }

    if (new_document_aspect_preset_ == 0) {
        int aspect_width = new_document_aspect_width_;
        int aspect_height = new_document_aspect_height_;
        if (ImGui::InputInt("Aspect W", &aspect_width)) {
            new_document_aspect_width_ = std::clamp(aspect_width, 1, kMaxDocumentSize);
            if (new_document_lock_aspect_) {
                new_document_size_preset_ = 0;
                apply_aspect_from_width();
            }
        }
        if (ImGui::InputInt("Aspect H", &aspect_height)) {
            new_document_aspect_height_ = std::clamp(aspect_height, 1, kMaxDocumentSize);
            if (new_document_lock_aspect_) {
                new_document_size_preset_ = 0;
                apply_aspect_from_width();
            }
        }
    } else {
        ImGui::Text("Aspect ratio: %d:%d", new_document_aspect_width_, new_document_aspect_height_);
    }

    ImGui::Spacing();

    int size_unit = new_document_size_unit_;
    if (ImGui::Combo("Size units", &size_unit, size_units, IM_ARRAYSIZE(size_units))) {
        set_size_unit_from_pixels(final_width(), final_height(), size_unit);
    }

    bool width_changed = false;
    bool height_changed = false;
    if (new_document_size_unit_ == 0) {
        int width = clamped_document_size(static_cast<int>(std::round(new_document_width_)));
        int height = clamped_document_size(static_cast<int>(std::round(new_document_height_)));
        width_changed = ImGui::InputInt("Width", &width);
        height_changed = ImGui::InputInt("Height", &height);
        if (width_changed) {
            new_document_size_preset_ = 0;
            new_document_width_ = static_cast<float>(clamped_document_size(width));
        }
        if (height_changed) {
            new_document_size_preset_ = 0;
            new_document_height_ = static_cast<float>(clamped_document_size(height));
        }
    } else {
        width_changed = ImGui::InputFloat("Width", &new_document_width_, 0.1f, 1.0f, "%.3f");
        height_changed = ImGui::InputFloat("Height", &new_document_height_, 0.1f, 1.0f, "%.3f");
        if (width_changed || height_changed) {
            new_document_size_preset_ = 0;
        }
        clamp_size_values();
    }

    if (new_document_lock_aspect_) {
        if (width_changed) {
            apply_aspect_from_width();
        } else if (height_changed) {
            apply_aspect_from_height();
        }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Resolution");
    ImGui::Separator();

    if (ImGui::InputFloat("Resolution", &new_document_resolution_, 1.0f, 10.0f, "%.2f")) {
        new_document_resolution_ = std::clamp(new_document_resolution_, 0.01f, 10000.0f);
        clamp_size_values();
    }

    int resolution_unit = new_document_resolution_unit_;
    if (ImGui::Combo("Resolution units", &resolution_unit, resolution_units, IM_ARRAYSIZE(resolution_units)) &&
        resolution_unit != new_document_resolution_unit_) {
        if (new_document_resolution_unit_ == 0 && resolution_unit == 1) {
            new_document_resolution_ /= kCentimetersPerInch;
        } else if (new_document_resolution_unit_ == 1 && resolution_unit == 0) {
            new_document_resolution_ *= kCentimetersPerInch;
        }
        new_document_resolution_unit_ = resolution_unit;
        new_document_resolution_ = std::clamp(new_document_resolution_, 0.01f, 10000.0f);
        clamp_size_values();
    }

    const int width = final_width();
    const int height = final_height();
    const float print_width_inches = static_cast<float>(width) / resolution_pixels_per_inch(new_document_resolution_,
                                                                                            new_document_resolution_unit_);
    const float print_height_inches = static_cast<float>(height) / resolution_pixels_per_inch(new_document_resolution_,
                                                                                              new_document_resolution_unit_);
    ImGui::Spacing();
    ImGui::Text("Pixel size: %d x %d px", width, height);
    ImGui::Text("Print size: %.2f x %.2f in / %.2f x %.2f cm",
                static_cast<double>(print_width_inches),
                static_cast<double>(print_height_inches),
                static_cast<double>(print_width_inches * kCentimetersPerInch),
                static_cast<double>(print_height_inches * kCentimetersPerInch));

    ImGui::Spacing();
    if (ImGui::Button("Create", ImVec2(96.0f, 0.0f))) {
        document_ = Document::create(width, height);
        model_ = ModelDocument::create_default();
        sync_model_texture_metadata();
        reset_history_tree();
        canvas_pan_ = ImVec2(0.0f, 0.0f);
        canvas_fit_requested_ = true;
        texture_dirty_ = true;
        history_pending_ = false;
        history_suppress_frame_ = true;
        set_status("New document created: " + std::to_string(width) + " x " + std::to_string(height) + " px");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void EditorApp::draw_toolbar() {
    bool open = settings_.show_tools_panel;
    if (!ImGui::Begin("Tools", &open)) {
        settings_.show_tools_panel = open;
        ImGui::End();
        return;
    }
    settings_.show_tools_panel = open;

    auto tool_button = [&](ToolType tool, const char* hint) {
        const bool selected = tool_.tool == tool;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(tool_name(tool), ImVec2(106.0f, 0.0f))) {
            tool_.tool = tool;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", hint);
        }
    };
    auto tool_pair = [&](ToolType first, const char* first_hint, ToolType second, const char* second_hint) {
        tool_button(first, first_hint);
        ImGui::SameLine();
        tool_button(second, second_hint);
    };

    ImGui::SeparatorText("Select");
    tool_pair(ToolType::RectSelect, "Select a rectangular area", ToolType::LassoSelect, "Draw a freeform selection");
    tool_pair(ToolType::MagicWand, "Select similar pixels", ToolType::MovePixels, "Move selected pixels");

    ImGui::SeparatorText("Retouch");
    tool_pair(ToolType::Eyedropper, "Pick a color from the image", ToolType::CloneStamp, "Paint from a sampled source");
    tool_pair(ToolType::Bucket, "Fill matching pixels", ToolType::Gradient, "Blend between the two active colors");

    ImGui::SeparatorText("Paint");
    tool_pair(ToolType::Pencil, "Draw hard pixels", ToolType::Brush, "Draw with a larger stroke");
    tool_button(ToolType::Eraser, "Erase pixels or mask values");

    ImGui::SeparatorText("Shapes");
    tool_pair(ToolType::Line, "Draw straight lines", ToolType::Rectangle, "Draw rectangles");
    tool_pair(ToolType::Ellipse, "Draw ellipses", ToolType::Text, "Place text on the canvas");
    ImGui::End();
}

void EditorApp::draw_tool_options_bar() {
    ImGui::SeparatorText("Tool Options");
    ImGui::TextUnformatted(tool_name(tool_.tool));
    ImGui::SameLine();
    if (tool_.tool == ToolType::Pencil || tool_.tool == ToolType::Brush || tool_.tool == ToolType::Eraser ||
        tool_.tool == ToolType::CloneStamp || tool_.tool == ToolType::Line ||
        tool_.tool == ToolType::Rectangle || tool_.tool == ToolType::Ellipse) {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderInt("Stroke", &tool_.brush_size, 1, 32);
        ImGui::SameLine();
    }
    if (tool_.tool == ToolType::Bucket || tool_.tool == ToolType::MagicWand) {
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderInt("Tolerance", &tool_.tolerance, 0, 442);
        ImGui::SameLine();
        ImGui::Checkbox("Contiguous", &tool_.contiguous);
        ImGui::SameLine();
    }
    if (tool_.tool == ToolType::Text) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("Text", text_buffer_, sizeof(text_buffer_));
        ImGui::SameLine();
    }
    if (tool_.tool == ToolType::CloneStamp) {
        ImGui::Text("Source %d, %d", tool_.clone_source_x, tool_.clone_source_y);
        ImGui::SameLine();
    }
    if (text_box_.active) {
        if (ImGui::Button("Apply Text")) commit_text_box();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) cancel_text_box();
        ImGui::SameLine();
    }
    ImGui::Checkbox("Edit Mask", &edit_layer_mask_);
    ImGui::SameLine();
    ImGui::Checkbox("Mask Overlay", &show_mask_overlay_);
    if (edit_layer_mask_ && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Brushes edit grayscale mask values.");
    }
}

void EditorApp::draw_canvas() {
    MemoryTraceScope trace("draw_canvas");
    ImGui::Begin("Canvas");
    bool zoom_out_requested = false;
    bool zoom_in_requested = false;
    bool actual_size_requested = false;
    bool fit_requested = canvas_fit_requested_;
    canvas_fit_requested_ = false;
    if (ImGui::Button("-##CanvasZoomOut")) {
        zoom_out_requested = true;
    }
    ImGui::SameLine();
    ImGui::Text("Zoom %.0f%%", static_cast<double>(zoom_ * 100.0f));
    ImGui::SameLine();
    if (ImGui::Button("+##CanvasZoomIn")) {
        zoom_in_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1##CanvasActualSize")) {
        actual_size_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit##CanvasFit")) {
        fit_requested = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &show_grid_);
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &onion_skin_);
    if (straighten_active_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("Resampling##Straighten", &image_transform_resampling_, kResamplingModeNames, IM_ARRAYSIZE(kResamplingModeNames));
    }
    draw_tool_options_bar();

    ImGui::BeginChild("CanvasView",
                      ImVec2(0, 0),
                      true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool canvas_active = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
                               ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImVec2 viewport_size = ImGui::GetContentRegionAvail();
    viewport_size.x = std::max(1.0f, viewport_size.x);
    viewport_size.y = std::max(1.0f, viewport_size.y);
    ImVec2 viewport_origin = floor_screen_pos(ImGui::GetCursorScreenPos());
    const ImVec2 viewport_center(viewport_origin.x + viewport_size.x * 0.5f,
                                 viewport_origin.y + viewport_size.y * 0.5f);

    if (zoom_out_requested) {
        zoom_canvas_at(viewport_origin, viewport_size, viewport_center, zoom_ / kCanvasZoomStep);
    }
    if (zoom_in_requested) {
        zoom_canvas_at(viewport_origin, viewport_size, viewport_center, zoom_ * kCanvasZoomStep);
    }
    if (actual_size_requested) {
        zoom_canvas_at(viewport_origin, viewport_size, viewport_center, 1.0f);
    }
    if (fit_requested) {
        zoom_canvas_at(viewport_origin,
                       viewport_size,
                       viewport_center,
                       fit_canvas_zoom(document_.width, document_.height, viewport_size));
    }
    zoom_ = clamped_canvas_zoom(zoom_, document_.width, document_.height, viewport_size);
    clamp_canvas_pan(viewport_size);

    ImVec2 canvas_size(pixel_scale(document_.width, zoom_), pixel_scale(document_.height, zoom_));
    ImVec2 center_offset = canvas_center_offset(viewport_size, canvas_size);
    ImVec2 origin = floor_screen_pos(ImVec2(viewport_origin.x + center_offset.x + canvas_pan_.x,
                                            viewport_origin.y + center_offset.y + canvas_pan_.y));
    ImGui::InvisibleButton("CanvasHitTarget",
                           viewport_size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    bool viewport_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    bool viewport_active = ImGui::IsItemActive();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImDrawListFlags old_flags = draw_list->Flags;
    draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);
    ImVec2 max(origin.x + canvas_size.x, origin.y + canvas_size.y);

    const bool draw_checkerboard = show_checker_ && !is_huge_document(document_);
    if (show_checker_ && !draw_checkerboard) {
        draw_list->AddRectFilled(origin, max, IM_COL32(178, 178, 178, 255));
        memory_trace_note("draw_canvas.checkerboard_disabled", "huge_document");
    } else if (draw_checkerboard) {
        const float tile = std::max(4.0f, zoom_ * 2.0f);
        const int rows = std::max(0, static_cast<int>(std::ceil(canvas_size.y / tile)));
        const int columns = std::max(0, static_cast<int>(std::ceil(canvas_size.x / tile)));
        for (int row = 0; row < rows; ++row) {
            const float y = origin.y + static_cast<float>(row) * tile;
            for (int column = 0; column < columns; ++column) {
                const float x = origin.x + static_cast<float>(column) * tile;
                const bool dark = ((column + row) % 2) == 0;
                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + tile, max.x), std::min(y + tile, max.y)),
                                         dark ? IM_COL32(150, 150, 150, 255) : IM_COL32(205, 205, 205, 255));
            }
        }
    }

    if (onion_skin_ && document_.frames.size() > 1 && document_.active_frame > 0) {
        auto previous = document_.composite_frame(document_.active_frame - 1);
        push_nearest_sampler(draw_list);
        tiled_onion_texture_.draw_visible(draw_list,
                                          document_.width,
                                          document_.height,
                                          previous,
                                          origin,
                                          zoom_,
                                          viewport_origin,
                                          ImVec2(viewport_origin.x + viewport_size.x, viewport_origin.y + viewport_size.y),
                                          IM_COL32(255, 160, 160, 90));
    }
    push_nearest_sampler(draw_list);
    memory_trace_vector("draw_canvas.canvas_pixels_before_draw", canvas_pixels());
    tiled_canvas_texture_.draw_visible(draw_list,
                                       document_.width,
                                       document_.height,
                                       canvas_pixels(),
                                       origin,
                                       zoom_,
                                       viewport_origin,
                                       ImVec2(viewport_origin.x + viewport_size.x, viewport_origin.y + viewport_size.y));
    const GLTiledCanvasTexture::DrawStats& tiled_stats = tiled_canvas_texture_.last_draw_stats();
    memory_trace_event("draw_stats",
                       {},
                       "draw_canvas.tiled_canvas",
                       nullptr,
                       static_cast<std::size_t>(std::max(0, tiled_stats.visible_tiles)),
                       static_cast<std::size_t>(std::max(0, tiled_stats.pending_tiles)),
                       static_cast<std::size_t>(std::max(0, tiled_stats.tiles_uploaded)),
                       "level=" + std::to_string(tiled_stats.selected_level) +
                           ";drew_lod=" + (tiled_stats.drew_lod ? std::string("1") : std::string("0")) +
                           ";lod_uploaded=" + (tiled_stats.lod_uploaded ? std::string("1") : std::string("0")));
    push_linear_sampler(draw_list);
    draw_mask_overlay(draw_list, origin, canvas_size);
    draw_grid_overlay(draw_list, origin, canvas_size);
    draw_floating_selection_overlay(draw_list, origin);
    draw_text_preview_overlay(draw_list, origin);
    draw_selected_model_face_overlay(draw_list, origin);
    draw_selection_overlay(draw_list, origin);
    draw_lasso_preview(draw_list, origin);
    draw_tool_drag_preview(draw_list, origin);
    draw_straighten_overlay(draw_list, origin);
    handle_canvas_input(origin, viewport_size, viewport_hovered, viewport_active, canvas_active);
    draw_list->Flags = old_flags;
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::clamp_canvas_pan(const ImVec2& viewport_size) {
    ImVec2 image_size(pixel_scale(document_.width, zoom_), pixel_scale(document_.height, zoom_));
    const ImVec2 center_offset = canvas_center_offset(viewport_size, image_size);
    ImVec2 image_origin(center_offset.x + canvas_pan_.x, center_offset.y + canvas_pan_.y);

    auto clamp_axis = [](float origin, float image_extent, float viewport_extent) {
        if (image_extent <= 0.0f || viewport_extent <= 0.0f) {
            return origin;
        }
        const float visible_margin = std::min(std::min(96.0f, viewport_extent * 0.45f), image_extent * 0.5f);
        const float min_origin = -image_extent + visible_margin;
        const float max_origin = viewport_extent - visible_margin;
        if (min_origin > max_origin) {
            return (viewport_extent - image_extent) * 0.5f;
        }
        return std::clamp(origin, min_origin, max_origin);
    };

    image_origin.x = clamp_axis(image_origin.x, image_size.x, viewport_size.x);
    image_origin.y = clamp_axis(image_origin.y, image_size.y, viewport_size.y);
    canvas_pan_ = ImVec2(image_origin.x - center_offset.x, image_origin.y - center_offset.y);
}

void EditorApp::zoom_canvas_at(const ImVec2& viewport_origin,
                               const ImVec2& viewport_size,
                               const ImVec2& focal_point,
                               float next_zoom) {
    const float old_zoom = clamped_canvas_zoom(zoom_, document_.width, document_.height, viewport_size);
    ImVec2 old_size(pixel_scale(document_.width, old_zoom), pixel_scale(document_.height, old_zoom));
    ImVec2 old_center = canvas_center_offset(viewport_size, old_size);
    ImVec2 old_origin(viewport_origin.x + old_center.x + canvas_pan_.x,
                      viewport_origin.y + old_center.y + canvas_pan_.y);
    ImVec2 old_max(old_origin.x + old_size.x, old_origin.y + old_size.y);
    ImVec2 zoom_focus = point_in_rect(focal_point, old_origin, old_max)
                            ? focal_point
                            : ImVec2(viewport_origin.x + viewport_size.x * 0.5f,
                                     viewport_origin.y + viewport_size.y * 0.5f);
    ImVec2 focal_pixel((zoom_focus.x - old_origin.x) / old_zoom,
                       (zoom_focus.y - old_origin.y) / old_zoom);

    zoom_ = clamped_canvas_zoom(next_zoom, document_.width, document_.height, viewport_size);
    ImVec2 new_size(pixel_scale(document_.width, zoom_), pixel_scale(document_.height, zoom_));
    ImVec2 new_center = canvas_center_offset(viewport_size, new_size);
    ImVec2 new_origin(zoom_focus.x - focal_pixel.x * zoom_,
                      zoom_focus.y - focal_pixel.y * zoom_);
    canvas_pan_ = ImVec2(new_origin.x - viewport_origin.x - new_center.x,
                         new_origin.y - viewport_origin.y - new_center.y);
    clamp_canvas_pan(viewport_size);
}

void EditorApp::handle_canvas_input(const ImVec2& origin,
                                    const ImVec2& viewport_size,
                                    bool viewport_hovered,
                                    bool viewport_active,
                                    bool canvas_active) {
    ImGuiIO& io = ImGui::GetIO();
    int px_i = 0;
    int py_i = 0;
    ImVec2 image_max(origin.x + pixel_scale(document_.width, zoom_),
                     origin.y + pixel_scale(document_.height, zoom_));
    bool image_hovered = viewport_hovered && point_in_rect(io.MousePos, origin, image_max);
    bool over_pixel = image_hovered && mouse_to_pixel(origin, px_i, py_i);
    const bool can_use_canvas_shortcuts = canvas_active && !text_box_.active && !io.WantTextInput && !ImGui::IsAnyItemActive();
    const bool has_active_cel = document_.has_active_cel();
    const bool mask_editing = edit_layer_mask_ && has_active_cel;
    auto mask_value_from_color = [](Pixel pixel) {
        return static_cast<std::uint8_t>(std::clamp(pixel_luma(pixel), 0.0f, 255.0f) + 0.5f);
    };

    ImVec2 image_size(image_max.x - origin.x, image_max.y - origin.y);
    ImVec2 center_offset = canvas_center_offset(viewport_size, image_size);
    ImVec2 viewport_origin(origin.x - center_offset.x - canvas_pan_.x,
                           origin.y - center_offset.y - canvas_pan_.y);

    if (viewport_hovered && io.MouseWheel != 0.0f) {
        const float factor = std::pow(kCanvasZoomStep, io.MouseWheel);
        zoom_canvas_at(viewport_origin, viewport_size, io.MousePos, zoom_ * factor);
    }
    const bool space_down = ImGui::IsKeyDown(ImGuiKey_Space);
    if (viewport_active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        canvas_pan_.x += io.MouseDelta.x;
        canvas_pan_.y += io.MouseDelta.y;
        clamp_canvas_pan(viewport_size);
        return;
    }
    if (viewport_active && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        return;
    }
    if (canvas_active && space_down && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        canvas_pan_.x += io.MouseDelta.x;
        canvas_pan_.y += io.MouseDelta.y;
        clamp_canvas_pan(viewport_size);
        return;
    }
    if (canvas_active && space_down && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        return;
    }

    if (straighten_active_ && handle_straighten_input(origin, viewport_hovered, canvas_active)) {
        return;
    }

    if (canvas_active && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (pixel_drag_preview_active_) {
            cancel_pixel_drag_preview();
        } else if (document_.floating_selection.active) {
            document_.cancel_floating_selection();
            document_.selection = selection_before_;
            move_drag_active_ = false;
            drag_active_ = false;
            texture_dirty_ = true;
        } else if (text_box_.active) {
            cancel_text_box();
        } else if (document_.selection.active) {
            clear_selection("Clear Selection");
        }
    }
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_A)) {
        auto before = document_.selection;
        document_.selection.select_all();
        document_.commit_selection_edit("Select All", before);
    }
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_D)) {
        clear_selection("Deselect");
    }
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_I)) {
        const SelectionMask before = document_.selection;
        document_.selection.invert();
        document_.commit_selection_edit("Invert Selection", before);
    }
    if (can_use_canvas_shortcuts && (shortcut_ctrl_or_super(ImGuiKey_Equal) || shortcut_ctrl_or_super(ImGuiKey_KeypadAdd))) {
        ImVec2 viewport_center(viewport_origin.x + viewport_size.x * 0.5f,
                               viewport_origin.y + viewport_size.y * 0.5f);
        zoom_canvas_at(viewport_origin, viewport_size, viewport_center, zoom_ * kCanvasZoomStep);
    }
    if (can_use_canvas_shortcuts && (shortcut_ctrl_or_super(ImGuiKey_Minus) || shortcut_ctrl_or_super(ImGuiKey_KeypadSubtract))) {
        ImVec2 viewport_center(viewport_origin.x + viewport_size.x * 0.5f,
                               viewport_origin.y + viewport_size.y * 0.5f);
        zoom_canvas_at(viewport_origin, viewport_size, viewport_center, zoom_ / kCanvasZoomStep);
    }
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_0)) {
        ImVec2 viewport_center(viewport_origin.x + viewport_size.x * 0.5f,
                               viewport_origin.y + viewport_size.y * 0.5f);
        zoom_canvas_at(viewport_origin,
                       viewport_size,
                       viewport_center,
                       fit_canvas_zoom(document_.width, document_.height, viewport_size));
    }
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_1)) {
        ImVec2 viewport_center(viewport_origin.x + viewport_size.x * 0.5f,
                               viewport_origin.y + viewport_size.y * 0.5f);
        zoom_canvas_at(viewport_origin, viewport_size, viewport_center, 1.0f);
    }
    if (text_box_.active && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        commit_text_box();
    }
    if (!text_box_.active && can_use_canvas_shortcuts &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        clear_selection("Deselect");
    }
    if (!text_box_.active && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        delete_selection_contents();
    }
    if (can_use_canvas_shortcuts) {
        int nudge_x = 0;
        int nudge_y = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
            --nudge_x;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
            ++nudge_x;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            --nudge_y;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            ++nudge_y;
        }
        if (nudge_x != 0 || nudge_y != 0) {
            if (space_down) {
                constexpr float kKeyboardPanStep = 24.0f;
                canvas_pan_.x -= static_cast<float>(nudge_x) * kKeyboardPanStep;
                canvas_pan_.y -= static_cast<float>(nudge_y) * kKeyboardPanStep;
                clamp_canvas_pan(viewport_size);
            } else {
                const int step = action_modifier_down(io) ? 10 : 1;
                nudge_canvas_selection(nudge_x * step, nudge_y * step);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) {
            const int step = action_modifier_down(io) ? 5 : 1;
            tool_.brush_size = std::max(1, tool_.brush_size - step);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true)) {
            const int step = action_modifier_down(io) ? 5 : 1;
            tool_.brush_size = std::min(128, tool_.brush_size + step);
        }
    }

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && tool_.tool == ToolType::CloneStamp) {
        if (!has_active_cel) {
            set_status("Create a layer to edit pixels");
            return;
        }
        tool_.clone_source_x = px_i;
        tool_.clone_source_y = py_i;
        set_status("Clone source set");
    }

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && tool_.tool == ToolType::Bucket) {
        if (!has_active_cel) {
            set_status("Create a layer to edit pixels");
            return;
        }
        if (mask_editing) {
            fill_mask_bucket(document_, px_i, py_i, mask_value_from_color(tool_.secondary), tool_.tolerance, tool_.contiguous != io.KeyShift);
            set_status("Filled mask");
        } else {
            fill_bucket(document_, px_i, py_i, tool_.secondary, tool_.tolerance, tool_.contiguous != io.KeyShift);
        }
        texture_dirty_ = true;
        drag_active_ = false;
    }

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && tool_.tool == ToolType::MagicWand) {
        selection_before_ = document_.selection;
        const SelectionCombineMode mode = selection_mode_from_input(io, true);
        magic_wand(document_, px_i, py_i, tool_.tolerance, tool_.contiguous != io.KeyShift, mode);
        document_.commit_selection_edit(selection_undo_name("Magic Wand", mode), selection_before_);
        drag_active_ = false;
    }

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        (tool_.tool == ToolType::RectSelect || tool_.tool == ToolType::LassoSelect)) {
        drag_active_ = true;
        canvas_drag_button_ = ImGuiMouseButton_Right;
        drag_start_x_ = px_i;
        drag_start_y_ = py_i;
        drag_current_x_ = px_i;
        drag_current_y_ = py_i;
        last_x_ = px_i;
        last_y_ = py_i;
        selection_before_ = document_.selection;
        selection_drag_mode_ = selection_mode_from_input(io, true);
        if (tool_.tool == ToolType::LassoSelect) {
            lasso_points_.clear();
            lasso_points_.push_back({px_i, py_i});
            lasso_active_ = true;
        }
    }

    if (over_pixel &&
        effect_preview_active_ &&
        effect_preview_kind_ == EffectPreviewKind::DepthOfField &&
        depth_of_field_pick_focus_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (const std::vector<Pixel>* depth = depth_of_field_pixels(document_)) {
            const std::size_t index = static_cast<std::size_t>(document_.pixel_index(px_i, py_i));
            depth_of_field_focus_ = r((*depth)[index]);
            depth_of_field_pick_focus_ = false;
            effect_preview_dirty_ = true;
            set_status("Sampled focus depth");
        } else {
            report_error("Depth of Field", "Select a depth-map layer before sampling focus.");
        }
        drag_active_ = false;
        return;
    }

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!has_active_cel) {
            set_status("Create a layer to edit pixels");
            drag_active_ = false;
            return;
        }
        drag_active_ = true;
        canvas_drag_button_ = ImGuiMouseButton_Left;
        drag_start_x_ = px_i;
        drag_start_y_ = py_i;
        drag_current_x_ = px_i;
        drag_current_y_ = py_i;
        last_x_ = px_i;
        last_y_ = py_i;

        switch (tool_.tool) {
            case ToolType::Pencil:
            case ToolType::Brush:
            case ToolType::Eraser:
                stroke_active_ = true;
                stroke_before_ = document_.snapshot_active_cel();
                if (mask_editing) {
                    const std::uint8_t mask_value = tool_.tool == ToolType::Eraser ? 0 : mask_value_from_color(tool_.primary);
                    plot_mask_brush_raw(document_, px_i, py_i, mask_value, tool_.tool == ToolType::Pencil ? 1 : tool_.brush_size);
                } else {
                    plot_brush_raw(document_, px_i, py_i, tool_.primary, tool_.tool == ToolType::Pencil ? 1 : tool_.brush_size, tool_.tool == ToolType::Eraser);
                }
                texture_dirty_ = true;
                break;
            case ToolType::Bucket:
                if (mask_editing) {
                    fill_mask_bucket(document_, px_i, py_i, mask_value_from_color(tool_.primary), tool_.tolerance, tool_.contiguous != io.KeyShift);
                    set_status("Filled mask");
                } else {
                    fill_bucket(document_, px_i, py_i, tool_.primary, tool_.tolerance, tool_.contiguous != io.KeyShift);
                }
                texture_dirty_ = true;
                drag_active_ = false;
                break;
            case ToolType::Eyedropper:
                if (auto picked = pick_color(document_, px_i, py_i)) {
                    tool_.primary = *picked;
                    set_status("Picked color");
                }
                drag_active_ = false;
                break;
            case ToolType::MagicWand:
                selection_before_ = document_.selection;
                selection_drag_mode_ = selection_mode_from_input(io, false);
                magic_wand(document_, px_i, py_i, tool_.tolerance, tool_.contiguous != io.KeyShift, selection_drag_mode_);
                document_.commit_selection_edit(selection_undo_name("Magic Wand", selection_drag_mode_), selection_before_);
                drag_active_ = false;
                break;
            case ToolType::CloneStamp:
                if (tool_.clone_source_x >= 0) {
                    clone_drag_active_ = true;
                    stroke_before_ = document_.snapshot_active_cel();
                    clone_source_pixels_ = document_.active_cel().pixels;
                    clone_stamp_raw(document_, clone_source_pixels_, tool_.clone_source_x, tool_.clone_source_y, px_i, py_i, tool_.brush_size);
                    texture_dirty_ = true;
                } else {
                    drag_active_ = false;
                    set_status("Right-click the canvas to set a clone source first");
                }
                break;
            case ToolType::Text:
                text_box_.active = true;
                text_box_.x = px_i;
                text_box_.y = py_i;
                text_box_.text = text_buffer_;
                text_box_.color = tool_.primary;
                drag_active_ = false;
                break;
            case ToolType::MovePixels:
                selection_before_ = document_.selection;
                stroke_before_ = document_.snapshot_active_cel();
                if (!document_.selection.active) {
                    document_.selection.select_all();
                }
                if (document_.begin_floating_selection()) {
                    move_drag_active_ = true;
                    move_start_x_ = px_i;
                    move_start_y_ = py_i;
                    texture_dirty_ = true;
                } else {
                    drag_active_ = false;
                }
                break;
            case ToolType::LassoSelect:
                selection_before_ = document_.selection;
                selection_drag_mode_ = selection_mode_from_input(io, false);
                lasso_points_.clear();
                lasso_points_.push_back({px_i, py_i});
                lasso_active_ = true;
                break;
            case ToolType::RectSelect:
                selection_before_ = document_.selection;
                selection_drag_mode_ = selection_mode_from_input(io, false);
                break;
            case ToolType::Line:
            case ToolType::Rectangle:
            case ToolType::Ellipse:
            case ToolType::Gradient:
                stroke_before_ = document_.snapshot_active_cel();
                pixel_drag_preview_active_ = true;
                update_pixel_drag_preview();
                break;
            default:
                break;
        }
    }

    if (drag_active_ && over_pixel && ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(canvas_drag_button_))) {
        drag_current_x_ = px_i;
        drag_current_y_ = py_i;
    }
    if (pixel_drag_preview_active_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        update_pixel_drag_preview();
    }

    if (stroke_active_ && over_pixel && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        int size = tool_.tool == ToolType::Pencil ? 1 : tool_.brush_size;
        if (mask_editing) {
            const std::uint8_t mask_value = tool_.tool == ToolType::Eraser ? 0 : mask_value_from_color(tool_.primary);
            draw_mask_line_raw(document_, last_x_, last_y_, px_i, py_i, mask_value, size);
        } else {
            draw_line_raw(document_, last_x_, last_y_, px_i, py_i, tool_.primary, size, tool_.tool == ToolType::Eraser);
        }
        last_x_ = px_i;
        last_y_ = py_i;
        texture_dirty_ = true;
    }

    if (clone_drag_active_ && over_pixel && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        int sx = tool_.clone_source_x + (px_i - drag_start_x_);
        int sy = tool_.clone_source_y + (py_i - drag_start_y_);
        clone_stamp_raw(document_, clone_source_pixels_, sx, sy, px_i, py_i, tool_.brush_size);
        texture_dirty_ = true;
    }

    if (move_drag_active_ && over_pixel && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        document_.move_floating_selection(px_i - move_start_x_, py_i - move_start_y_);
    }

    if (lasso_active_ && over_pixel && ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(canvas_drag_button_))) {
        if (lasso_points_.empty() || lasso_points_.back()[0] != px_i || lasso_points_.back()[1] != py_i) {
            lasso_points_.push_back({px_i, py_i});
        }
    }

    if (stroke_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        commit_stroke();
    } else if (clone_drag_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        document_.commit_active_cel_edit("Clone Stamp Stroke", std::move(stroke_before_));
        clone_drag_active_ = false;
        drag_active_ = false;
        texture_dirty_ = true;
    } else if (move_drag_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        document_.commit_floating_selection("Move Pixels", std::move(stroke_before_));
        move_drag_active_ = false;
        drag_active_ = false;
        texture_dirty_ = true;
    } else if (lasso_active_ && ImGui::IsMouseReleased(static_cast<ImGuiMouseButton>(canvas_drag_button_))) {
        if (lasso_points_.size() >= 3) {
            document_.selection.select_polygon(lasso_points_, selection_drag_mode_);
            document_.commit_selection_edit(selection_undo_name("Lasso Selection", selection_drag_mode_), selection_before_);
        }
        lasso_points_.clear();
        lasso_active_ = false;
        drag_active_ = false;
    } else if (pixel_drag_preview_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        update_pixel_drag_preview();
        commit_pixel_drag_preview();
    } else if (drag_active_ && over_pixel && ImGui::IsMouseReleased(static_cast<ImGuiMouseButton>(canvas_drag_button_))) {
        finish_drag(px_i, py_i);
    }
}

bool EditorApp::mouse_to_pixel(const ImVec2& origin, int& out_x, int& out_y) const {
    ImVec2 mouse = ImGui::GetIO().MousePos;
    out_x = static_cast<int>(std::floor((mouse.x - origin.x) / zoom_));
    out_y = static_cast<int>(std::floor((mouse.y - origin.y) / zoom_));
    return document_.in_bounds(out_x, out_y);
}

void EditorApp::start_straighten_tool() {
    const float inset_x = std::max(1.0f, static_cast<float>(document_.width) * 0.18f);
    const float inset_y = std::max(1.0f, static_cast<float>(document_.height) * 0.18f);
    const float max_x = std::max(0.0f, static_cast<float>(document_.width - 1));
    const float max_y = std::max(0.0f, static_cast<float>(document_.height - 1));
    straighten_points_[0] = ImVec2(std::min(inset_x, max_x), std::min(inset_y, max_y));
    straighten_points_[1] = ImVec2(std::max(0.0f, max_x - inset_x), std::min(inset_y, max_y));
    straighten_points_[2] = ImVec2(std::max(0.0f, max_x - inset_x), std::max(0.0f, max_y - inset_y));
    straighten_points_[3] = ImVec2(std::min(inset_x, max_x), std::max(0.0f, max_y - inset_y));
    straighten_drag_point_ = -1;
    straighten_active_ = true;
    set_status("Straighten: drag points, Enter applies, Esc cancels");
}

void EditorApp::cancel_straighten_tool() {
    straighten_active_ = false;
    straighten_drag_point_ = -1;
    set_status("Canceled Straighten");
}

void EditorApp::apply_straighten_tool() {
    const ImVec2 top(straighten_points_[1].x - straighten_points_[0].x,
                     straighten_points_[1].y - straighten_points_[0].y);
    const ImVec2 bottom(straighten_points_[2].x - straighten_points_[3].x,
                        straighten_points_[2].y - straighten_points_[3].y);
    const ImVec2 axis(top.x + bottom.x, top.y + bottom.y);
    const float angle_degrees = -std::atan2(axis.y, axis.x) * 180.0f / 3.14159265358979323846f;
    apply_affine_transform_to_document("Straighten",
                                       angle_degrees,
                                       1.0f,
                                       0,
                                       0,
                                       resampling_mode_from_index(image_transform_resampling_));
    straighten_active_ = false;
    straighten_drag_point_ = -1;
}

bool EditorApp::handle_straighten_input(const ImVec2& origin, bool viewport_hovered, bool canvas_active) {
    if (!straighten_active_) {
        return false;
    }
    if (canvas_active && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancel_straighten_tool();
        return true;
    }
    if (canvas_active && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        apply_straighten_tool();
        return true;
    }

    ImGuiIO& io = ImGui::GetIO();
    auto point_to_screen = [&](const ImVec2& point) {
        return ImVec2(origin.x + point.x * zoom_, origin.y + point.y * zoom_);
    };
    auto mouse_to_doc = [&]() {
        return ImVec2(std::clamp((io.MousePos.x - origin.x) / zoom_, 0.0f, static_cast<float>(document_.width - 1)),
                      std::clamp((io.MousePos.y - origin.y) / zoom_, 0.0f, static_cast<float>(document_.height - 1)));
    };

    if (viewport_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float best_distance = std::numeric_limits<float>::max();
        int best_point = -1;
        for (int i = 0; i < 4; ++i) {
            const ImVec2 screen = point_to_screen(straighten_points_[static_cast<std::size_t>(i)]);
            const float dx = io.MousePos.x - screen.x;
            const float dy = io.MousePos.y - screen.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < best_distance) {
                best_distance = distance;
                best_point = i;
            }
        }
        straighten_drag_point_ = std::max(0, best_point);
        straighten_points_[static_cast<std::size_t>(straighten_drag_point_)] = mouse_to_doc();
    }
    if (straighten_drag_point_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        straighten_points_[static_cast<std::size_t>(straighten_drag_point_)] = mouse_to_doc();
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        straighten_drag_point_ = -1;
    }
    return true;
}

void EditorApp::finish_drag(int x, int y) {
    const bool constrain = tool_.tool == ToolType::RectSelect ? ImGui::GetIO().KeyShift
                                                              : action_modifier_down(ImGui::GetIO());
    const auto constrained_end =
        constrained_tool_endpoint(tool_.tool, drag_start_x_, drag_start_y_, x, y, constrain);
    const int end_x = constrained_end[0];
    const int end_y = constrained_end[1];
    switch (tool_.tool) {
        case ToolType::Line:
            draw_line(document_, drag_start_x_, drag_start_y_, end_x, end_y, tool_.primary, tool_.brush_size);
            texture_dirty_ = true;
            break;
        case ToolType::Rectangle:
            draw_rect(document_,
                      drag_start_x_,
                      drag_start_y_,
                      end_x,
                      end_y,
                      tool_.primary,
                      tool_.brush_size,
                      ImGui::GetIO().KeyShift);
            texture_dirty_ = true;
            break;
        case ToolType::Ellipse:
            draw_ellipse(document_,
                         drag_start_x_,
                         drag_start_y_,
                         end_x,
                         end_y,
                         tool_.primary,
                         tool_.brush_size,
                         ImGui::GetIO().KeyShift);
            texture_dirty_ = true;
            break;
        case ToolType::Gradient:
            fill_gradient(document_, drag_start_x_, drag_start_y_, x, y, tool_.primary, tool_.secondary);
            texture_dirty_ = true;
            break;
        case ToolType::RectSelect:
            document_.selection.select_rect(drag_start_x_, drag_start_y_, end_x, end_y, selection_drag_mode_);
            document_.commit_selection_edit(selection_undo_name("Rectangle Selection", selection_drag_mode_), selection_before_);
            break;
        default:
            break;
    }
    drag_active_ = false;
}

void EditorApp::commit_stroke() {
    document_.commit_active_cel_edit(tool_.tool == ToolType::Eraser ? "Eraser Stroke" : "Brush Stroke", std::move(stroke_before_));
    stroke_active_ = false;
    drag_active_ = false;
}

void EditorApp::update_pixel_drag_preview() {
    if (!pixel_drag_preview_active_ || !document_.has_active_cel() || stroke_before_.size() != document_.active_cel().pixels.size()) {
        return;
    }
    document_.active_cel().pixels = stroke_before_;
    bool filled = ImGui::GetIO().KeyShift;
    const auto constrained_end = constrained_tool_endpoint(tool_.tool,
                                                          drag_start_x_,
                                                          drag_start_y_,
                                                          drag_current_x_,
                                                          drag_current_y_,
                                                          action_modifier_down(ImGui::GetIO()));
    const int end_x = constrained_end[0];
    const int end_y = constrained_end[1];
    switch (tool_.tool) {
        case ToolType::Line:
            draw_line_raw(document_, drag_start_x_, drag_start_y_, end_x, end_y, tool_.primary, tool_.brush_size, false);
            break;
        case ToolType::Rectangle:
            draw_rect_raw(document_, drag_start_x_, drag_start_y_, end_x, end_y, tool_.primary, tool_.brush_size, filled);
            break;
        case ToolType::Ellipse:
            draw_ellipse_raw(document_, drag_start_x_, drag_start_y_, end_x, end_y, tool_.primary, tool_.brush_size, filled);
            break;
        case ToolType::Gradient:
            fill_gradient_raw(document_, drag_start_x_, drag_start_y_, drag_current_x_, drag_current_y_, tool_.primary, tool_.secondary);
            break;
        default:
            break;
    }
    texture_dirty_ = true;
}

void EditorApp::commit_pixel_drag_preview() {
    const bool filled = ImGui::GetIO().KeyShift;
    const char* label = "Paint Stroke";
    switch (tool_.tool) {
        case ToolType::Line:
            label = "Line";
            break;
        case ToolType::Rectangle:
            label = filled ? "Filled Rectangle" : "Rectangle";
            break;
        case ToolType::Ellipse:
            label = filled ? "Filled Ellipse" : "Ellipse";
            break;
        case ToolType::Gradient:
            label = "Gradient";
            break;
        default:
            break;
    }
    document_.commit_active_cel_edit(label, std::move(stroke_before_));
    pixel_drag_preview_active_ = false;
    drag_active_ = false;
    texture_dirty_ = true;
}

void EditorApp::cancel_pixel_drag_preview() {
    if (document_.has_active_cel() && stroke_before_.size() == document_.active_cel().pixels.size()) {
        document_.active_cel().pixels = std::move(stroke_before_);
    }
    pixel_drag_preview_active_ = false;
    drag_active_ = false;
    texture_dirty_ = true;
}

bool EditorApp::delete_selection_contents() {
    if (!document_.has_active_cel()) {
        return false;
    }
    if (document_.floating_selection.active) {
        if (stroke_before_.size() != document_.active_cel().pixels.size()) {
            stroke_before_ = document_.snapshot_active_cel();
        }
        document_.floating_selection.clear();
        document_.selection.clear();
        document_.commit_active_cel_edit("Delete Selection", std::move(stroke_before_));
        move_drag_active_ = false;
        drag_active_ = false;
        texture_dirty_ = true;
        set_status("Deleted selected pixels");
        return true;
    }

    if (!document_.delete_selected_pixels()) {
        return false;
    }
    texture_dirty_ = true;
    set_status("Deleted selected pixels");
    return true;
}

void EditorApp::draw_grid_overlay(ImDrawList* draw_list, const ImVec2& origin, const ImVec2& size) const {
    if (!show_grid_ || zoom_ < 4.0f) {
        return;
    }
    ImU32 color = zoom_ >= 12.0f ? IM_COL32(0, 0, 0, 80) : IM_COL32(0, 0, 0, 35);
    for (int x = 0; x <= document_.width; ++x) {
        float sx = origin.x + pixel_scale(x, zoom_);
        draw_list->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + size.y), color);
    }
    for (int y = 0; y <= document_.height; ++y) {
        float sy = origin.y + pixel_scale(y, zoom_);
        draw_list->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + size.x, sy), color);
    }
}

void EditorApp::draw_selection_overlay(ImDrawList* draw_list, const ImVec2& origin) const {
    if (!document_.selection.active) {
        return;
    }
    ImU32 fill = IM_COL32(60, 120, 255, 40);
    ImU32 edge = IM_COL32(35, 75, 220, 200);
    for (int y = 0; y < document_.height; ++y) {
        for (int x = 0; x < document_.width; ++x) {
            if (!document_.selection.contains(x, y)) {
                continue;
            }
            ImVec2 a(origin.x + pixel_scale(x, zoom_), origin.y + pixel_scale(y, zoom_));
            ImVec2 b(a.x + zoom_, a.y + zoom_);
            draw_list->AddRectFilled(a, b, fill);
            bool boundary = !document_.selection.contains(x - 1, y) || !document_.selection.contains(x + 1, y) ||
                            !document_.selection.contains(x, y - 1) || !document_.selection.contains(x, y + 1);
            if (boundary) draw_list->AddRect(a, b, edge);
        }
    }
}

void EditorApp::draw_floating_selection_overlay(ImDrawList* draw_list, const ImVec2& origin) const {
    const FloatingSelection& floating = document_.floating_selection;
    if (!floating.active) {
        return;
    }
    ImU32 edge = IM_COL32(255, 230, 40, 230);
    for (int y = 0; y < floating.height; ++y) {
        for (int x = 0; x < floating.width; ++x) {
            if (!floating.contains_local(x, y)) {
                continue;
            }
            Pixel p = floating.pixels[static_cast<std::size_t>(y * floating.width + x)];
            if (a(p) == 0) {
                continue;
            }
            int dx = floating.source_x + floating.offset_x + x;
            int dy = floating.source_y + floating.offset_y + y;
            if (!document_.in_bounds(dx, dy)) {
                continue;
            }
            ImVec2 a_pos(origin.x + pixel_scale(dx, zoom_), origin.y + pixel_scale(dy, zoom_));
            ImVec2 b_pos(a_pos.x + zoom_, a_pos.y + zoom_);
            draw_list->AddRectFilled(a_pos, b_pos, color_u32(p));
        }
    }
    ImVec2 a_pos(origin.x + pixel_scale(floating.source_x + floating.offset_x, zoom_),
                 origin.y + pixel_scale(floating.source_y + floating.offset_y, zoom_));
    ImVec2 b_pos(a_pos.x + pixel_scale(floating.width, zoom_), a_pos.y + pixel_scale(floating.height, zoom_));
    draw_list->AddRect(a_pos, b_pos, edge, 0.0f, 0, 2.0f);
}

void EditorApp::draw_mask_overlay(ImDrawList* draw_list, const ImVec2& origin, const ImVec2&) const {
    if (!edit_layer_mask_ && !show_mask_overlay_) {
        return;
    }
    if (document_.active_layer < 0 || document_.active_layer >= static_cast<int>(document_.layers.size())) {
        return;
    }
    const Layer& layer = document_.layers[static_cast<std::size_t>(document_.active_layer)];
    if (!layer.mask_enabled || layer.mask.size() != static_cast<std::size_t>(document_.width * document_.height)) {
        return;
    }
    constexpr std::size_t kMaxOverlayPixels = 1048576;
    if (layer.mask.size() > kMaxOverlayPixels) {
        return;
    }
    for (int y = 0; y < document_.height; ++y) {
        for (int x = 0; x < document_.width; ++x) {
            const std::uint8_t value = layer.mask[static_cast<std::size_t>(document_.pixel_index(x, y))];
            if (value == 255) {
                continue;
            }
            const int alpha = std::clamp((255 - static_cast<int>(value)) / 2, 32, 128);
            const ImVec2 min_pos(origin.x + pixel_scale(x, zoom_), origin.y + pixel_scale(y, zoom_));
            const ImVec2 max_pos(origin.x + pixel_scale(x + 1, zoom_), origin.y + pixel_scale(y + 1, zoom_));
            draw_list->AddRectFilled(min_pos, max_pos, IM_COL32(255, 40, 80, alpha));
        }
    }
}

void EditorApp::draw_text_preview_overlay(ImDrawList* draw_list, const ImVec2& origin) const {
    if (!text_box_.active) {
        return;
    }
    ImU32 color = color_u32(with_alpha(text_box_.color, 210));
    for (const auto& point : raster_text_points(text_box_.x, text_box_.y, text_box_.text)) {
        if (!document_.in_bounds(point[0], point[1])) {
            continue;
        }
        ImVec2 a_pos(origin.x + pixel_scale(point[0], zoom_), origin.y + pixel_scale(point[1], zoom_));
        ImVec2 b_pos(a_pos.x + zoom_, a_pos.y + zoom_);
        draw_list->AddRectFilled(a_pos, b_pos, color);
    }
    ImVec2 top_left(origin.x + pixel_scale(text_box_.x, zoom_), origin.y + pixel_scale(text_box_.y, zoom_));
    float width = std::max(6.0f, static_cast<float>(text_box_.text.size() * 6) * zoom_);
    draw_list->AddRect(top_left, ImVec2(top_left.x + width, top_left.y + 7.0f * zoom_), IM_COL32(30, 80, 220, 230), 0.0f, 0, 2.0f);
}

void EditorApp::draw_lasso_preview(ImDrawList* draw_list, const ImVec2& origin) const {
    if (!lasso_active_ || lasso_points_.size() < 2) {
        return;
    }
    for (std::size_t i = 1; i < lasso_points_.size(); ++i) {
        ImVec2 a_pos(origin.x + (static_cast<float>(lasso_points_[i - 1][0]) + 0.5f) * zoom_,
                     origin.y + (static_cast<float>(lasso_points_[i - 1][1]) + 0.5f) * zoom_);
        ImVec2 b_pos(origin.x + (static_cast<float>(lasso_points_[i][0]) + 0.5f) * zoom_,
                     origin.y + (static_cast<float>(lasso_points_[i][1]) + 0.5f) * zoom_);
        draw_list->AddLine(a_pos, b_pos, IM_COL32(40, 80, 220, 230), 2.0f);
    }
}

void EditorApp::draw_selected_model_face_overlay(ImDrawList* draw_list, const ImVec2& origin) const {
    if (!settings_.show_canvas_cuboid_uv_overlay) {
        return;
    }
    if (model_.cuboids.empty()) {
        return;
    }
    int cuboid_index = std::clamp(model_.selected_cuboid, 0, static_cast<int>(model_.cuboids.size()) - 1);
    int face_index = std::clamp(model_.selected_face, 0, 5);
    const Cuboid& cuboid = model_.cuboids[static_cast<std::size_t>(cuboid_index)];
    float z = zoom_;
    auto draw_face = [&](int face, bool selected) {
        UvRect uv = clamped_uv_rect(cuboid.uv[static_cast<std::size_t>(face)], document_.width, document_.height);
        ImVec2 a_pos(origin.x + pixel_scale(uv.x, z), origin.y + pixel_scale(uv.y, z));
        ImVec2 b_pos(origin.x + pixel_scale(uv.x + uv.w, z), origin.y + pixel_scale(uv.y + uv.h, z));
        ImVec2 c_pos(b_pos.x, a_pos.y);
        ImVec2 d_pos(a_pos.x, b_pos.y);
        ImU32 fill = selected ? IM_COL32(255, 220, 40, 36) : IM_COL32(40, 180, 255, 20);
        ImU32 edge = selected ? IM_COL32(255, 230, 40, 255) : IM_COL32(40, 180, 255, 210);
        ImU32 shadow = IM_COL32(20, 20, 20, selected ? 210 : 150);
        float edge_width = selected ? 2.0f : 1.5f;

        draw_list->AddRectFilled(a_pos, b_pos, fill);
        draw_list->AddRect(a_pos, b_pos, shadow, 0.0f, 0, edge_width + 2.0f);
        draw_list->AddRect(a_pos, b_pos, edge, 0.0f, 0, edge_width);
        draw_list->AddLine(a_pos, b_pos, shadow, edge_width + 1.0f);
        draw_list->AddLine(c_pos, d_pos, shadow, edge_width + 1.0f);
        draw_list->AddLine(a_pos, b_pos, edge, 1.0f);
        draw_list->AddLine(c_pos, d_pos, edge, 1.0f);

        const char* label = face_name(face);
        ImVec2 label_pos(a_pos.x + 4.0f, a_pos.y + 3.0f);
        draw_list->AddText(ImVec2(label_pos.x + 1.0f, label_pos.y + 1.0f), shadow, label);
        draw_list->AddText(label_pos, edge, label);
    };

    if (show_all_cuboid_wireframes_) {
        for (int face = 0; face < 6; ++face) {
            if (face != face_index) {
                draw_face(face, false);
            }
        }
    }
    draw_face(face_index, true);
}

void EditorApp::draw_tool_drag_preview(ImDrawList* draw_list, const ImVec2& origin) const {
    if (!drag_active_ || pixel_drag_preview_active_ || stroke_active_ || clone_drag_active_ || move_drag_active_ || lasso_active_) {
        return;
    }
    switch (tool_.tool) {
        case ToolType::RectSelect:
            break;
        default:
            return;
    }

    float z = zoom_;
    auto pixel_rect = [&](int x0, int y0, int x1, int y1, ImVec2& min_pos, ImVec2& max_pos) {
        int min_x = std::min(x0, x1);
        int max_x = std::max(x0, x1);
        int min_y = std::min(y0, y1);
        int max_y = std::max(y0, y1);
        min_pos = ImVec2(origin.x + pixel_scale(min_x, z), origin.y + pixel_scale(min_y, z));
        max_pos = ImVec2(origin.x + pixel_scale(max_x + 1, z), origin.y + pixel_scale(max_y + 1, z));
    };

    ImU32 selection_edge = IM_COL32(40, 120, 255, 240);
    ImU32 selection_fill = IM_COL32(40, 120, 255, 34);
    ImU32 shadow = IM_COL32(0, 0, 0, 180);

    ImVec2 min_pos;
    ImVec2 max_pos;
    const auto constrained_end = constrained_tool_endpoint(ToolType::RectSelect,
                                                          drag_start_x_,
                                                          drag_start_y_,
                                                          drag_current_x_,
                                                          drag_current_y_,
                                                          ImGui::GetIO().KeyShift);
    pixel_rect(drag_start_x_, drag_start_y_, constrained_end[0], constrained_end[1], min_pos, max_pos);

    draw_list->AddRectFilled(min_pos, max_pos, selection_fill);
    draw_list->AddRect(min_pos, max_pos, shadow, 0.0f, 0, 3.0f);
    draw_list->AddRect(min_pos, max_pos, selection_edge, 0.0f, 0, 2.0f);
}

void EditorApp::draw_straighten_overlay(ImDrawList* draw_list, const ImVec2& origin) const {
    if (!straighten_active_) {
        return;
    }
    std::array<ImVec2, 4> points{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        points[i] = ImVec2(origin.x + straighten_points_[i].x * zoom_,
                           origin.y + straighten_points_[i].y * zoom_);
    }
    draw_list->AddQuadFilled(points[0], points[1], points[2], points[3], IM_COL32(230, 35, 35, 46));
    draw_list->AddQuad(points[0], points[1], points[2], points[3], IM_COL32(255, 78, 78, 245), 2.0f);
    draw_list->AddLine(points[0], points[2], IM_COL32(255, 78, 78, 150), 1.0f);
    draw_list->AddLine(points[1], points[3], IM_COL32(255, 78, 78, 150), 1.0f);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const bool active = straighten_drag_point_ == static_cast<int>(i);
        draw_list->AddCircleFilled(points[i], active ? 7.0f : 6.0f, IM_COL32(255, 235, 235, 255));
        draw_list->AddCircle(points[i], active ? 7.0f : 6.0f, IM_COL32(160, 0, 0, 255), 0, 2.0f);
    }
}

void EditorApp::commit_text_box() {
    if (!text_box_.active) {
        return;
    }
    stamp_text(document_, text_box_.x, text_box_.y, text_box_.text, text_box_.color);
    text_box_.active = false;
    texture_dirty_ = true;
}

void EditorApp::cancel_text_box() {
    text_box_.active = false;
}

void EditorApp::draw_color_panel() {
    bool open = settings_.show_colors_panel;
    if (!ImGui::Begin("Colors", &open)) {
        settings_.show_colors_panel = open;
        ImGui::End();
        return;
    }
    settings_.show_colors_panel = open;
    float primary[4] = {r(tool_.primary) / 255.0f, g(tool_.primary) / 255.0f, b(tool_.primary) / 255.0f, a(tool_.primary) / 255.0f};
    float secondary[4] = {r(tool_.secondary) / 255.0f, g(tool_.secondary) / 255.0f, b(tool_.secondary) / 255.0f, a(tool_.secondary) / 255.0f};
    if (ImGui::ColorPicker4("Primary", primary, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_AlphaBar)) {
        tool_.primary = from_float4(primary);
    }
    if (ImGui::ColorEdit4("Secondary", secondary, ImGuiColorEditFlags_AlphaBar)) {
        tool_.secondary = from_float4(secondary);
    }
    if (ImGui::Button("Swap")) {
        std::swap(tool_.primary, tool_.secondary);
    }
    ImGui::SeparatorText("Palette");
    int columns = 8;
    for (int i = 0; i < static_cast<int>(document_.palette.colors.size()); ++i) {
        ImGui::PushID(i);
        ImGui::ColorButton("swatch", to_imvec4(document_.palette.colors[static_cast<std::size_t>(i)]), ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            tool_.primary = document_.palette.colors[static_cast<std::size_t>(i)];
            document_.palette.active = i;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            auto before = document_.palette;
            document_.palette.colors[static_cast<std::size_t>(i)] = tool_.primary;
            document_.commit_palette_edit("Edit Swatch", before);
        }
        if ((i + 1) % columns != 0) ImGui::SameLine();
        ImGui::PopID();
    }
    if (ImGui::Button("Add Swatch")) {
        auto before = document_.palette;
        document_.palette.colors.push_back(tool_.primary);
        document_.commit_palette_edit("Add Swatch", before);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove") && !document_.palette.colors.empty()) {
        auto before = document_.palette;
        document_.palette.colors.pop_back();
        document_.palette.active = std::clamp(document_.palette.active, 0, std::max(0, static_cast<int>(document_.palette.colors.size()) - 1));
        document_.commit_palette_edit("Remove Swatch", before);
    }
    if (ImGui::Button("Extract")) {
        std::vector<Pixel> colors = extracted_palette_from_document(document_, 32);
        if (!colors.empty()) {
            auto before = document_.palette;
            document_.palette.colors = std::move(colors);
            document_.palette.active = 0;
            document_.commit_palette_edit("Extract Palette", before);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Sort")) {
        auto before = document_.palette;
        std::sort(document_.palette.colors.begin(), document_.palette.colors.end(), [](Pixel lhs, Pixel rhs) {
            const float lhs_hue = pixel_hue(lhs);
            const float rhs_hue = pixel_hue(rhs);
            if (lhs_hue != rhs_hue) {
                return lhs_hue < rhs_hue;
            }
            return pixel_luma(lhs) < pixel_luma(rhs);
        });
        document_.commit_palette_edit("Sort Palette", before);
    }
    ImGui::SameLine();
    if (ImGui::Button("Ramp")) {
        auto before = document_.palette;
        document_.palette.colors = palette_ramp(tool_.primary, tool_.secondary, 8);
        document_.palette.active = 0;
        document_.commit_palette_edit("Generate Palette Ramp", before);
    }
    if (ImGui::Button("Remap")) {
        apply_effect_to_document(EffectPreviewKind::PaletteQuantize);
    }
    ImGui::SameLine();
    if (ImGui::Button("Dither Remap")) {
        apply_effect_to_document(EffectPreviewKind::PaletteDither);
    }
    ImGui::End();
}

void EditorApp::draw_layers_panel() {
    bool open = settings_.show_layers_panel;
    if (!ImGui::Begin("Layers", &open)) {
        settings_.show_layers_panel = open;
        ImGui::End();
        return;
    }
    settings_.show_layers_panel = open;
    const char* blend_modes[] = {"Normal", "Multiply", "Additive", "Color Burn", "Color Dodge", "Reflect", "Glow",
                                 "Overlay", "Difference", "Negation", "Lighten", "Darken", "Screen", "Xor"};
    const std::size_t mask_size = static_cast<std::size_t>(document_.width * document_.height);
    const bool has_layers = !document_.layers.empty();
    if (has_layers) {
        document_.active_layer = std::clamp(document_.active_layer, 0, static_cast<int>(document_.layers.size()) - 1);
    } else {
        document_.active_layer = 0;
        edit_layer_mask_ = false;
    }

    auto tooltip = [](const char* text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", text);
        }
    };
    auto guarded_button = [&](const char* label, bool disabled, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
        if (disabled) {
            ImGui::BeginDisabled();
        }
        const bool clicked = ImGui::Button(label, size);
        if (disabled) {
            ImGui::EndDisabled();
        }
        return clicked && !disabled;
    };
    if (has_layers) {
        Layer& active_layer = document_.layers[static_cast<std::size_t>(document_.active_layer)];
        if (edit_layer_mask_ && (!active_layer.mask_enabled || active_layer.mask.size() != mask_size)) {
            active_layer.mask_enabled = true;
            ensure_layer_mask(active_layer, document_.width, document_.height, 255);
            texture_dirty_ = true;
        }

        ImGui::SeparatorText("Selected Layer");
        char name[128];
        copy_path(name, sizeof(name), active_layer.name);
        if (ImGui::InputText("Name", name, sizeof(name))) {
            active_layer.name = name;
        }

        bool visible = active_layer.visible;
        if (ImGui::Checkbox("Visible", &visible)) {
            active_layer.visible = visible;
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        bool clip_to_below = active_layer.clip_to_below;
        if (ImGui::Checkbox("Clip to below", &clip_to_below)) {
            active_layer.clip_to_below = clip_to_below;
            texture_dirty_ = true;
        }

        int blend = static_cast<int>(active_layer.blend_mode);
        ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.72f));
        if (ImGui::Combo("Blend mode", &blend, blend_modes, IM_ARRAYSIZE(blend_modes))) {
            active_layer.blend_mode = static_cast<LayerBlendMode>(blend);
            texture_dirty_ = true;
        }

        float opacity_percent = active_layer.opacity * 100.0f;
        if (ImGui::SliderFloat("Opacity", &opacity_percent, 0.0f, 100.0f, "%.0f%%")) {
            active_layer.opacity = std::clamp(opacity_percent / 100.0f, 0.0f, 1.0f);
            texture_dirty_ = true;
        }

        bool mask_enabled = active_layer.mask_enabled;
        if (ImGui::Checkbox("Layer mask", &mask_enabled)) {
            active_layer.mask_enabled = mask_enabled;
            if (active_layer.mask_enabled) {
                ensure_layer_mask(active_layer, document_.width, document_.height, 255);
            } else if (edit_layer_mask_) {
                edit_layer_mask_ = false;
            }
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        bool edit_mask = edit_layer_mask_;
        if (ImGui::Checkbox("Edit mask", &edit_mask)) {
            edit_layer_mask_ = edit_mask;
            if (edit_layer_mask_) {
                active_layer.mask_enabled = true;
                ensure_layer_mask(active_layer, document_.width, document_.height, 255);
                texture_dirty_ = true;
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Overlay", &show_mask_overlay_);

        ImGui::TextDisabled("Mask: %s", active_layer.mask.empty() ? "Not created" : (active_layer.mask_enabled ? "Enabled" : "Disabled"));
        if (ImGui::CollapsingHeader("Mask Actions")) {
            if (ImGui::Button("Reveal All")) {
                active_layer.mask.assign(static_cast<std::size_t>(document_.width * document_.height), 255);
                active_layer.mask_enabled = true;
                texture_dirty_ = true;
            }
            tooltip("Fill the active layer mask with white");
            ImGui::SameLine();
            if (ImGui::Button("Hide All")) {
                active_layer.mask.assign(static_cast<std::size_t>(document_.width * document_.height), 0);
                active_layer.mask_enabled = true;
                texture_dirty_ = true;
            }
            tooltip("Fill the active layer mask with black");
            ImGui::SameLine();
            if (ImGui::Button("From Selection")) {
                fill_layer_mask_from_selection(active_layer, document_.selection, document_.width, document_.height);
                texture_dirty_ = true;
            }
            tooltip("Build a mask from the current selection");

            if (ImGui::Button("From Alpha")) {
                fill_layer_mask_from_alpha(active_layer, document_.cel(document_.active_frame, document_.active_layer), document_.width, document_.height);
                texture_dirty_ = true;
            }
            tooltip("Build a mask from the active layer alpha");
            ImGui::SameLine();
            if (ImGui::Button("Invert")) {
                invert_layer_mask(active_layer, document_.width, document_.height);
                texture_dirty_ = true;
            }
            tooltip("Invert the active layer mask");
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                active_layer.mask.clear();
                active_layer.mask_enabled = false;
                if (edit_layer_mask_) {
                    edit_layer_mask_ = false;
                }
                texture_dirty_ = true;
            }
            tooltip("Remove the active layer mask");
            ImGui::SameLine();
            if (ImGui::Button("Select Mask")) {
                load_selection_from_layer_mask(document_.selection, active_layer, document_.width, document_.height);
            }
            tooltip("Load the active layer mask as a selection");
        }
    } else {
        ImGui::TextDisabled("No layers");
    }

    ImGui::SeparatorText("Layer Stack");
    const float stack_height = std::clamp(ImGui::GetContentRegionAvail().y - 68.0f, 180.0f, 360.0f);
    ImGui::BeginChild("LayerStack", ImVec2(0.0f, stack_height), true);
    for (int i = static_cast<int>(document_.layers.size()) - 1; i >= 0; --i) {
        ImGui::PushID(i);
        Layer& layer = document_.layers[static_cast<std::size_t>(i)];
        const bool selected = document_.active_layer == i;
        const float row_height = 58.0f;
        const float row_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 row_min = ImGui::GetCursorScreenPos();
        const ImVec2 row_max(row_min.x + row_width, row_min.y + row_height);
        const bool row_hovered = ImGui::IsMouseHoveringRect(row_min, row_max);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec4 selected_color = ImGui::GetStyleColorVec4(ImGuiCol_Header);
        const ImVec4 hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
        const ImVec4 border_color = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        if (selected || row_hovered) {
            draw_list->AddRectFilled(row_min,
                                     row_max,
                                     ImGui::ColorConvertFloat4ToU32(selected ? selected_color : hovered_color),
                                     3.0f);
        }
        draw_list->AddRect(row_min, row_max, ImGui::ColorConvertFloat4ToU32(border_color), 3.0f);
        ImGui::SetCursorScreenPos(ImVec2(row_min.x + 6.0f, row_min.y + 7.0f));

        bool row_visible = layer.visible;
        if (ImGui::Checkbox("##visible", &row_visible)) {
            layer.visible = row_visible;
            texture_dirty_ = true;
        }
        tooltip(layer.visible ? "Hide layer" : "Show layer");
        ImGui::SameLine();
        draw_layer_thumbnail(document_, i, ImVec2(42.0f, 42.0f));
        if (ImGui::IsItemClicked()) {
            document_.active_layer = i;
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        const ImVec2 name_min = ImGui::GetCursorScreenPos();
        const float name_width = std::max(36.0f, row_max.x - name_min.x - 8.0f);
        const float name_height = ImGui::GetTextLineHeight();
        if (ImGui::Selectable("##select", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(name_width, name_height))) {
            document_.active_layer = i;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::CalcTextSize(layer.name.c_str()).x > name_width) {
            ImGui::SetTooltip("%s", layer.name.c_str());
        }
        draw_list->PushClipRect(name_min, ImVec2(name_min.x + name_width, name_min.y + name_height), true);
        draw_list->AddText(name_min, ImGui::GetColorU32(ImGuiCol_Text), layer.name.c_str());
        draw_list->PopClipRect();
        ImGui::TextDisabled("Layer %d  |  %s  |  %.0f%%",
                            i + 1,
                            layer_blend_mode_name(layer.blend_mode),
                            static_cast<double>(layer.opacity * 100.0f));
        if (!layer.visible || layer.mask_enabled || layer.clip_to_below) {
            if (!layer.visible) {
                ImGui::TextDisabled("Hidden");
            }
            if (layer.mask_enabled) {
                if (!layer.visible) {
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("Mask");
            }
            if (layer.clip_to_below) {
                if (!layer.visible || layer.mask_enabled) {
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("Clipped");
            }
        }
        ImGui::EndGroup();
        if (row_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
            document_.active_layer = i;
        }
        ImGui::SetCursorScreenPos(row_min);
        ImGui::Dummy(ImVec2(row_width, row_height));
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("New", ImVec2(58.0f, 0.0f))) {
        document_.add_layer("Layer " + std::to_string(document_.layers.size() + 1));
        texture_dirty_ = true;
    }
    tooltip("Create a new layer above the stack");
    ImGui::SameLine();
    if (guarded_button("Duplicate", !has_layers, ImVec2(80.0f, 0.0f))) {
        document_.duplicate_layer(document_.active_layer);
        texture_dirty_ = true;
    }
    tooltip("Duplicate the selected layer");
    ImGui::SameLine();
    if (guarded_button("Delete", !has_layers, ImVec2(64.0f, 0.0f))) {
        texture_dirty_ = document_.remove_layer(document_.active_layer) || texture_dirty_;
    }
    tooltip("Delete the selected layer");

    if (guarded_button("Up", document_.active_layer >= static_cast<int>(document_.layers.size()) - 1, ImVec2(58.0f, 0.0f))) {
        document_.move_layer(document_.active_layer, document_.active_layer + 1);
        texture_dirty_ = true;
    }
    tooltip("Move the selected layer toward the top");
    ImGui::SameLine();
    if (guarded_button("Down", document_.active_layer <= 0, ImVec2(58.0f, 0.0f))) {
        document_.move_layer(document_.active_layer, document_.active_layer - 1);
        texture_dirty_ = true;
    }
    tooltip("Move the selected layer toward the bottom");
    ImGui::SameLine();
    if (guarded_button("Merge Down", document_.active_layer <= 0, ImVec2(96.0f, 0.0f))) {
        texture_dirty_ = document_.merge_layer_down(document_.active_layer) || texture_dirty_;
    }
    tooltip("Merge the selected layer into the layer below");
    ImGui::SameLine();
    if (guarded_button("Depth Map", depth_job_running_ || !has_layers, ImVec2(92.0f, 0.0f))) {
        depth_source_layer_ = document_.active_layer;
        depth_map_popup_requested_ = true;
        depth_map_open_ = true;
    }
    tooltip("Generate a depth map layer from the selected layer");

    ImGui::End();
}

void EditorApp::draw_timeline_panel() {
    bool open = settings_.show_animation_panel;
    if (!ImGui::Begin("Animation", &open)) {
        settings_.show_animation_panel = open;
        ImGui::End();
        return;
    }
    settings_.show_animation_panel = open;

    if (document_.frames.empty()) {
        ImGui::TextDisabled("No frames");
        ImGui::End();
        return;
    }
    document_.active_frame = std::clamp(document_.active_frame, 0, static_cast<int>(document_.frames.size()) - 1);
    constexpr float kTimelineMinClipWidth = 44.0f;
    constexpr float kTimelineBaseMinZoom = 80.0f;
    constexpr float kTimelineBaseMaxZoom = 520.0f;
    auto shortest_frame_duration_ms = [&]() {
        int shortest = std::numeric_limits<int>::max();
        for (const Frame& frame : document_.frames) {
            shortest = std::min(shortest, std::max(1, frame.duration_ms));
        }
        return std::max(1, shortest);
    };
    auto minimum_timeline_zoom = [&]() {
        return std::max(kTimelineBaseMinZoom,
                        std::ceil(kTimelineMinClipWidth * 1000.0f /
                                  static_cast<float>(shortest_frame_duration_ms())));
    };
    auto maximum_timeline_zoom = [&]() {
        return std::max(kTimelineBaseMaxZoom, minimum_timeline_zoom());
    };
    auto timeline_quantum_ms = [&]() {
        return std::max(1, static_cast<int>(std::round(1000.0f / static_cast<float>(animation_timeline_fps_))));
    };
    auto quantize_timeline_delta = [&](float delta_ms) {
        if (ImGui::GetIO().KeyShift) {
            return static_cast<int>(std::round(delta_ms));
        }
        const int quantum = timeline_quantum_ms();
        return static_cast<int>(std::round(delta_ms / static_cast<float>(quantum))) * quantum;
    };
    auto clamp_timeline_zoom = [&]() {
        animation_timeline_pixels_per_second_ = std::clamp(animation_timeline_pixels_per_second_,
                                                           minimum_timeline_zoom(),
                                                           maximum_timeline_zoom());
    };
    auto begin_timeline_trim = [&](int frame_index, int neighbor_index) {
        timeline_trim_frame_ = frame_index;
        timeline_trim_neighbor_frame_ = neighbor_index;
        timeline_trim_start_duration_ms_ = document_.frames[static_cast<std::size_t>(frame_index)].duration_ms;
        timeline_trim_neighbor_start_duration_ms_ = neighbor_index >= 0
                                                       ? document_.frames[static_cast<std::size_t>(neighbor_index)].duration_ms
                                                       : 0;
        timeline_trim_start_mouse_x_ = ImGui::GetIO().MousePos.x;
    };
    auto apply_timeline_trim = [&](int frame_index, int neighbor_index) {
        constexpr int kMinFrameDurationMs = 20;
        if (timeline_trim_frame_ != frame_index || timeline_trim_neighbor_frame_ != neighbor_index) {
            begin_timeline_trim(frame_index, neighbor_index);
        }
        int delta_ms = quantize_timeline_delta((ImGui::GetIO().MousePos.x - timeline_trim_start_mouse_x_) *
                                               1000.0f / animation_timeline_pixels_per_second_);
        if (neighbor_index >= 0) {
            const int min_delta = kMinFrameDurationMs - timeline_trim_neighbor_start_duration_ms_;
            const int max_delta = timeline_trim_start_duration_ms_ - kMinFrameDurationMs;
            delta_ms = std::clamp(delta_ms, min_delta, max_delta);
            document_.frames[static_cast<std::size_t>(neighbor_index)].duration_ms =
                timeline_trim_neighbor_start_duration_ms_ + delta_ms;
            document_.frames[static_cast<std::size_t>(frame_index)].duration_ms =
                timeline_trim_start_duration_ms_ - delta_ms;
        } else {
            document_.frames[static_cast<std::size_t>(frame_index)].duration_ms =
                std::clamp(timeline_trim_start_duration_ms_ + delta_ms, kMinFrameDurationMs, 10000);
        }
        document_.active_frame = frame_index;
        clamp_timeline_zoom();
    };
    animation_timeline_fps_ = std::clamp(animation_timeline_fps_, 1, 60);
    clamp_timeline_zoom();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        timeline_trim_frame_ = -1;
        timeline_trim_neighbor_frame_ = -1;
    }

    auto frame_start_ms = [&](int frame_index) {
        int total = 0;
        for (int i = 0; i < frame_index && i < static_cast<int>(document_.frames.size()); ++i) {
            total += std::max(1, document_.frames[static_cast<std::size_t>(i)].duration_ms);
        }
        return total;
    };
    auto time_x = [&](float lane_x, int ms) {
        return lane_x + static_cast<float>(ms) * animation_timeline_pixels_per_second_ / 1000.0f;
    };
    auto format_time = [](int ms) {
        char value[32];
        const int seconds = ms / 1000;
        const int millis = ms % 1000;
        std::snprintf(value, sizeof(value), "%02d:%02d.%03d", seconds / 60, seconds % 60, millis);
        return std::string(value);
    };
    auto add_cue_at_active_frame = [&]() {
        const std::string name = "Cue " + std::to_string(document_.tags.size() + 1);
        document_.add_tag(name, document_.active_frame, document_.active_frame);
    };
    auto move_cue_to_frame = [&](int cue_index, int frame_index) {
        if (cue_index < 0 || cue_index >= static_cast<int>(document_.tags.size())) {
            return;
        }
        AnimationTag& cue = document_.tags[static_cast<std::size_t>(cue_index)];
        const int length = std::max(0, cue.to - cue.from);
        const int last = static_cast<int>(document_.frames.size()) - 1;
        cue.from = std::clamp(frame_index, 0, last);
        cue.to = std::clamp(cue.from + length, cue.from, last);
    };

    ImGuiIO& io = ImGui::GetIO();
    const bool animation_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool animation_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    const bool can_use_animation_shortcuts = animation_focused && !io.WantTextInput && !ImGui::IsAnyItemActive();
    if (can_use_animation_shortcuts && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        playing_ = !playing_;
        playback_direction_ = 1;
    }
    if (can_use_animation_shortcuts && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        playing_ = false;
        playback_accum_ = 0.0f;
    }
    if (can_use_animation_shortcuts && ImGui::IsKeyPressed(ImGuiKey_P)) {
        document_.playback_mode = document_.playback_mode == PlaybackMode::PingPong ? PlaybackMode::Loop : PlaybackMode::PingPong;
        playback_direction_ = 1;
    }
    if (can_use_animation_shortcuts && ImGui::IsKeyPressed(ImGuiKey_N)) {
        document_.add_frame(false);
        texture_dirty_ = true;
    }
    if (can_use_animation_shortcuts && (shortcut_ctrl_or_super(ImGuiKey_C) || shortcut_ctrl_or_super(ImGuiKey_V))) {
        document_.duplicate_frame(document_.active_frame);
        texture_dirty_ = true;
    }
    if (can_use_animation_shortcuts && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        texture_dirty_ = document_.remove_frame(document_.active_frame) || texture_dirty_;
    }
    if (can_use_animation_shortcuts && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        document_.move_frame(document_.active_frame, std::max(0, document_.active_frame - 1));
        texture_dirty_ = true;
    } else if (can_use_animation_shortcuts && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        document_.move_frame(document_.active_frame, std::min(static_cast<int>(document_.frames.size()) - 1, document_.active_frame + 1));
        texture_dirty_ = true;
    } else if (can_use_animation_shortcuts && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        document_.active_frame = std::max(0, document_.active_frame - 1);
        playback_accum_ = 0.0f;
        texture_dirty_ = true;
    } else if (can_use_animation_shortcuts && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        document_.active_frame = std::min(static_cast<int>(document_.frames.size()) - 1, document_.active_frame + 1);
        playback_accum_ = 0.0f;
        texture_dirty_ = true;
    }
    if (can_use_animation_shortcuts && ImGui::IsKeyPressed(ImGuiKey_M)) {
        add_cue_at_active_frame();
    }

    int total_ms = 0;
    for (const Frame& frame : document_.frames) {
        total_ms += std::max(1, frame.duration_ms);
    }
    ImGui::TextDisabled("Frame %d/%d  |  %s / %s",
                        document_.active_frame + 1,
                        static_cast<int>(document_.frames.size()),
                        format_time(frame_start_ms(document_.active_frame)).c_str(),
                        format_time(total_ms).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| %s", playback_mode_name(document_.playback_mode));
    ImGui::SameLine();
    ImGui::Checkbox("Onion Skin", &onion_skin_);

    const float timeline_height = 158.0f;
    ImGui::BeginChild("Timeline", ImVec2(0.0f, timeline_height), true, ImGuiWindowFlags_HorizontalScrollbar);
    if ((animation_focused || animation_hovered) && io.MouseWheel != 0.0f) {
        if (action_modifier_down(io)) {
            const float factor = std::pow(1.12f, io.MouseWheel);
            animation_timeline_pixels_per_second_ *= factor;
            clamp_timeline_zoom();
        } else if (io.KeyShift) {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * 64.0f);
        }
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const float lane_x = canvas_min.x + 12.0f;
    const float ruler_y = canvas_min.y + 16.0f;
    const float cue_y = canvas_min.y + 34.0f;
    const float frame_y = canvas_min.y + 72.0f;
    const float frame_h = 48.0f;
    const float total_width = std::max(ImGui::GetContentRegionAvail().x - 24.0f,
                                       static_cast<float>(total_ms) * animation_timeline_pixels_per_second_ / 1000.0f + 24.0f);

    const ImU32 ruler_color = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim_text_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw_list->AddLine(ImVec2(lane_x, ruler_y), ImVec2(lane_x + total_width, ruler_y), ruler_color, 1.0f);
    const int tick_step_ms = animation_timeline_pixels_per_second_ >= 260.0f ? 500 : 1000;
    for (int tick = 0; tick <= total_ms + tick_step_ms; tick += tick_step_ms) {
        const float x = time_x(lane_x, tick);
        const bool major = tick % 1000 == 0;
        draw_list->AddLine(ImVec2(x, ruler_y - (major ? 6.0f : 3.0f)),
                           ImVec2(x, frame_y + frame_h + 10.0f),
                           major ? ruler_color : ImGui::GetColorU32(ImGuiCol_Separator),
                           major ? 1.0f : 0.5f);
        if (major) {
            const std::string label = format_time(tick);
            draw_list->AddText(ImVec2(x + 4.0f, canvas_min.y + 1.0f), dim_text_color, label.c_str());
        }
    }

    constexpr std::array<ImU32, 6> tag_colors = {
        IM_COL32(93, 150, 255, 185),
        IM_COL32(245, 174, 73, 185),
        IM_COL32(111, 207, 151, 185),
        IM_COL32(218, 112, 214, 185),
        IM_COL32(235, 91, 91, 185),
        IM_COL32(118, 214, 214, 185)
    };
    for (int tag_index = 0; tag_index < static_cast<int>(document_.tags.size()); ++tag_index) {
        const AnimationTag& tag = document_.tags[static_cast<std::size_t>(tag_index)];
        const int from = std::clamp(tag.from, 0, static_cast<int>(document_.frames.size()) - 1);
        const int to = std::clamp(tag.to, from, static_cast<int>(document_.frames.size()) - 1);
        const float x0 = time_x(lane_x, frame_start_ms(from));
        const float x1 = time_x(lane_x, frame_start_ms(to) + document_.frames[static_cast<std::size_t>(to)].duration_ms);
        const float y0 = cue_y + static_cast<float>(tag_index % 2) * 18.0f;
        const float cue_width = std::max(8.0f, x1 - x0);
        const ImU32 color = tag_colors[static_cast<std::size_t>(tag_index) % tag_colors.size()];
        draw_list->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + cue_width, y0 + 14.0f), color, 3.0f);
        draw_list->PushClipRect(ImVec2(x0 + 4.0f, y0), ImVec2(x0 + cue_width - 3.0f, y0 + 14.0f), true);
        draw_list->AddText(ImVec2(x0 + 4.0f, y0 - 1.0f), text_color, tag.name.c_str());
        draw_list->PopClipRect();
        ImGui::PushID(tag_index);
        ImGui::SetCursorScreenPos(ImVec2(x0, y0));
        ImGui::InvisibleButton("cue", ImVec2(cue_width, 14.0f));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("PX_TIMELINE_CUE", &tag_index, sizeof(tag_index));
            ImGui::Text("Move %s", tag.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            timeline_rename_cue_ = tag_index;
            copy_path(timeline_rename_cue_name_, sizeof(timeline_rename_cue_name_), tag.name);
            timeline_rename_popup_requested_ = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s\nDrag to move\nRight-click to rename", tag.name.c_str());
        }
        ImGui::PopID();
    }

    int cursor_ms = 0;
    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        ImGui::PushID(i);
        const int duration_ms = std::max(1, document_.frames[static_cast<std::size_t>(i)].duration_ms);
        const float x0 = time_x(lane_x, cursor_ms);
        const float width = std::max(44.0f, static_cast<float>(duration_ms) * animation_timeline_pixels_per_second_ / 1000.0f);
        const float x1 = x0 + width;
        const bool selected = i == document_.active_frame;
        const float body_x = x0 + (i > 0 ? 7.0f : 0.0f);
        const float body_width = std::max(1.0f, width - (i > 0 ? 14.0f : 7.0f));
        ImGui::SetCursorScreenPos(ImVec2(body_x, frame_y));
        ImGui::InvisibleButton("frame", ImVec2(body_width, frame_h));
        if (ImGui::IsItemClicked()) {
            document_.active_frame = i;
            playback_accum_ = 0.0f;
            texture_dirty_ = true;
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("PX_TIMELINE_FRAME", &i, sizeof(i));
            ImGui::Text("Move Frame %d", i + 1);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PX_TIMELINE_FRAME")) {
                const int source_frame = *static_cast<const int*>(payload->Data);
                if (source_frame != i &&
                    source_frame >= 0 &&
                    source_frame < static_cast<int>(document_.frames.size())) {
                    document_.move_frame(source_frame, i);
                    document_.active_frame = i;
                    playback_accum_ = 0.0f;
                    texture_dirty_ = true;
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PX_TIMELINE_CUE")) {
                const int cue_index = *static_cast<const int*>(payload->Data);
                move_cue_to_frame(cue_index, i);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            const int hover_hold = std::max(1, static_cast<int>(std::round(static_cast<float>(duration_ms) *
                                                                           static_cast<float>(animation_timeline_fps_) / 1000.0f)));
            ImGui::SetTooltip("Frame %d\nStart %s\nHold %d frame%s\nDrag to move",
                              i + 1,
                              format_time(cursor_ms).c_str(),
                              hover_hold,
                              hover_hold == 1 ? "" : "s");
        }

        if (i > 0) {
            ImGui::SetCursorScreenPos(ImVec2(x0, frame_y));
            ImGui::InvisibleButton("trim_left", ImVec2(7.0f, frame_h));
            if (ImGui::IsItemActivated()) {
                begin_timeline_trim(i, i - 1);
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                apply_timeline_trim(i, i - 1);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Adjacent trim\nShift: fine trim");
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(x1 - 7.0f, frame_y));
        ImGui::InvisibleButton("trim", ImVec2(7.0f, frame_h));
        const bool adjacent_right_trim = action_modifier_down(ImGui::GetIO()) && i + 1 < static_cast<int>(document_.frames.size());
        if (ImGui::IsItemActivated()) {
            begin_timeline_trim(adjacent_right_trim ? i + 1 : i, adjacent_right_trim ? i : -1);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const int trim_frame = timeline_trim_frame_ >= 0 ? timeline_trim_frame_ : i;
            const int trim_neighbor = timeline_trim_frame_ >= 0 ? timeline_trim_neighbor_frame_ : -1;
            apply_timeline_trim(trim_frame, trim_neighbor);
            if (trim_neighbor == i) {
                document_.active_frame = i;
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Drag to trim frame hold\nCtrl/Cmd: adjacent trim\nShift: fine trim");
        }

        const ImU32 fill = selected ? ImGui::GetColorU32(ImGuiCol_HeaderActive)
                                    : ImGui::GetColorU32(ImGuiCol_Header);
        const ImU32 border = selected ? IM_COL32(255, 214, 92, 255)
                                      : ImGui::GetColorU32(ImGuiCol_Border);
        draw_list->AddRectFilled(ImVec2(x0, frame_y), ImVec2(x1 - 1.0f, frame_y + frame_h), fill, 4.0f);
        draw_list->AddRect(ImVec2(x0, frame_y), ImVec2(x1 - 1.0f, frame_y + frame_h), border, 4.0f, 0, selected ? 2.0f : 1.0f);
        draw_list->AddRectFilled(ImVec2(x1 - 7.0f, frame_y + 4.0f), ImVec2(x1 - 3.0f, frame_y + frame_h - 4.0f), border, 2.0f);

        char label[64];
        const int hold = std::max(1, static_cast<int>(std::round(static_cast<float>(duration_ms) *
                                                                 static_cast<float>(animation_timeline_fps_) / 1000.0f)));
        std::snprintf(label, sizeof(label), "F%d", i + 1);
        draw_list->AddText(ImVec2(x0 + 8.0f, frame_y + 7.0f), text_color, label);
        std::snprintf(label, sizeof(label), "%d fr", hold);
        draw_list->AddText(ImVec2(x0 + 8.0f, frame_y + 25.0f), dim_text_color, label);

        cursor_ms += duration_ms;
        ImGui::PopID();
    }
    const float playhead_x = time_x(lane_x, frame_start_ms(document_.active_frame));
    draw_list->AddLine(ImVec2(playhead_x, ruler_y - 10.0f),
                       ImVec2(playhead_x, frame_y + frame_h + 16.0f),
                       IM_COL32(255, 214, 92, 255),
                       2.0f);
    draw_list->AddTriangleFilled(ImVec2(playhead_x, ruler_y - 10.0f),
                                 ImVec2(playhead_x - 5.0f, ruler_y - 1.0f),
                                 ImVec2(playhead_x + 5.0f, ruler_y - 1.0f),
                                 IM_COL32(255, 214, 92, 255));
    ImGui::SetCursorScreenPos(canvas_min);
    ImGui::Dummy(ImVec2(total_width + 32.0f, timeline_height - 32.0f));
    ImGui::EndChild();

    if (timeline_rename_popup_requested_) {
        ImGui::OpenPopup("Rename Cue");
        timeline_rename_popup_requested_ = false;
    }
    if (ImGui::BeginPopup("Rename Cue")) {
        if (timeline_rename_cue_ >= 0 && timeline_rename_cue_ < static_cast<int>(document_.tags.size())) {
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Name", timeline_rename_cue_name_, sizeof(timeline_rename_cue_name_), ImGuiInputTextFlags_EnterReturnsTrue)) {
                document_.tags[static_cast<std::size_t>(timeline_rename_cue_)].name = timeline_rename_cue_name_;
                timeline_rename_cue_ = -1;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Apply", ImVec2(72.0f, 0.0f))) {
                document_.tags[static_cast<std::size_t>(timeline_rename_cue_)].name = timeline_rename_cue_name_;
                timeline_rename_cue_ = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(72.0f, 0.0f))) {
                timeline_rename_cue_ = -1;
                ImGui::CloseCurrentPopup();
            }
        } else {
            timeline_rename_cue_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}

void EditorApp::draw_adjustments_panel() {
    bool open = settings_.show_adjustments_panel;
    if (!ImGui::Begin("Adjustments", &open)) {
        settings_.show_adjustments_panel = open;
        ImGui::End();
        return;
    }
    settings_.show_adjustments_panel = open;
    draw_histogram_plot();
    auto apply_button = [&](const char* label, EffectPreviewKind kind, float width = 0.0f) {
        if (ImGui::Button(label, ImVec2(width, 0.0f))) {
            apply_effect_to_document(kind);
        }
    };
    auto preview_button = [&](const char* label, EffectPreviewKind kind, float width = 0.0f) {
        if (ImGui::Button(label, ImVec2(width, 0.0f))) {
            start_effect_preview(kind);
        }
    };
    auto apply_cell = [&](const char* label, EffectPreviewKind kind) {
        ImGui::TableNextColumn();
        apply_button(label, kind, -1.0f);
    };
    auto preview_cell = [&](const char* label, EffectPreviewKind kind) {
        ImGui::TableNextColumn();
        preview_button(label, kind, -1.0f);
    };

    if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("LightActions", 2, ImGuiTableFlags_SizingStretchSame)) {
            preview_cell("Brightness / Contrast", EffectPreviewKind::BrightnessContrast);
            preview_cell("Tonal Range", EffectPreviewKind::TonalRange);
            preview_cell("Curves", EffectPreviewKind::Curves);
            apply_cell("Auto-Level", EffectPreviewKind::AutoLevel);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("ColorActions", 3, ImGuiTableFlags_SizingStretchSame)) {
            preview_cell("Hue / Saturation", EffectPreviewKind::Hsv);
            preview_cell("Warmth / Coolness", EffectPreviewKind::Temperature);
            apply_cell("B&W", EffectPreviewKind::Grayscale);
            apply_cell("Sepia", EffectPreviewKind::Sepia);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Levels", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("LevelsActions", 2, ImGuiTableFlags_SizingStretchSame)) {
            preview_cell("Levels", EffectPreviewKind::Levels);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Detail", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("DetailActions", 3, ImGuiTableFlags_SizingStretchSame)) {
            preview_cell("Blur", EffectPreviewKind::GaussianBlur);
            preview_cell("Depth of Field", EffectPreviewKind::DepthOfField);
            preview_cell("Sharpen", EffectPreviewKind::Sharpen);
            preview_cell("Reduce Noise", EffectPreviewKind::ReduceNoise);
            preview_cell("Surface Blur", EffectPreviewKind::SurfaceBlur);
            preview_cell("Glow", EffectPreviewKind::Glow);
            preview_cell("Edge Detect", EffectPreviewKind::EdgeDetect);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("TransformActions", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            if (ImGui::Button("Straighten", ImVec2(-1.0f, 0.0f))) {
                start_straighten_tool();
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Rotate / Zoom", ImVec2(-1.0f, 0.0f))) {
                rotate_zoom_angle_ = static_cast<float>(effect_angle_);
                rotate_zoom_zoom_ = effect_zoom_;
                rotate_zoom_pan_x_ = 0;
                rotate_zoom_pan_y_ = 0;
                rotate_zoom_preview_dirty_ = true;
                rotate_zoom_popup_requested_ = true;
            }
            preview_cell("Motion Blur", EffectPreviewKind::MotionBlur);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Creative")) {
        if (ImGui::BeginTable("CreativeActions", 3, ImGuiTableFlags_SizingStretchSame)) {
            preview_cell("Pixelate", EffectPreviewKind::Pixelate);
            preview_cell("Vignette", EffectPreviewKind::Vignette);
            preview_cell("Twist", EffectPreviewKind::Twist);
            preview_cell("Add Noise", EffectPreviewKind::AddNoise);
            preview_cell("Clouds", EffectPreviewKind::Clouds);
            preview_cell("Turbulence", EffectPreviewKind::Turbulence);
            preview_cell("Emboss", EffectPreviewKind::Emboss);
            preview_cell("Posterize", EffectPreviewKind::Posterize);
            apply_cell("Quantize", EffectPreviewKind::PaletteQuantize);
            apply_cell("Dither", EffectPreviewKind::PaletteDither);
            apply_cell("Invert RGB", EffectPreviewKind::InvertColors);
            apply_cell("Invert Alpha", EffectPreviewKind::InvertAlpha);
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void EditorApp::start_effect_preview(EffectPreviewKind kind) {
    if (kind == EffectPreviewKind::None) {
        return;
    }
    if (kind == EffectPreviewKind::DepthOfField && !valid_depth_of_field_layer(document_)) {
        depth_of_field_layer_ = default_depth_of_field_layer();
    }
    effect_preview_kind_ = kind;
    effect_preview_active_ = true;
    effect_preview_dirty_ = true;
    effect_preview_popup_requested_ = true;
    rebuild_effect_preview();
    set_status(std::string("Previewing ") + effect_preview_name(kind));
}

GpuEffectRequest EditorApp::gpu_effect_request(EffectPreviewKind kind) const {
    GpuEffectRequest request;
    request.primary = tool_.primary;
    request.secondary = tool_.secondary;
    switch (kind) {
        case EffectPreviewKind::InkSketch:
            request.mode = GpuEffectMode::InkSketch;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::OilPainting:
            request.mode = GpuEffectMode::OilPainting;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_cell_size_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::PencilSketch:
            request.mode = GpuEffectMode::PencilSketch;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::GaussianBlur:
            request.mode = GpuEffectMode::GaussianBlur;
            request.params = {static_cast<float>(effect_radius_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::DepthOfField:
            request.mode = GpuEffectMode::DepthOfField;
            request.params = {static_cast<float>(std::clamp(depth_of_field_focus_, 0, 255)) / 255.0f,
                              static_cast<float>(std::clamp(depth_of_field_aperture_, 0, 100)) / 100.0f,
                              static_cast<float>(std::clamp(depth_of_field_falloff_, 1, 100)),
                              static_cast<float>(std::clamp(depth_of_field_max_radius_, 1, 32))};
            if (const std::vector<Pixel>* depth = depth_of_field_pixels(document_)) {
                request.depth_pixels = *depth;
            }
            break;
        case EffectPreviewKind::MotionBlur:
            request.mode = GpuEffectMode::MotionBlur;
            request.params = {static_cast<float>(effect_radius_), 0.0f, radians(static_cast<float>(effect_angle_)), 0.0f};
            break;
        case EffectPreviewKind::RadialBlur:
            request.mode = GpuEffectMode::RadialBlur;
            request.params = {0.0f, static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::ZoomBlur:
            request.mode = GpuEffectMode::ZoomBlur;
            request.params = {0.0f, static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::MedianBlur:
            request.mode = GpuEffectMode::MedianBlur;
            request.params = {static_cast<float>(effect_radius_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::SurfaceBlur:
            request.mode = GpuEffectMode::SurfaceBlur;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::BrightnessContrast:
            request.mode = GpuEffectMode::BrightnessContrast;
            request.params = {static_cast<float>(brightness_) / 255.0f, static_cast<float>(contrast_) / 255.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Hsv:
            request.mode = GpuEffectMode::Hsv;
            request.params = {hue_ / 360.0f, saturation_, value_, 0.0f};
            break;
        case EffectPreviewKind::Temperature:
            request.mode = GpuEffectMode::Temperature;
            request.params = {static_cast<float>(temperature_) / 100.0f, 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Levels:
            request.mode = GpuEffectMode::Levels;
            request.params = {static_cast<float>(levels_.in_black) / 255.0f,
                              static_cast<float>(levels_.in_white) / 255.0f,
                              levels_.gamma,
                              static_cast<float>(levels_.out_black) / 255.0f};
            request.params2 = {static_cast<float>(levels_.out_white) / 255.0f, 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::TonalRange:
            request.mode = GpuEffectMode::TonalRange;
            request.params = {static_cast<float>(white_point_) / 100.0f,
                              static_cast<float>(highlights_) / 100.0f,
                              static_cast<float>(shadows_) / 100.0f,
                              static_cast<float>(black_point_) / 100.0f};
            break;
        case EffectPreviewKind::Curves:
            request.mode = GpuEffectMode::Curves;
            request.params2 = {histogram_luma_visible_ ? 1.0f : 0.0f,
                               histogram_red_visible_ ? 1.0f : 0.0f,
                               histogram_green_visible_ ? 1.0f : 0.0f,
                               histogram_blue_visible_ ? 1.0f : 0.0f};
            request.curve_x = curves_.x;
            request.curve_y = curves_.y;
            request.curve_point_count = std::clamp(curves_.point_count, 2, kMaxCurvePoints);
            break;
        case EffectPreviewKind::PaletteQuantize:
            request.mode = GpuEffectMode::PaletteQuantize;
            request.params = {static_cast<float>(std::max(2, static_cast<int>(document_.palette.colors.size()))), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::PaletteDither:
            request.mode = GpuEffectMode::PaletteDither;
            request.params = {static_cast<float>(std::max(2, static_cast<int>(document_.palette.colors.size()))), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::AutoLevel:
            request.mode = GpuEffectMode::AutoLevel;
            break;
        case EffectPreviewKind::Grayscale:
            request.mode = GpuEffectMode::Grayscale;
            break;
        case EffectPreviewKind::Sepia:
            request.mode = GpuEffectMode::Sepia;
            break;
        case EffectPreviewKind::InvertColors:
            request.mode = GpuEffectMode::InvertColors;
            break;
        case EffectPreviewKind::InvertAlpha:
            request.mode = GpuEffectMode::InvertAlpha;
            break;
        case EffectPreviewKind::Posterize:
            request.mode = GpuEffectMode::Posterize;
            request.params = {static_cast<float>(posterize_levels_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Bulge:
            request.mode = GpuEffectMode::Bulge;
            request.params = {effect_strength_, 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Crystalize:
            request.mode = GpuEffectMode::Crystalize;
            request.params = {static_cast<float>(effect_cell_size_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Dents:
            request.mode = GpuEffectMode::Dents;
            request.params = {static_cast<float>(effect_scale_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::FrostedGlass:
            request.mode = GpuEffectMode::FrostedGlass;
            request.params = {static_cast<float>(effect_radius_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Pixelate:
            request.mode = GpuEffectMode::Pixelate;
            request.params = {static_cast<float>(effect_cell_size_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::PolarInversion:
            request.mode = GpuEffectMode::PolarInversion;
            request.params = {effect_zoom_, 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::TileReflection:
            request.mode = GpuEffectMode::TileReflection;
            request.params = {static_cast<float>(effect_cell_size_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Twist:
            request.mode = GpuEffectMode::Twist;
            request.params = {effect_strength_, 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::AddNoise:
            request.mode = GpuEffectMode::AddNoise;
            request.params = {static_cast<float>(effect_noise_), static_cast<float>(effect_amount_), static_cast<float>(effect_amount_), 0.0f};
            break;
        case EffectPreviewKind::ReduceNoise:
            request.mode = GpuEffectMode::ReduceNoise;
            request.params = {static_cast<float>(effect_radius_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Feather:
            request.mode = GpuEffectMode::SurfaceBlur;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Outline:
            request.mode = GpuEffectMode::Outline;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Glow:
            request.mode = GpuEffectMode::Glow;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::RedEyeRemoval:
            request.mode = GpuEffectMode::RedEyeRemoval;
            request.params = {0.0f, static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Sharpen:
            request.mode = GpuEffectMode::Sharpen;
            request.params = {0.0f, static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::SoftenPortrait:
            request.mode = GpuEffectMode::SoftenPortrait;
            request.params = {static_cast<float>(effect_radius_), static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Vignette:
            request.mode = GpuEffectMode::Vignette;
            request.params = {0.0f, static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Clouds:
            request.mode = GpuEffectMode::Clouds;
            request.params = {static_cast<float>(effect_scale_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::JuliaFractal:
            request.mode = GpuEffectMode::JuliaFractal;
            request.params = {effect_zoom_, 0.0f, radians(static_cast<float>(effect_angle_)), 0.0f};
            break;
        case EffectPreviewKind::MandelbrotFractal:
            request.mode = GpuEffectMode::MandelbrotFractal;
            request.params = {effect_zoom_, 0.0f, radians(static_cast<float>(effect_angle_)), 0.0f};
            break;
        case EffectPreviewKind::Turbulence:
            request.mode = GpuEffectMode::Turbulence;
            request.params = {static_cast<float>(effect_scale_), 0.0f, 0.0f, 0.0f};
            break;
        case EffectPreviewKind::EdgeDetect:
            request.mode = GpuEffectMode::EdgeDetect;
            request.params = {0.0f, static_cast<float>(effect_amount_), 0.0f, 0.0f};
            break;
        case EffectPreviewKind::Emboss:
            request.mode = GpuEffectMode::Emboss;
            request.params = {0.0f, 0.0f, radians(static_cast<float>(effect_angle_)), 0.0f};
            break;
        case EffectPreviewKind::Relief:
            request.mode = GpuEffectMode::Relief;
            request.params = {0.0f, 0.0f, radians(static_cast<float>(effect_angle_)), 0.0f};
            break;
        case EffectPreviewKind::None:
            break;
    }
    return request;
}

bool EditorApp::try_gpu_effect(EffectPreviewKind kind, std::vector<Pixel>& out_pixels) {
    if (!settings_.heavy_gpu_optimization || kind == EffectPreviewKind::None) {
        return false;
    }
    if (!document_.has_active_cel()) {
        return false;
    }
    if (kind == EffectPreviewKind::DepthOfField && !valid_depth_of_field_layer(document_)) {
        return false;
    }
    if (try_mps_effect(kind, out_pixels)) {
        return true;
    }
    const GpuEffectRequest request = gpu_effect_request(kind);
    if (!gpu_effect_renderer_.render_active_cel(document_, request)) {
        if (!gpu_effect_renderer_.last_error().empty()) {
            report_error("Heavy GPU Optimization", gpu_effect_renderer_.last_error());
        }
        return false;
    }
    if (!gpu_effect_renderer_.read_output_pixels(out_pixels)) {
        report_error("Heavy GPU Optimization", "Could not read GPU effect output");
        return false;
    }
    return out_pixels.size() == document_.active_cel().pixels.size();
}

bool EditorApp::try_mps_effect(EffectPreviewKind kind, std::vector<Pixel>& out_pixels) {
    if (!settings_.heavy_gpu_optimization || !settings_.mps_backend || kind == EffectPreviewKind::None) {
        return false;
    }
    if (!document_.has_active_cel()) {
        return false;
    }
#if defined(__APPLE__)
    const GpuEffectRequest request = gpu_effect_request(kind);
    if (!mps_effect_renderer_.render_active_cel(document_, request)) {
        if (!mps_effect_renderer_.last_error().empty()) {
            report_error("MPS Backend", mps_effect_renderer_.last_error());
        }
        return false;
    }
    if (!mps_effect_renderer_.read_output_pixels(out_pixels)) {
        report_error("MPS Backend", "Could not read MPS effect output");
        return false;
    }
    return out_pixels.size() == document_.active_cel().pixels.size();
#else
    (void)out_pixels;
    return false;
#endif
}

bool EditorApp::try_gpu_affine_transform(float angle_degrees,
                                         float zoom,
                                         int pan_x,
                                         int pan_y,
                                         ResamplingMode resampling,
                                         std::vector<Pixel>& out_pixels) {
    if (!settings_.heavy_gpu_optimization || resampling == ResamplingMode::Bicubic) {
        return false;
    }
    if (!document_.has_active_cel()) {
        return false;
    }
    GpuEffectRequest request;
    request.mode = GpuEffectMode::AffineTransform;
    request.params = {radians(angle_degrees),
                      zoom,
                      static_cast<float>(pan_x),
                      static_cast<float>(pan_y)};
    request.params2 = {resampling == ResamplingMode::Bilinear ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    if (!gpu_effect_renderer_.render_active_cel(document_, request)) {
        if (!gpu_effect_renderer_.last_error().empty()) {
            report_error("Heavy GPU Optimization", gpu_effect_renderer_.last_error());
        }
        return false;
    }
    if (!gpu_effect_renderer_.read_output_pixels(out_pixels)) {
        report_error("Heavy GPU Optimization", "Could not read GPU transform output");
        return false;
    }
    return out_pixels.size() == document_.active_cel().pixels.size();
}

bool EditorApp::apply_affine_transform_to_document(const char* undo_name,
                                                   float angle_degrees,
                                                   float zoom,
                                                   int pan_x,
                                                   int pan_y,
                                                   ResamplingMode resampling) {
    if (!document_.has_active_cel()) {
        set_status("Create a layer to edit pixels");
        return false;
    }
    std::vector<Pixel> gpu_pixels;
    if (try_gpu_affine_transform(angle_degrees, zoom, pan_x, pan_y, resampling, gpu_pixels)) {
        document_.replace_active_pixels(std::move(gpu_pixels), undo_name);
        texture_dirty_ = true;
        set_status(std::string("Applied ") + undo_name + " using Heavy GPU Optimization");
        return true;
    }
    if (std::string_view(undo_name) == "Straighten") {
        apply_straighten(document_, angle_degrees, resampling);
    } else {
        apply_rotate_zoom(document_, angle_degrees, zoom, pan_x, pan_y, resampling);
    }
    texture_dirty_ = true;
    set_status(std::string("Applied ") + undo_name);
    return true;
}

bool EditorApp::apply_effect_to_document(EffectPreviewKind kind) {
    if (kind == EffectPreviewKind::None) {
        return false;
    }
    if (!document_.has_active_cel()) {
        set_status("Create a layer to edit pixels");
        return false;
    }
    if (kind == EffectPreviewKind::DepthOfField && !valid_depth_of_field_layer(document_)) {
        report_error("Depth of Field", "Select a depth-map layer that matches the current document.");
        return false;
    }
    std::vector<Pixel> gpu_pixels;
    if (try_gpu_effect(kind, gpu_pixels)) {
        document_.replace_active_pixels(std::move(gpu_pixels), effect_preview_name(kind));
        texture_dirty_ = true;
        set_status(std::string("Applied ") + effect_preview_name(kind) + " using Heavy GPU Optimization");
        return true;
    }

    const EffectPreviewKind previous_kind = effect_preview_kind_;
    effect_preview_kind_ = kind;
    apply_effect_to(document_);
    effect_preview_kind_ = previous_kind;
    texture_dirty_ = true;
    set_status(std::string("Applied ") + effect_preview_name(kind));
    return true;
}

void EditorApp::apply_effect_to(Document& target) const {
    switch (effect_preview_kind_) {
        case EffectPreviewKind::InkSketch:
            apply_ink_sketch(target, effect_amount_, effect_amount_);
            break;
        case EffectPreviewKind::OilPainting:
            apply_oil_painting(target, effect_radius_, effect_cell_size_);
            break;
        case EffectPreviewKind::PencilSketch:
            apply_pencil_sketch(target, effect_radius_, effect_amount_);
            break;
        case EffectPreviewKind::GaussianBlur:
            apply_gaussian_blur(target, effect_radius_);
            break;
        case EffectPreviewKind::DepthOfField:
            if (const std::vector<Pixel>* depth = depth_of_field_pixels(target)) {
                DepthOfFieldSettings settings;
                settings.focus_depth = depth_of_field_focus_;
                settings.aperture = depth_of_field_aperture_;
                settings.falloff = depth_of_field_falloff_;
                settings.max_radius = depth_of_field_max_radius_;
                apply_depth_of_field(target, *depth, settings);
            }
            break;
        case EffectPreviewKind::MotionBlur:
            apply_motion_blur(target, effect_radius_, static_cast<float>(effect_angle_));
            break;
        case EffectPreviewKind::RadialBlur:
            apply_radial_blur(target, effect_amount_);
            break;
        case EffectPreviewKind::ZoomBlur:
            apply_zoom_blur(target, effect_amount_);
            break;
        case EffectPreviewKind::MedianBlur:
            apply_median_blur(target, effect_radius_);
            break;
        case EffectPreviewKind::SurfaceBlur:
            apply_surface_blur(target, effect_radius_, effect_amount_);
            break;
        case EffectPreviewKind::BrightnessContrast:
            apply_brightness_contrast(target, brightness_, contrast_);
            break;
        case EffectPreviewKind::Hsv:
            apply_hsv(target, hue_, saturation_, value_);
            break;
        case EffectPreviewKind::Temperature:
            apply_temperature(target, temperature_);
            break;
        case EffectPreviewKind::Levels:
            apply_levels(target, levels_);
            break;
        case EffectPreviewKind::TonalRange:
            apply_tonal_range(target, white_point_, highlights_, shadows_, black_point_);
            break;
        case EffectPreviewKind::Curves: {
            CurvesSettings settings = curves_;
            settings.luma = histogram_luma_visible_;
            settings.red = histogram_red_visible_;
            settings.green = histogram_green_visible_;
            settings.blue = histogram_blue_visible_;
            apply_curves(target, settings);
            break;
        }
        case EffectPreviewKind::PaletteQuantize:
            apply_palette_quantize(target, target.palette, false);
            break;
        case EffectPreviewKind::PaletteDither:
            apply_palette_quantize(target, target.palette, true);
            break;
        case EffectPreviewKind::AutoLevel:
            apply_auto_level(target);
            break;
        case EffectPreviewKind::Grayscale:
            apply_grayscale(target);
            break;
        case EffectPreviewKind::Sepia:
            apply_sepia(target);
            break;
        case EffectPreviewKind::InvertColors:
            apply_invert(target, false);
            break;
        case EffectPreviewKind::InvertAlpha:
            apply_invert(target, true);
            break;
        case EffectPreviewKind::Posterize:
            apply_posterize(target, posterize_levels_);
            break;
        case EffectPreviewKind::Bulge:
            apply_bulge(target, effect_strength_);
            break;
        case EffectPreviewKind::Crystalize:
            apply_crystalize(target, effect_cell_size_);
            break;
        case EffectPreviewKind::Dents:
            apply_dents(target, effect_scale_, effect_amount_);
            break;
        case EffectPreviewKind::FrostedGlass:
            apply_frosted_glass(target, effect_radius_);
            break;
        case EffectPreviewKind::Pixelate:
            apply_pixelate(target, effect_cell_size_);
            break;
        case EffectPreviewKind::PolarInversion:
            apply_polar_inversion(target, effect_zoom_);
            break;
        case EffectPreviewKind::TileReflection:
            apply_tile_reflection(target, effect_cell_size_);
            break;
        case EffectPreviewKind::Twist:
            apply_twist(target, effect_strength_);
            break;
        case EffectPreviewKind::AddNoise:
            apply_add_noise(target, effect_noise_, effect_amount_, effect_amount_);
            break;
        case EffectPreviewKind::ReduceNoise:
            apply_reduce_noise(target, effect_radius_);
            break;
        case EffectPreviewKind::Feather:
            apply_surface_blur(target, effect_radius_, effect_amount_);
            break;
        case EffectPreviewKind::Outline:
            apply_outline(target, effect_radius_, effect_amount_);
            break;
        case EffectPreviewKind::Glow:
            apply_glow(target, effect_radius_, effect_amount_, effect_amount_);
            break;
        case EffectPreviewKind::RedEyeRemoval:
            apply_red_eye_removal(target, effect_amount_);
            break;
        case EffectPreviewKind::Sharpen:
            apply_sharpen(target, effect_amount_);
            break;
        case EffectPreviewKind::SoftenPortrait:
            apply_soften_portrait(target, effect_radius_, effect_amount_, effect_amount_ / 2);
            break;
        case EffectPreviewKind::Vignette:
            apply_vignette(target, effect_amount_, effect_amount_);
            break;
        case EffectPreviewKind::Clouds:
            apply_clouds(target, effect_scale_, 5, tool_.primary, tool_.secondary);
            break;
        case EffectPreviewKind::JuliaFractal:
            apply_julia_fractal(target, effect_zoom_, static_cast<float>(effect_angle_));
            break;
        case EffectPreviewKind::MandelbrotFractal:
            apply_mandelbrot_fractal(target, effect_zoom_, static_cast<float>(effect_angle_), false);
            break;
        case EffectPreviewKind::Turbulence:
            apply_turbulence(target, effect_scale_, 5);
            break;
        case EffectPreviewKind::EdgeDetect:
            apply_edge_detect(target, effect_amount_);
            break;
        case EffectPreviewKind::Emboss:
            apply_emboss(target, static_cast<float>(effect_angle_));
            break;
        case EffectPreviewKind::Relief:
            apply_relief(target, static_cast<float>(effect_angle_));
            break;
        case EffectPreviewKind::None:
            break;
    }
}

void EditorApp::rebuild_effect_preview() {
    if (!effect_preview_active_ || effect_preview_kind_ == EffectPreviewKind::None) {
        return;
    }
    if (!document_.has_active_cel()) {
        composite_ = document_.composite_active();
        composite_uses_active_cel_ = false;
        tiled_canvas_texture_.invalidate();
        canvas_texture_.update(document_.width, document_.height, composite_);
        full_canvas_texture_dirty_ = false;
        effect_preview_dirty_ = false;
        return;
    }
    effect_preview_document_ = document_;
    std::vector<Pixel> gpu_pixels;
    if (try_gpu_effect(effect_preview_kind_, gpu_pixels)) {
        effect_preview_document_.active_cel().pixels = std::move(gpu_pixels);
    } else {
        apply_effect_to(effect_preview_document_);
    }
    composite_ = effect_preview_document_.composite_active();
    composite_uses_active_cel_ = false;
    tiled_canvas_texture_.invalidate();
    canvas_texture_.update(effect_preview_document_.width, effect_preview_document_.height, composite_);
    full_canvas_texture_dirty_ = false;
    effect_preview_dirty_ = false;
}

void EditorApp::rebuild_rotate_zoom_preview() {
    rotate_zoom_preview_document_ = document_;
    if (!rotate_zoom_preview_document_.has_active_cel()) {
        auto preview = rotate_zoom_preview_document_.composite_active();
        transform_preview_texture_.update(rotate_zoom_preview_document_.width, rotate_zoom_preview_document_.height, preview);
        rotate_zoom_preview_dirty_ = false;
        return;
    }
    apply_rotate_zoom(rotate_zoom_preview_document_,
                      rotate_zoom_angle_,
                      rotate_zoom_zoom_,
                      rotate_zoom_pan_x_,
                      rotate_zoom_pan_y_,
                      resampling_mode_from_index(image_transform_resampling_));
    auto preview = rotate_zoom_preview_document_.composite_active();
    transform_preview_texture_.update(rotate_zoom_preview_document_.width, rotate_zoom_preview_document_.height, preview);
    rotate_zoom_preview_dirty_ = false;
}

void EditorApp::apply_effect_preview_to_document() {
    if (!effect_preview_active_ || effect_preview_kind_ == EffectPreviewKind::None) {
        return;
    }
    const std::string name = effect_preview_name(effect_preview_kind_);
    if (!apply_effect_to_document(effect_preview_kind_)) {
        set_status(std::string("Could not apply ") + name);
        return;
    }
}

void EditorApp::close_effect_preview(bool apply) {
    if (apply) {
        apply_effect_preview_to_document();
    } else if (effect_preview_kind_ != EffectPreviewKind::None) {
        set_status(std::string("Canceled ") + effect_preview_name(effect_preview_kind_));
    }
    effect_preview_active_ = false;
    effect_preview_dirty_ = false;
    effect_preview_popup_requested_ = false;
    effect_preview_kind_ = EffectPreviewKind::None;
    depth_of_field_pick_focus_ = false;
    texture_dirty_ = true;
    refresh_texture();
}

void EditorApp::draw_effect_preview_parameters() {
    bool changed = false;
    auto radius_slider = [&]() {
        changed |= ImGui::SliderInt("Radius", &effect_radius_, 1, 32);
    };
    auto amount_slider = [&]() {
        changed |= ImGui::SliderInt("Amount", &effect_amount_, 0, 100);
    };
    auto angle_slider = [&]() {
        changed |= ImGui::SliderInt("Angle", &effect_angle_, -180, 180);
    };
    auto cell_slider = [&]() {
        changed |= ImGui::SliderInt("Cell Size", &effect_cell_size_, 2, 128);
    };
    auto scale_slider = [&]() {
        changed |= ImGui::SliderInt("Scale", &effect_scale_, 2, 256);
    };
    auto noise_slider = [&]() {
        changed |= ImGui::SliderInt("Noise Intensity", &effect_noise_, 0, 255);
    };
    auto strength_slider = [&]() {
        changed |= ImGui::SliderFloat("Strength", &effect_strength_, -2.0f, 2.0f, "%.2f");
    };
    auto zoom_slider = [&]() {
        changed |= ImGui::SliderFloat("Zoom", &effect_zoom_, 0.1f, 8.0f, "%.2f");
    };

    switch (effect_preview_kind_) {
        case EffectPreviewKind::InkSketch:
            amount_slider();
            break;
        case EffectPreviewKind::OilPainting:
            radius_slider();
            cell_slider();
            break;
        case EffectPreviewKind::PencilSketch:
        case EffectPreviewKind::SurfaceBlur:
        case EffectPreviewKind::Feather:
        case EffectPreviewKind::Outline:
        case EffectPreviewKind::Glow:
        case EffectPreviewKind::SoftenPortrait:
            radius_slider();
            amount_slider();
            break;
        case EffectPreviewKind::GaussianBlur:
        case EffectPreviewKind::MedianBlur:
        case EffectPreviewKind::FrostedGlass:
        case EffectPreviewKind::ReduceNoise:
            radius_slider();
            break;
        case EffectPreviewKind::DepthOfField: {
            if (!document_.layers.empty()) {
                depth_of_field_layer_ = std::clamp(depth_of_field_layer_, 0, static_cast<int>(document_.layers.size()) - 1);
                const char* current_name = document_.layers[static_cast<std::size_t>(depth_of_field_layer_)].name.c_str();
                if (ImGui::BeginCombo("Depth Layer", current_name)) {
                    for (int i = static_cast<int>(document_.layers.size()) - 1; i >= 0; --i) {
                        const bool selected = depth_of_field_layer_ == i;
                        if (ImGui::Selectable(document_.layers[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                            depth_of_field_layer_ = i;
                            changed = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            changed |= ImGui::SliderInt("Focus Depth", &depth_of_field_focus_, 0, 255);
            changed |= ImGui::SliderInt("Aperture", &depth_of_field_aperture_, 0, 100);
            changed |= ImGui::SliderInt("Falloff", &depth_of_field_falloff_, 1, 100);
            changed |= ImGui::SliderInt("Max Radius", &depth_of_field_max_radius_, 1, 32);
            if (ImGui::Checkbox("Focus Eyedropper", &depth_of_field_pick_focus_)) {
                set_status(depth_of_field_pick_focus_ ? "Click the canvas to sample focus depth" : "Focus eyedropper disabled");
            }
            if (!valid_depth_of_field_layer(document_)) {
                ImGui::TextWrapped("Select a depth-map layer with the same size as the document.");
            }
            break;
        }
        case EffectPreviewKind::MotionBlur:
            radius_slider();
            angle_slider();
            break;
        case EffectPreviewKind::BrightnessContrast:
            changed |= ImGui::SliderInt("Brightness", &brightness_, -255, 255);
            changed |= ImGui::SliderInt("Contrast", &contrast_, -255, 255);
            break;
        case EffectPreviewKind::Hsv:
            changed |= ImGui::SliderFloat("Hue", &hue_, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::SliderFloat("Saturation", &saturation_, -1.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat("Value", &value_, -1.0f, 1.0f, "%.2f");
            break;
        case EffectPreviewKind::Temperature:
            changed |= ImGui::SliderInt("Temperature", &temperature_, -100, 100);
            break;
        case EffectPreviewKind::Levels:
            changed |= ImGui::SliderInt("Input Black", &levels_.in_black, 0, 254);
            changed |= ImGui::SliderInt("Input White", &levels_.in_white, 1, 255);
            changed |= ImGui::SliderFloat("Gamma", &levels_.gamma, 0.1f, 4.0f);
            changed |= ImGui::SliderInt("Output Black", &levels_.out_black, 0, 255);
            changed |= ImGui::SliderInt("Output White", &levels_.out_white, 0, 255);
            break;
        case EffectPreviewKind::TonalRange:
            changed |= ImGui::SliderInt("White Point", &white_point_, -100, 100);
            changed |= ImGui::SliderInt("Highlights", &highlights_, -100, 100);
            changed |= ImGui::SliderInt("Shadows", &shadows_, -100, 100);
            changed |= ImGui::SliderInt("Black Point", &black_point_, -100, 100);
            break;
        case EffectPreviewKind::Curves:
            changed |= draw_curves_editor();
            break;
        case EffectPreviewKind::RadialBlur:
        case EffectPreviewKind::ZoomBlur:
        case EffectPreviewKind::RedEyeRemoval:
        case EffectPreviewKind::Sharpen:
        case EffectPreviewKind::Vignette:
        case EffectPreviewKind::EdgeDetect:
            amount_slider();
            break;
        case EffectPreviewKind::Posterize:
            changed |= ImGui::SliderInt("Levels", &posterize_levels_, 2, 32);
            break;
        case EffectPreviewKind::Bulge:
        case EffectPreviewKind::Twist:
            strength_slider();
            break;
        case EffectPreviewKind::Crystalize:
        case EffectPreviewKind::Pixelate:
        case EffectPreviewKind::TileReflection:
            cell_slider();
            break;
        case EffectPreviewKind::Dents:
            scale_slider();
            amount_slider();
            break;
        case EffectPreviewKind::PolarInversion:
            zoom_slider();
            break;
        case EffectPreviewKind::AddNoise:
            noise_slider();
            amount_slider();
            break;
        case EffectPreviewKind::Clouds:
        case EffectPreviewKind::Turbulence:
            scale_slider();
            break;
        case EffectPreviewKind::JuliaFractal:
        case EffectPreviewKind::MandelbrotFractal:
            zoom_slider();
            angle_slider();
            break;
        case EffectPreviewKind::Emboss:
        case EffectPreviewKind::Relief:
            angle_slider();
            break;
        case EffectPreviewKind::AutoLevel:
        case EffectPreviewKind::Grayscale:
        case EffectPreviewKind::Sepia:
        case EffectPreviewKind::InvertColors:
        case EffectPreviewKind::InvertAlpha:
        case EffectPreviewKind::PaletteQuantize:
        case EffectPreviewKind::PaletteDither:
        case EffectPreviewKind::None:
            ImGui::TextDisabled("This effect has no adjustable parameters.");
            break;
    }

    if (changed) {
        effect_preview_dirty_ = true;
    }
}

void EditorApp::draw_effect_preview_popup() {
    if (!effect_preview_active_) {
        return;
    }
    if (effect_preview_popup_requested_) {
        ImGui::OpenPopup("Effect Preview");
        effect_preview_popup_requested_ = false;
    }
    if (effect_preview_dirty_) {
        rebuild_effect_preview();
    }

    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::BeginPopupModal("Effect Preview", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(effect_preview_name(effect_preview_kind_));
        ImGui::Separator();
        draw_effect_preview_parameters();
        ImGui::Separator();
        if (ImGui::Button("Apply", ImVec2(96.0f, 0.0f))) {
            close_effect_preview(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
            close_effect_preview(false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!open) {
        close_effect_preview(false);
    }
}

void EditorApp::draw_rotate_zoom_popup() {
    if (rotate_zoom_popup_requested_) {
        ImGui::OpenPopup("Rotate / Zoom");
        rotate_zoom_popup_requested_ = false;
        rotate_zoom_open_ = true;
        rotate_zoom_preview_dirty_ = true;
    }

    bool open = rotate_zoom_open_;
    if (!ImGui::BeginPopupModal("Rotate / Zoom", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        rotate_zoom_open_ = open;
        return;
    }

    bool changed = false;
    changed |= ImGui::SliderFloat("Angle", &rotate_zoom_angle_, -180.0f, 180.0f, "%.1f deg");
    changed |= ImGui::SliderFloat("Zoom", &rotate_zoom_zoom_, 0.1f, 8.0f, "%.2f");
    changed |= ImGui::SliderInt("Pan X", &rotate_zoom_pan_x_, -document_.width, document_.width);
    changed |= ImGui::SliderInt("Pan Y", &rotate_zoom_pan_y_, -document_.height, document_.height);
    changed |= ImGui::Combo("Resampling", &image_transform_resampling_, kResamplingModeNames, IM_ARRAYSIZE(kResamplingModeNames));
    if (changed) {
        rotate_zoom_preview_dirty_ = true;
    }

    ImGui::BeginGroup();
    const ImVec2 dial_size(104.0f, 104.0f);
    const ImVec2 dial_min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##RotateDial", dial_size);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 center(dial_min.x + dial_size.x * 0.5f, dial_min.y + dial_size.y * 0.5f);
    const float radius = 40.0f;
    draw_list->AddCircle(center, radius, IM_COL32(190, 190, 190, 255), 64, 2.0f);
    const float radians_value = rotate_zoom_angle_ * 3.14159265358979323846f / 180.0f;
    const ImVec2 dot(center.x + std::cos(radians_value) * radius, center.y + std::sin(radians_value) * radius);
    draw_list->AddLine(center, dot, IM_COL32(245, 180, 70, 255), 2.0f);
    draw_list->AddCircleFilled(dot, 6.0f, IM_COL32(245, 180, 70, 255));
    ImGui::EndGroup();

    ImGui::SameLine();
    if (rotate_zoom_preview_dirty_ || transform_preview_texture_.id() == 0) {
        rebuild_rotate_zoom_preview();
    }
    ImGui::BeginGroup();
    const float max_preview_size = 220.0f;
    const float aspect = rotate_zoom_preview_document_.height > 0
                             ? static_cast<float>(rotate_zoom_preview_document_.width) /
                                   static_cast<float>(rotate_zoom_preview_document_.height)
                             : 1.0f;
    ImVec2 preview_size(max_preview_size, max_preview_size);
    if (aspect >= 1.0f) {
        preview_size.y = max_preview_size / std::max(0.01f, aspect);
    } else {
        preview_size.x = max_preview_size * aspect;
    }
    const ImVec2 preview_min = ImGui::GetCursorScreenPos();
    const ImVec2 preview_max(preview_min.x + preview_size.x, preview_min.y + preview_size.y);
    draw_list->AddRectFilled(preview_min, preview_max, IM_COL32(38, 38, 38, 255));
    transform_preview_texture_.bind_nearest();
    draw_list->AddImage(gl_texture_id(transform_preview_texture_.id()), preview_min, preview_max);
    draw_list->AddRect(preview_min, preview_max, IM_COL32(95, 95, 95, 255));
    ImGui::Dummy(preview_size);
    ImGui::EndGroup();

    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(96.0f, 0.0f))) {
        apply_affine_transform_to_document("Rotate / Zoom",
                                           rotate_zoom_angle_,
                                           rotate_zoom_zoom_,
                                           rotate_zoom_pan_x_,
                                           rotate_zoom_pan_y_,
                                           resampling_mode_from_index(image_transform_resampling_));
        effect_angle_ = static_cast<int>(std::round(rotate_zoom_angle_));
        effect_zoom_ = rotate_zoom_zoom_;
        rotate_zoom_open_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
        rotate_zoom_open_ = false;
        ImGui::CloseCurrentPopup();
        set_status("Canceled Rotate / Zoom");
    }
    ImGui::EndPopup();
    rotate_zoom_open_ = open && rotate_zoom_open_;
}

void EditorApp::start_depth_map_extraction() {
    if (depth_job_running_ || document_.frames.empty() || document_.layers.empty()) {
        return;
    }
    depth_source_layer_ = std::clamp(depth_source_layer_, 0, static_cast<int>(document_.layers.size()) - 1);
    const int source_layer = depth_source_layer_;
    const std::vector<Pixel> source_pixels = document_.cel(document_.active_frame, source_layer).pixels;
    const int width = document_.width;
    const int height = document_.height;

    settings_.depth_tile_size = std::clamp(depth_tile_size_, 64, 4096);
    settings_.depth_tile_overlap = std::clamp(depth_tile_overlap_, 0, settings_.depth_tile_size / 2);
    depth_tile_size_ = settings_.depth_tile_size;
    depth_tile_overlap_ = settings_.depth_tile_overlap;

    DepthExtractionSettings request;
    request.cache_dir = default_depth_model_cache_dir();
    request.tile_size = depth_tile_size_;
    request.overlap = depth_tile_overlap_;
    request.allow_cpu_fallback = settings_.depth_allow_cpu_fallback;
    if (settings_.heavy_gpu_optimization && settings_.mps_backend) {
        request.acceleration = DepthAccelerationPreference::Metal;
    } else if (settings_.heavy_gpu_optimization) {
        request.acceleration = DepthAccelerationPreference::Gpu;
    } else {
        request.acceleration = DepthAccelerationPreference::Cpu;
    }

    if (depth_thread_.joinable()) {
        depth_thread_.join();
    }
    depth_cancel_requested_.store(false);
    {
        std::lock_guard lock(depth_mutex_);
        depth_progress_ = {0.0f, "Queued depth extraction"};
        depth_result_ = {};
        depth_error_.clear();
        depth_result_pending_ = false;
        depth_job_running_ = true;
    }

    depth_thread_ = std::thread([this, source_pixels, width, height, request]() {
        DepthMapExtractor extractor;
        DepthExtractionResult result;
        DepthExtractionError error;
        auto progress = [this](const DepthExtractionProgress& update) {
            std::lock_guard lock(depth_mutex_);
            depth_progress_ = update;
        };
        const bool ok = extractor.extract(source_pixels, width, height, request, depth_cancel_requested_, progress, result, error);
        std::lock_guard lock(depth_mutex_);
        if (ok) {
            depth_result_ = std::move(result);
            depth_result_pending_ = true;
            depth_error_.clear();
        } else {
            depth_error_ = error.message.empty() ? "Depth extraction failed" : error.message;
            depth_result_pending_ = false;
        }
        depth_job_running_ = false;
    });
}

void EditorApp::finish_depth_map_job_if_ready() {
    bool running = false;
    bool pending = false;
    DepthExtractionResult result;
    std::string error;
    {
        std::lock_guard lock(depth_mutex_);
        running = depth_job_running_;
        pending = depth_result_pending_;
        if (pending) {
            result = depth_result_;
        }
        error = depth_error_;
    }
    if (!running && depth_thread_.joinable()) {
        depth_thread_.join();
    }
    if (!running && pending) {
        insert_depth_map_layer(result);
        {
            std::lock_guard lock(depth_mutex_);
            depth_result_pending_ = false;
        }
        depth_map_open_ = false;
        set_status(result.status.empty() ? "Depth map generated" : result.status);
        return;
    }
    if (!running && !error.empty()) {
        report_error("Depth Map", error);
        std::lock_guard lock(depth_mutex_);
        depth_error_.clear();
    }
}

void EditorApp::insert_depth_map_layer(const DepthExtractionResult& result) {
    if (result.pixels.size() != static_cast<std::size_t>(document_.width * document_.height)) {
        report_error("Depth Map", "Depth result size does not match the document");
        return;
    }
    const int source_layer = document_.layers.empty()
                                 ? 0
                                 : std::clamp(depth_source_layer_, 0, static_cast<int>(document_.layers.size()) - 1);
    const std::string source_name = document_.layers.empty() ? std::string("Layer") : document_.layers[static_cast<std::size_t>(source_layer)].name;
    const int insert_index = std::min(source_layer + 1, static_cast<int>(document_.layers.size()));
    document_.insert_layer(insert_index, source_name + " Depth Map", result.pixels, "Generate Depth Map");
    texture_dirty_ = true;
}

void EditorApp::start_image_document_import(const std::string& path) {
    MemoryTraceScope trace("start_image_document_import", path);
    {
        std::lock_guard lock(image_import_mutex_);
        if (image_import_job_running_) {
            set_status("Image import is already running");
            return;
        }
    }
    if (image_import_thread_.joinable()) {
        image_import_thread_.join();
    }
    {
        std::lock_guard lock(image_import_mutex_);
        image_import_progress_ = {0.0f, 0, 0, true, "Queued", "Queued image import"};
        image_import_result_ = {};
        image_import_pyramid_ = {};
        image_import_error_.clear();
        image_import_path_ = path;
        image_import_result_pending_ = false;
        image_import_job_running_ = true;
    }
    set_status("Importing image in background");

    image_import_thread_ = std::thread([this, path]() {
        MemoryTraceScope worker_trace("start_image_document_import.worker", path);
        Document imported;
        GLTiledCanvasTexture::CpuPyramid pyramid;
        std::string error;
        auto progress = [this](const ImageImportProgress& update) {
            std::lock_guard lock(image_import_mutex_);
            image_import_progress_ = {0.02f + update.fraction * 0.68f,
                                      update.done,
                                      update.total,
                                      update.indeterminate,
                                      update.phase.empty() ? std::string("Decoding image") : update.phase,
                                      update.status};
        };
        const bool ok = import_image(path, imported, &error, progress);
        trace_document_pixel_buffers("image_import_worker.imported_after_decode", imported);
        if (ok && imported.has_active_cel() &&
            GLTiledCanvasTexture::should_use_pyramid(imported.width, imported.height)) {
            const auto pyramid_progress = [this](int done, int total, const char* status) {
                const float fraction = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
                std::lock_guard lock(image_import_mutex_);
                image_import_progress_ = {0.72f + fraction * 0.22f,
                                          done,
                                          total,
                                          total <= 0,
                                          "Building pyramid",
                                          status == nullptr ? std::string("Building image pyramid") : std::string(status)};
            };
            pyramid = GLTiledCanvasTexture::build_cpu_pyramid(imported.width,
                                                              imported.height,
                                                              imported.active_cel().pixels,
                                                              pyramid_progress);
            memory_trace_event("counter",
                               {},
                               "image_import_worker.pyramid_pixels",
                               nullptr,
                               pyramid.pixel_count(),
                               pyramid.pixel_count(),
                               sizeof(Pixel));
        }
        {
            std::lock_guard lock(image_import_mutex_);
            image_import_progress_ = {ok ? 0.96f : 1.0f,
                                      0,
                                      0,
                                      ok,
                                      ok ? std::string("Preparing document") : std::string("Import failed"),
                                      ok ? std::string("Preparing document for display") : error};
        }
        std::lock_guard lock(image_import_mutex_);
        if (ok) {
            image_import_result_ = std::move(imported);
            image_import_pyramid_ = std::move(pyramid);
            trace_document_pixel_buffers("image_import_worker.result_buffer", image_import_result_);
            memory_trace_event("counter",
                               {},
                               "image_import_worker.result_pyramid_pixels",
                               nullptr,
                               image_import_pyramid_.pixel_count(),
                               image_import_pyramid_.pixel_count(),
                               sizeof(Pixel));
            image_import_result_pending_ = true;
            image_import_error_.clear();
        } else {
            image_import_error_ = error.empty() ? "Image import failed" : error;
            image_import_result_pending_ = false;
        }
        image_import_job_running_ = false;
    });
}

void EditorApp::finish_image_import_job_if_ready() {
    MemoryTraceScope trace("finish_image_import_job_if_ready");
    bool running = false;
    bool pending = false;
    ImageImportJobProgress progress;
    std::string error;
    std::string path;
    {
        std::lock_guard lock(image_import_mutex_);
        running = image_import_job_running_;
        pending = image_import_result_pending_;
        progress = image_import_progress_;
        error = image_import_error_;
        path = image_import_path_;
    }
    if (running) {
        if (!progress.status.empty()) {
            set_status(progress.status + " (" + std::to_string(static_cast<int>(progress.fraction * 100.0f)) + "%)");
        }
        return;
    }
    if (image_import_thread_.joinable()) {
        image_import_thread_.join();
    }
    if (pending) {
        Document imported;
        GLTiledCanvasTexture::CpuPyramid pyramid;
        {
            std::lock_guard lock(image_import_mutex_);
            imported = std::move(image_import_result_);
            pyramid = std::move(image_import_pyramid_);
            image_import_result_pending_ = false;
        }
        trace_document_pixel_buffers("finish_image_import_job_if_ready.imported_local", imported);
        memory_trace_event("counter",
                           {},
                           "finish_image_import_job_if_ready.pyramid_local_pixels",
                           nullptr,
                           pyramid.pixel_count(),
                           pyramid.pixel_count(),
                           sizeof(Pixel));
        document_ = std::move(imported);
        trace_document_pixel_buffers("finish_image_import_job_if_ready.document_after_move", document_);
        sync_model_texture_metadata();
        reset_history_tree();
        texture_dirty_ = true;
        full_canvas_texture_dirty_ = true;
        tiled_canvas_texture_.invalidate();
        tiled_onion_texture_.invalidate();
        refresh_texture();
        if (!pyramid.empty()) {
            tiled_canvas_texture_.set_prepared_pyramid(std::move(pyramid));
        } else {
            tiled_canvas_texture_.clear_prepared_pyramid();
        }
        set_status(std::string("Imported ") + path);
        return;
    }
    if (!error.empty()) {
        report_error("Import image as document: " + path, error);
        std::lock_guard lock(image_import_mutex_);
        image_import_error_.clear();
    }
}

void EditorApp::draw_image_import_popup() {
    bool running = false;
    ImageImportJobProgress progress;
    std::string path;
    {
        std::lock_guard lock(image_import_mutex_);
        running = image_import_job_running_;
        progress = image_import_progress_;
        path = image_import_path_;
    }

    constexpr const char* kPopupName = "Importing Image";
    if (running) {
        ImGui::OpenPopup(kPopupName);
    }
    if (!ImGui::BeginPopupModal(kPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    if (!running) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const float fraction = std::clamp(progress.fraction, 0.0f, 1.0f);
    std::string filename = path;
    if (!path.empty()) {
        filename = std::filesystem::path(path).filename().string();
        if (filename.empty()) {
            filename = path;
        }
    }

    ImGui::TextUnformatted("Import image as document");
    ImGui::TextDisabled("%s", filename.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("%s", progress.phase.empty() ? "Loading image" : progress.phase.c_str());
    if (progress.indeterminate || progress.total <= 0) {
        const ImVec2 bar_size(420.0f, ImGui::GetFrameHeight());
        const ImVec2 min_pos = ImGui::GetCursorScreenPos();
        const ImVec2 max_pos(min_pos.x + bar_size.x, min_pos.y + bar_size.y);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(min_pos, max_pos, IM_COL32(55, 58, 65, 255), 3.0f);
        const float segment_width = bar_size.x * 0.28f;
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.85f, 1.0f);
        const float x = min_pos.x - segment_width + phase * (bar_size.x + segment_width * 2.0f);
        draw_list->PushClipRect(min_pos, max_pos, true);
        draw_list->AddRectFilled(ImVec2(x, min_pos.y),
                                 ImVec2(x + segment_width, max_pos.y),
                                 IM_COL32(105, 190, 255, 220),
                                 3.0f);
        draw_list->PopClipRect();
        ImGui::Dummy(bar_size);
    } else {
        ImGui::ProgressBar(fraction, ImVec2(420.0f, 0.0f));
        ImGui::TextDisabled("%d / %d", progress.done, progress.total);
    }
    ImGui::TextWrapped("%s", progress.status.empty() ? "Loading image" : progress.status.c_str());
    ImGui::EndPopup();
}

int EditorApp::default_depth_of_field_layer() const {
    if (document_.layers.empty()) {
        return 0;
    }
    auto contains_depth = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value.find("depth") != std::string::npos;
    };
    for (int i = 0; i < static_cast<int>(document_.layers.size()); ++i) {
        if (i != document_.active_layer && contains_depth(document_.layers[static_cast<std::size_t>(i)].name)) {
            return i;
        }
    }
    const int above = document_.active_layer + 1;
    if (above >= 0 && above < static_cast<int>(document_.layers.size())) {
        return above;
    }
    const int below = document_.active_layer - 1;
    if (below >= 0 && below < static_cast<int>(document_.layers.size())) {
        return below;
    }
    return std::clamp(document_.active_layer, 0, static_cast<int>(document_.layers.size()) - 1);
}

const std::vector<Pixel>* EditorApp::depth_of_field_pixels(const Document& document) const {
    if (document.layers.empty() || document.frames.empty()) {
        return nullptr;
    }
    if (document.active_frame < 0 || document.active_frame >= static_cast<int>(document.frames.size())) {
        return nullptr;
    }
    if (depth_of_field_layer_ < 0 || depth_of_field_layer_ >= static_cast<int>(document.layers.size())) {
        return nullptr;
    }
    if (depth_of_field_layer_ >= static_cast<int>(document.frames[static_cast<std::size_t>(document.active_frame)].cels.size())) {
        return nullptr;
    }
    const auto& pixels = document.cel(document.active_frame, depth_of_field_layer_).pixels;
    if (pixels.size() != static_cast<std::size_t>(document.width * document.height)) {
        return nullptr;
    }
    return &pixels;
}

bool EditorApp::valid_depth_of_field_layer(const Document& document) const {
    return depth_of_field_pixels(document) != nullptr;
}

void EditorApp::draw_depth_map_popup() {
    finish_depth_map_job_if_ready();
    if (depth_map_popup_requested_) {
        ImGui::OpenPopup("Generate Depth Map");
        depth_map_popup_requested_ = false;
        depth_map_open_ = true;
    }

    bool open = depth_map_open_;
    if (!ImGui::BeginPopupModal("Generate Depth Map", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        depth_map_open_ = open;
        return;
    }

    bool running = false;
    DepthExtractionProgress progress;
    {
        std::lock_guard lock(depth_mutex_);
        running = depth_job_running_;
        progress = depth_progress_;
    }

    ImGui::TextUnformatted("Depth Anything V2 Small");
    ImGui::TextDisabled("Compiled backends: %s", depth_backend_build_description().c_str());
    ImGui::Separator();

    if (document_.layers.empty()) {
        ImGui::TextDisabled("No layer available");
    } else {
        depth_source_layer_ = std::clamp(depth_source_layer_, 0, static_cast<int>(document_.layers.size()) - 1);
        const char* current_name = document_.layers[static_cast<std::size_t>(depth_source_layer_)].name.c_str();
        if (ImGui::BeginCombo("Source layer", current_name)) {
            for (int i = static_cast<int>(document_.layers.size()) - 1; i >= 0; --i) {
                const bool selected = depth_source_layer_ == i;
                if (ImGui::Selectable(document_.layers[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                    depth_source_layer_ = i;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (running) {
        ImGui::BeginDisabled();
    }
    ImGui::InputInt("Tile size", &depth_tile_size_, 64, 256);
    depth_tile_size_ = std::clamp(depth_tile_size_, 64, 4096);
    ImGui::InputInt("Overlap", &depth_tile_overlap_, 16, 64);
    depth_tile_overlap_ = std::clamp(depth_tile_overlap_, 0, depth_tile_size_ / 2);
    ImGui::Checkbox("Allow real CPU model fallback", &settings_.depth_allow_cpu_fallback);
    if (running) {
        ImGui::EndDisabled();
    }

    const char* requested_backend = "CPU";
    if (settings_.heavy_gpu_optimization && settings_.mps_backend) {
        requested_backend = "Metal/CoreML if ONNX Runtime provides it";
    } else if (settings_.heavy_gpu_optimization) {
        requested_backend = "GPU provider if ONNX Runtime provides one";
    }
    ImGui::TextDisabled("Requested backend: %s", requested_backend);
    if (!depth_backend_compiled()) {
        ImGui::TextWrapped("No real depth backend was found when this build was configured. Install ONNX Runtime or OpenCV DNN support and reconfigure; fake grayscale depth generation is disabled.");
    } else {
        ImGui::TextWrapped("CPU fallback still runs the depth model. It is slower, but it is not a grayscale approximation.");
    }

    ImGui::Separator();
    if (running) {
        ImGui::ProgressBar(progress.fraction, ImVec2(360.0f, 0.0f));
        ImGui::TextWrapped("%s", progress.status.c_str());
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
            depth_cancel_requested_.store(true);
            set_status("Canceling depth extraction");
        }
    } else {
        const bool can_generate = !document_.layers.empty();
        if (!can_generate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Generate", ImVec2(96.0f, 0.0f))) {
            start_depth_map_extraction();
        }
        if (!can_generate) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(96.0f, 0.0f))) {
            depth_map_open_ = false;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
    depth_map_open_ = open && depth_map_open_;
}

void EditorApp::draw_undo_tree_window() {
    if (!settings_.show_history_panel) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("History", &settings_.show_history_panel)) {
        ImGui::End();
        return;
    }

    if (huge_document_history_mode()) {
        ImGui::TextUnformatted("Lightweight history");
        ImGui::Separator();
        ImGui::TextWrapped("Huge image documents use tile-diff undo to avoid multi-gigabyte history snapshots.");
        ImGui::TextDisabled("Undo and redo still work for pixel edits.");
        ImGui::End();
        return;
    }

    ImGui::Text("Actions: %d", std::max(0, static_cast<int>(history_nodes_.size()) - 1));
    ImGui::Separator();

    const ImVec2 canvas_size(std::max(260.0f, ImGui::GetContentRegionAvail().x),
                             std::max(180.0f, ImGui::GetContentRegionAvail().y));
    ImGui::BeginChild("UndoTreeCanvas", canvas_size, true, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    constexpr float indent = 28.0f;
    constexpr float row_height = 30.0f;
    constexpr float node_height = 22.0f;
    constexpr float node_radius = 4.0f;

    struct PositionedHistoryNode {
        int id = 0;
        int depth = 0;
        ImVec2 pos = ImVec2(0, 0);
    };
    std::vector<PositionedHistoryNode> positioned;
    int row = 0;
    std::function<void(int, int)> layout_node = [&](int node_id, int depth) {
        const EditorHistoryNode* node = history_node_by_id(node_id);
        if (node == nullptr) {
            return;
        }
        positioned.push_back({node_id, depth, ImVec2(8.0f + indent * static_cast<float>(depth),
                                                     8.0f + row_height * static_cast<float>(row++))});
        for (int child_id : node->children) {
            layout_node(child_id, depth + 1);
        }
    };
    layout_node(0, 0);

    auto position_for_id = [&](int node_id) {
        for (const PositionedHistoryNode& node : positioned) {
            if (node.id == node_id) {
                return node.pos;
            }
        }
        return ImVec2(-1.0f, -1.0f);
    };

    for (const PositionedHistoryNode& node_position : positioned) {
        const EditorHistoryNode* node = history_node_by_id(node_position.id);
        if (node == nullptr) {
            continue;
        }
        const ImVec2 parent_pos = position_for_id(node->parent);
        if (parent_pos.x >= 0.0f) {
            const ImVec2 a(origin.x + parent_pos.x + 9.0f, origin.y + parent_pos.y + node_height * 0.5f);
            const ImVec2 b(origin.x + node_position.pos.x + 9.0f, origin.y + node_position.pos.y + node_height * 0.5f);
            draw_list->AddLine(a, ImVec2(a.x, b.y), IM_COL32(120, 120, 120, 170), 1.5f);
            draw_list->AddLine(ImVec2(a.x, b.y), b, IM_COL32(120, 120, 120, 170), 1.5f);
        }
    }

    const float label_width = std::max(150.0f, canvas_size.x - 24.0f);
    for (const PositionedHistoryNode& node_position : positioned) {
        const EditorHistoryNode* node = history_node_by_id(node_position.id);
        if (node == nullptr) {
            continue;
        }
        const bool active = node->id == history_current_node_;
        const bool ancestor = active || !node->children.empty();
        const ImVec2 node_min(origin.x + node_position.pos.x, origin.y + node_position.pos.y);
        const ImVec2 node_max(node_min.x + std::min(label_width, 240.0f), node_min.y + node_height);
        const ImU32 fill = active ? IM_COL32(72, 118, 190, 235)
                                  : ancestor ? IM_COL32(48, 48, 48, 235)
                                             : IM_COL32(36, 36, 36, 210);
        const ImU32 edge = active ? IM_COL32(160, 205, 255, 255) : IM_COL32(110, 110, 110, 210);
        draw_list->AddRectFilled(node_min, node_max, fill, node_radius);
        draw_list->AddRect(node_min, node_max, edge, node_radius, 0, active ? 2.0f : 1.0f);
        draw_list->AddCircleFilled(ImVec2(node_min.x + 9.0f, node_min.y + node_height * 0.5f),
                                   active ? 4.5f : 3.5f,
                                   active ? IM_COL32(245, 245, 255, 255) : IM_COL32(180, 180, 180, 230));
        const char* label = node->name.c_str();
        draw_list->AddText(ImVec2(node_min.x + 18.0f, node_min.y + 3.0f),
                           active ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 235),
                           label);

        ImGui::SetCursorScreenPos(node_min);
        ImGui::PushID(node->id);
        if (ImGui::InvisibleButton("history-node", ImVec2(node_max.x - node_min.x, node_height))) {
            if (EditorHistoryNode* parent = history_node_by_id(node->parent)) {
                parent->preferred_child = node->id;
            }
            restore_history_node(node->id);
            set_status("Restored history: " + node->name);
        }
        ImGui::PopID();
    }

    const float required_height = std::max(canvas_size.y, 16.0f + row_height * static_cast<float>(positioned.size()));
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(canvas_size.x, required_height));
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::draw_histogram_plot() {
    MemoryTraceScope trace("draw_histogram_plot");
    ImGui::TextUnformatted("Histogram");
    bool curve_changed = false;
    curve_changed |= ImGui::Checkbox("Luma", &histogram_luma_visible_);
    ImGui::SameLine();
    curve_changed |= ImGui::Checkbox("Red", &histogram_red_visible_);
    ImGui::SameLine();
    curve_changed |= ImGui::Checkbox("Green", &histogram_green_visible_);
    ImGui::SameLine();
    curve_changed |= ImGui::Checkbox("Blue", &histogram_blue_visible_);

    update_histogram_cache();
    if (histogram_cache_approximate_) {
        ImGui::TextDisabled("Display histogram is sampled for this huge image.");
    }

    const float plot_height = 104.0f;
    const float plot_width = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 plot_size(plot_width, plot_height);
    const ImVec2 plot_min = ImGui::GetCursorScreenPos();
    const ImVec2 plot_max(plot_min.x + plot_size.x, plot_min.y + plot_size.y);
    ImGui::InvisibleButton("##HistogramPlot", plot_size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(plot_min, plot_max, IM_COL32(24, 24, 24, 255), 3.0f);
    for (int i = 1; i < 4; ++i) {
        const float x = plot_min.x + plot_size.x * static_cast<float>(i) / 4.0f;
        const float y = plot_min.y + plot_size.y * static_cast<float>(i) / 4.0f;
        draw_list->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), IM_COL32(58, 58, 58, 140));
        draw_list->AddLine(ImVec2(plot_min.x, y), ImVec2(plot_max.x, y), IM_COL32(58, 58, 58, 140));
    }
    draw_list->AddRect(plot_min, plot_max, IM_COL32(92, 92, 92, 210), 3.0f);

    auto draw_curve = [&](const std::array<float, 256>& values, ImU32 color) {
        std::array<ImVec2, 256> points{};
        for (std::size_t i = 0; i < points.size(); ++i) {
            const float t = static_cast<float>(i) / 255.0f;
            const float value = std::clamp(values[i], 0.0f, 1.0f);
            points[i] = ImVec2(plot_min.x + t * plot_size.x, plot_max.y - value * plot_size.y);
        }
        draw_list->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, 1.8f);
    };

    if (histogram_luma_visible_) {
        draw_curve(histogram_luma_values_, IM_COL32(230, 230, 230, 235));
    }
    if (histogram_red_visible_) {
        draw_curve(histogram_red_values_, IM_COL32(245, 78, 78, 225));
    }
    if (histogram_green_visible_) {
        draw_curve(histogram_green_values_, IM_COL32(80, 210, 116, 225));
    }
    if (histogram_blue_visible_) {
        draw_curve(histogram_blue_values_, IM_COL32(92, 145, 255, 225));
    }

    if (!histogram_luma_visible_ && !histogram_red_visible_ && !histogram_green_visible_ && !histogram_blue_visible_) {
        const char* label = "No histogram enabled";
        const ImVec2 label_size = ImGui::CalcTextSize(label);
        draw_list->AddText(ImVec2(plot_min.x + (plot_size.x - label_size.x) * 0.5f,
                                  plot_min.y + (plot_size.y - label_size.y) * 0.5f),
                           IM_COL32(160, 160, 160, 255),
                           label);
    }

    if (curve_changed && effect_preview_active_ && effect_preview_kind_ == EffectPreviewKind::Curves) {
        effect_preview_dirty_ = true;
    }
}

bool EditorApp::draw_curves_editor() {
    MemoryTraceScope trace("draw_curves_editor");
    bool changed = false;
    changed |= ImGui::Checkbox("Luma", &histogram_luma_visible_);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Red", &histogram_red_visible_);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Green", &histogram_green_visible_);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Blue", &histogram_blue_visible_);

    update_histogram_cache();
    if (histogram_cache_approximate_) {
        ImGui::TextDisabled("Display histogram is sampled for this huge image; curve math remains exact.");
    }

    const float plot_height = 180.0f;
    const float plot_width = std::max(260.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 plot_size(plot_width, plot_height);
    const ImVec2 plot_min = ImGui::GetCursorScreenPos();
    const ImVec2 plot_max(plot_min.x + plot_size.x, plot_min.y + plot_size.y);
    ImGui::InvisibleButton("##CurvesEditor", plot_size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(plot_min, plot_max, IM_COL32(24, 24, 24, 255), 3.0f);
    for (int i = 1; i < 4; ++i) {
        const float x = plot_min.x + plot_size.x * static_cast<float>(i) / 4.0f;
        const float y = plot_min.y + plot_size.y * static_cast<float>(i) / 4.0f;
        draw_list->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), IM_COL32(58, 58, 58, 140));
        draw_list->AddLine(ImVec2(plot_min.x, y), ImVec2(plot_max.x, y), IM_COL32(58, 58, 58, 140));
    }
    draw_list->AddRect(plot_min, plot_max, IM_COL32(92, 92, 92, 210), 3.0f);

    auto draw_histogram_curve = [&](const std::array<float, 256>& values, ImU32 color) {
        std::array<ImVec2, 256> points{};
        for (std::size_t i = 0; i < points.size(); ++i) {
            const float t = static_cast<float>(i) / 255.0f;
            const float value = std::clamp(values[i], 0.0f, 1.0f);
            points[i] = ImVec2(plot_min.x + t * plot_size.x, plot_max.y - value * plot_size.y);
        }
        draw_list->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_None, 1.2f);
    };

    if (histogram_luma_visible_) {
        draw_histogram_curve(histogram_luma_values_, IM_COL32(230, 230, 230, 145));
    }
    if (histogram_red_visible_) {
        draw_histogram_curve(histogram_red_values_, IM_COL32(245, 78, 78, 150));
    }
    if (histogram_green_visible_) {
        draw_histogram_curve(histogram_green_values_, IM_COL32(80, 210, 116, 150));
    }
    if (histogram_blue_visible_) {
        draw_histogram_curve(histogram_blue_values_, IM_COL32(92, 145, 255, 150));
    }

    curves_.point_count = std::clamp(curves_.point_count, 2, kMaxCurvePoints);
    auto to_plot = [&](float x, float y) {
        return ImVec2(plot_min.x + std::clamp(x, 0.0f, 1.0f) * plot_size.x,
                      plot_max.y - std::clamp(y, 0.0f, 1.0f) * plot_size.y);
    };
    auto from_plot = [&](ImVec2 position) {
        return ImVec2(std::clamp((position.x - plot_min.x) / plot_size.x, 0.0f, 1.0f),
                      std::clamp(1.0f - (position.y - plot_min.y) / plot_size.y, 0.0f, 1.0f));
    };
    auto curve_y_at = [&](float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        if (value <= curves_.x[0]) {
            return std::clamp(curves_.y[0], 0.0f, 1.0f);
        }
        for (int point = 1; point < curves_.point_count; ++point) {
            const float x0 = std::clamp(curves_.x[static_cast<std::size_t>(point - 1)], 0.0f, 1.0f);
            const float x1 = std::clamp(curves_.x[static_cast<std::size_t>(point)], x0 + 0.001f, 1.0f);
            if (value <= x1 || point == curves_.point_count - 1) {
                const float y0 = std::clamp(curves_.y[static_cast<std::size_t>(point - 1)], 0.0f, 1.0f);
                const float y1 = std::clamp(curves_.y[static_cast<std::size_t>(point)], 0.0f, 1.0f);
                const float t = std::clamp((value - x0) / std::max(0.001f, x1 - x0), 0.0f, 1.0f);
                const float smooth = t * t * (3.0f - 2.0f * t);
                return y0 + (y1 - y0) * smooth;
            }
        }
        return std::clamp(curves_.y[static_cast<std::size_t>(curves_.point_count - 1)], 0.0f, 1.0f);
    };

    std::array<ImVec2, 80> curve_points{};
    for (std::size_t i = 0; i < curve_points.size(); ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(curve_points.size() - 1);
        curve_points[i] = to_plot(x, curve_y_at(x));
    }
    draw_list->AddPolyline(curve_points.data(), static_cast<int>(curve_points.size()), IM_COL32(255, 205, 80, 245), ImDrawFlags_None, 2.2f);

    auto distance_sq = [](ImVec2 lhs, ImVec2 rhs) {
        const float dx = lhs.x - rhs.x;
        const float dy = lhs.y - rhs.y;
        return dx * dx + dy * dy;
    };
    auto point_position = [&](int index) {
        return to_plot(curves_.x[static_cast<std::size_t>(index)], curves_.y[static_cast<std::size_t>(index)]);
    };
    int hovered_point = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int point = 0; point < curves_.point_count; ++point) {
        const ImVec2 position = point_position(point);
        if (distance_sq(mouse, position) <= 100.0f) {
            hovered_point = point;
            break;
        }
    }

    for (int point = 0; point < curves_.point_count; ++point) {
        const ImVec2 position = point_position(point);
        const bool active = histogram_curve_drag_handle_ == point + 1;
        const bool hovered = hovered_point == point;
        draw_list->AddCircleFilled(position,
                                   active || hovered ? 6.8f : 5.2f,
                                   hovered ? IM_COL32(255, 230, 130, 255) : IM_COL32(255, 205, 80, 255),
                                   18);
        draw_list->AddCircle(position, 7.5f, IM_COL32(30, 30, 30, 220), 18, 1.2f);
    }

    auto sort_curve_points = [&]() {
        for (int i = 0; i < curves_.point_count - 1; ++i) {
            for (int j = i + 1; j < curves_.point_count; ++j) {
                if (curves_.x[static_cast<std::size_t>(j)] < curves_.x[static_cast<std::size_t>(i)]) {
                    std::swap(curves_.x[static_cast<std::size_t>(i)], curves_.x[static_cast<std::size_t>(j)]);
                    std::swap(curves_.y[static_cast<std::size_t>(i)], curves_.y[static_cast<std::size_t>(j)]);
                }
            }
        }
    };
    auto insert_curve_point = [&](ImVec2 value) {
        if (curves_.point_count >= kMaxCurvePoints) {
            return false;
        }
        const std::size_t index = static_cast<std::size_t>(curves_.point_count);
        curves_.x[index] = value.x;
        curves_.y[index] = value.y;
        ++curves_.point_count;
        sort_curve_points();
        return true;
    };
    auto delete_curve_point = [&](int index) {
        if (index < 0 || curves_.point_count <= 2) {
            return false;
        }
        for (int point = index; point < curves_.point_count - 1; ++point) {
            curves_.x[static_cast<std::size_t>(point)] = curves_.x[static_cast<std::size_t>(point + 1)];
            curves_.y[static_cast<std::size_t>(point)] = curves_.y[static_cast<std::size_t>(point + 1)];
        }
        --curves_.point_count;
        histogram_curve_drag_handle_ = 0;
        return true;
    };

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (hovered_point >= 0) {
            changed |= delete_curve_point(hovered_point);
        } else {
            changed |= insert_curve_point(from_plot(mouse));
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        histogram_curve_drag_handle_ = hovered_point >= 0 ? hovered_point + 1 : 0;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        histogram_curve_drag_handle_ = 0;
    }
    if (histogram_curve_drag_handle_ != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 value = from_plot(ImGui::GetIO().MousePos);
        const int point = histogram_curve_drag_handle_ - 1;
        const float min_x = point > 0 ? curves_.x[static_cast<std::size_t>(point - 1)] + 0.001f : 0.0f;
        const float max_x = point + 1 < curves_.point_count ? curves_.x[static_cast<std::size_t>(point + 1)] - 0.001f : 1.0f;
        curves_.x[static_cast<std::size_t>(point)] = std::clamp(value.x, min_x, max_x);
        curves_.y[static_cast<std::size_t>(point)] = value.y;
        changed = true;
    }

    ImGui::Text("Points: %d / %d", curves_.point_count, kMaxCurvePoints);
    if (ImGui::Button("Reset Curves", ImVec2(112.0f, 0.0f))) {
        curves_ = CurvesSettings{};
        histogram_curve_drag_handle_ = 0;
        changed = true;
    }
    return changed;
}

void EditorApp::draw_model_panel() {
    if (!settings_.show_model_uv_panel) {
        return;
    }

    bool open = settings_.show_model_uv_panel;
    if (!ImGui::Begin("Model / UV", &open)) {
        settings_.show_model_uv_panel = open;
        ImGui::End();
        return;
    }
    settings_.show_model_uv_panel = open;
    sync_model_texture_metadata();
    if (ImGui::Button("+ Cuboid")) model_.add_cuboid();
    ImGui::SameLine();
    if (ImGui::Button("- Cuboid")) model_.remove_selected();
    ImGui::SameLine();
    ImGui::Checkbox("All Canvas UVs", &show_all_cuboid_wireframes_);
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(model_.cuboids.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(model_.cuboids[static_cast<std::size_t>(i)].name.c_str(), model_.selected_cuboid == i)) {
            model_.selected_cuboid = i;
        }
        ImGui::PopID();
    }
    if (model_.cuboids.empty()) {
        ImGui::End();
        return;
    }
    Cuboid& cuboid = model_.selected();
    char name[96];
    copy_path(name, sizeof(name), cuboid.name);
    if (ImGui::InputText("Name", name, sizeof(name))) {
        cuboid.name = name;
    }
    ImGui::InputFloat3("From", cuboid.from.data());
    ImGui::InputFloat3("To", cuboid.to.data());
    ImGui::SliderInt("Face", &model_.selected_face, 0, 5, face_name(model_.selected_face));
    UvRect& uv = cuboid.uv[static_cast<std::size_t>(model_.selected_face)];
    if (ImGui::SliderInt("UV X", &uv.x, 0, std::max(0, document_.width - 1))) uv = clamped_uv_rect(uv, document_.width, document_.height);
    if (ImGui::SliderInt("UV Y", &uv.y, 0, std::max(0, document_.height - 1))) uv = clamped_uv_rect(uv, document_.width, document_.height);
    if (ImGui::SliderInt("UV W", &uv.w, 1, document_.width)) uv = clamped_uv_rect(uv, document_.width, document_.height);
    if (ImGui::SliderInt("UV H", &uv.h, 1, document_.height)) uv = clamped_uv_rect(uv, document_.width, document_.height);
    ImGui::SeparatorText("Texture Atlas");
    float uv_scale = std::min(256.0f / static_cast<float>(document_.width), 256.0f / static_cast<float>(document_.height));
    ImVec2 atlas_size(pixel_scale(document_.width, uv_scale), pixel_scale(document_.height, uv_scale));
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("UVAtlas", atlas_size, ImGuiButtonFlags_MouseButtonLeft);
    handle_uv_input(origin, uv_scale);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin, ImVec2(origin.x + atlas_size.x, origin.y + atlas_size.y), IM_COL32(70, 70, 70, 255));
    if (ensure_full_canvas_texture()) {
        canvas_texture_.bind_nearest();
        push_nearest_sampler(draw_list);
        draw_list->AddImage(gl_texture_id(canvas_texture_.id()), origin, ImVec2(origin.x + atlas_size.x, origin.y + atlas_size.y));
        push_linear_sampler(draw_list);
        draw_uv_overlay(draw_list, origin, uv_scale);
    } else {
        draw_list->AddText(origin, IM_COL32(230, 230, 230, 220), "Texture too large for atlas preview");
    }
    ImGui::End();
}

void EditorApp::draw_model_preview_window() {
    if (!settings_.show_3d_preview) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(460, 340), ImGuiCond_FirstUseEver);
    bool open = settings_.show_3d_preview;
    if (ImGui::Begin("3D Preview", &open)) {
        draw_model_preview();
    }
    settings_.show_3d_preview = open;
    ImGui::End();
}

void EditorApp::draw_tile_preview_window() {
    if (!show_tile_preview_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Tile Preview", &show_tile_preview_)) {
        ImVec2 available = ImGui::GetContentRegionAvail();
        available.x = std::max(96.0f, available.x);
        available.y = std::max(96.0f, available.y);
        const float cell = std::floor(std::max(1.0f, std::min(available.x / 3.0f, available.y / 3.0f)));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 preview_size(cell * 3.0f, cell * 3.0f);
        ImGui::InvisibleButton("TilePreviewSurface", preview_size);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(origin,
                                 ImVec2(origin.x + preview_size.x, origin.y + preview_size.y),
                                 IM_COL32(92, 92, 92, 255));
        if (ensure_full_canvas_texture()) {
            canvas_texture_.bind_nearest();
            push_nearest_sampler(draw_list);
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 3; ++x) {
                    const ImVec2 min_pos(origin.x + static_cast<float>(x) * cell,
                                         origin.y + static_cast<float>(y) * cell);
                    const ImVec2 max_pos(min_pos.x + cell, min_pos.y + cell);
                    draw_list->AddImage(gl_texture_id(canvas_texture_.id()), min_pos, max_pos);
                }
            }
            push_linear_sampler(draw_list);
        } else {
            draw_list->AddText(origin, IM_COL32(230, 230, 230, 220), "Texture too large for tile preview");
        }
        draw_list->AddRect(origin,
                           ImVec2(origin.x + preview_size.x, origin.y + preview_size.y),
                           IM_COL32(20, 20, 20, 160));
        for (int i = 1; i < 3; ++i) {
            const float offset = static_cast<float>(i) * cell;
            draw_list->AddLine(ImVec2(origin.x + offset, origin.y),
                               ImVec2(origin.x + offset, origin.y + preview_size.y),
                               IM_COL32(255, 230, 40, 160));
            draw_list->AddLine(ImVec2(origin.x, origin.y + offset),
                               ImVec2(origin.x + preview_size.x, origin.y + offset),
                               IM_COL32(255, 230, 40, 160));
        }
    }
    ImGui::End();
}

void EditorApp::draw_uv_overlay(ImDrawList* draw_list, const ImVec2& origin, float scale) const {
    for (int ci = 0; ci < static_cast<int>(model_.cuboids.size()); ++ci) {
        const auto& cuboid = model_.cuboids[static_cast<std::size_t>(ci)];
        for (int fi = 0; fi < 6; ++fi) {
            const UvRect& uv = cuboid.uv[static_cast<std::size_t>(fi)];
            ImVec2 a(origin.x + pixel_scale(uv.x, scale), origin.y + pixel_scale(uv.y, scale));
            ImVec2 b(origin.x + pixel_scale(uv.x + uv.w, scale), origin.y + pixel_scale(uv.y + uv.h, scale));
            bool selected = ci == model_.selected_cuboid && fi == model_.selected_face;
            draw_list->AddRect(a, b, selected ? IM_COL32(255, 230, 40, 255) : IM_COL32(40, 180, 255, 180), 0.0f, 0, selected ? 3.0f : 1.0f);
            draw_list->AddText(ImVec2(a.x + 2, a.y + 2), IM_COL32(255, 255, 255, 230), face_name(fi));
            if (selected) {
                float handle = 5.0f;
                draw_list->AddRectFilled(ImVec2(a.x - handle, a.y - handle), ImVec2(a.x + handle, a.y + handle), IM_COL32(255, 230, 40, 255));
                draw_list->AddRectFilled(ImVec2(b.x - handle, a.y - handle), ImVec2(b.x + handle, a.y + handle), IM_COL32(255, 230, 40, 255));
                draw_list->AddRectFilled(ImVec2(b.x - handle, b.y - handle), ImVec2(b.x + handle, b.y + handle), IM_COL32(255, 230, 40, 255));
                draw_list->AddRectFilled(ImVec2(a.x - handle, b.y - handle), ImVec2(a.x + handle, b.y + handle), IM_COL32(255, 230, 40, 255));
            }
        }
    }
}

void EditorApp::handle_uv_input(const ImVec2& origin, float scale) {
    if (model_.cuboids.empty() || scale <= 0.0f) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    auto hit_handle = [&](const UvRect& rect) {
        ImVec2 a(origin.x + pixel_scale(rect.x, scale), origin.y + pixel_scale(rect.y, scale));
        ImVec2 b(origin.x + pixel_scale(rect.x + rect.w, scale), origin.y + pixel_scale(rect.y + rect.h, scale));
        auto near_point = [&](ImVec2 p) {
            return std::abs(mouse.x - p.x) <= 7.0f && std::abs(mouse.y - p.y) <= 7.0f;
        };
        if (near_point(a)) return 2;
        if (near_point(ImVec2(b.x, a.y))) return 3;
        if (near_point(b)) return 4;
        if (near_point(ImVec2(a.x, b.y))) return 5;
        if (mouse.x >= a.x && mouse.y >= a.y && mouse.x <= b.x && mouse.y <= b.y) return 1;
        return 0;
    };

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        uv_drag_mode_ = 0;
        for (int ci = static_cast<int>(model_.cuboids.size()) - 1; ci >= 0 && uv_drag_mode_ == 0; --ci) {
            for (int fi = 5; fi >= 0; --fi) {
                UvRect& candidate = model_.cuboids[static_cast<std::size_t>(ci)].uv[static_cast<std::size_t>(fi)];
                int mode = hit_handle(candidate);
                if (mode != 0) {
                    model_.selected_cuboid = ci;
                    model_.selected_face = fi;
                    uv_drag_active_ = true;
                    uv_drag_mode_ = mode;
                    uv_drag_start_mouse_ = mouse;
                    uv_drag_start_rect_ = candidate;
                    break;
                }
            }
        }
    }

    if (uv_drag_active_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        int dx = static_cast<int>(std::round((mouse.x - uv_drag_start_mouse_.x) / scale));
        int dy = static_cast<int>(std::round((mouse.y - uv_drag_start_mouse_.y) / scale));
        UvRect next = uv_drag_start_rect_;
        if (uv_drag_mode_ == 1) {
            next.x += dx;
            next.y += dy;
        } else if (uv_drag_mode_ == 2) {
            next.x += dx;
            next.y += dy;
            next.w -= dx;
            next.h -= dy;
        } else if (uv_drag_mode_ == 3) {
            next.y += dy;
            next.w += dx;
            next.h -= dy;
        } else if (uv_drag_mode_ == 4) {
            next.w += dx;
            next.h += dy;
        } else if (uv_drag_mode_ == 5) {
            next.x += dx;
            next.w -= dx;
            next.h += dy;
        }
        Cuboid& cuboid = model_.selected();
        cuboid.uv[static_cast<std::size_t>(model_.selected_face)] = clamped_uv_rect(next, document_.width, document_.height);
    }

    if (uv_drag_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        uv_drag_active_ = false;
        uv_drag_mode_ = 0;
    }
}

bool EditorApp::ensure_skybox_texture() {
    if (skybox_index_ <= 0) {
        return false;
    }
    if (loaded_skybox_index_ == skybox_index_ && skybox_face_textures_[0].id() != 0) {
        return true;
    }
    loaded_skybox_index_ = -1;
    for (GLCanvasTexture& texture : skybox_face_textures_) {
        texture.destroy();
    }

    const int index = std::clamp(skybox_index_, 0, static_cast<int>(IM_ARRAYSIZE(kSkyboxes)) - 1);
    for (int face = 0; face < static_cast<int>(IM_ARRAYSIZE(kSkyboxFaceNames)); ++face) {
        int width = 0;
        int height = 0;
        std::vector<Pixel> pixels;
        const std::string path = std::string(kSkyboxes[index].directory) + "/" + kSkyboxFaceNames[face] + ".jpg";
        if (!load_jpeg_pixels(path.c_str(), width, height, pixels)) {
            report_error("Load skybox", "Could not load " + path);
            return false;
        }
        skybox_face_textures_[static_cast<std::size_t>(face)].update(width, height, pixels);
    }
    loaded_skybox_index_ = index;
    return skybox_face_textures_[0].id() != 0;
}

void EditorApp::draw_skybox_background(ImDrawList* draw_list, const ImVec2& origin, const ImVec2& size) {
    if (skybox_index_ <= 0 || !ensure_skybox_texture()) {
        return;
    }
    const float pitch = model_viewport_.pitch_degrees;
    int face = 0;
    if (pitch > 58.0f) {
        face = 4;
    } else if (pitch < -58.0f) {
        face = 5;
    } else {
        const float yaw = std::fmod(model_viewport_.yaw_degrees + 360.0f + 45.0f, 360.0f);
        face = static_cast<int>(yaw / 90.0f) % 4;
    }

    GLCanvasTexture& texture = skybox_face_textures_[static_cast<std::size_t>(face)];
    texture.bind_linear_repeat();
    push_linear_sampler(draw_list);

    const float aspect = size.x / std::max(1.0f, size.y);
    ImVec2 uv_min(0.0f, 0.0f);
    ImVec2 uv_max(1.0f, 1.0f);
    if (aspect > 1.0f) {
        const float vertical_span = 1.0f / aspect;
        uv_min.y = (1.0f - vertical_span) * 0.5f;
        uv_max.y = uv_min.y + vertical_span;
    } else {
        const float horizontal_span = aspect;
        uv_min.x = (1.0f - horizontal_span) * 0.5f;
        uv_max.x = uv_min.x + horizontal_span;
    }

    draw_list->AddImage(gl_texture_id(texture.id()),
                        origin,
                        ImVec2(origin.x + size.x, origin.y + size.y),
                        uv_min,
                        uv_max);
    push_linear_sampler(draw_list);
}

bool EditorApp::ensure_transform_icon_textures() {
    if (transform_icons_loaded_) {
        return true;
    }
    for (int mode = 0; mode < static_cast<int>(IM_ARRAYSIZE(kTransformIconAssets)); ++mode) {
        int width = 0;
        int height = 0;
        std::vector<Pixel> pixels;
        const TransformIconAsset& asset = kTransformIconAssets[mode];
        if (!load_png_pixels_from_memory(asset.bytes, asset.byte_count, width, height, pixels)) {
            report_error("Load transform icon", asset.tooltip);
            return false;
        }
        transform_icon_textures_[static_cast<std::size_t>(mode)].update(width, height, pixels);
    }
    transform_icons_loaded_ = true;
    return true;
}

ImVec2 transform_toolbar_button_min(const ImVec2& origin, int mode) {
    return ImVec2(origin.x + kTransformToolbarPadding,
                  origin.y + kTransformToolbarPadding +
                      static_cast<float>(mode) * (kTransformToolbarButtonSize + kTransformToolbarGap));
}

bool EditorApp::handle_model_transform_toolbar_input(const ImVec2& origin, const ImVec2& size) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 toolbar_min(origin.x + kTransformToolbarPadding, origin.y + kTransformToolbarPadding);
    const ImVec2 toolbar_max(toolbar_min.x + kTransformToolbarButtonSize,
                             toolbar_min.y +
                                 static_cast<float>(IM_ARRAYSIZE(kTransformIconAssets)) * kTransformToolbarButtonSize +
                                 static_cast<float>(IM_ARRAYSIZE(kTransformIconAssets) - 1) * kTransformToolbarGap);
    if (!point_in_rect(io.MousePos, toolbar_min, toolbar_max) ||
        !point_in_rect(io.MousePos, origin, ImVec2(origin.x + size.x, origin.y + size.y))) {
        return false;
    }

    for (int mode = 0; mode < static_cast<int>(IM_ARRAYSIZE(kTransformIconAssets)); ++mode) {
        const ImVec2 button_min = transform_toolbar_button_min(origin, mode);
        const ImVec2 button_max(button_min.x + kTransformToolbarButtonSize,
                                button_min.y + kTransformToolbarButtonSize);
        if (!point_in_rect(io.MousePos, button_min, button_max)) {
            continue;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            model_transform_mode_ = mode;
            model_transform_drag_active_ = false;
            model_view_gizmo_drag_active_ = false;
            set_status(std::string("3D transform: ") + kTransformIconAssets[mode].tooltip);
        }
        ImGui::SetTooltip("%s", kTransformIconAssets[mode].tooltip);
        return true;
    }
    return true;
}

void EditorApp::draw_model_transform_toolbar(ImDrawList* draw_list, const ImVec2& origin) {
    const bool icons_ready = ensure_transform_icon_textures();
    for (int mode = 0; mode < static_cast<int>(IM_ARRAYSIZE(kTransformIconAssets)); ++mode) {
        const ImVec2 button_min = transform_toolbar_button_min(origin, mode);
        const ImVec2 button_max(button_min.x + kTransformToolbarButtonSize,
                                button_min.y + kTransformToolbarButtonSize);
        const bool selected = model_transform_mode_ == mode;
        const bool hovered = point_in_rect(ImGui::GetIO().MousePos, button_min, button_max);
        const ImU32 fill = selected ? IM_COL32(52, 128, 230, 238)
                                    : (hovered ? IM_COL32(228, 232, 238, 236) : IM_COL32(198, 204, 212, 218));
        const ImU32 border = selected ? IM_COL32(134, 198, 255, 255) : IM_COL32(20, 24, 30, 210);
        draw_list->AddRectFilled(button_min, button_max, fill, 4.0f);
        draw_list->AddRect(button_min, button_max, border, 4.0f, 0, selected ? 2.4f : 1.2f);

        const ImVec2 icon_min(button_min.x + kTransformToolbarIconInset,
                              button_min.y + kTransformToolbarIconInset);
        const ImVec2 icon_max(button_max.x - kTransformToolbarIconInset,
                              button_max.y - kTransformToolbarIconInset);
        if (icons_ready && transform_icon_textures_[static_cast<std::size_t>(mode)].id() != 0) {
            transform_icon_textures_[static_cast<std::size_t>(mode)].bind_nearest();
            draw_list->AddImage(gl_texture_id(transform_icon_textures_[static_cast<std::size_t>(mode)].id()),
                                icon_min,
                                icon_max);
        } else {
            const char* label = kTransformIconAssets[mode].tooltip;
            draw_list->AddText(ImVec2(button_min.x + 9.0f, button_min.y + 10.0f),
                               IM_COL32(10, 12, 16, 255),
                               label);
        }
    }
}

void EditorApp::draw_model_preview() {
    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 size(std::max(300.0f, available.x), std::max(220.0f, available.y));
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ModelPreview", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    const bool transform_toolbar_consumed_input = hovered && handle_model_transform_toolbar_input(origin, size);
    if (hovered && !transform_toolbar_consumed_input && io.MouseWheel != 0.0f) {
        model_viewport_.distance = std::clamp(model_viewport_.distance - io.MouseWheel * 2.0f, 6.0f, 160.0f);
    }
    if (hovered && !transform_toolbar_consumed_input && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && !io.KeyShift) {
        model_viewport_.yaw_degrees -= io.MouseDelta.x * 0.35f;
        model_viewport_.pitch_degrees = std::clamp(model_viewport_.pitch_degrees + io.MouseDelta.y * 0.35f, -85.0f, 85.0f);
    }
    if (hovered && !transform_toolbar_consumed_input && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && io.KeyShift) {
        model_viewport_.pan_x += io.MouseDelta.x * 0.04f;
        model_viewport_.pan_y -= io.MouseDelta.y * 0.04f;
    }
    AxisGizmoGeometry view_gizmo = build_axis_gizmo_geometry(model_viewport_, origin, size);
    int view_gizmo_hit = hovered && !transform_toolbar_consumed_input ? hit_axis_gizmo(view_gizmo, io.MousePos) : -1;
    bool view_gizmo_consumed_input = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && view_gizmo_hit >= 0) {
        view_gizmo_consumed_input = true;
        model_transform_drag_active_ = false;
        if (view_gizmo_hit < 6) {
            apply_axis_gizmo_view(model_viewport_, view_gizmo_hit);
            set_status(axis_gizmo_status(view_gizmo_hit));
        } else {
            model_view_gizmo_drag_active_ = true;
            model_view_gizmo_drag_start_mouse_ = io.MousePos;
            model_view_gizmo_start_yaw_ = model_viewport_.yaw_degrees;
            model_view_gizmo_start_pitch_ = model_viewport_.pitch_degrees;
        }
    }
    if (model_view_gizmo_drag_active_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 delta(io.MousePos.x - model_view_gizmo_drag_start_mouse_.x,
                           io.MousePos.y - model_view_gizmo_drag_start_mouse_.y);
        model_viewport_.yaw_degrees = model_view_gizmo_start_yaw_ - delta.x * 0.35f;
        model_viewport_.pitch_degrees = std::clamp(model_view_gizmo_start_pitch_ + delta.y * 0.35f, -85.0f, 85.0f);
        view_gizmo_consumed_input = true;
    }
    if (model_view_gizmo_drag_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        model_view_gizmo_drag_active_ = false;
    }
    view_gizmo = build_axis_gizmo_geometry(model_viewport_, origin, size);
    view_gizmo_hit = model_view_gizmo_drag_active_ ? 6 : (hovered && !transform_toolbar_consumed_input ? hit_axis_gizmo(view_gizmo, io.MousePos) : -1);
    const ModelGizmoGeometry transform_gizmo =
        build_model_gizmo_geometry(renderer3d_, model_, model_viewport_, origin, size);
    model_transform_hover_axis_ = hovered && !transform_toolbar_consumed_input && !view_gizmo_consumed_input
                                      ? hit_model_gizmo(transform_gizmo, model_viewport_, model_transform_mode_, io.MousePos)
                                      : -1;
    if (!transform_toolbar_consumed_input && !view_gizmo_consumed_input) {
        handle_model_transform_drag(origin, size, hovered);
    }
    if (hovered && !transform_toolbar_consumed_input && !view_gizmo_consumed_input && model_transform_mode_ == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 local(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
        FaceHit hit = renderer3d_.pick_face(model_, model_viewport_, static_cast<int>(size.x), static_cast<int>(size.y), local.x, local.y);
        if (hit.hit) {
            model_.selected_cuboid = hit.cuboid;
            model_.selected_face = hit.face;
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(38, 42, 46, 255));
    draw_skybox_background(draw_list, origin, size);
    if (!ensure_full_canvas_texture()) {
        draw_list->AddText(origin, IM_COL32(230, 230, 230, 220), "Texture too large for 3D preview");
        draw_model_transform_toolbar(draw_list, origin);
        return;
    }
    const bool rendered_model = renderer3d_.render_model_to_texture(model_,
                                                                    canvas_texture_.id(),
                                                                    document_.width,
                                                                    document_.height,
                                                                    model_viewport_,
                                                                    static_cast<int>(size.x),
                                                                    static_cast<int>(size.y),
                                                                    canvas_pixels());
    if (rendered_model) {
        model_render_error_reported_ = false;
        draw_list->AddImage(gl_texture_id(renderer3d_.texture_id()),
                            origin,
                            ImVec2(origin.x + size.x, origin.y + size.y),
                            ImVec2(0, 1), ImVec2(1, 0));
        draw_axis_gizmo(draw_list, view_gizmo, view_gizmo_hit);
        draw_transform_gizmo(draw_list,
                             transform_gizmo,
                             model_viewport_,
                             model_transform_mode_,
                             model_transform_drag_active_ ? model_transform_axis_ : -1,
                             model_transform_hover_axis_,
                             model_transform_start_angle_radians_,
                             model_transform_rotation_delta_degrees_);

        float world_center_x = 0.0f;
        float world_center_y = 0.0f;
        if (renderer3d_.project_world_point(model_,
                                            model_viewport_,
                                            static_cast<int>(size.x),
                                            static_cast<int>(size.y),
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            world_center_x,
                                            world_center_y)) {
            const ImVec2 center(origin.x + world_center_x, origin.y + world_center_y);
            const ImVec2 min_pos(origin.x, origin.y);
            const ImVec2 max_pos(origin.x + size.x, origin.y + size.y);
            if (center.x >= min_pos.x && center.y >= min_pos.y && center.x <= max_pos.x && center.y <= max_pos.y) {
                draw_list->AddCircleFilled(center, 3.0f, IM_COL32(255, 255, 255, 255), 12);
                draw_list->AddCircle(center, 5.0f, IM_COL32(0, 0, 0, 180), 12, 1.0f);
            }
        }
    } else {
        if (!model_render_error_reported_) {
            const std::string details = renderer3d_.last_error().empty()
                                            ? "Renderer3D failed without returning a diagnostic message."
                                            : renderer3d_.last_error();
            report_error("3D preview render", details);
            model_render_error_reported_ = true;
        }
        draw_list->AddText(ImVec2(origin.x + 12, origin.y + 12), IM_COL32(255, 255, 255, 230), "OpenGL preview unavailable");
    }
    draw_list->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                       hovered ? IM_COL32(255, 230, 40, 220) : IM_COL32(20, 20, 20, 120));
    draw_model_transform_toolbar(draw_list, origin);
}

void EditorApp::handle_model_transform_drag(const ImVec2& origin, const ImVec2& size, bool hovered) {
    if (model_transform_mode_ == 0 || model_.cuboids.empty()) {
        model_transform_drag_active_ = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    const ModelGizmoGeometry transform_gizmo =
        build_model_gizmo_geometry(renderer3d_, model_, model_viewport_, origin, size);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && transform_gizmo.visible) {
        const int hit_axis = hit_model_gizmo(transform_gizmo, model_viewport_, model_transform_mode_, io.MousePos);
        if (hit_axis < 0) {
            return;
        }
        model_transform_drag_active_ = true;
        model_transform_axis_ = hit_axis;
        model_transform_start_mouse_ = io.MousePos;
        model_transform_drag_center_ = transform_gizmo.center;
        model_transform_start_cuboid_ = model_.selected();
        model_transform_start_angle_radians_ =
            rotation_ring_angle_at_mouse(transform_gizmo, model_viewport_, hit_axis, io.MousePos);
        model_transform_rotation_delta_degrees_ = 0.0f;
    }
    if (model_transform_drag_active_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 delta(io.MousePos.x - model_transform_start_mouse_.x,
                           io.MousePos.y - model_transform_start_mouse_.y);
        constexpr float transform_drag_threshold_pixels = 4.0f;
        if ((delta.x * delta.x + delta.y * delta.y) <
            transform_drag_threshold_pixels * transform_drag_threshold_pixels) {
            model_transform_rotation_delta_degrees_ = 0.0f;
            return;
        }

        Cuboid next = model_transform_start_cuboid_;
        const ImVec2 axis = normalized_screen_axis({model_transform_axis_ == 0 ? 1.0f : 0.0f,
                                                    model_transform_axis_ == 1 ? 1.0f : 0.0f,
                                                    model_transform_axis_ == 2 ? 1.0f : 0.0f},
                                                   model_viewport_);
        const float signed_pixels = delta.x * axis.x + delta.y * axis.y;
        const bool constrained = io.KeyCtrl || io.KeySuper;
        if (model_transform_mode_ == 1) {
            translate_cuboid(next, model_transform_axis_, signed_pixels * 0.06f, constrained);
        } else if (model_transform_mode_ == 2) {
            const float current_angle =
                rotation_ring_angle_at_mouse(transform_gizmo, model_viewport_, model_transform_axis_, io.MousePos);
            float angle_delta = unwrap_angle_delta(current_angle, model_transform_start_angle_radians_) * 180.0f / pi;
            if (io.KeyShift) {
                angle_delta *= 0.20f;
            }
            if (constrained) {
                angle_delta = std::round(angle_delta / 15.0f) * 15.0f;
            }
            model_transform_rotation_delta_degrees_ = angle_delta;
            rotate_cuboid(next,
                          model_transform_axis_,
                          model_transform_start_cuboid_.rotation_angle + angle_delta,
                          false);
        } else if (model_transform_mode_ == 3) {
            scale_cuboid(next, model_transform_axis_, 1.0f + signed_pixels * 0.012f, constrained);
        }
        model_.selected() = next;
    }
    if (model_transform_drag_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        model_transform_drag_active_ = false;
        model_transform_rotation_delta_degrees_ = 0.0f;
    }
}

void EditorApp::draw_status_bar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 24.0f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 24.0f));
    ImGui::Begin("Status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const int layer_count = static_cast<int>(document_.layers.size());
    const int shown_layer = layer_count == 0 ? 0 : document_.active_layer + 1;
    ImGui::Text("%s | %dx%d | Frame %d/%d | Layer %d/%d | %s",
                status_, document_.width, document_.height,
                document_.active_frame + 1, static_cast<int>(document_.frames.size()),
                shown_layer, layer_count,
                tool_name(tool_.tool));
    ImGui::End();
}

void EditorApp::draw_error_console() {
    if (!error_console_open_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(560.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Error Console", &error_console_open_)) {
        ImGui::TextDisabled("%d captured error%s",
                            static_cast<int>(error_console_entries_.size()),
                            error_console_entries_.size() == 1U ? "" : "s");
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            std::string text;
            for (const ErrorConsoleEntry& entry : error_console_entries_) {
                text += "#";
                text += std::to_string(entry.sequence);
                text += " ";
                text += entry.context;
                text += "\n";
                if (!entry.details.empty()) {
                    text += entry.details;
                    text += "\n";
                }
                text += "\n";
            }
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            error_console_entries_.clear();
        }

        ImGui::Separator();
        if (error_console_entries_.empty()) {
            ImGui::TextDisabled("No errors captured.");
        } else {
            if (ImGui::BeginChild("ErrorConsoleScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar)) {
                for (const ErrorConsoleEntry& entry : error_console_entries_) {
                    ImGui::PushID(entry.sequence);
                    ImGui::TextColored(ImVec4(0.72f, 0.08f, 0.08f, 1.0f), "#%d %s", entry.sequence, entry.context.c_str());
                    if (!entry.details.empty()) {
                        ImGui::TextWrapped("%s", entry.details.c_str());
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (error_console_scroll_to_bottom_) {
                    ImGui::SetScrollHereY(1.0f);
                    error_console_scroll_to_bottom_ = false;
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

} // namespace px
