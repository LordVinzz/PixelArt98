// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <queue>
#include <set>
#include <unordered_map>
#include <utility>

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

struct UvVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct PositionKey {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    bool operator==(const PositionKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct PositionKeyHash {
    std::size_t operator()(const PositionKey& key) const {
        std::size_t seed = 1469598103934665603ULL;
        auto mix = [&](std::int64_t value) {
            const auto bits = static_cast<std::uint64_t>(value);
            seed ^= static_cast<std::size_t>(bits + 0x9e3779b97f4a7c15ULL + (static_cast<std::uint64_t>(seed) << 6U) +
                                            (static_cast<std::uint64_t>(seed) >> 2U));
        };
        mix(key.x);
        mix(key.y);
        mix(key.z);
        return seed;
    }
};

struct EdgeKey {
    PositionKey a;
    PositionKey b;

    bool operator==(const EdgeKey& other) const {
        return a == other.a && b == other.b;
    }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& key) const {
        PositionKeyHash hash;
        return hash(key.a) ^ (hash(key.b) + 0x9e3779b97f4a7c15ULL + (hash(key.a) << 6U) + (hash(key.a) >> 2U));
    }
};

struct EdgeRef {
    int triangle = -1;
    int edge = -1;
};

struct UvAdjacency {
    int triangle = -1;
    int edge = -1;
};

struct UvSpring {
    int a = 0;
    int b = 0;
    float rest = 0.0f;
    float stiffness = 0.0f;
};

struct UvIslandBox {
    int island = 0;
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float pack_x = 0.0f;
    float pack_y = 0.0f;
};

bool position_key_less(const PositionKey& lhs, const PositionKey& rhs) {
    if (lhs.x != rhs.x) return lhs.x < rhs.x;
    if (lhs.y != rhs.y) return lhs.y < rhs.y;
    return lhs.z < rhs.z;
}

PositionKey position_key(const std::array<float, 3>& position) {
    constexpr double scale = 100000.0;
    return {
        static_cast<std::int64_t>(std::llround(static_cast<double>(position[0]) * scale)),
        static_cast<std::int64_t>(std::llround(static_cast<double>(position[1]) * scale)),
        static_cast<std::int64_t>(std::llround(static_cast<double>(position[2]) * scale))
    };
}

EdgeKey edge_key(PositionKey a, PositionKey b) {
    if (position_key_less(b, a)) {
        std::swap(a, b);
    }
    return {a, b};
}

UvVec2 operator+(UvVec2 lhs, UvVec2 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

UvVec2 operator-(UvVec2 lhs, UvVec2 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

UvVec2 operator*(UvVec2 value, float scale) {
    return {value.x * scale, value.y * scale};
}

float uv_length(UvVec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

float uv_cross(UvVec2 a, UvVec2 b) {
    return a.x * b.y - a.y * b.x;
}

float distance_3d(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int triangle_node_index(int triangle, int corner) {
    return triangle * 3 + corner;
}

UvVec2 place_third_triangle_point(UvVec2 a, UvVec2 b, float ac, float bc, float side_sign) {
    const UvVec2 edge = b - a;
    const float ab = std::max(0.000001f, uv_length(edge));
    const float x = std::clamp((ac * ac + ab * ab - bc * bc) / (2.0f * ab), -ac, ac + ab);
    const float height = std::sqrt(std::max(0.0f, ac * ac - x * x));
    const UvVec2 along = edge * (x / ab);
    const UvVec2 normal{-edge.y / ab, edge.x / ab};
    return a + along + normal * (height * (side_sign < 0.0f ? -1.0f : 1.0f));
}

std::array<UvVec2, 3> flattened_seed_triangle(const MeshObject& mesh, int triangle_index) {
    const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(triangle_index)];
    const auto& p0 = mesh.vertices[static_cast<std::size_t>(triangle.indices[0])].position;
    const auto& p1 = mesh.vertices[static_cast<std::size_t>(triangle.indices[1])].position;
    const auto& p2 = mesh.vertices[static_cast<std::size_t>(triangle.indices[2])].position;
    const float ab = distance_3d(p0, p1);
    const float ac = distance_3d(p0, p2);
    const float bc = distance_3d(p1, p2);
    return {{{0.0f, 0.0f}, {ab, 0.0f}, place_third_triangle_point({0.0f, 0.0f}, {ab, 0.0f}, ac, bc, 1.0f)}};
}

void expand_mesh_to_triangle_soup(MeshObject& mesh) {
    MeshObject expanded;
    expanded.name = mesh.name;
    expanded.vertices.reserve(mesh.triangles.size() * 3U);
    expanded.triangles.reserve(mesh.triangles.size());
    for (const MeshTriangle& triangle : mesh.triangles) {
        bool valid = true;
        for (int index : triangle.indices) {
            if (index < 0 || index >= static_cast<int>(mesh.vertices.size())) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }
        const int base = static_cast<int>(expanded.vertices.size());
        for (int index : triangle.indices) {
            expanded.vertices.push_back(mesh.vertices[static_cast<std::size_t>(index)]);
        }
        MeshTriangle next = triangle;
        next.indices = {base, base + 1, base + 2};
        expanded.triangles.push_back(next);
    }
    expanded.selected_vertices.assign(expanded.vertices.size(), 0);
    expanded.selected_faces.assign(expanded.triangles.size(), 0);
    mesh = std::move(expanded);
}

void build_mesh_unwrap_adjacency(const MeshObject& mesh,
                                 std::vector<std::array<PositionKey, 3>>& keys,
                                 std::vector<std::array<UvAdjacency, 3>>& adjacency) {
    const int triangle_count = static_cast<int>(mesh.triangles.size());
    keys.resize(static_cast<std::size_t>(triangle_count));
    adjacency.assign(static_cast<std::size_t>(triangle_count), {});
    std::unordered_map<EdgeKey, EdgeRef, EdgeKeyHash> edges;
    edges.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    for (int triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(triangle_index)];
        for (int corner = 0; corner < 3; ++corner) {
            const MeshVertex& vertex = mesh.vertices[static_cast<std::size_t>(triangle.indices[static_cast<std::size_t>(corner)])];
            keys[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(corner)] = position_key(vertex.position);
        }
        for (int edge = 0; edge < 3; ++edge) {
            const EdgeKey key = edge_key(keys[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(edge)],
                                         keys[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>((edge + 1) % 3)]);
            const auto [it, inserted] = edges.emplace(key, EdgeRef{triangle_index, edge});
            if (inserted) {
                continue;
            }
            const EdgeRef other = it->second;
            if (other.triangle < 0 ||
                adjacency[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(edge)].triangle >= 0 ||
                adjacency[static_cast<std::size_t>(other.triangle)][static_cast<std::size_t>(other.edge)].triangle >= 0) {
                continue;
            }
            adjacency[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(edge)] = {other.triangle, other.edge};
            adjacency[static_cast<std::size_t>(other.triangle)][static_cast<std::size_t>(other.edge)] = {triangle_index, edge};
        }
    }
}

std::vector<int> build_triangle_islands(const std::vector<std::array<UvAdjacency, 3>>& adjacency, int& island_count) {
    const int triangle_count = static_cast<int>(adjacency.size());
    std::vector<int> islands(static_cast<std::size_t>(triangle_count), -1);
    island_count = 0;
    std::queue<int> pending;
    for (int start = 0; start < triangle_count; ++start) {
        if (islands[static_cast<std::size_t>(start)] >= 0) {
            continue;
        }
        islands[static_cast<std::size_t>(start)] = island_count;
        pending.push(start);
        while (!pending.empty()) {
            const int triangle = pending.front();
            pending.pop();
            for (const UvAdjacency& neighbor : adjacency[static_cast<std::size_t>(triangle)]) {
                if (neighbor.triangle < 0 || islands[static_cast<std::size_t>(neighbor.triangle)] >= 0) {
                    continue;
                }
                islands[static_cast<std::size_t>(neighbor.triangle)] = island_count;
                pending.push(neighbor.triangle);
            }
        }
        ++island_count;
    }
    return islands;
}

bool matching_corner(const std::array<PositionKey, 3>& keys, PositionKey key, int& out_corner) {
    for (int corner = 0; corner < 3; ++corner) {
        if (keys[static_cast<std::size_t>(corner)] == key) {
            out_corner = corner;
            return true;
        }
    }
    return false;
}

void unfold_mesh_islands(const MeshObject& mesh,
                         const std::vector<std::array<PositionKey, 3>>& keys,
                         const std::vector<std::array<UvAdjacency, 3>>& adjacency,
                         std::vector<UvVec2>& uv_nodes) {
    const int triangle_count = static_cast<int>(mesh.triangles.size());
    uv_nodes.assign(static_cast<std::size_t>(triangle_count) * 3U, {});
    std::vector<std::uint8_t> placed(static_cast<std::size_t>(triangle_count), 0);
    std::queue<int> pending;
    for (int start = 0; start < triangle_count; ++start) {
        if (placed[static_cast<std::size_t>(start)] != 0) {
            continue;
        }
        const auto seed = flattened_seed_triangle(mesh, start);
        for (int corner = 0; corner < 3; ++corner) {
            uv_nodes[static_cast<std::size_t>(triangle_node_index(start, corner))] = seed[static_cast<std::size_t>(corner)];
        }
        placed[static_cast<std::size_t>(start)] = 1;
        pending.push(start);
        while (!pending.empty()) {
            const int triangle = pending.front();
            pending.pop();
            for (int edge = 0; edge < 3; ++edge) {
                const UvAdjacency neighbor = adjacency[static_cast<std::size_t>(triangle)][static_cast<std::size_t>(edge)];
                if (neighbor.triangle < 0 || placed[static_cast<std::size_t>(neighbor.triangle)] != 0) {
                    continue;
                }
                const int a_corner = edge;
                const int b_corner = (edge + 1) % 3;
                const int current_third = 3 - a_corner - b_corner;
                int neighbor_a = -1;
                int neighbor_b = -1;
                const auto& neighbor_keys = keys[static_cast<std::size_t>(neighbor.triangle)];
                if (!matching_corner(neighbor_keys,
                                     keys[static_cast<std::size_t>(triangle)][static_cast<std::size_t>(a_corner)],
                                     neighbor_a) ||
                    !matching_corner(neighbor_keys,
                                     keys[static_cast<std::size_t>(triangle)][static_cast<std::size_t>(b_corner)],
                                     neighbor_b) ||
                    neighbor_a == neighbor_b) {
                    continue;
                }
                const int neighbor_third = 3 - neighbor_a - neighbor_b;
                const UvVec2 uv_a = uv_nodes[static_cast<std::size_t>(triangle_node_index(triangle, a_corner))];
                const UvVec2 uv_b = uv_nodes[static_cast<std::size_t>(triangle_node_index(triangle, b_corner))];
                const UvVec2 uv_current_third =
                    uv_nodes[static_cast<std::size_t>(triangle_node_index(triangle, current_third))];
                const MeshTriangle& neighbor_triangle = mesh.triangles[static_cast<std::size_t>(neighbor.triangle)];
                const auto& pos_a = mesh.vertices[static_cast<std::size_t>(neighbor_triangle.indices[static_cast<std::size_t>(neighbor_a)])].position;
                const auto& pos_b = mesh.vertices[static_cast<std::size_t>(neighbor_triangle.indices[static_cast<std::size_t>(neighbor_b)])].position;
                const auto& pos_c = mesh.vertices[static_cast<std::size_t>(neighbor_triangle.indices[static_cast<std::size_t>(neighbor_third)])].position;
                const float side = uv_cross(uv_b - uv_a, uv_current_third - uv_a);
                uv_nodes[static_cast<std::size_t>(triangle_node_index(neighbor.triangle, neighbor_a))] = uv_a;
                uv_nodes[static_cast<std::size_t>(triangle_node_index(neighbor.triangle, neighbor_b))] = uv_b;
                uv_nodes[static_cast<std::size_t>(triangle_node_index(neighbor.triangle, neighbor_third))] =
                    place_third_triangle_point(uv_a,
                                               uv_b,
                                               distance_3d(pos_a, pos_c),
                                               distance_3d(pos_b, pos_c),
                                               side >= 0.0f ? -1.0f : 1.0f);
                placed[static_cast<std::size_t>(neighbor.triangle)] = 1;
                pending.push(neighbor.triangle);
            }
        }
    }
}

void add_uv_spring(std::vector<UvSpring>& springs, int a, int b, float rest, float stiffness) {
    if (a == b || !std::isfinite(rest) || rest < 0.000001f) {
        return;
    }
    springs.push_back({a, b, rest, stiffness});
}

void relax_unwrapped_uvs(const MeshObject& mesh,
                         const std::vector<std::array<PositionKey, 3>>& keys,
                         const std::vector<std::array<UvAdjacency, 3>>& adjacency,
                         std::vector<UvVec2>& uv_nodes) {
    std::vector<UvSpring> springs;
    springs.reserve(mesh.triangles.size() * 6U);
    for (int triangle_index = 0; triangle_index < static_cast<int>(mesh.triangles.size()); ++triangle_index) {
        const MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(triangle_index)];
        for (int edge = 0; edge < 3; ++edge) {
            const int a_corner = edge;
            const int b_corner = (edge + 1) % 3;
            const auto& a_position = mesh.vertices[static_cast<std::size_t>(triangle.indices[static_cast<std::size_t>(a_corner)])].position;
            const auto& b_position = mesh.vertices[static_cast<std::size_t>(triangle.indices[static_cast<std::size_t>(b_corner)])].position;
            add_uv_spring(springs,
                          triangle_node_index(triangle_index, a_corner),
                          triangle_node_index(triangle_index, b_corner),
                          distance_3d(a_position, b_position),
                          0.08f);
        }
    }
    for (int triangle_index = 0; triangle_index < static_cast<int>(adjacency.size()); ++triangle_index) {
        for (int edge = 0; edge < 3; ++edge) {
            const UvAdjacency neighbor = adjacency[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(edge)];
            if (neighbor.triangle <= triangle_index) {
                continue;
            }
            const int a_corner = edge;
            const int b_corner = (edge + 1) % 3;
            int neighbor_a = -1;
            int neighbor_b = -1;
            const auto& neighbor_keys = keys[static_cast<std::size_t>(neighbor.triangle)];
            if (!matching_corner(neighbor_keys,
                                 keys[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(a_corner)],
                                 neighbor_a) ||
                !matching_corner(neighbor_keys,
                                 keys[static_cast<std::size_t>(triangle_index)][static_cast<std::size_t>(b_corner)],
                                 neighbor_b)) {
                continue;
            }
            springs.push_back({triangle_node_index(triangle_index, a_corner),
                               triangle_node_index(neighbor.triangle, neighbor_a),
                               0.0f,
                               0.025f});
            springs.push_back({triangle_node_index(triangle_index, b_corner),
                               triangle_node_index(neighbor.triangle, neighbor_b),
                               0.0f,
                               0.025f});
        }
    }

    const int triangle_count = static_cast<int>(mesh.triangles.size());
    const int iterations = triangle_count > 100000 ? 4 : (triangle_count > 20000 ? 6 : 12);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (const UvSpring& spring : springs) {
            UvVec2 delta = uv_nodes[static_cast<std::size_t>(spring.b)] - uv_nodes[static_cast<std::size_t>(spring.a)];
            const float length = uv_length(delta);
            if (length < 0.000001f) {
                continue;
            }
            const float factor = std::clamp(((length - spring.rest) / length) * spring.stiffness, -0.20f, 0.20f);
            const UvVec2 move = delta * (factor * 0.5f);
            uv_nodes[static_cast<std::size_t>(spring.a)] = uv_nodes[static_cast<std::size_t>(spring.a)] + move;
            uv_nodes[static_cast<std::size_t>(spring.b)] = uv_nodes[static_cast<std::size_t>(spring.b)] - move;
        }
    }
}

std::vector<UvIslandBox> island_boxes(const std::vector<int>& islands,
                                      int island_count,
                                      const std::vector<UvVec2>& uv_nodes) {
    std::vector<UvIslandBox> boxes(static_cast<std::size_t>(island_count));
    for (int island = 0; island < island_count; ++island) {
        boxes[static_cast<std::size_t>(island)].island = island;
        boxes[static_cast<std::size_t>(island)].min_x = std::numeric_limits<float>::max();
        boxes[static_cast<std::size_t>(island)].min_y = std::numeric_limits<float>::max();
        boxes[static_cast<std::size_t>(island)].max_x = std::numeric_limits<float>::lowest();
        boxes[static_cast<std::size_t>(island)].max_y = std::numeric_limits<float>::lowest();
    }
    for (int triangle = 0; triangle < static_cast<int>(islands.size()); ++triangle) {
        const int island = islands[static_cast<std::size_t>(triangle)];
        if (island < 0 || island >= island_count) {
            continue;
        }
        UvIslandBox& box = boxes[static_cast<std::size_t>(island)];
        for (int corner = 0; corner < 3; ++corner) {
            const UvVec2 uv = uv_nodes[static_cast<std::size_t>(triangle_node_index(triangle, corner))];
            box.min_x = std::min(box.min_x, uv.x);
            box.min_y = std::min(box.min_y, uv.y);
            box.max_x = std::max(box.max_x, uv.x);
            box.max_y = std::max(box.max_y, uv.y);
        }
    }
    for (UvIslandBox& box : boxes) {
        if (box.min_x == std::numeric_limits<float>::max()) {
            box.min_x = 0.0f;
            box.min_y = 0.0f;
            box.max_x = 1.0f;
            box.max_y = 1.0f;
        }
    }
    return boxes;
}

void pack_unwrapped_islands(std::vector<UvIslandBox>& boxes,
                            int current_width,
                            int current_height,
                            float& out_scale,
                            int& out_width,
                            int& out_height) {
    constexpr int max_texture_size = 4096;
    constexpr float target_pixels_per_unit = 4.0f;
    constexpr float padding = 6.0f;
    float max_span = 1.0f;
    float total_area = 0.0f;
    for (const UvIslandBox& box : boxes) {
        const float width = std::max(1.0f, box.max_x - box.min_x);
        const float height = std::max(1.0f, box.max_y - box.min_y);
        max_span = std::max(max_span, std::max(width, height));
        total_area += width * height;
    }
    out_scale = target_pixels_per_unit;
    if (max_span * out_scale + padding * 2.0f > static_cast<float>(max_texture_size)) {
        out_scale = std::max(0.01f, (static_cast<float>(max_texture_size) - padding * 2.0f) / max_span);
    }
    std::sort(boxes.begin(), boxes.end(), [](const UvIslandBox& lhs, const UvIslandBox& rhs) {
        const float lhs_height = lhs.max_y - lhs.min_y;
        const float rhs_height = rhs.max_y - rhs.min_y;
        if (lhs_height != rhs_height) {
            return lhs_height > rhs_height;
        }
        return (lhs.max_x - lhs.min_x) > (rhs.max_x - rhs.min_x);
    });

    const float target_width = std::clamp(std::sqrt(std::max(1.0f, total_area)) * out_scale * 1.35f,
                                          64.0f,
                                          static_cast<float>(max_texture_size));
    float cursor_x = padding;
    float cursor_y = padding;
    float row_height = 0.0f;
    float used_width = 0.0f;
    float used_height = 0.0f;
    for (UvIslandBox& box : boxes) {
        const float width = std::max(1.0f, (box.max_x - box.min_x) * out_scale);
        const float height = std::max(1.0f, (box.max_y - box.min_y) * out_scale);
        if (cursor_x > padding && cursor_x + width + padding > target_width) {
            cursor_x = padding;
            cursor_y += row_height + padding;
            row_height = 0.0f;
        }
        box.pack_x = cursor_x - box.min_x * out_scale;
        box.pack_y = cursor_y - box.min_y * out_scale;
        cursor_x += width + padding;
        row_height = std::max(row_height, height);
        used_width = std::max(used_width, cursor_x);
        used_height = std::max(used_height, cursor_y + row_height + padding);
    }
    out_width = std::clamp(static_cast<int>(std::ceil(std::max(used_width, static_cast<float>(current_width)))), 1, max_texture_size);
    out_height = std::clamp(static_cast<int>(std::ceil(std::max(used_height, static_cast<float>(current_height)))), 1, max_texture_size);
}

MeshUvUnwrapResult unwrap_single_mesh_uvs(MeshObject& mesh, int texture_width, int texture_height) {
    MeshUvUnwrapResult result;
    if (mesh.triangles.empty() || mesh.vertices.empty()) {
        result.recommended_width = std::max(1, texture_width);
        result.recommended_height = std::max(1, texture_height);
        return result;
    }
    expand_mesh_to_triangle_soup(mesh);
    const int triangle_count = static_cast<int>(mesh.triangles.size());
    result.triangle_count = triangle_count;
    if (triangle_count == 0) {
        result.recommended_width = std::max(1, texture_width);
        result.recommended_height = std::max(1, texture_height);
        return result;
    }

    std::vector<std::array<PositionKey, 3>> keys;
    std::vector<std::array<UvAdjacency, 3>> adjacency;
    build_mesh_unwrap_adjacency(mesh, keys, adjacency);
    int island_count = 0;
    const std::vector<int> islands = build_triangle_islands(adjacency, island_count);
    result.island_count = island_count;
    std::vector<UvVec2> uv_nodes;
    unfold_mesh_islands(mesh, keys, adjacency, uv_nodes);
    relax_unwrapped_uvs(mesh, keys, adjacency, uv_nodes);

    std::vector<UvIslandBox> boxes = island_boxes(islands, island_count, uv_nodes);
    float scale = 1.0f;
    int atlas_width = std::max(1, texture_width);
    int atlas_height = std::max(1, texture_height);
    pack_unwrapped_islands(boxes, texture_width, texture_height, scale, atlas_width, atlas_height);
    std::vector<UvIslandBox> boxes_by_island(static_cast<std::size_t>(island_count));
    for (const UvIslandBox& box : boxes) {
        boxes_by_island[static_cast<std::size_t>(box.island)] = box;
    }
    for (int triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        const int island = islands[static_cast<std::size_t>(triangle_index)];
        const UvIslandBox& box = boxes_by_island[static_cast<std::size_t>(std::clamp(island, 0, island_count - 1))];
        MeshTriangle& triangle = mesh.triangles[static_cast<std::size_t>(triangle_index)];
        for (int corner = 0; corner < 3; ++corner) {
            MeshVertex& vertex = mesh.vertices[static_cast<std::size_t>(triangle.indices[static_cast<std::size_t>(corner)])];
            const UvVec2 uv = uv_nodes[static_cast<std::size_t>(triangle_node_index(triangle_index, corner))];
            const float pixel_x = uv.x * scale + box.pack_x;
            const float pixel_y = uv.y * scale + box.pack_y;
            vertex.uv = {
                std::clamp(pixel_x / static_cast<float>(atlas_width), 0.0f, 1.0f),
                std::clamp(pixel_y / static_cast<float>(atlas_height), 0.0f, 1.0f)
            };
        }
    }
    mesh.selected_vertices.assign(mesh.vertices.size(), 0);
    mesh.selected_faces.assign(mesh.triangles.size(), 0);
    result.changed = true;
    result.recommended_width = atlas_width;
    result.recommended_height = atlas_height;
    return result;
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

MeshUvUnwrapResult unwrap_model_mesh_uvs(ModelDocument& model, int texture_width, int texture_height) {
    MeshUvUnwrapResult combined;
    combined.recommended_width = std::max(1, texture_width);
    combined.recommended_height = std::max(1, texture_height);
    if (model.meshes.empty()) {
        return combined;
    }
    for (MeshObject& mesh : model.meshes) {
        if (mesh.triangles.empty() || mesh.vertices.empty()) {
            continue;
        }
        MeshUvUnwrapResult mesh_result = unwrap_single_mesh_uvs(mesh,
                                                                combined.recommended_width,
                                                                combined.recommended_height);
        combined.changed = combined.changed || mesh_result.changed;
        combined.mesh_count += mesh_result.changed ? 1 : 0;
        combined.island_count += mesh_result.island_count;
        combined.triangle_count += mesh_result.triangle_count;
        combined.recommended_width = std::max(combined.recommended_width, mesh_result.recommended_width);
        combined.recommended_height = std::max(combined.recommended_height, mesh_result.recommended_height);
    }
    if (combined.changed) {
        model.texture_width = combined.recommended_width;
        model.texture_height = combined.recommended_height;
        clamp_model_uvs(model);
    }
    return combined;
}

} // namespace px
