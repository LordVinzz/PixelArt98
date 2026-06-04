// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "io/ProjectIO.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#include <gifenc.h>
#include <miniz.h>
#include <stb_image.h>
#include <stb_image_write.h>

namespace px {

namespace {

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
        document.active_layer = std::clamp(root.value("active_layer", 0), 0, static_cast<int>(document.layers.size()) - 1);
        document.active_frame = std::clamp(root.value("active_frame", 0), 0, static_cast<int>(document.frames.size()) - 1);
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

int nearest_palette_index(Pixel p, const std::vector<Pixel>& palette) {
    int best_index = 0;
    int best_d = 1 << 30;
    for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
        int d = color_distance(p, palette[static_cast<std::size_t>(i)], true);
        if (d < best_d) {
            best_d = d;
            best_index = i;
        }
    }
    return best_index;
}

std::vector<Pixel> gif_palette(const Document& document) {
    std::vector<Pixel> palette = document.palette.colors;
    if (palette.empty()) {
        palette = default_palette();
    }
    if (palette.size() > 256) {
        palette.resize(256);
    }
    while (palette.size() < 256) {
        palette.push_back(rgba(0, 0, 0, 0));
    }
    return palette;
}

void write_be32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void write_be16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

void write_chunk(std::vector<unsigned char>& png, const char type[4], const std::vector<unsigned char>& data) {
    write_be32(png, static_cast<std::uint32_t>(data.size()));
    std::size_t type_offset = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    mz_ulong crc = mz_crc32(0, nullptr, 0);
    crc = mz_crc32(crc, png.data() + type_offset, static_cast<std::size_t>(4 + data.size()));
    write_be32(png, static_cast<std::uint32_t>(crc));
}

std::vector<unsigned char> filtered_rgba_scanlines(int width, int height, const std::vector<Pixel>& pixels) {
    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>((width * 4 + 1) * height));
    const auto* bytes = reinterpret_cast<const unsigned char*>(pixels.data());
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const unsigned char* row = bytes + static_cast<std::size_t>(y * width * 4);
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width * 4));
    }
    return raw;
}

bool compress_png_data(const std::vector<unsigned char>& raw, std::vector<unsigned char>& compressed) {
    if (raw.size() > static_cast<std::size_t>(std::numeric_limits<mz_ulong>::max())) {
        compressed.clear();
        return false;
    }
    const auto raw_size = static_cast<mz_ulong>(raw.size());
    mz_ulong bound = mz_compressBound(raw_size);
    compressed.resize(static_cast<std::size_t>(bound));
    int result = mz_compress2(compressed.data(), &bound, raw.data(), raw_size, MZ_BEST_COMPRESSION);
    if (result != MZ_OK) {
        compressed.clear();
        return false;
    }
    compressed.resize(static_cast<std::size_t>(bound));
    return true;
}

void write_u8(std::vector<unsigned char>& out, std::uint8_t value) {
    out.push_back(value);
}

void write_le16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

void write_le32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
}

void write_i16(std::vector<unsigned char>& out, std::int16_t value) {
    write_le16(out, static_cast<std::uint16_t>(value));
}

void write_ase_string(std::vector<unsigned char>& out, const std::string& value) {
    constexpr std::size_t max_ase_string_size = 65535;
    const std::size_t size = std::min(value.size(), max_ase_string_size);
    write_le16(out, static_cast<std::uint16_t>(size));
    const auto signed_size = static_cast<std::string::difference_type>(size);
    out.insert(out.end(), value.begin(), std::next(value.begin(), signed_size));
}

bool read_u8(const std::vector<unsigned char>& in, std::size_t& offset, std::uint8_t& value) {
    if (offset + 1 > in.size()) return false;
    value = in[offset++];
    return true;
}

bool read_le16(const std::vector<unsigned char>& in, std::size_t& offset, std::uint16_t& value) {
    if (offset + 2 > in.size()) return false;
    value = static_cast<std::uint16_t>(in[offset] | (in[offset + 1] << 8));
    offset += 2;
    return true;
}

bool read_i16(const std::vector<unsigned char>& in, std::size_t& offset, std::int16_t& value) {
    std::uint16_t raw = 0;
    if (!read_le16(in, offset, raw)) return false;
    value = static_cast<std::int16_t>(raw);
    return true;
}

