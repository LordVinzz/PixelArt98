// Internal implementation fragment for ProjectIO document serialization.
// This file is included by its owning translation unit; do not compile it separately.

struct MemoryPng {
    std::vector<unsigned char> bytes;
};

void png_write_callback(void* context, void* data, int size) {
    auto* out = static_cast<MemoryPng*>(context);
    const auto* begin = static_cast<const unsigned char*>(data);
    out->bytes.insert(out->bytes.end(), begin, begin + size);
}

bool encode_png_rgba(int width, int height, const std::vector<Pixel>& pixels, std::vector<unsigned char>& out) {
    MemoryPng png;
    if (!stbi_write_png_to_func(png_write_callback, &png, width, height, 4, pixels.data(), width * 4)) {
        return false;
    }
    out = std::move(png.bytes);
    return true;
}

bool decode_png_rgba(const unsigned char* data, std::size_t size, int expected_w, int expected_h, std::vector<Pixel>& out) {
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!decoded) {
        return false;
    }
    bool ok = w == expected_w && h == expected_h;
    if (ok) {
        out.assign(reinterpret_cast<Pixel*>(decoded), reinterpret_cast<Pixel*>(decoded + (w * h * 4)));
    }
    stbi_image_free(decoded);
    return ok;
}

std::vector<Pixel> mask_to_rgba(const std::vector<std::uint8_t>& mask) {
    std::vector<Pixel> pixels;
    pixels.reserve(mask.size());
    for (std::uint8_t value : mask) {
        pixels.push_back(rgba(value, value, value, 255));
    }
    return pixels;
}

std::vector<std::uint8_t> rgba_to_mask(const std::vector<Pixel>& pixels) {
    std::vector<std::uint8_t> mask;
    mask.reserve(pixels.size());
    for (Pixel pixel : pixels) {
        mask.push_back(r(pixel));
    }
    return mask;
}

void set_error(std::string* error, const std::string& value) {
    if (error) {
        *error = value;
    }
}

