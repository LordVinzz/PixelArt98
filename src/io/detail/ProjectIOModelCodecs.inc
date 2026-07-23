// Internal implementation fragment for ProjectIO model codecs.
// This file is included by its owning translation unit; do not compile it separately.

struct ExportVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct ExportVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ExportFaceQuad {
    std::array<ExportVec3, 4> p;
    int cuboid = 0;
    int face = 0;
};

constexpr float kPi = 3.14159265358979323846f;

float export_radians(float degrees) {
    return degrees * kPi / 180.0f;
}

ExportVec3 operator+(ExportVec3 a, ExportVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

ExportVec3 operator-(ExportVec3 a, ExportVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float export_dot(ExportVec3 a, ExportVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

ExportVec3 export_cross(ExportVec3 a, ExportVec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

ExportVec3 export_normalize(ExportVec3 value) {
    const float length = std::sqrt(std::max(0.000001f, export_dot(value, value)));
    return {value.x / length, value.y / length, value.z / length};
}

ExportVec3 export_vec3_from_array(const std::array<float, 3>& value) {
    return {value[0], value[1], value[2]};
}

ExportVec3 export_triangle_normal(ExportVec3 a, ExportVec3 b, ExportVec3 c) {
    return export_normalize(export_cross(b - a, c - a));
}

ExportVec3 rotate_export_point(ExportVec3 point, const Cuboid& cuboid) {
    if (std::abs(cuboid.rotation_angle) <= 0.0001f) {
        return point;
    }

    const ExportVec3 origin{cuboid.rotation_origin[0], cuboid.rotation_origin[1], cuboid.rotation_origin[2]};
    const ExportVec3 p = point - origin;
    const float angle = export_radians(cuboid.rotation_angle);
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    ExportVec3 rotated = p;
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

void add_export_face_quad(std::vector<ExportFaceQuad>& out, const Cuboid& cuboid, int cuboid_index, int face) {
    const float x0 = std::min(cuboid.from[0], cuboid.to[0]);
    const float y0 = std::min(cuboid.from[1], cuboid.to[1]);
    const float z0 = std::min(cuboid.from[2], cuboid.to[2]);
    const float x1 = std::max(cuboid.from[0], cuboid.to[0]);
    const float y1 = std::max(cuboid.from[1], cuboid.to[1]);
    const float z1 = std::max(cuboid.from[2], cuboid.to[2]);

    ExportFaceQuad quad;
    quad.cuboid = cuboid_index;
    quad.face = face;
    switch (face) {
        case 0: quad.p = {{{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}}; break;
        case 1: quad.p = {{{x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}}}; break;
        case 2: quad.p = {{{x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}}}; break;
        case 3: quad.p = {{{x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}}}; break;
        case 4: quad.p = {{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}}; break;
        default: quad.p = {{{x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}}}; break;
    }
    for (ExportVec3& point : quad.p) {
        point = rotate_export_point(point, cuboid);
    }
    out.push_back(quad);
}

std::vector<ExportFaceQuad> export_face_quads(const ModelDocument& model) {
    std::vector<ExportFaceQuad> out;
    out.reserve(model.cuboids.size() * 6U);
    for (int ci = 0; ci < static_cast<int>(model.cuboids.size()); ++ci) {
        for (int face = 0; face < 6; ++face) {
            add_export_face_quad(out, model.cuboids[static_cast<std::size_t>(ci)], ci, face);
        }
    }
    return out;
}

void align_bytes(std::vector<unsigned char>& bytes, std::size_t alignment) {
    while (bytes.size() % alignment != 0U) {
        bytes.push_back(0);
    }
}

void append_u32_le(std::vector<unsigned char>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xffU));
}

void append_u16_le(std::vector<unsigned char>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xffU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xffU));
}

void append_float_le(std::vector<unsigned char>& bytes, float value) {
    std::uint32_t raw = 0;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&raw, &value, sizeof(value));
    append_u32_le(bytes, raw);
}

std::string base64_encode(const std::vector<unsigned char>& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2U) / 3U) * 4U);
    for (std::size_t i = 0; i < bytes.size(); i += 3U) {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = i + 1U < bytes.size() ? bytes[i + 1U] : 0U;
        const std::uint32_t b2 = i + 2U < bytes.size() ? bytes[i + 2U] : 0U;
        const std::uint32_t triple = (b0 << 16U) | (b1 << 8U) | b2;
        out.push_back(alphabet[(triple >> 18U) & 0x3fU]);
        out.push_back(alphabet[(triple >> 12U) & 0x3fU]);
        out.push_back(i + 1U < bytes.size() ? alphabet[(triple >> 6U) & 0x3fU] : '=');
        out.push_back(i + 2U < bytes.size() ? alphabet[triple & 0x3fU] : '=');
    }
    return out;
}

