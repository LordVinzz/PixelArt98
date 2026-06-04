// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "ui/EditorApp.hpp"

#include "io/ProjectIO.hpp"
#include "ui/EmbeddedTransformIcons.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <string_view>
#include <utility>

namespace px {

namespace {

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
        case EffectPreviewKind::Levels: return "Levels";
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

constexpr FileFilter kProjectFilters[] = {{"PixelArt project", "pixart"}};
constexpr FileFilter kImageFilters[] = {{"Image files", "png,jpg,jpeg,bmp,tga"}};
constexpr FileFilter kPngFilters[] = {{"PNG image", "png"}};
constexpr FileFilter kGifFilters[] = {{"GIF animation", "gif"}};
constexpr FileFilter kAsepriteFilters[] = {{"Aseprite sprite", "aseprite,ase"}};
constexpr FileFilter kJsonFilters[] = {{"JSON", "json"}};

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
    tool_.primary = rgba(0, 0, 0);
    tool_.secondary = rgba(255, 255, 255);
}

void EditorApp::render() {
    update_playback();
    refresh_texture();
    begin_history_frame();
    draw_dockspace();
    draw_main_menu();
    draw_toolbar();
    draw_canvas();
    draw_color_panel();
    draw_layers_panel();
    draw_timeline_panel();
    draw_adjustments_panel();
    draw_model_panel();
    draw_model_preview_window();
    draw_tile_preview_window();
    draw_effect_preview_popup();
    draw_error_console();
    draw_status_bar();
    end_history_frame();
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
    }
}

