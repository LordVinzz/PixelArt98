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