nlohmann::json gltf_json_for_model(const ModelDocument& source_model, const std::string& texture_path) {
    ModelDocument model = source_model;
    clamp_model_uvs(model);

    std::vector<float> positions;
    std::vector<float> texcoords;
    std::vector<std::uint32_t> indices;
    std::array<float, 3> min_position = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    std::array<float, 3> max_position = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };

    const int texture_width = std::max(1, model.texture_width);
    const int texture_height = std::max(1, model.texture_height);
    for (const ExportFaceQuad& quad : export_face_quads(model)) {
        const auto& uv = model.cuboids[static_cast<std::size_t>(quad.cuboid)].uv[static_cast<std::size_t>(quad.face)];
        const float u0 = static_cast<float>(uv.x) / static_cast<float>(texture_width);
        const float v0 = static_cast<float>(uv.y) / static_cast<float>(texture_height);
        const float u1 = static_cast<float>(uv.x + uv.w) / static_cast<float>(texture_width);
        const float v1 = static_cast<float>(uv.y + uv.h) / static_cast<float>(texture_height);
        const std::array<ExportVec2, 4> uvp = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
        const auto base_index = static_cast<std::uint32_t>(positions.size() / 3U);
        for (int i = 0; i < 4; ++i) {
            const ExportVec3 p = quad.p[static_cast<std::size_t>(i)];
            positions.push_back(p.x);
            positions.push_back(p.y);
            positions.push_back(p.z);
            texcoords.push_back(uvp[static_cast<std::size_t>(i)].x);
            texcoords.push_back(uvp[static_cast<std::size_t>(i)].y);
            min_position[0] = std::min(min_position[0], p.x);
            min_position[1] = std::min(min_position[1], p.y);
            min_position[2] = std::min(min_position[2], p.z);
            max_position[0] = std::max(max_position[0], p.x);
            max_position[1] = std::max(max_position[1], p.y);
            max_position[2] = std::max(max_position[2], p.z);
        }
        indices.insert(indices.end(), {
            base_index + 0U,
            base_index + 1U,
            base_index + 2U,
            base_index + 0U,
            base_index + 2U,
            base_index + 3U
        });
    }
    for (const MeshObject& mesh : model.meshes) {
        for (const MeshTriangle& triangle : mesh.triangles) {
            const auto base_index = static_cast<std::uint32_t>(positions.size() / 3U);
            bool valid_triangle = true;
            for (int index : triangle.indices) {
                if (index < 0 || index >= static_cast<int>(mesh.vertices.size())) {
                    valid_triangle = false;
                    break;
                }
            }
            if (!valid_triangle) {
                continue;
            }
            for (int index : triangle.indices) {
                const MeshVertex& vertex = mesh.vertices[static_cast<std::size_t>(index)];
                positions.push_back(vertex.position[0]);
                positions.push_back(vertex.position[1]);
                positions.push_back(vertex.position[2]);
                texcoords.push_back(std::clamp(vertex.uv[0], 0.0f, 1.0f));
                texcoords.push_back(std::clamp(vertex.uv[1], 0.0f, 1.0f));
                min_position[0] = std::min(min_position[0], vertex.position[0]);
                min_position[1] = std::min(min_position[1], vertex.position[1]);
                min_position[2] = std::min(min_position[2], vertex.position[2]);
                max_position[0] = std::max(max_position[0], vertex.position[0]);
                max_position[1] = std::max(max_position[1], vertex.position[1]);
                max_position[2] = std::max(max_position[2], vertex.position[2]);
            }
            indices.insert(indices.end(), {base_index + 0U, base_index + 1U, base_index + 2U});
        }
    }
    if (positions.empty()) {
        min_position = {0.0f, 0.0f, 0.0f};
        max_position = {0.0f, 0.0f, 0.0f};
    }

    std::vector<unsigned char> buffer;
    const std::size_t position_offset = buffer.size();
    for (float value : positions) {
        append_float_le(buffer, value);
    }
    align_bytes(buffer, 4U);
    const std::size_t texcoord_offset = buffer.size();
    for (float value : texcoords) {
        append_float_le(buffer, value);
    }
    align_bytes(buffer, 4U);
    const std::size_t index_offset = buffer.size();
    for (std::uint32_t value : indices) {
        append_u32_le(buffer, value);
    }

    nlohmann::json root;
    root["asset"] = {{"version", "2.0"}, {"generator", "PixelArt98"}};
    root["extensionsUsed"] = {"KHR_materials_unlit"};
    root["scene"] = 0;
    root["scenes"] = {{{"nodes", {0}}}};
    root["nodes"] = {{{"name", "PixelArt98 Model"}, {"mesh", 0}}};
    root["meshes"] = {{
        {"name", "PixelArt98 Cuboids"},
        {"primitives", {{
            {"attributes", {{"POSITION", 0}, {"TEXCOORD_0", 1}}},
            {"indices", 2},
            {"material", 0},
            {"mode", 4}
        }}}
    }};
    root["materials"] = {{
        {"name", "PixelArt98 Texture"},
        {"pbrMetallicRoughness", {
            {"baseColorTexture", {{"index", 0}}},
            {"metallicFactor", 0.0},
            {"roughnessFactor", 1.0}
        }},
        {"alphaMode", "BLEND"},
        {"doubleSided", true},
        {"extensions", {{"KHR_materials_unlit", nlohmann::json::object()}}}
    }};
    root["textures"] = {{{"sampler", 0}, {"source", 0}}};
    root["samplers"] = {{
        {"magFilter", 9728},
        {"minFilter", 9728},
        {"wrapS", 33071},
        {"wrapT", 33071}
    }};
    root["images"] = {{{"uri", texture_path}, {"mimeType", "image/png"}}};
    root["buffers"] = {{
        {"uri", "data:application/octet-stream;base64," + base64_encode(buffer)},
        {"byteLength", buffer.size()}
    }};
    root["bufferViews"] = {
        {
            {"buffer", 0},
            {"byteOffset", position_offset},
            {"byteLength", positions.size() * sizeof(float)},
            {"target", 34962}
        },
        {
            {"buffer", 0},
            {"byteOffset", texcoord_offset},
            {"byteLength", texcoords.size() * sizeof(float)},
            {"target", 34962}
        },
        {
            {"buffer", 0},
            {"byteOffset", index_offset},
            {"byteLength", indices.size() * sizeof(std::uint32_t)},
            {"target", 34963}
        }
    };
    root["accessors"] = {
        {
            {"bufferView", 0},
            {"componentType", 5126},
            {"count", positions.size() / 3U},
            {"type", "VEC3"},
            {"min", min_position},
            {"max", max_position}
        },
        {
            {"bufferView", 1},
            {"componentType", 5126},
            {"count", texcoords.size() / 2U},
            {"type", "VEC2"}
        },
        {
            {"bufferView", 2},
            {"componentType", 5125},
            {"count", indices.size()},
            {"type", "SCALAR"}
        }
    };
    root["extras"] = {
        {"pixelart98", {
            {"texture_size", {texture_width, texture_height}},
            {"source_model", nlohmann::json::parse(model_to_json(model))}
        }}
    };
    return root;
}

