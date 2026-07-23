// Internal implementation fragment for ProjectIO model formats.
// This file is included by its owning translation unit; do not compile it separately.

bool export_model_json(const std::string& path, const ModelDocument& model, std::string* error) {
    std::ofstream file(path);
    if (!file) {
        set_error(error, "Could not write model JSON: " + path);
        return false;
    }
    file << model_to_json(model);
    return true;
}

bool import_model_json(const std::string& path, ModelDocument& out_model, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        set_error(error, "Could not open model JSON: " + path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return model_from_json(buffer.str(), out_model, error);
}

bool export_gltf_model(const std::string& path, const ModelDocument& model, const std::string& texture_path, std::string* error) {
    if (!model_has_geometry(model)) {
        set_error(error, "Cannot export an empty model as glTF");
        return false;
    }

    std::ofstream file(path);
    if (!file) {
        set_error(error, "Could not write glTF model: " + path);
        return false;
    }
    file << gltf_json_for_model(model, texture_path).dump(2);
    return true;
}

bool export_threejs_pack(const std::string& path, const Document& document, const ModelDocument& model, std::string* error) {
    if (!model_has_geometry(model)) {
        set_error(error, "Cannot export an empty model as ThreeJSPack");
        return false;
    }
    if (document.frames.empty()) {
        set_error(error, "Cannot export ThreeJSPack without a texture frame");
        return false;
    }

    const int frame = std::clamp(document.active_frame, 0, static_cast<int>(document.frames.size()) - 1);
    std::vector<unsigned char> texture_png;
    if (!encode_png_rgba(document.width, document.height, document.composite_frame(frame), texture_png)) {
        set_error(error, "Could not encode ThreeJSPack texture");
        return false;
    }

    const std::string gltf = gltf_json_for_model(model, "texture.png").dump(2);
    const std::string loader = threejs_pack_loader_js();
    const nlohmann::json manifest = {
        {"format", "pixelart98-threejspack"},
        {"version", 1},
        {"generator", "PixelArt98"},
        {"model", "model.gltf"},
        {"texture", "texture.png"},
        {"loader", "PixelArt98ThreeJSPack.js"},
        {"texture_size", {document.width, document.height}},
        {"active_frame", frame}
    };
    const std::string manifest_text = manifest.dump(2);

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) {
        set_error(error, "Could not create ThreeJSPack archive: " + path);
        return false;
    }

    bool ok = true;
    ok = ok && mz_zip_writer_add_mem(&zip, "model.gltf", gltf.data(), gltf.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_add_mem(&zip, "texture.png", texture_png.data(), texture_png.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_add_mem(&zip, "PixelArt98ThreeJSPack.js", loader.data(), loader.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_add_mem(&zip, "manifest.json", manifest_text.data(), manifest_text.size(), MZ_BEST_COMPRESSION);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok) {
        set_error(error, "Could not finish ThreeJSPack archive");
    }
    return ok;
}

bool import_stl_model(const std::string& path,
                      ModelDocument& out_model,
                      std::string* error,
                      MeshUvUnwrapResult* unwrap_result) {
    MeshObject mesh;
    if (!read_stl_mesh(path, mesh, error)) {
        return false;
    }
    out_model = model_from_imported_mesh(std::move(mesh), unwrap_result);
    return true;
}

bool export_stl_model(const std::string& path, const ModelDocument& model, std::string* error) {
    const std::vector<StlTriangle> triangles = stl_triangles_for_model(model);
    if (triangles.empty()) {
        set_error(error, "Cannot export an empty model as STL");
        return false;
    }
    if (triangles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        set_error(error, "STL triangle count is too large");
        return false;
    }

    std::vector<unsigned char> bytes;
    bytes.resize(80U, 0);
    const std::string header = "PixelArt98 binary STL";
    std::copy(header.begin(), header.end(), bytes.begin());
    append_u32_le(bytes, static_cast<std::uint32_t>(triangles.size()));
    for (const StlTriangle& triangle : triangles) {
        append_float_le(bytes, triangle.normal.x);
        append_float_le(bytes, triangle.normal.y);
        append_float_le(bytes, triangle.normal.z);
        for (ExportVec3 point : triangle.points) {
            append_float_le(bytes, point.x);
            append_float_le(bytes, point.y);
            append_float_le(bytes, point.z);
        }
        append_u16_le(bytes, 0);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not write STL model: " + path);
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        set_error(error, "Could not finish STL model: " + path);
        return false;
    }
    return true;
}

bool export_minecraft_model(const std::string& path, const ModelDocument& model, const std::string& texture_path, std::string* error) {
    nlohmann::json root;
    root["credit"] = "Made with PixelArt98";
    root["texture_size"] = {model.texture_width, model.texture_height};
    root["textures"] = {{"texture", texture_path}};
    root["elements"] = nlohmann::json::array();
    for (const auto& cuboid : model.cuboids) {
        nlohmann::json element;
        element["name"] = cuboid.name;
        element["from"] = cuboid.from;
        element["to"] = cuboid.to;
        if (std::abs(cuboid.rotation_angle) > 0.0001f) {
            element["rotation"] = {
                {"angle", cuboid.rotation_angle},
                {"axis", model_rotation_axis_name(cuboid.rotation_axis)},
                {"origin", cuboid.rotation_origin},
                {"rescale", cuboid.rotation_rescale}
            };
        }
        element["faces"] = nlohmann::json::object();
        for (int face = 0; face < 6; ++face) {
            const UvRect& uv = cuboid.uv[static_cast<std::size_t>(face)];
            element["faces"][model_face_name(face)] = {
                {"uv", {uv.x, uv.y, uv.x + uv.w, uv.y + uv.h}},
                {"texture", "#texture"}
            };
        }
        root["elements"].push_back(std::move(element));
    }

    std::ofstream file(path);
    if (!file) {
        set_error(error, "Could not write Minecraft model JSON: " + path);
        return false;
    }
    file << root.dump(2);
    return true;
}

bool import_minecraft_model(const std::string& path, ModelDocument& out_model, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        set_error(error, "Could not open Minecraft model JSON: " + path);
        return false;
    }
    try {
        nlohmann::json root = nlohmann::json::parse(file);
        ModelDocument model;
        auto size = root.value("texture_size", std::vector<int>{64, 64});
        if (size.size() >= 2) {
            model.texture_width = std::max(1, size[0]);
            model.texture_height = std::max(1, size[1]);
        }
        if (root.contains("elements")) {
            for (const auto& element : root.at("elements")) {
                Cuboid cuboid;
                cuboid.name = element.value("name", "Cuboid");
                auto from = element.value("from", std::vector<float>{0, 0, 0});
                auto to = element.value("to", std::vector<float>{16, 16, 16});
                for (int i = 0; i < 3 && i < static_cast<int>(from.size()); ++i) cuboid.from[static_cast<std::size_t>(i)] = from[static_cast<std::size_t>(i)];
                for (int i = 0; i < 3 && i < static_cast<int>(to.size()); ++i) cuboid.to[static_cast<std::size_t>(i)] = to[static_cast<std::size_t>(i)];
                cuboid.rotation_origin = {(cuboid.from[0] + cuboid.to[0]) * 0.5f,
                                          (cuboid.from[1] + cuboid.to[1]) * 0.5f,
                                          (cuboid.from[2] + cuboid.to[2]) * 0.5f};
                if (element.contains("rotation")) {
                    const auto& rotation = element.at("rotation");
                    cuboid.rotation_angle = rotation.value("angle", 0.0f);
                    cuboid.rotation_axis = model_rotation_axis_from_name(rotation.value("axis", "y"));
                    cuboid.rotation_rescale = rotation.value("rescale", false);
                    auto origin = rotation.value("origin", std::vector<float>{cuboid.rotation_origin[0], cuboid.rotation_origin[1], cuboid.rotation_origin[2]});
                    for (int i = 0; i < 3 && i < static_cast<int>(origin.size()); ++i) {
                        cuboid.rotation_origin[static_cast<std::size_t>(i)] = origin[static_cast<std::size_t>(i)];
                    }
                }
                if (element.contains("faces")) {
                    const auto& faces = element.at("faces");
                    for (int face = 0; face < 6; ++face) {
                        const char* name = model_face_name(face);
                        if (!faces.contains(name)) {
                            continue;
                        }
                        auto uv = faces.at(name).value("uv", std::vector<float>{0, 0, 16, 16});
                        if (uv.size() >= 4) {
                            int x0 = static_cast<int>(std::round(uv[0]));
                            int y0 = static_cast<int>(std::round(uv[1]));
                            int x1 = static_cast<int>(std::round(uv[2]));
                            int y1 = static_cast<int>(std::round(uv[3]));
                            cuboid.uv[static_cast<std::size_t>(face)] = {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
                        }
                    }
                }
                model.cuboids.push_back(std::move(cuboid));
            }
        }
        clamp_model_uvs(model);
        out_model = std::move(model);
        return true;
    } catch (const std::exception& ex) {
        set_error(error, ex.what());
        return false;
    }
}
