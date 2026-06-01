#include "ui/EditorApp.hpp"

#include "io/ProjectIO.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

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

float radians(float degrees) {
    return degrees * 3.14159265358979323846f / 180.0f;
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

    float best_distance = 14.0f;
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
                          int hover_axis) {
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

const char* model_transform_mode_name(int mode) {
    static const char* names[] = {"Select", "Translate", "Rotate", "Scale"};
    return names[std::clamp(mode, 0, 3)];
}

const char* model_transform_axis_name(int axis) {
    static const char* names[] = {"X", "Y", "Z"};
    return names[std::clamp(axis, 0, 2)];
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

float pixel_zoom(float zoom) {
    return static_cast<float>(std::clamp(static_cast<int>(std::round(zoom)), 1, 64));
}

ImVec2 floor_screen_pos(ImVec2 value) {
    return ImVec2(std::floor(value.x), std::floor(value.y));
}

float pixel_scale(int value, float scale) {
    return static_cast<float>(value) * scale;
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

constexpr FileFilter kProjectFilters[] = {{"PixelArt project", "pixart"}};
constexpr FileFilter kImageFilters[] = {{"Image files", "png,jpg,jpeg,bmp,tga"}};
constexpr FileFilter kPngFilters[] = {{"PNG image", "png"}};
constexpr FileFilter kGifFilters[] = {{"GIF animation", "gif"}};
constexpr FileFilter kAsepriteFilters[] = {{"Aseprite sprite", "aseprite,ase"}};
constexpr FileFilter kJsonFilters[] = {{"JSON", "json"}};

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
    draw_effect_preview_popup();
    draw_error_console();
    draw_status_bar();
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
            model_.texture_width = document_.width;
            model_.texture_height = document_.height;
            clamp_model_uvs(model_);
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
                    model_.texture_width = document_.width;
                    model_.texture_height = document_.height;
                    clamp_model_uvs(model_);
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
                    model_.texture_width = document_.width;
                    model_.texture_height = document_.height;
                    clamp_model_uvs(model_);
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
        if (ImGui::MenuItem("Undo", "Ctrl+Z")) texture_dirty_ = document_.undo() || texture_dirty_;
        if (ImGui::MenuItem("Redo", "Ctrl+Y")) texture_dirty_ = document_.redo() || texture_dirty_;
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
        ImGui::MenuItem("Error Console", nullptr, &error_console_open_);
        int zoom_value = static_cast<int>(pixel_zoom(zoom_));
        if (ImGui::SliderInt("Zoom", &zoom_value, 1, 64, "%dx")) {
            zoom_ = static_cast<float>(zoom_value);
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
    int zoom_value = static_cast<int>(pixel_zoom(zoom_));
    if (ImGui::SliderInt("Zoom", &zoom_value, 1, 64, "%dx")) {
        zoom_ = static_cast<float>(zoom_value);
    } else {
        zoom_ = pixel_zoom(zoom_);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &show_grid_);
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &onion_skin_);

    float z = pixel_zoom(zoom_);
    ImVec2 canvas_size(pixel_scale(document_.width, z), pixel_scale(document_.height, z));
    ImGui::BeginChild("CanvasScroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 origin = floor_screen_pos(ImGui::GetCursorScreenPos());
    ImGui::InvisibleButton("CanvasHitTarget", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImDrawListFlags old_flags = draw_list->Flags;
    draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);
    ImVec2 max(origin.x + canvas_size.x, origin.y + canvas_size.y);

    if (show_checker_) {
        float tile = std::max(4.0f, z * 2.0f);
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
    draw_grid_overlay(draw_list, origin, canvas_size);
    draw_floating_selection_overlay(draw_list, origin);
    draw_text_preview_overlay(draw_list, origin);
    draw_selected_model_face_overlay(draw_list, origin);
    draw_selection_overlay(draw_list, origin);
    draw_lasso_preview(draw_list, origin);
    draw_tool_drag_preview(draw_list, origin);
    handle_canvas_input(origin, canvas_size, hovered);
    draw_list->Flags = old_flags;
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::handle_canvas_input(const ImVec2& origin, const ImVec2&, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    int px_i = 0;
    int py_i = 0;
    bool over_pixel = hovered && mouse_to_pixel(origin, px_i, py_i);
    const bool can_use_canvas_shortcuts = !text_box_.active && !io.WantTextInput && !ImGui::IsAnyItemActive();

    if (hovered && action_modifier_down(io) && io.MouseWheel != 0.0f) {
        zoom_ = std::clamp(pixel_zoom(zoom_) + io.MouseWheel, 1.0f, 64.0f);
    }
    const bool space_down = ImGui::IsKeyDown(ImGuiKey_Space);
    if (hovered && space_down && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        return;
    }
    if (hovered && space_down && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    if (shortcut_ctrl_or_super(ImGuiKey_Z)) {
        texture_dirty_ = document_.undo() || texture_dirty_;
    }
    if (shortcut_ctrl_or_super(ImGuiKey_Y) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::Shortcut(ImGuiMod_Super | ImGuiMod_Shift | ImGuiKey_Z)) {
        texture_dirty_ = document_.redo() || texture_dirty_;
    }
    if (!io.WantTextInput && shortcut_ctrl_or_super(ImGuiKey_A)) {
        auto before = document_.selection;
        document_.selection.select_all();
        document_.commit_selection_edit("Select All", before);
    }
    if (!io.WantTextInput && shortcut_ctrl_or_super(ImGuiKey_D)) {
        clear_selection("Deselect");
    }
    if (!io.WantTextInput && shortcut_ctrl_or_super(ImGuiKey_I)) {
        const SelectionMask before = document_.selection;
        document_.selection.invert();
        document_.commit_selection_edit("Invert Selection", before);
    }
    if (can_use_canvas_shortcuts && (shortcut_ctrl_or_super(ImGuiKey_Equal) || shortcut_ctrl_or_super(ImGuiKey_KeypadAdd))) {
        zoom_ = std::min(64.0f, pixel_zoom(zoom_) + 1.0f);
    }
    if (can_use_canvas_shortcuts && (shortcut_ctrl_or_super(ImGuiKey_Minus) || shortcut_ctrl_or_super(ImGuiKey_KeypadSubtract))) {
        zoom_ = std::max(1.0f, pixel_zoom(zoom_) - 1.0f);
    }
    if (can_use_canvas_shortcuts && shortcut_ctrl_or_super(ImGuiKey_0)) {
        zoom_ = 12.0f;
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
                ImGui::SetScrollX(ImGui::GetScrollX() + static_cast<float>(nudge_x) * kKeyboardPanStep);
                ImGui::SetScrollY(ImGui::GetScrollY() + static_cast<float>(nudge_y) * kKeyboardPanStep);
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
        fill_bucket(document_, px_i, py_i, tool_.secondary, tool_.tolerance, tool_.contiguous != io.KeyShift);
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
                plot_brush_raw(document_, px_i, py_i, tool_.primary, tool_.tool == ToolType::Pencil ? 1 : tool_.brush_size, tool_.tool == ToolType::Eraser);
                texture_dirty_ = true;
                break;
            case ToolType::Bucket:
                fill_bucket(document_, px_i, py_i, tool_.primary, tool_.tolerance, tool_.contiguous != io.KeyShift);
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
        draw_line_raw(document_, last_x_, last_y_, px_i, py_i, tool_.primary, size, tool_.tool == ToolType::Eraser);
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
    float z = pixel_zoom(zoom_);
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

    float z = pixel_zoom(zoom_);
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
    ImGui::SliderFloat("Hue", &hue_, -180.0f, 180.0f, "%.1f");
    ImGui::SliderFloat("Saturation", &saturation_, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Value", &value_, -1.0f, 1.0f, "%.2f");
    if (ImGui::Button("Apply HSV")) {
        apply_hsv(document_, hue_, saturation_, value_);
        hue_ = saturation_ = value_ = 0.0f;
        texture_dirty_ = true;
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
    if (ImGui::Button("Dither")) {
        apply_palette_quantize(document_, document_.palette, true);
        texture_dirty_ = true;
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
    model_.texture_width = document_.width;
    model_.texture_height = document_.height;
    clamp_model_uvs(model_);
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

void EditorApp::draw_model_preview() {
    const char* modes[] = {"Select", "Move", "Rotate", "Scale"};
    for (int mode = 0; mode < static_cast<int>(IM_ARRAYSIZE(modes)); ++mode) {
        if (mode > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(mode);
        if (ImGui::Selectable(modes[mode], model_transform_mode_ == mode, 0, ImVec2(64.0f, 0.0f))) {
            model_transform_mode_ = mode;
            model_transform_drag_active_ = false;
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s %s", model_transform_mode_name(model_transform_mode_), model_transform_axis_name(model_transform_axis_));

    ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 size(std::max(300.0f, available.x), std::max(220.0f, available.y));
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ModelPreview", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.0f) {
        model_viewport_.distance = std::clamp(model_viewport_.distance - io.MouseWheel * 2.0f, 6.0f, 160.0f);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && !io.KeyShift) {
        model_viewport_.yaw_degrees -= io.MouseDelta.x * 0.35f;
        model_viewport_.pitch_degrees = std::clamp(model_viewport_.pitch_degrees + io.MouseDelta.y * 0.35f, -85.0f, 85.0f);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && io.KeyShift) {
        model_viewport_.pan_x += io.MouseDelta.x * 0.04f;
        model_viewport_.pan_y -= io.MouseDelta.y * 0.04f;
    }
    AxisGizmoGeometry view_gizmo = build_axis_gizmo_geometry(model_viewport_, origin, size);
    int view_gizmo_hit = hovered ? hit_axis_gizmo(view_gizmo, io.MousePos) : -1;
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
    view_gizmo_hit = model_view_gizmo_drag_active_ ? 6 : (hovered ? hit_axis_gizmo(view_gizmo, io.MousePos) : -1);
    const ModelGizmoGeometry transform_gizmo =
        build_model_gizmo_geometry(renderer3d_, model_, model_viewport_, origin, size);
    model_transform_hover_axis_ = hovered && !view_gizmo_consumed_input
                                      ? hit_model_gizmo(transform_gizmo, model_viewport_, model_transform_mode_, io.MousePos)
                                      : -1;
    if (!view_gizmo_consumed_input) {
        handle_model_transform_drag(origin, size, hovered);
    }
    if (hovered && !view_gizmo_consumed_input && model_transform_mode_ == 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 local(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
        FaceHit hit = renderer3d_.pick_face(model_, model_viewport_, static_cast<int>(size.x), static_cast<int>(size.y), local.x, local.y);
        if (hit.hit) {
            model_.selected_cuboid = hit.cuboid;
            model_.selected_face = hit.face;
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(38, 42, 46, 255));
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
                             model_transform_hover_axis_);

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
    }
    if (model_transform_drag_active_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Cuboid next = model_transform_start_cuboid_;
        const ImVec2 axis = normalized_screen_axis({model_transform_axis_ == 0 ? 1.0f : 0.0f,
                                                    model_transform_axis_ == 1 ? 1.0f : 0.0f,
                                                    model_transform_axis_ == 2 ? 1.0f : 0.0f},
                                                   model_viewport_);
        const ImVec2 delta(io.MousePos.x - model_transform_start_mouse_.x,
                           io.MousePos.y - model_transform_start_mouse_.y);
        const float signed_pixels = delta.x * axis.x + delta.y * axis.y;
        const bool constrained = io.KeyCtrl || io.KeySuper;
        if (model_transform_mode_ == 1) {
            translate_cuboid(next, model_transform_axis_, signed_pixels * 0.06f, constrained);
        } else if (model_transform_mode_ == 2) {
            const float start_angle = std::atan2(model_transform_start_mouse_.y - model_transform_drag_center_.y,
                                                model_transform_start_mouse_.x - model_transform_drag_center_.x);
            const float current_angle = std::atan2(io.MousePos.y - model_transform_drag_center_.y,
                                                  io.MousePos.x - model_transform_drag_center_.x);
            const float angle_delta = (current_angle - start_angle) * 180.0f / 3.14159265358979323846f;
            rotate_cuboid(next,
                          model_transform_axis_,
                          model_transform_start_cuboid_.rotation_angle + angle_delta,
                          constrained);
        } else if (model_transform_mode_ == 3) {
            scale_cuboid(next, model_transform_axis_, 1.0f + signed_pixels * 0.012f, constrained);
        }
        model_.selected() = next;
    }
    if (model_transform_drag_active_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        model_transform_drag_active_ = false;
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