bool read_le32(const std::vector<unsigned char>& in, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > in.size()) return false;
    value = static_cast<std::uint32_t>(in[offset]) |
            (static_cast<std::uint32_t>(in[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(in[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(in[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool skip_bytes(const std::vector<unsigned char>& in, std::size_t& offset, std::size_t count) {
    if (offset + count > in.size()) return false;
    offset += count;
    return true;
}

bool read_ase_string(const std::vector<unsigned char>& in, std::size_t& offset, std::string& value) {
    std::uint16_t length = 0;
    if (!read_le16(in, offset, length) || offset + length > in.size()) return false;
    value.assign(reinterpret_cast<const char*>(in.data() + offset), length);
    offset += length;
    return true;
}

bool compress_zlib(const std::vector<unsigned char>& raw, std::vector<unsigned char>& compressed) {
    if (raw.size() > static_cast<std::size_t>(std::numeric_limits<mz_ulong>::max())) {
        compressed.clear();
        return false;
    }
    const auto raw_size = static_cast<mz_ulong>(raw.size());
    mz_ulong bound = mz_compressBound(raw_size);
    compressed.resize(static_cast<std::size_t>(bound));
    int result = mz_compress2(compressed.data(), &bound, raw.data(), raw_size, MZ_BEST_SPEED);
    if (result != MZ_OK) {
        compressed.clear();
        return false;
    }
    compressed.resize(static_cast<std::size_t>(bound));
    return true;
}

bool uncompress_zlib(const unsigned char* data, std::size_t size, std::size_t expected_size, std::vector<unsigned char>& raw) {
    raw.resize(expected_size);
    mz_ulong out_size = static_cast<mz_ulong>(raw.size());
    int result = mz_uncompress(raw.data(), &out_size, data, static_cast<mz_ulong>(size));
    if (result != MZ_OK || out_size != expected_size) {
        raw.clear();
        return false;
    }
    return true;
}

std::vector<unsigned char> chunk_with_header(std::uint16_t type, const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> out;
    write_le32(out, static_cast<std::uint32_t>(payload.size() + 6));
    write_le16(out, type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

} // namespace

bool save_project(const std::string& path, const Document& document, const ModelDocument& model, std::string* error) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.c_str(), 0)) {
        set_error(error, "Could not create project archive: " + path);
        return false;
    }

    bool ok = true;
    auto root = document_to_json(document);
    std::string json = root.dump(2);
    ok = ok && mz_zip_writer_add_mem(&zip, "project.json", json.data(), json.size(), MZ_BEST_COMPRESSION);
    std::string model_json = model_to_json(model);
    ok = ok && mz_zip_writer_add_mem(&zip, "model.json", model_json.data(), model_json.size(), MZ_BEST_COMPRESSION);

    for (std::size_t fi = 0; fi < document.frames.size() && ok; ++fi) {
        for (std::size_t li = 0; li < document.frames[fi].cels.size() && ok; ++li) {
            std::vector<unsigned char> png;
            ok = encode_png_rgba(document.width, document.height, document.frames[fi].cels[li].pixels, png);
            if (ok) {
                std::string name = "cels/frame_" + std::to_string(fi) + "_layer_" + std::to_string(li) + ".png";
                ok = mz_zip_writer_add_mem(&zip, name.c_str(), png.data(), png.size(), MZ_BEST_COMPRESSION);
            }
        }
    }
    for (std::size_t li = 0; li < document.layers.size() && ok; ++li) {
        const Layer& layer = document.layers[li];
        if (layer.mask.empty()) {
            continue;
        }
        std::vector<unsigned char> png;
        ok = encode_png_rgba(document.width, document.height, mask_to_rgba(layer.mask), png);
        if (ok) {
            std::string name = "masks/layer_" + std::to_string(li) + ".png";
            ok = mz_zip_writer_add_mem(&zip, name.c_str(), png.data(), png.size(), MZ_BEST_COMPRESSION);
        }
    }

    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok) {
        set_error(error, "Could not finish project archive");
    }
    return ok;
}

bool load_project(const std::string& path, ProjectBundle& out_bundle, std::string* error) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        set_error(error, "Could not open project archive: " + path);
        return false;
    }

    std::size_t json_size = 0;
    void* json_mem = mz_zip_reader_extract_file_to_heap(&zip, "project.json", &json_size, 0);
    if (!json_mem) {
        mz_zip_reader_end(&zip);
        set_error(error, "project.json missing from archive");
        return false;
    }

    Document document;
    bool ok = false;
    try {
        ok = json_to_document(nlohmann::json::parse(std::string(static_cast<char*>(json_mem), json_size)), document, error);
    } catch (const std::exception& ex) {
        set_error(error, ex.what());
    }
    mz_free(json_mem);

    if (ok) {
        auto root = document_to_json(document);
        (void)root;
        for (std::size_t fi = 0; fi < document.frames.size() && ok; ++fi) {
            for (std::size_t li = 0; li < document.frames[fi].cels.size() && ok; ++li) {
                std::string name = "cels/frame_" + std::to_string(fi) + "_layer_" + std::to_string(li) + ".png";
                std::size_t png_size = 0;
                void* png_mem = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &png_size, 0);
                if (!png_mem) {
                    set_error(error, "Missing cel image: " + name);
                    ok = false;
                    break;
                }
                ok = decode_png_rgba(static_cast<unsigned char*>(png_mem), png_size, document.width, document.height,
                                     document.frames[fi].cels[li].pixels);
                mz_free(png_mem);
            }
        }
        for (std::size_t li = 0; li < document.layers.size() && ok; ++li) {
            std::string name = "masks/layer_" + std::to_string(li) + ".png";
            std::size_t png_size = 0;
            void* png_mem = mz_zip_reader_extract_file_to_heap(&zip, name.c_str(), &png_size, 0);
            if (!png_mem) {
                if (document.layers[li].mask_enabled) {
                    document.layers[li].mask.assign(static_cast<std::size_t>(document.width * document.height), 255);
                }
                continue;
            }
            std::vector<Pixel> pixels;
            ok = decode_png_rgba(static_cast<unsigned char*>(png_mem), png_size, document.width, document.height, pixels);
            mz_free(png_mem);
            if (ok) {
                document.layers[li].mask = rgba_to_mask(pixels);
            }
        }
    }

    ModelDocument model = ModelDocument::create_default();
    std::size_t model_size = 0;
    void* model_mem = mz_zip_reader_extract_file_to_heap(&zip, "model.json", &model_size, 0);
    if (model_mem) {
        std::string model_text(static_cast<char*>(model_mem), model_size);
        std::string model_error;
        if (!model_from_json(model_text, model, &model_error)) {
            set_error(error, model_error);
            ok = false;
        }
        mz_free(model_mem);
    }

    mz_zip_reader_end(&zip);
    if (!ok) {
        return false;
    }
    out_bundle = {std::move(document), std::move(model)};
    return true;
}

bool import_image(const std::string& path, Document& out_document, std::string* error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        set_error(error, "Could not import image: " + path);
        return false;
    }
    Document document = Document::create(width, height);
    document.active_cel().pixels.assign(reinterpret_cast<Pixel*>(pixels),
                                        reinterpret_cast<Pixel*>(pixels + static_cast<std::size_t>(width * height * 4)));
    stbi_image_free(pixels);
    document.clear_history();
    out_document = std::move(document);
    return true;
}