void EditorApp::refresh_texture() {
    if (effect_preview_active_) {
        return;
    }
    if (!texture_dirty_) {
        return;
    }
    composite_ = document_.composite_active();
    canvas_texture_.update(document_.width, document_.height, composite_);
    texture_dirty_ = false;
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

void EditorApp::sync_model_texture_metadata() {
    model_.texture_width = document_.width;
    model_.texture_height = document_.height;
    clamp_model_uvs(model_);
}

void EditorApp::begin_history_frame() {
    if (!history_pending_) {
        history_before_document_ = document_;
        history_before_model_ = model_;
    }
}

bool EditorApp::editor_state_changed_from_history_baseline() const {
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

void EditorApp::push_history_entry(const std::string& name,
                                   Document before_document,
                                   ModelDocument before_model,
                                   Document after_document,
                                   ModelDocument after_model) {
    if (document_state_equal(before_document, after_document) && model_state_equal(before_model, after_model)) {
        return;
    }
    undo_stack_.push_back({name, std::move(before_document), std::move(after_document), std::move(before_model), std::move(after_model)});
    if (undo_stack_.size() > 128) {
        undo_stack_.pop_front();
    }
    redo_stack_.clear();
}

void EditorApp::end_history_frame() {
    if (history_suppress_frame_) {
        history_pending_ = false;
        history_before_document_ = document_;
        history_before_model_ = model_;
        history_suppress_frame_ = false;
        return;
    }

    if (!editor_state_changed_from_history_baseline()) {
        history_pending_ = false;
        return;
    }
    if (history_interaction_in_progress()) {
        history_pending_ = true;
        return;
    }
    push_history_entry("Edit", history_before_document_, history_before_model_, document_, model_);
    history_pending_ = false;
    history_before_document_ = document_;
    history_before_model_ = model_;
}

bool EditorApp::undo_editor() {
    if (!undo_stack_.empty()) {
        EditorHistoryEntry entry = std::move(undo_stack_.back());
        undo_stack_.pop_back();
        document_ = entry.before_document;
        model_ = entry.before_model;
        redo_stack_.push_back(std::move(entry));
        sync_model_texture_metadata();
        texture_dirty_ = true;
        history_pending_ = false;
        history_suppress_frame_ = true;
        set_status("Undo");
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
    if (!redo_stack_.empty()) {
        EditorHistoryEntry entry = std::move(redo_stack_.back());
        redo_stack_.pop_back();
        document_ = entry.after_document;
        model_ = entry.after_model;
        undo_stack_.push_back(std::move(entry));
        sync_model_texture_metadata();
        texture_dirty_ = true;
        history_pending_ = false;
        history_suppress_frame_ = true;
        set_status("Redo");
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
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        ImGui::InputInt("Width", &new_width_);
        ImGui::InputInt("Height", &new_height_);
        if (ImGui::MenuItem("New")) {
            document_ = Document::create(std::clamp(new_width_, 1, 4096), std::clamp(new_height_, 1, 4096));
            model_ = ModelDocument::create_default();
            sync_model_texture_metadata();
            texture_dirty_ = true;
            set_status("New document created");
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
                std::string error;
                Document imported;
                if (import_image(path, imported, &error)) {
                    document_ = std::move(imported);
                    sync_model_texture_metadata();
                    texture_dirty_ = true;
                    set_status(std::string("Imported ") + path);
                } else {
                    report_error("Import image as document: " + path, error);
                }
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
        if (ImGui::MenuItem("Flip Horizontal")) {
            apply_flip_horizontal(document_);
            texture_dirty_ = true;
            set_status("Applied Flip Horizontal");
        }
        if (ImGui::MenuItem("Flip Vertical")) {
            apply_flip_vertical(document_);
            texture_dirty_ = true;
            set_status("Applied Flip Vertical");
        }
        if (ImGui::MenuItem("Rotate 90 Clockwise")) {
            apply_rotate_90_clockwise(document_);
            texture_dirty_ = true;
            set_status("Applied Rotate 90 Clockwise");
        }
        if (ImGui::MenuItem("Rotate 90 Counter-Clockwise")) {
            apply_rotate_90_counter_clockwise(document_);
            texture_dirty_ = true;
            set_status("Applied Rotate 90 Counter-Clockwise");
        }
        if (ImGui::MenuItem("Rotate 180")) {
            apply_rotate_180(document_);
            texture_dirty_ = true;
            set_status("Applied Rotate 180");
        }
        if (ImGui::MenuItem("Straighten")) {
            apply_straighten(document_, static_cast<float>(effect_angle_));
            texture_dirty_ = true;
            set_status("Applied Straighten");
        }
        if (ImGui::MenuItem("Rotate / Zoom")) {
            apply_rotate_zoom(document_, static_cast<float>(effect_angle_), effect_zoom_, 0, 0);
            texture_dirty_ = true;
            set_status("Applied Rotate / Zoom");
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
        ImGui::MenuItem("Tile Preview", nullptr, &show_tile_preview_);
        ImGui::MenuItem("Error Console", nullptr, &error_console_open_);
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
    if (ImGui::BeginMenu("Options")) {
        if (ImGui::MenuItem("Show Splash Screen", nullptr, &settings_.show_splash_screen)) {
            save_settings();
        }
        if (ImGui::MenuItem("Auto-open Error Console", nullptr, &settings_.auto_open_error_console)) {
            save_settings();
        }
        ImGui::EndMenu();
    }
    ImGui::TextDisabled("PixelArt98");
    ImGui::EndMainMenuBar();
}

void EditorApp::draw_toolbar() {
    ImGui::Begin("Tools");
    constexpr ToolType tools[] = {
        ToolType::Pencil, ToolType::Brush, ToolType::Eraser, ToolType::Line,
        ToolType::Rectangle, ToolType::Ellipse, ToolType::Bucket, ToolType::Gradient,
        ToolType::Eyedropper, ToolType::CloneStamp, ToolType::RectSelect, ToolType::LassoSelect,
        ToolType::MagicWand, ToolType::MovePixels, ToolType::Text
    };
    for (ToolType tool : tools) {
        bool selected = tool_.tool == tool;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(tool_name(tool), ImVec2(132, 0))) {
            tool_.tool = tool;
        }
        if (selected) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::SliderInt("Stroke", &tool_.brush_size, 1, 32);
    ImGui::SliderInt("Tolerance", &tool_.tolerance, 0, 442);
    ImGui::Checkbox("Contiguous", &tool_.contiguous);
    ImGui::SeparatorText("Mask");
    ImGui::Checkbox("Edit Active Mask", &edit_layer_mask_);
    ImGui::Checkbox("Mask Overlay", &show_mask_overlay_);
    if (edit_layer_mask_) {
        ImGui::TextDisabled("Brushes edit grayscale mask values.");
    }
    ImGui::InputText("Text", text_buffer_, sizeof(text_buffer_));
    if (text_box_.active) {
        ImGui::Text("Text box at %d, %d", text_box_.x, text_box_.y);
        if (ImGui::Button("Commit Text")) commit_text_box();
        ImGui::SameLine();
        if (ImGui::Button("Cancel Text")) cancel_text_box();
    }
    ImGui::Text("Clone source: %d, %d", tool_.clone_source_x, tool_.clone_source_y);
    ImGui::End();
}

void EditorApp::draw_canvas() {
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

    if (show_checker_) {
        float tile = std::max(4.0f, zoom_ * 2.0f);
        for (float y = origin.y; y < max.y; y += tile) {
            for (float x = origin.x; x < max.x; x += tile) {
                bool dark = (static_cast<int>((x - origin.x) / tile) + static_cast<int>((y - origin.y) / tile)) % 2 == 0;
                draw_list->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + tile, max.x), std::min(y + tile, max.y)),
                                         dark ? IM_COL32(150, 150, 150, 255) : IM_COL32(205, 205, 205, 255));
            }
        }
    }

    if (onion_skin_ && document_.frames.size() > 1 && document_.active_frame > 0) {
        auto previous = document_.composite_frame(document_.active_frame - 1);
        onion_texture_.update(document_.width, document_.height, previous);
        onion_texture_.bind_nearest();
        push_nearest_sampler(draw_list);
        draw_list->AddImage(gl_texture_id(onion_texture_.id()), origin, max,
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 160, 160, 90));
    }
    canvas_texture_.bind_nearest();
    push_nearest_sampler(draw_list);
    draw_list->AddImage(gl_texture_id(canvas_texture_.id()), origin, max);
    push_linear_sampler(draw_list);
    draw_mask_overlay(draw_list, origin, canvas_size);
    draw_grid_overlay(draw_list, origin, canvas_size);
    draw_floating_selection_overlay(draw_list, origin);
    draw_text_preview_overlay(draw_list, origin);
    draw_selected_model_face_overlay(draw_list, origin);
    draw_selection_overlay(draw_list, origin);
    draw_lasso_preview(draw_list, origin);
    draw_tool_drag_preview(draw_list, origin);
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
    const bool mask_editing = edit_layer_mask_ &&
                              document_.active_layer >= 0 &&
                              document_.active_layer < static_cast<int>(document_.layers.size());
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
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_Z)) {
        texture_dirty_ = undo_editor() || texture_dirty_;
    }
    if (can_use_canvas_shortcuts && (shortcut_ctrl_or_super(ImGuiKey_Y) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::Shortcut(ImGuiMod_Super | ImGuiMod_Shift | ImGuiKey_Z))) {
        texture_dirty_ = redo_editor() || texture_dirty_;
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
        tool_.clone_source_x = px_i;
        tool_.clone_source_y = py_i;
        set_status("Clone source set");
    }

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && tool_.tool == ToolType::Bucket) {
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

    if (over_pixel && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
    if (!pixel_drag_preview_active_ || stroke_before_.size() != document_.active_cel().pixels.size()) {
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
    if (stroke_before_.size() == document_.active_cel().pixels.size()) {
        document_.active_cel().pixels = std::move(stroke_before_);
    }
    pixel_drag_preview_active_ = false;
    drag_active_ = false;
    texture_dirty_ = true;
}

bool EditorApp::delete_selection_contents() {
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
    ImGui::Begin("Colors");
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
        apply_palette_quantize(document_, document_.palette, false);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Dither Remap")) {
        apply_palette_quantize(document_, document_.palette, true);
        texture_dirty_ = true;
    }
    ImGui::End();
}

void EditorApp::draw_layers_panel() {
    ImGui::Begin("Layers");
    if (ImGui::Button("+ Layer")) {
        document_.add_layer("Layer " + std::to_string(document_.layers.size() + 1));
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("- Layer")) {
        texture_dirty_ = document_.remove_layer(document_.active_layer) || texture_dirty_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        document_.duplicate_layer(document_.active_layer);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Merge Down")) {
        texture_dirty_ = document_.merge_layer_down(document_.active_layer) || texture_dirty_;
    }
    if (ImGui::Button("Move Up")) {
        document_.move_layer(document_.active_layer, std::min(static_cast<int>(document_.layers.size()) - 1, document_.active_layer + 1));
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Move Down")) {
        document_.move_layer(document_.active_layer, std::max(0, document_.active_layer - 1));
        texture_dirty_ = true;
    }
    for (int i = static_cast<int>(document_.layers.size()) - 1; i >= 0; --i) {
        ImGui::PushID(i);
        Layer& layer = document_.layers[static_cast<std::size_t>(i)];
        bool selected = document_.active_layer == i;
        char name[128];
        copy_path(name, sizeof(name), layer.name);
        if (ImGui::Selectable("##select", selected, 0, ImVec2(18, 0))) {
            document_.active_layer = i;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        if (ImGui::InputText("Name", name, sizeof(name))) {
            layer.name = name;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("V", &layer.visible)) texture_dirty_ = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::SliderFloat("Opacity", &layer.opacity, 0.0f, 1.0f, "%.2f")) texture_dirty_ = true;
        ImGui::SetNextItemWidth(120);
        int blend = static_cast<int>(layer.blend_mode);
        const char* modes[] = {"Normal", "Multiply", "Additive", "Color Burn", "Color Dodge", "Reflect", "Glow",
                               "Overlay", "Difference", "Negation", "Lighten", "Darken", "Screen", "Xor"};
        if (ImGui::Combo("Blend", &blend, modes, IM_ARRAYSIZE(modes))) {
            layer.blend_mode = static_cast<LayerBlendMode>(blend);
            texture_dirty_ = true;
        }
        if (ImGui::Checkbox("Mask", &layer.mask_enabled)) {
            ensure_layer_mask(layer, document_.width, document_.height, 255);
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Clip", &layer.clip_to_below)) {
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", layer.mask.empty() ? "No mask" : "Mask ready");
        if (ImGui::Button("Reveal")) {
            layer.mask.assign(static_cast<std::size_t>(document_.width * document_.height), 255);
            layer.mask_enabled = true;
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Hide")) {
            layer.mask.assign(static_cast<std::size_t>(document_.width * document_.height), 0);
            layer.mask_enabled = true;
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("From Selection")) {
            fill_layer_mask_from_selection(layer, document_.selection, document_.width, document_.height);
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("From Alpha")) {
            fill_layer_mask_from_alpha(layer, document_.cel(document_.active_frame, i), document_.width, document_.height);
            texture_dirty_ = true;
        }
        if (ImGui::Button("Invert Mask")) {
            invert_layer_mask(layer, document_.width, document_.height);
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Mask")) {
            layer.mask.clear();
            layer.mask_enabled = false;
            texture_dirty_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Select Mask")) {
            load_selection_from_layer_mask(document_.selection, layer, document_.width, document_.height);
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void EditorApp::draw_timeline_panel() {
    ImGui::Begin("Animation");
    if (ImGui::Button(playing_ ? "Pause" : "Play")) {
        playing_ = !playing_;
        playback_direction_ = 1;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Onion Skin", &onion_skin_);
    ImGui::SameLine();
    int playback_mode = static_cast<int>(document_.playback_mode);
    const char* playback_modes[] = {"Loop", "Ping-Pong"};
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Mode", &playback_mode, playback_modes, IM_ARRAYSIZE(playback_modes))) {
        document_.playback_mode = static_cast<PlaybackMode>(playback_mode);
        playback_direction_ = 1;
    }
    if (ImGui::Button("+ Blank")) {
        document_.add_frame(false);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Duplicate")) {
        document_.add_frame(true);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("- Frame")) {
        texture_dirty_ = document_.remove_frame(document_.active_frame) || texture_dirty_;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Frame")) {
        document_.move_frame(document_.active_frame, std::max(0, document_.active_frame - 1));
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame >")) {
        document_.move_frame(document_.active_frame, std::min(static_cast<int>(document_.frames.size()) - 1, document_.active_frame + 1));
        texture_dirty_ = true;
    }
    for (int i = 0; i < static_cast<int>(document_.frames.size()); ++i) {
        ImGui::PushID(i);
        bool selected = i == document_.active_frame;
        char label[32];
        std::snprintf(label, sizeof(label), "F%d", i + 1);
        if (ImGui::Selectable(label, selected, 0, ImVec2(42, 24))) {
            document_.active_frame = i;
            texture_dirty_ = true;
        }
        if ((i + 1) % 12 != 0) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::NewLine();
    ImGui::SliderInt("Frame duration ms", &document_.frames[static_cast<std::size_t>(document_.active_frame)].duration_ms, 20, 2000);
    ImGui::SeparatorText("Tags");
    ImGui::InputText("Tag Name", tag_name_, sizeof(tag_name_));
    if (ImGui::Button("+ Tag")) {
        document_.add_tag(tag_name_, document_.active_frame, document_.active_frame);
    }
    for (int i = 0; i < static_cast<int>(document_.tags.size()); ++i) {
        ImGui::PushID(i);
        AnimationTag& tag = document_.tags[static_cast<std::size_t>(i)];
        char name[96];
        copy_path(name, sizeof(name), tag.name);
        if (ImGui::InputText("Name", name, sizeof(name))) tag.name = name;
        ImGui::SetNextItemWidth(80);
        ImGui::SliderInt("From", &tag.from, 0, static_cast<int>(document_.frames.size()) - 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::SliderInt("To", &tag.to, tag.from, static_cast<int>(document_.frames.size()) - 1);
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            document_.remove_tag(i);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void EditorApp::draw_adjustments_panel() {
    ImGui::Begin("Adjustments");
    draw_histogram_plot();
    ImGui::SeparatorText("Core");
    ImGui::SliderInt("Brightness", &brightness_, -255, 255);
    ImGui::SliderInt("Contrast", &contrast_, -255, 255);
    if (ImGui::Button("Apply Brightness / Contrast")) {
        apply_brightness_contrast(document_, brightness_, contrast_);
        brightness_ = 0;
        contrast_ = 0;
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview##BrightnessContrast")) {
        start_effect_preview(EffectPreviewKind::BrightnessContrast);
    }
    ImGui::SliderFloat("Hue", &hue_, -180.0f, 180.0f, "%.1f");
    ImGui::SliderFloat("Saturation", &saturation_, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Value", &value_, -1.0f, 1.0f, "%.2f");
    if (ImGui::Button("Apply HSV")) {
        apply_hsv(document_, hue_, saturation_, value_);
        hue_ = saturation_ = value_ = 0.0f;
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview##HSV")) {
        start_effect_preview(EffectPreviewKind::Hsv);
    }
    ImGui::SeparatorText("Levels");
    ImGui::SliderInt("Input Black", &levels_.in_black, 0, 254);
    ImGui::SliderInt("Input White", &levels_.in_white, 1, 255);
    ImGui::SliderFloat("Gamma", &levels_.gamma, 0.1f, 4.0f);
    ImGui::SliderInt("Output Black", &levels_.out_black, 0, 255);
    ImGui::SliderInt("Output White", &levels_.out_white, 0, 255);
    if (ImGui::Button("Apply Levels")) {
        apply_levels(document_, levels_);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview##Levels")) {
        start_effect_preview(EffectPreviewKind::Levels);
    }
    ImGui::SeparatorText("Pixel Art");
    ImGui::SliderInt("Posterize Levels", &posterize_levels_, 2, 32);
    if (ImGui::Button("Posterize")) {
        apply_posterize(document_, posterize_levels_);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Invert")) {
        apply_invert(document_, false);
        texture_dirty_ = true;
    }
    if (ImGui::Button("Grayscale")) {
        apply_grayscale(document_);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Quantize")) {
        apply_palette_quantize(document_, document_.palette, false);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview Quantize")) {
        start_effect_preview(EffectPreviewKind::PaletteQuantize);
    }
    ImGui::SameLine();
    if (ImGui::Button("Dither")) {
        apply_palette_quantize(document_, document_.palette, true);
        texture_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Preview Dither")) {
        start_effect_preview(EffectPreviewKind::PaletteDither);
    }
    ImGui::SeparatorText("Paint.NET-style Effects");
    ImGui::SliderInt("Effect Radius", &effect_radius_, 1, 32);
    ImGui::SliderInt("Effect Amount", &effect_amount_, 0, 100);
    ImGui::SliderInt("Effect Angle", &effect_angle_, -180, 180);
    ImGui::SliderInt("Effect Cell Size", &effect_cell_size_, 2, 128);
    ImGui::SliderInt("Effect Scale", &effect_scale_, 2, 256);
    ImGui::SliderInt("Noise Intensity", &effect_noise_, 0, 255);
    ImGui::SliderFloat("Effect Strength", &effect_strength_, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Effect Zoom", &effect_zoom_, 0.1f, 8.0f, "%.2f");
    if (ImGui::Button("Blur")) {
        start_effect_preview(EffectPreviewKind::GaussianBlur);
    }
    ImGui::SameLine();
    if (ImGui::Button("Sharpen")) {
        start_effect_preview(EffectPreviewKind::Sharpen);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pixelate")) {
        start_effect_preview(EffectPreviewKind::Pixelate);
    }
    if (ImGui::Button("Glow")) {
        start_effect_preview(EffectPreviewKind::Glow);
    }
    ImGui::SameLine();
    if (ImGui::Button("Vignette")) {
        start_effect_preview(EffectPreviewKind::Vignette);
    }
    ImGui::SameLine();
    if (ImGui::Button("Twist")) {
        start_effect_preview(EffectPreviewKind::Twist);
    }
    if (ImGui::Button("Clouds")) {
        start_effect_preview(EffectPreviewKind::Clouds);
    }
    ImGui::SameLine();
    if (ImGui::Button("Mandelbrot")) {
        start_effect_preview(EffectPreviewKind::MandelbrotFractal);
    }
    ImGui::SameLine();
    if (ImGui::Button("Edge Detect")) {
        start_effect_preview(EffectPreviewKind::EdgeDetect);
    }
    ImGui::End();
}

void EditorApp::start_effect_preview(EffectPreviewKind kind) {
    if (kind == EffectPreviewKind::None) {
        return;
    }
    effect_preview_kind_ = kind;
    effect_preview_active_ = true;
    effect_preview_dirty_ = true;
    effect_preview_popup_requested_ = true;
    rebuild_effect_preview();
    set_status(std::string("Previewing ") + effect_preview_name(kind));
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
        case EffectPreviewKind::Levels:
            apply_levels(target, levels_);
            break;
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
    effect_preview_document_ = document_;
    apply_effect_to(effect_preview_document_);
    composite_ = effect_preview_document_.composite_active();
    canvas_texture_.update(effect_preview_document_.width, effect_preview_document_.height, composite_);
    effect_preview_dirty_ = false;
}

void EditorApp::apply_effect_preview_to_document() {
    if (!effect_preview_active_ || effect_preview_kind_ == EffectPreviewKind::None) {
        return;
    }
    const std::string name = effect_preview_name(effect_preview_kind_);
    apply_effect_to(document_);
    set_status(std::string("Applied ") + name);
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
        case EffectPreviewKind::Levels:
            changed |= ImGui::SliderInt("Input Black", &levels_.in_black, 0, 254);
            changed |= ImGui::SliderInt("Input White", &levels_.in_white, 1, 255);
            changed |= ImGui::SliderFloat("Gamma", &levels_.gamma, 0.1f, 4.0f);
            changed |= ImGui::SliderInt("Output Black", &levels_.out_black, 0, 255);
            changed |= ImGui::SliderInt("Output White", &levels_.out_white, 0, 255);
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

void EditorApp::draw_histogram_plot() {
    auto hist = document_.histogram_luma();
    static float values[256];
    int max_value = 1;
    for (int v : hist) max_value = std::max(max_value, v);
    for (int i = 0; i < 256; ++i) {
        values[i] = static_cast<float>(hist[static_cast<std::size_t>(i)]) / static_cast<float>(max_value);
    }
    ImGui::PlotHistogram("Luma", values, 256, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 80));
}

void EditorApp::draw_model_panel() {
    ImGui::Begin("Model / UV");
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
    canvas_texture_.bind_nearest();
    push_nearest_sampler(draw_list);
    draw_list->AddImage(gl_texture_id(canvas_texture_.id()), origin, ImVec2(origin.x + atlas_size.x, origin.y + atlas_size.y));
    push_linear_sampler(draw_list);
    draw_uv_overlay(draw_list, origin, uv_scale);
    ImGui::End();
}

void EditorApp::draw_model_preview_window() {
    ImGui::SetNextWindowSize(ImVec2(460, 340), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("3D Preview")) {
        draw_model_preview();
    }
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
    const bool rendered_model = renderer3d_.render_model_to_texture(model_,
                                                                    canvas_texture_.id(),
                                                                    document_.width,
                                                                    document_.height,
                                                                    model_viewport_,
                                                                    static_cast<int>(size.x),
                                                                    static_cast<int>(size.y),
                                                                    composite_);
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
    ImGui::Text("%s | %dx%d | Frame %d/%d | Layer %d/%d | %s",
                status_, document_.width, document_.height,
                document_.active_frame + 1, static_cast<int>(document_.frames.size()),
                document_.active_layer + 1, static_cast<int>(document_.layers.size()),
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