std::string threejs_pack_loader_js() {
    return R"(import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

export const PixelArt98ThreeJSPack = {
  model: 'model.gltf',
  texture: 'texture.png',
  generator: 'PixelArt98'
};

export function configurePixelArt98Model(root) {
  root.traverse((object) => {
    if (!object.isMesh) return;
    const materials = Array.isArray(object.material) ? object.material : [object.material];
    for (const material of materials) {
      if (!material) continue;
      if (material.map) {
        material.map.magFilter = THREE.NearestFilter;
        material.map.minFilter = THREE.NearestFilter;
        material.map.needsUpdate = true;
      }
      material.transparent = true;
      material.needsUpdate = true;
    }
  });
  return root;
}

export async function loadPixelArt98Model(options = {}) {
  const { manager, onProgress, path = new URL('./', import.meta.url).href } = options;
  const loader = new GLTFLoader(manager);
  loader.setPath(path.endsWith('/') ? path : `${path}/`);
  const gltf = await loader.loadAsync(PixelArt98ThreeJSPack.model, onProgress);
  configurePixelArt98Model(gltf.scene);
  return gltf;
}

export async function addPixelArt98Model(scene, options = {}) {
  const gltf = await loadPixelArt98Model(options);
  scene.add(gltf.scene);
  return gltf;
}
)";
}

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