bool import_image_as_layer(const std::string& path, Document& document, const std::string& layer_name, std::string* error) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        set_error(error, "Could not import image layer: " + path);
        return false;
    }
    auto before_layers = document.layers;
    auto before_frames = document.frames;
    auto before_tags = document.tags;
    int before_layer = document.active_layer;
    int before_frame = document.active_frame;
    Layer layer;
    layer.name = layer_name.empty() ? std::filesystem::path(path).stem().string() : layer_name;
    document.layers.push_back(std::move(layer));
    for (auto& frame : document.frames) {
        Cel cel;
        cel.pixels.assign(static_cast<std::size_t>(document.width * document.height), 0);
        frame.cels.push_back(std::move(cel));
    }
    document.active_layer = static_cast<int>(document.layers.size()) - 1;
    Cel& cel = document.active_cel();
    int copy_w = std::min(width, document.width);
    int copy_h = std::min(height, document.height);
    for (int y = 0; y < copy_h; ++y) {
        const Pixel* src = reinterpret_cast<const Pixel*>(pixels) + static_cast<std::size_t>(y * width);
        Pixel* dst = cel.pixels.data() + static_cast<std::size_t>(y * document.width);
        std::copy(src, src + copy_w, dst);
    }
    stbi_image_free(pixels);
    document.commit_structure_edit("Import Image Layer", std::move(before_layers), std::move(before_frames), std::move(before_tags), before_layer, before_frame);
    return true;
}

bool export_png(const std::string& path, const Document& document, int frame, std::string* error) {
    auto pixels = document.composite_frame(frame);
    if (!stbi_write_png(path.c_str(), document.width, document.height, 4, pixels.data(), document.width * 4)) {
        set_error(error, "Could not write PNG: " + path);
        return false;
    }
    return true;
}