std::string path_display(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::FILE* open_project_file(const std::filesystem::path& path, bool write) {
#if defined(_WIN32)
    return _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

nlohmann::json document_to_json(const Document& document) {
    nlohmann::json root;
    root["format"] = "pixelart98-project";
    root["version"] = 2;
    root["width"] = document.width;
    root["height"] = document.height;
    root["active_layer"] = document.active_layer;
    root["active_frame"] = document.active_frame;
    root["playback_mode"] = playback_mode_name(document.playback_mode);
    root["palette"] = nlohmann::json::array();
    for (Pixel p : document.palette.colors) {
        root["palette"].push_back({r(p), g(p), b(p), a(p)});
    }
    root["layers"] = nlohmann::json::array();
    for (std::size_t layer_index = 0; layer_index < document.layers.size(); ++layer_index) {
        const auto& layer = document.layers[layer_index];
        root["layers"].push_back({
            {"name", layer.name},
            {"visible", layer.visible},
            {"opacity", layer.opacity},
            {"blend_mode", layer_blend_mode_name(layer.blend_mode)},
            {"mask_enabled", layer.mask_enabled},
            {"clip_to_below", layer.clip_to_below},
            {"mask", layer.mask.empty() ? "" : "masks/layer_" + std::to_string(layer_index) + ".png"}
        });
    }
    root["tags"] = nlohmann::json::array();
    for (const auto& tag : document.tags) {
        root["tags"].push_back({{"name", tag.name}, {"from", tag.from}, {"to", tag.to}});
    }
    root["frames"] = nlohmann::json::array();
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        const auto& frame = document.frames[fi];
        nlohmann::json frame_json;
        frame_json["duration_ms"] = frame.duration_ms;
        frame_json["cels"] = nlohmann::json::array();
        for (std::size_t li = 0; li < frame.cels.size(); ++li) {
            frame_json["cels"].push_back({
                {"x", frame.cels[li].x},
                {"y", frame.cels[li].y},
                {"image", "cels/frame_" + std::to_string(fi) + "_layer_" + std::to_string(li) + ".png"}
            });
        }
        root["frames"].push_back(frame_json);
    }
    return root;
}

LayerBlendMode layer_blend_mode_from_json(const std::string& value) {
    if (value == "Multiply") return LayerBlendMode::Multiply;
    if (value == "Additive" || value == "Add") return LayerBlendMode::Additive;
    if (value == "Color Burn") return LayerBlendMode::ColorBurn;
    if (value == "Color Dodge") return LayerBlendMode::ColorDodge;
    if (value == "Reflect") return LayerBlendMode::Reflect;
    if (value == "Glow") return LayerBlendMode::Glow;
    if (value == "Overlay") return LayerBlendMode::Overlay;
    if (value == "Difference") return LayerBlendMode::Difference;
    if (value == "Negation") return LayerBlendMode::Negation;
    if (value == "Lighten" || value == "Light") return LayerBlendMode::Lighten;
    if (value == "Darken") return LayerBlendMode::Darken;
    if (value == "Screen") return LayerBlendMode::Screen;
    if (value == "Xor" || value == "XOR") return LayerBlendMode::Xor;
    return LayerBlendMode::Normal;
}

PlaybackMode playback_mode_from_json(const std::string& value) {
    if (value == "Ping-Pong" || value == "PingPong" || value == "pingpong") {
        return PlaybackMode::PingPong;
    }
    return PlaybackMode::Loop;
}

bool json_to_document(const nlohmann::json& root, Document& out, std::string* error) {
    try {
        int width = root.at("width").get<int>();
        int height = root.at("height").get<int>();
        Document document = Document::create(width, height);
        document.layers.clear();
        document.frames.clear();
        document.palette.colors.clear();

        for (const auto& p : root.at("palette")) {
            document.palette.colors.push_back(rgba(p.at(0).get<std::uint8_t>(), p.at(1).get<std::uint8_t>(),
                                                   p.at(2).get<std::uint8_t>(), p.at(3).get<std::uint8_t>()));
        }
        if (document.palette.colors.empty()) {
            document.palette.colors = default_palette();
        }

        for (const auto& layer_json : root.at("layers")) {
            Layer layer;
            layer.name = layer_json.value("name", "Layer");
            layer.visible = layer_json.value("visible", true);
            layer.opacity = layer_json.value("opacity", 1.0f);
            layer.blend_mode = layer_blend_mode_from_json(layer_json.value("blend_mode", "Normal"));
            layer.mask_enabled = layer_json.value("mask_enabled", false);
            layer.clip_to_below = layer_json.value("clip_to_below", false);
            document.layers.push_back(layer);
        }
        for (const auto& frame_json : root.at("frames")) {
            Frame frame;
            frame.duration_ms = frame_json.value("duration_ms", 100);
            for (const auto& cel_json : frame_json.at("cels")) {
                Cel cel;
                cel.x = cel_json.value("x", 0);
                cel.y = cel_json.value("y", 0);
                cel.pixels.assign(static_cast<std::size_t>(width * height), 0);
                frame.cels.push_back(std::move(cel));
            }
            document.frames.push_back(std::move(frame));
        }
        document.active_layer = document.layers.empty()
                                    ? 0
                                    : std::clamp(root.value("active_layer", 0), 0, static_cast<int>(document.layers.size()) - 1);
        document.active_frame = document.frames.empty()
                                    ? 0
                                    : std::clamp(root.value("active_frame", 0), 0, static_cast<int>(document.frames.size()) - 1);
        document.playback_mode = playback_mode_from_json(root.value("playback_mode", "Loop"));
        document.tags.clear();
        if (root.contains("tags")) {
            int last = std::max(0, static_cast<int>(document.frames.size()) - 1);
            for (const auto& tag_json : root.at("tags")) {
                AnimationTag tag;
                tag.name = tag_json.value("name", "Tag");
                tag.from = std::clamp(tag_json.value("from", 0), 0, last);
                tag.to = std::clamp(tag_json.value("to", tag.from), tag.from, last);
                document.tags.push_back(std::move(tag));
            }
        }
        document.selection.resize(width, height);
        document.clear_history();
        out = std::move(document);
        return true;
    } catch (const std::exception& ex) {
        set_error(error, ex.what());
        return false;
    }
}