float read_float_le(const unsigned char* bytes) {
    const std::uint32_t raw = read_u32_le(bytes);
    float value = 0.0f;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::string path_stem_or_default(const std::string& path, const char* fallback) {
    const std::filesystem::path fs_path = std::filesystem::path(path);
    const std::filesystem::path stem = fs_path.stem();
    if (stem.empty()) {
        return fallback;
    }
    return stem.string();
}

bool finite_export_vec3(ExportVec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void add_mesh_triangle(MeshObject& mesh, ExportVec3 normal, std::array<ExportVec3, 3> points) {
    if (!finite_export_vec3(points[0]) || !finite_export_vec3(points[1]) || !finite_export_vec3(points[2])) {
        return;
    }
    if (export_dot(normal, normal) <= 0.000001f || !finite_export_vec3(normal)) {
        normal = export_triangle_normal(points[0], points[1], points[2]);
    } else {
        normal = export_normalize(normal);
    }
    const int base = static_cast<int>(mesh.vertices.size());
    for (ExportVec3 point : points) {
        MeshVertex vertex;
        vertex.position = {point.x, point.y, point.z};
        mesh.vertices.push_back(vertex);
    }
    MeshTriangle triangle;
    triangle.indices = {base, base + 1, base + 2};
    triangle.normal = {normal.x, normal.y, normal.z};
    mesh.triangles.push_back(triangle);
}

void generate_box_projection_uvs(MeshObject& mesh) {
    if (mesh.vertices.empty()) {
        return;
    }
    std::array<float, 3> minp = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    std::array<float, 3> maxp = {
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    };
    for (const MeshVertex& vertex : mesh.vertices) {
        for (int i = 0; i < 3; ++i) {
            minp[static_cast<std::size_t>(i)] =
                std::min(minp[static_cast<std::size_t>(i)], vertex.position[static_cast<std::size_t>(i)]);
            maxp[static_cast<std::size_t>(i)] =
                std::max(maxp[static_cast<std::size_t>(i)], vertex.position[static_cast<std::size_t>(i)]);
        }
    }
    auto normalized_coordinate = [&](float value, int axis) {
        const float range = std::max(0.000001f,
                                     maxp[static_cast<std::size_t>(axis)] -
                                         minp[static_cast<std::size_t>(axis)]);
        return std::clamp((value - minp[static_cast<std::size_t>(axis)]) / range, 0.0f, 1.0f);
    };
    for (MeshTriangle& triangle : mesh.triangles) {
        const std::array<float, 3> normal = triangle.normal;
        const float ax = std::abs(normal[0]);
        const float ay = std::abs(normal[1]);
        const float az = std::abs(normal[2]);
        int u_axis = 0;
        int v_axis = 1;
        if (ax >= ay && ax >= az) {
            u_axis = 2;
            v_axis = 1;
        } else if (ay >= ax && ay >= az) {
            u_axis = 0;
            v_axis = 2;
        }
        for (int index : triangle.indices) {
            if (index < 0 || index >= static_cast<int>(mesh.vertices.size())) {
                continue;
            }
            MeshVertex& vertex = mesh.vertices[static_cast<std::size_t>(index)];
            vertex.uv = {
                normalized_coordinate(vertex.position[static_cast<std::size_t>(u_axis)], u_axis),
                normalized_coordinate(vertex.position[static_cast<std::size_t>(v_axis)], v_axis)
            };
        }
    }
}

ModelDocument model_from_imported_mesh(MeshObject mesh, MeshUvUnwrapResult* unwrap_result) {
    generate_box_projection_uvs(mesh);
    mesh.selected_vertices.assign(mesh.vertices.size(), 0);
    mesh.selected_faces.assign(mesh.triangles.size(), 0);
    if (!mesh.selected_faces.empty()) {
        mesh.selected_faces[0] = 1;
    }
    ModelDocument model;
    model.cuboids.clear();
    model.selected_cuboid = -1;
    model.selected_face = 0;
    model.selected_mesh = 0;
    model.mesh_selection_mode = 0;
    model.meshes.push_back(std::move(mesh));
    clamp_model_uvs(model);
    const MeshUvUnwrapResult result = unwrap_model_mesh_uvs(model, model.texture_width, model.texture_height);
    if (unwrap_result != nullptr) {
        *unwrap_result = result;
    }
    return model;
}

bool parse_binary_stl(const std::vector<unsigned char>& bytes, MeshObject& mesh, std::string* error) {
    if (bytes.size() < 84U) {
        set_error(error, "Binary STL is too small");
        return false;
    }
    const std::uint32_t triangle_count = read_u32_le(bytes.data() + 80U);
    const std::uint64_t expected_size = 84ULL + static_cast<std::uint64_t>(triangle_count) * 50ULL;
    if (expected_size != static_cast<std::uint64_t>(bytes.size())) {
        set_error(error, "Binary STL size does not match triangle count");
        return false;
    }
    mesh.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    mesh.triangles.reserve(triangle_count);
    const unsigned char* cursor = bytes.data() + 84U;
    for (std::uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        ExportVec3 normal{
            read_float_le(cursor + 0),
            read_float_le(cursor + 4),
            read_float_le(cursor + 8)
        };
        std::array<ExportVec3, 3> points = {{
            {read_float_le(cursor + 12), read_float_le(cursor + 16), read_float_le(cursor + 20)},
            {read_float_le(cursor + 24), read_float_le(cursor + 28), read_float_le(cursor + 32)},
            {read_float_le(cursor + 36), read_float_le(cursor + 40), read_float_le(cursor + 44)}
        }};
        add_mesh_triangle(mesh, normal, points);
        cursor += 50;
    }
    if (mesh.triangles.empty()) {
        set_error(error, "STL contains no triangles");
        return false;
    }
    return true;
}

bool parse_ascii_stl(const std::vector<unsigned char>& bytes, MeshObject& mesh, std::string* error) {
    const std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string token;
    while (in >> token) {
        if (token != "facet") {
            continue;
        }
        std::string normal_token;
        ExportVec3 normal;
        if (!(in >> normal_token >> normal.x >> normal.y >> normal.z) || normal_token != "normal") {
            set_error(error, "Invalid ASCII STL facet normal");
            return false;
        }
        std::array<ExportVec3, 3> points{};
        int vertex_count = 0;
        while (in >> token) {
            if (token == "vertex") {
                if (vertex_count >= 3 ||
                    !(in >> points[static_cast<std::size_t>(vertex_count)].x >>
                           points[static_cast<std::size_t>(vertex_count)].y >>
                           points[static_cast<std::size_t>(vertex_count)].z)) {
                    set_error(error, "Invalid ASCII STL vertex");
                    return false;
                }
                ++vertex_count;
            } else if (token == "endfacet") {
                break;
            }
        }
        if (vertex_count != 3) {
            set_error(error, "ASCII STL facet does not contain exactly three vertices");
            return false;
        }
        add_mesh_triangle(mesh, normal, points);
    }
    if (mesh.triangles.empty()) {
        set_error(error, "STL contains no triangles");
        return false;
    }
    return true;
}

bool read_stl_mesh(const std::string& path, MeshObject& mesh, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not open STL: " + path);
        return false;
    }
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    mesh.name = path_stem_or_default(path, "STL Mesh");
    if (bytes.size() >= 84U) {
        const std::uint32_t triangle_count = read_u32_le(bytes.data() + 80U);
        const std::uint64_t expected_size = 84ULL + static_cast<std::uint64_t>(triangle_count) * 50ULL;
        if (expected_size == static_cast<std::uint64_t>(bytes.size())) {
            return parse_binary_stl(bytes, mesh, error);
        }
    }
    return parse_ascii_stl(bytes, mesh, error);
}

struct StlTriangle {
    ExportVec3 normal;
    std::array<ExportVec3, 3> points;
};

void add_stl_triangle(std::vector<StlTriangle>& triangles, ExportVec3 a, ExportVec3 b, ExportVec3 c) {
    triangles.push_back({export_triangle_normal(a, b, c), {{a, b, c}}});
}

std::vector<StlTriangle> stl_triangles_for_model(const ModelDocument& model) {
    std::vector<StlTriangle> triangles;
    for (const ExportFaceQuad& quad : export_face_quads(model)) {
        add_stl_triangle(triangles, quad.p[0], quad.p[1], quad.p[2]);
        add_stl_triangle(triangles, quad.p[0], quad.p[2], quad.p[3]);
    }
    for (const MeshObject& mesh : model.meshes) {
        for (const MeshTriangle& triangle : mesh.triangles) {
            bool valid_triangle = true;
            std::array<ExportVec3, 3> points{};
            for (int i = 0; i < 3; ++i) {
                const int index = triangle.indices[static_cast<std::size_t>(i)];
                if (index < 0 || index >= static_cast<int>(mesh.vertices.size())) {
                    valid_triangle = false;
                    break;
                }
                points[static_cast<std::size_t>(i)] =
                    export_vec3_from_array(mesh.vertices[static_cast<std::size_t>(index)].position);
            }
            if (valid_triangle) {
                add_stl_triangle(triangles, points[0], points[1], points[2]);
            }
        }
    }
    return triangles;
}