bool export_spritesheet(const std::string& png_path, const std::string& json_path, const Document& document, std::string* error) {
    int sheet_w = document.width * static_cast<int>(document.frames.size());
    int sheet_h = document.height;
    std::vector<Pixel> sheet(static_cast<std::size_t>(sheet_w * sheet_h), 0);
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        auto frame = document.composite_frame(static_cast<int>(fi));
        for (int y = 0; y < document.height; ++y) {
            for (int x = 0; x < document.width; ++x) {
                sheet[static_cast<std::size_t>(y * sheet_w + x + static_cast<int>(fi) * document.width)] =
                    frame[static_cast<std::size_t>(y * document.width + x)];
            }
        }
    }
    if (!stbi_write_png(png_path.c_str(), sheet_w, sheet_h, 4, sheet.data(), sheet_w * 4)) {
        set_error(error, "Could not write spritesheet PNG: " + png_path);
        return false;
    }

    nlohmann::json meta;
    meta["frame_width"] = document.width;
    meta["frame_height"] = document.height;
    meta["frames"] = nlohmann::json::array();
    for (std::size_t i = 0; i < document.frames.size(); ++i) {
        meta["frames"].push_back({
            {"x", static_cast<int>(i) * document.width},
            {"y", 0},
            {"w", document.width},
            {"h", document.height},
            {"duration_ms", document.frames[i].duration_ms}
        });
    }
    std::ofstream json_file(json_path);
    if (!json_file) {
        set_error(error, "Could not write spritesheet JSON: " + json_path);
        return false;
    }
    json_file << meta.dump(2);
    return true;
}

bool export_gif(const std::string& path, const Document& document, std::string* error) {
    auto palette_pixels = gif_palette(document);
    std::vector<std::uint8_t> raw_palette(256 * 3, 0);
    for (int i = 0; i < 256; ++i) {
        raw_palette[static_cast<std::size_t>(i * 3 + 0)] = r(palette_pixels[static_cast<std::size_t>(i)]);
        raw_palette[static_cast<std::size_t>(i * 3 + 1)] = g(palette_pixels[static_cast<std::size_t>(i)]);
        raw_palette[static_cast<std::size_t>(i * 3 + 2)] = b(palette_pixels[static_cast<std::size_t>(i)]);
    }

    ge_GIF* gif = ge_new_gif(path.c_str(), static_cast<std::uint16_t>(document.width),
                             static_cast<std::uint16_t>(document.height), raw_palette.data(), 8, -1, 0);
    if (!gif) {
        set_error(error, "Could not create GIF: " + path);
        return false;
    }
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        auto frame = document.composite_frame(static_cast<int>(fi));
        for (std::size_t i = 0; i < frame.size(); ++i) {
            gif->frame[i] = static_cast<std::uint8_t>(nearest_palette_index(frame[i], palette_pixels));
        }
        ge_add_frame(gif, static_cast<std::uint16_t>(std::max(2, document.frames[fi].duration_ms / 10)));
    }
    ge_close_gif(gif);
    return true;
}

bool export_apng(const std::string& path, const Document& document, std::string* error) {
    if (document.frames.empty()) {
        set_error(error, "Document has no frames");
        return false;
    }
    std::vector<unsigned char> png = {137, 80, 78, 71, 13, 10, 26, 10};

    std::vector<unsigned char> ihdr;
    write_be32(ihdr, static_cast<std::uint32_t>(document.width));
    write_be32(ihdr, static_cast<std::uint32_t>(document.height));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    write_chunk(png, "IHDR", ihdr);

    std::vector<unsigned char> actl;
    write_be32(actl, static_cast<std::uint32_t>(document.frames.size()));
    write_be32(actl, 0);
    write_chunk(png, "acTL", actl);

    std::uint32_t sequence = 0;
    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        int delay_ms = std::max(1, document.frames[fi].duration_ms);
        std::vector<unsigned char> fctl;
        write_be32(fctl, sequence++);
        write_be32(fctl, static_cast<std::uint32_t>(document.width));
        write_be32(fctl, static_cast<std::uint32_t>(document.height));
        write_be32(fctl, 0);
        write_be32(fctl, 0);
        write_be16(fctl, static_cast<std::uint16_t>(std::min(delay_ms, 65535)));
        write_be16(fctl, 1000);
        fctl.push_back(0);
        fctl.push_back(0);
        write_chunk(png, "fcTL", fctl);

        auto frame = document.composite_frame(static_cast<int>(fi));
        auto raw = filtered_rgba_scanlines(document.width, document.height, frame);
        std::vector<unsigned char> compressed;
        if (!compress_png_data(raw, compressed)) {
            set_error(error, "Could not compress APNG frame");
            return false;
        }
        if (fi == 0) {
            write_chunk(png, "IDAT", compressed);
        } else {
            std::vector<unsigned char> fdat;
            write_be32(fdat, sequence++);
            fdat.insert(fdat.end(), compressed.begin(), compressed.end());
            write_chunk(png, "fdAT", fdat);
        }
    }
    write_chunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not write APNG: " + path);
        return false;
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return true;
}

bool export_aseprite(const std::string& path, const Document& document, std::string* error) {
    if (document.width <= 0 || document.height <= 0 || document.layers.empty() || document.frames.empty()) {
        set_error(error, "Cannot export empty document as Aseprite");
        return false;
    }

    std::vector<unsigned char> file;
    write_le32(file, 0);
    write_le16(file, 0xA5E0);
    write_le16(file, static_cast<std::uint16_t>(std::min<std::size_t>(document.frames.size(), 65535)));
    write_le16(file, static_cast<std::uint16_t>(std::min(document.width, 65535)));
    write_le16(file, static_cast<std::uint16_t>(std::min(document.height, 65535)));
    write_le16(file, 32);
    write_le32(file, 1);
    write_le16(file, 100);
    write_le32(file, 0);
    write_le32(file, 0);
    write_u8(file, 0);
    write_u8(file, 0);
    write_u8(file, 0);
    write_u8(file, 0);
    write_le16(file, 0);
    write_u8(file, 1);
    write_u8(file, 1);
    write_i16(file, 0);
    write_i16(file, 0);
    write_le16(file, static_cast<std::uint16_t>(std::min(document.width, 65535)));
    write_le16(file, static_cast<std::uint16_t>(std::min(document.height, 65535)));
    while (file.size() < 128) {
        file.push_back(0);
    }

    for (std::size_t fi = 0; fi < document.frames.size(); ++fi) {
        std::vector<std::vector<unsigned char>> chunks;
        if (fi == 0) {
            for (const auto& layer : document.layers) {
                std::vector<unsigned char> payload;
                write_le16(payload, layer.visible ? 1 : 0);
                write_le16(payload, 0);
                write_le16(payload, 0);
                write_le16(payload, static_cast<std::uint16_t>(std::min(document.width, 65535)));
                write_le16(payload, static_cast<std::uint16_t>(std::min(document.height, 65535)));
                write_le16(payload, 0);
                write_u8(payload, static_cast<std::uint8_t>(std::clamp(layer.opacity, 0.0f, 1.0f) * 255.0f + 0.5f));
                write_u8(payload, 0);
                write_u8(payload, 0);
                write_u8(payload, 0);
                write_ase_string(payload, layer.name);
                chunks.push_back(chunk_with_header(0x2004, payload));
            }

            std::vector<unsigned char> palette_payload;
            std::size_t count = std::clamp<std::size_t>(document.palette.colors.size(), 1, 256);
            write_le32(palette_payload, static_cast<std::uint32_t>(count));
            write_le32(palette_payload, 0);
            write_le32(palette_payload, static_cast<std::uint32_t>(count - 1));
            for (int i = 0; i < 8; ++i) write_u8(palette_payload, 0);
            for (std::size_t i = 0; i < count; ++i) {
                Pixel p = i < document.palette.colors.size() ? document.palette.colors[i] : rgba(0, 0, 0, 0);
                write_le16(palette_payload, 0);
                write_u8(palette_payload, r(p));
                write_u8(palette_payload, g(p));
                write_u8(palette_payload, b(p));
                write_u8(palette_payload, a(p));
            }
            chunks.push_back(chunk_with_header(0x2019, palette_payload));

            if (!document.tags.empty()) {
                std::vector<unsigned char> tag_payload;
                write_le16(tag_payload, static_cast<std::uint16_t>(std::min<std::size_t>(document.tags.size(), 65535)));
                for (int i = 0; i < 8; ++i) write_u8(tag_payload, 0);
                for (const auto& tag : document.tags) {
                    write_le16(tag_payload, static_cast<std::uint16_t>(std::clamp(tag.from, 0, static_cast<int>(document.frames.size()) - 1)));
                    write_le16(tag_payload, static_cast<std::uint16_t>(std::clamp(tag.to, 0, static_cast<int>(document.frames.size()) - 1)));
                    write_u8(tag_payload, document.playback_mode == PlaybackMode::PingPong ? 2 : 0);
                    for (int i = 0; i < 8; ++i) write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_u8(tag_payload, 0);
                    write_ase_string(tag_payload, tag.name);
                }
                chunks.push_back(chunk_with_header(0x2018, tag_payload));
            }
        }

        for (std::size_t li = 0; li < document.layers.size(); ++li) {
            if (li >= document.frames[fi].cels.size()) {
                continue;
            }
            const Cel& cel = document.frames[fi].cels[li];
            std::vector<unsigned char> raw(reinterpret_cast<const unsigned char*>(cel.pixels.data()),
                                           reinterpret_cast<const unsigned char*>(cel.pixels.data()) + cel.pixels.size() * 4);
            std::vector<unsigned char> compressed;
            if (!compress_zlib(raw, compressed)) {
                set_error(error, "Could not compress Aseprite cel");
                return false;
            }
            std::vector<unsigned char> payload;
            write_le16(payload, static_cast<std::uint16_t>(li));
            write_i16(payload, static_cast<std::int16_t>(std::clamp(cel.x, -32768, 32767)));
            write_i16(payload, static_cast<std::int16_t>(std::clamp(cel.y, -32768, 32767)));
            write_u8(payload, 255);
            write_le16(payload, 2);
            for (int i = 0; i < 7; ++i) write_u8(payload, 0);
            write_le16(payload, static_cast<std::uint16_t>(std::min(document.width, 65535)));
            write_le16(payload, static_cast<std::uint16_t>(std::min(document.height, 65535)));
            payload.insert(payload.end(), compressed.begin(), compressed.end());
            chunks.push_back(chunk_with_header(0x2005, payload));
        }

        std::vector<unsigned char> frame;
        write_le32(frame, 0);
        write_le16(frame, 0xF1FA);
        write_le16(frame, static_cast<std::uint16_t>(std::min<std::size_t>(chunks.size(), 65535)));
        write_le16(frame, static_cast<std::uint16_t>(std::clamp(document.frames[fi].duration_ms, 1, 65535)));
        write_le16(frame, 0);
        write_le32(frame, static_cast<std::uint32_t>(chunks.size()));
        for (const auto& chunk : chunks) {
            frame.insert(frame.end(), chunk.begin(), chunk.end());
        }
        std::uint32_t frame_size = static_cast<std::uint32_t>(frame.size());
        frame[0] = static_cast<unsigned char>(frame_size & 0xff);
        frame[1] = static_cast<unsigned char>((frame_size >> 8) & 0xff);
        frame[2] = static_cast<unsigned char>((frame_size >> 16) & 0xff);
        frame[3] = static_cast<unsigned char>((frame_size >> 24) & 0xff);
        file.insert(file.end(), frame.begin(), frame.end());
    }

    std::uint32_t file_size = static_cast<std::uint32_t>(file.size());
    file[0] = static_cast<unsigned char>(file_size & 0xff);
    file[1] = static_cast<unsigned char>((file_size >> 8) & 0xff);
    file[2] = static_cast<unsigned char>((file_size >> 16) & 0xff);
    file[3] = static_cast<unsigned char>((file_size >> 24) & 0xff);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        set_error(error, "Could not write Aseprite file: " + path);
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return true;
}

bool import_aseprite(const std::string& path, Document& out_document, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not open Aseprite file: " + path);
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::size_t offset = 0;
    std::uint32_t file_size = 0;
    std::uint16_t magic = 0;
    std::uint16_t frame_count = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t color_depth = 0;
    if (!read_le32(bytes, offset, file_size) ||
        !read_le16(bytes, offset, magic) ||
        !read_le16(bytes, offset, frame_count) ||
        !read_le16(bytes, offset, width) ||
        !read_le16(bytes, offset, height) ||
        !read_le16(bytes, offset, color_depth) ||
        magic != 0xA5E0 || color_depth != 32 || width == 0 || height == 0) {
        set_error(error, "Unsupported Aseprite file header");
        return false;
    }
    offset = 128;

    struct ImportedCel {
        int frame = 0;
        int layer = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<Pixel> pixels;
    };

    std::vector<Layer> layers;
    const auto duration_count = static_cast<std::size_t>(frame_count == 0 ? 1U : frame_count);
    std::vector<int> durations(duration_count, 100);
    std::vector<ImportedCel> imported_cels;
    Palette palette;
    palette.colors = default_palette();
    std::vector<AnimationTag> tags;
    PlaybackMode playback_mode = PlaybackMode::Loop;

    for (int fi = 0; fi < frame_count && offset < bytes.size(); ++fi) {
        std::size_t frame_start = offset;
        std::uint32_t frame_bytes = 0;
        std::uint16_t frame_magic = 0;
        std::uint16_t old_chunks = 0;
        std::uint16_t duration = 0;
        std::uint16_t reserved = 0;
        std::uint32_t new_chunks = 0;
        if (!read_le32(bytes, offset, frame_bytes) ||
            !read_le16(bytes, offset, frame_magic) ||
            !read_le16(bytes, offset, old_chunks) ||
            !read_le16(bytes, offset, duration) ||
            !read_le16(bytes, offset, reserved) ||
            !read_le32(bytes, offset, new_chunks) ||
            frame_magic != 0xF1FA || frame_bytes < 16) {
            set_error(error, "Invalid Aseprite frame");
            return false;
        }
        durations[static_cast<std::size_t>(fi)] = std::max<int>(1, duration);
        std::uint32_t chunk_count = old_chunks == 0xFFFF ? new_chunks : old_chunks;
        std::size_t frame_end = std::min<std::size_t>(bytes.size(), frame_start + frame_bytes);
        for (std::uint32_t ci = 0; ci < chunk_count && offset + 6 <= frame_end; ++ci) {
            std::size_t chunk_start = offset;
            std::uint32_t chunk_size = 0;
            std::uint16_t chunk_type = 0;
            if (!read_le32(bytes, offset, chunk_size) || !read_le16(bytes, offset, chunk_type) || chunk_size < 6) {
                set_error(error, "Invalid Aseprite chunk");
                return false;
            }
            std::size_t chunk_end = std::min<std::size_t>(frame_end, chunk_start + chunk_size);
            if (chunk_type == 0x2004) {
                std::uint16_t flags = 0;
                std::uint16_t layer_type = 0;
                std::uint16_t child_level = 0;
                std::uint16_t default_w = 0;
                std::uint16_t default_h = 0;
                std::uint16_t blend = 0;
                std::uint8_t opacity = 255;
                std::string name;
                if (read_le16(bytes, offset, flags) &&
                    read_le16(bytes, offset, layer_type) &&
                    read_le16(bytes, offset, child_level) &&
                    read_le16(bytes, offset, default_w) &&
                    read_le16(bytes, offset, default_h) &&
                    read_le16(bytes, offset, blend) &&
                    read_u8(bytes, offset, opacity) &&
                    skip_bytes(bytes, offset, 3) &&
                    read_ase_string(bytes, offset, name)) {
                    Layer layer;
                    layer.name = name.empty() ? "Layer" : name;
                    layer.visible = (flags & 1) != 0;
                    layer.opacity = static_cast<float>(opacity) / 255.0f;
                    layer.blend_mode = LayerBlendMode::Normal;
                    layers.push_back(std::move(layer));
                }
            } else if (chunk_type == 0x2005) {
                std::uint16_t layer_index = 0;
                std::int16_t cel_x = 0;
                std::int16_t cel_y = 0;
                std::uint8_t opacity = 0;
                std::uint16_t cel_type = 0;
                std::uint16_t cel_w = 0;
                std::uint16_t cel_h = 0;
                if (read_le16(bytes, offset, layer_index) &&
                    read_i16(bytes, offset, cel_x) &&
                    read_i16(bytes, offset, cel_y) &&
                    read_u8(bytes, offset, opacity) &&
                    read_le16(bytes, offset, cel_type) &&
                    skip_bytes(bytes, offset, 7)) {
                    (void)opacity;
                    if ((cel_type == 0 || cel_type == 2) &&
                        read_le16(bytes, offset, cel_w) &&
                        read_le16(bytes, offset, cel_h) &&
                        cel_w > 0 && cel_h > 0) {
                        std::vector<unsigned char> raw;
                        std::size_t expected = static_cast<std::size_t>(cel_w) * cel_h * 4;
                        bool ok = false;
                        if (cel_type == 0 && offset + expected <= chunk_end) {
                            raw.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + expected));
                            ok = true;
                        } else if (cel_type == 2 && offset < chunk_end) {
                            ok = uncompress_zlib(bytes.data() + offset, chunk_end - offset, expected, raw);
                        }
                        if (ok) {
                            ImportedCel cel;
                            cel.frame = fi;
                            cel.layer = static_cast<int>(layer_index);
                            cel.x = cel_x;
                            cel.y = cel_y;
                            cel.width = cel_w;
                            cel.height = cel_h;
                            cel.pixels.assign(reinterpret_cast<Pixel*>(raw.data()),
                                              reinterpret_cast<Pixel*>(raw.data() + raw.size()));
                            imported_cels.push_back(std::move(cel));
                        }
                    }
                }
            } else if (chunk_type == 0x2019) {
                std::uint32_t palette_size = 0;
                std::uint32_t first = 0;
                std::uint32_t last = 0;
                if (read_le32(bytes, offset, palette_size) &&
                    read_le32(bytes, offset, first) &&
                    read_le32(bytes, offset, last) &&
                    skip_bytes(bytes, offset, 8) &&
                    palette_size > 0) {
                    palette.colors.assign(static_cast<std::size_t>(palette_size), rgba(0, 0, 0, 0));
                    for (std::uint32_t pi = first; pi <= last && pi < palette_size && offset + 6 <= chunk_end; ++pi) {
                        std::uint16_t flags = 0;
                        std::uint8_t rr = 0, gg = 0, bb = 0, aa = 0;
                        if (!read_le16(bytes, offset, flags) ||
                            !read_u8(bytes, offset, rr) ||
                            !read_u8(bytes, offset, gg) ||
                            !read_u8(bytes, offset, bb) ||
                            !read_u8(bytes, offset, aa)) {
                            break;
                        }
                        palette.colors[static_cast<std::size_t>(pi)] = rgba(rr, gg, bb, aa);
                        if ((flags & 1) != 0) {
                            std::string ignored;
                            if (!read_ase_string(bytes, offset, ignored)) break;
                        }
                    }
                }
            } else if (chunk_type == 0x2018) {
                std::uint16_t tag_count = 0;
                if (read_le16(bytes, offset, tag_count) && skip_bytes(bytes, offset, 8)) {
                    for (std::uint16_t ti = 0; ti < tag_count && offset + 16 <= chunk_end; ++ti) {
                        std::uint16_t from = 0, to = 0;
                        std::uint8_t direction = 0;
                        std::string name;
                        if (!read_le16(bytes, offset, from) ||
                            !read_le16(bytes, offset, to) ||
                            !read_u8(bytes, offset, direction) ||
                            !skip_bytes(bytes, offset, 12) ||
                            !read_ase_string(bytes, offset, name)) {
                            break;
                        }
                        AnimationTag tag;
                        tag.name = name.empty() ? "Tag" : name;
                        tag.from = from;
                        tag.to = to;
                        tags.push_back(std::move(tag));
                        if (direction == 2) playback_mode = PlaybackMode::PingPong;
                    }
                }
            }
            offset = chunk_end;
        }
        offset = frame_end;
    }

    if (layers.empty()) {
        Layer layer;
        layer.name = "Layer 1";
        layers.push_back(std::move(layer));
    }
    Document document = Document::create(width, height);
    document.layers = std::move(layers);
    document.frames.clear();
    for (int fi = 0; fi < std::max<int>(1, frame_count); ++fi) {
        Frame frame;
        frame.duration_ms = durations[static_cast<std::size_t>(fi)];
        for (std::size_t li = 0; li < document.layers.size(); ++li) {
            Cel cel;
            cel.pixels.assign(static_cast<std::size_t>(document.width * document.height), 0);
            frame.cels.push_back(std::move(cel));
        }
        document.frames.push_back(std::move(frame));
    }
    for (const auto& imported : imported_cels) {
        if (imported.frame < 0 || imported.frame >= static_cast<int>(document.frames.size()) ||
            imported.layer < 0 || imported.layer >= static_cast<int>(document.layers.size())) {
            continue;
        }
        Cel& dst = document.frames[static_cast<std::size_t>(imported.frame)].cels[static_cast<std::size_t>(imported.layer)];
        for (int y = 0; y < imported.height; ++y) {
            for (int x = 0; x < imported.width; ++x) {
                int dx = imported.x + x;
                int dy = imported.y + y;
                if (dx < 0 || dy < 0 || dx >= document.width || dy >= document.height) {
                    continue;
                }
                std::size_t src_i = static_cast<std::size_t>(y * imported.width + x);
                std::size_t dst_i = static_cast<std::size_t>(document.pixel_index(dx, dy));
                if (src_i < imported.pixels.size()) {
                    dst.pixels[dst_i] = imported.pixels[src_i];
                }
            }
        }
    }
    if (!palette.colors.empty()) {
        document.palette = std::move(palette);
    }
    int last = std::max(0, static_cast<int>(document.frames.size()) - 1);
    for (auto& tag : tags) {
        tag.from = std::clamp(tag.from, 0, last);
        tag.to = std::clamp(tag.to, tag.from, last);
    }
    document.tags = std::move(tags);
    document.playback_mode = playback_mode;
    document.active_frame = 0;
    document.active_layer = 0;
    document.selection.resize(document.width, document.height);
    document.clear_history();
    out_document = std::move(document);
    return true;
}

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

} // namespace px
