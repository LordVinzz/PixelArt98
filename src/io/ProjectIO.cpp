// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "io/ProjectIO.hpp"

#include "core/MemoryTrace.hpp"

#include <algorithm>
#include <array>
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

void publish_image_progress(const ImageImportProgressCallback& progress,
                            float fraction,
                            std::string status,
                            std::string phase = {},
                            int done = 0,
                            int total = 0,
                            bool indeterminate = false) {
    if (progress) {
        progress({std::clamp(fraction, 0.0f, 1.0f),
                  done,
                  total,
                  indeterminate,
                  std::move(phase),
                  std::move(status)});
    }
}

Document document_from_pixels_impl(int width, int height, std::vector<Pixel> pixels) {
    Document document;
    document.width = std::max(1, width);
    document.height = std::max(1, height);
    Layer background;
    background.name = "Background";
    document.layers.push_back(std::move(background));
    Frame frame;
    Cel cel;
    cel.pixels = std::move(pixels);
    frame.cels.push_back(std::move(cel));
    document.frames.push_back(std::move(frame));
    document.palette.colors = default_palette();
    document.selection.resize(document.width, document.height);
    document.clear_history();
    return document;
}

std::uint32_t read_be32_bytes(const std::array<unsigned char, 4>& bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
}

bool read_exact(std::ifstream& file, unsigned char* data, std::size_t size) {
    file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return file.good() || (size == 0U && !file.bad());
}

bool skip_exact(std::ifstream& file, std::uint32_t size) {
    constexpr std::streamoff kSkipChunk = 1024 * 1024;
    std::uint32_t remaining = size;
    while (remaining > 0U) {
        const std::streamoff step = std::min<std::streamoff>(kSkipChunk, static_cast<std::streamoff>(remaining));
        file.seekg(step, std::ios::cur);
        if (!file) {
            return false;
        }
        remaining -= static_cast<std::uint32_t>(step);
    }
    return true;
}

int png_channel_count(int color_type) {
    switch (color_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

unsigned char png_paeth(unsigned char a_value, unsigned char b_value, unsigned char c_value) {
    const int a_int = static_cast<int>(a_value);
    const int b_int = static_cast<int>(b_value);
    const int c_int = static_cast<int>(c_value);
    const int p = a_int + b_int - c_int;
    const int pa = std::abs(p - a_int);
    const int pb = std::abs(p - b_int);
    const int pc = std::abs(p - c_int);
    if (pa <= pb && pa <= pc) {
        return a_value;
    }
    return pb <= pc ? b_value : c_value;
}

struct StreamingPngState {
    int width = 0;
    int height = 0;
    int bit_depth = 0;
    int color_type = 0;
    int channels = 0;
    std::size_t row_bytes = 0;
    std::size_t scanline_bytes = 0;
    std::size_t scanline_offset = 0;
    int row = 0;
    std::vector<unsigned char> scanline;
    std::vector<unsigned char> previous;
    std::vector<unsigned char> current;
    std::vector<Pixel> pixels;
    std::array<Pixel, 256> palette = {};
    std::array<std::uint8_t, 256> palette_alpha = {};
    int palette_size = 0;
};

bool process_png_scanline(StreamingPngState& state, std::string& error) {
    MemoryTraceScope trace("process_png_scanline");
    if (state.scanline.empty() || state.row >= state.height) {
        error = "PNG scanline overflow";
        return false;
    }
    const unsigned char filter = state.scanline[0];
    if (filter > 4U) {
        error = "Unsupported PNG filter";
        return false;
    }
    for (std::size_t i = 0; i < state.row_bytes; ++i) {
        const unsigned char raw = state.scanline[i + 1U];
        const unsigned char left = i >= static_cast<std::size_t>(state.channels) ? state.current[i - static_cast<std::size_t>(state.channels)] : 0U;
        const unsigned char up = state.previous.empty() ? 0U : state.previous[i];
        const unsigned char up_left = (!state.previous.empty() && i >= static_cast<std::size_t>(state.channels))
                                          ? state.previous[i - static_cast<std::size_t>(state.channels)]
                                          : 0U;
        unsigned int value = raw;
        switch (filter) {
            case 1: value += left; break;
            case 2: value += up; break;
            case 3: value += (static_cast<unsigned int>(left) + static_cast<unsigned int>(up)) / 2U; break;
            case 4: value += png_paeth(left, up, up_left); break;
            default: break;
        }
        state.current[i] = static_cast<unsigned char>(value & 0xffU);
    }

    for (int x = 0; x < state.width; ++x) {
        const std::size_t src = static_cast<std::size_t>(x) * static_cast<std::size_t>(state.channels);
        Pixel pixel = 0;
        switch (state.color_type) {
            case 0: {
                const std::uint8_t value = state.current[src];
                pixel = rgba(value, value, value, 255);
                break;
            }
            case 2:
                pixel = rgba(state.current[src], state.current[src + 1U], state.current[src + 2U], 255);
                break;
            case 3: {
                const int index = state.current[src];
                pixel = index >= 0 && index < state.palette_size ? state.palette[static_cast<std::size_t>(index)] : rgba(0, 0, 0, 0);
                break;
            }
            case 4: {
                const std::uint8_t value = state.current[src];
                pixel = rgba(value, value, value, state.current[src + 1U]);
                break;
            }
            case 6:
                pixel = rgba(state.current[src], state.current[src + 1U], state.current[src + 2U], state.current[src + 3U]);
                break;
            default:
                error = "Unsupported PNG color type";
                return false;
        }
        state.pixels[static_cast<std::size_t>(state.row) * static_cast<std::size_t>(state.width) + static_cast<std::size_t>(x)] = pixel;
    }

    state.previous = state.current;
    ++state.row;
    return true;
}

bool process_png_output_bytes(StreamingPngState& state,
                              const unsigned char* bytes,
                              std::size_t size,
                              const ImageImportProgressCallback& progress,
                              std::string& error) {
    MemoryTraceScope trace("process_png_output_bytes");
    memory_trace_event("buffer", {}, "process_png_output_bytes.input", bytes, size, size, 1U);
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t copy_size = std::min(size - offset, state.scanline_bytes - state.scanline_offset);
        std::copy_n(bytes + offset, copy_size, state.scanline.data() + state.scanline_offset);
        offset += copy_size;
        state.scanline_offset += copy_size;
        if (state.scanline_offset == state.scanline_bytes) {
            if (!process_png_scanline(state, error)) {
                return false;
            }
            state.scanline_offset = 0;
            if ((state.row & 255) == 0 || state.row == state.height) {
                const float fraction = 0.10f + 0.85f * (static_cast<float>(state.row) / static_cast<float>(std::max(1, state.height)));
                publish_image_progress(progress,
                                       fraction,
                                       "Decoding PNG rows " + std::to_string(state.row) + " / " + std::to_string(state.height),
                                       "Decoding PNG",
                                       state.row,
                                       state.height);
            }
        }
    }
    return true;
}

bool decode_png_streaming_rgba_impl(const std::string& path,
                                    int& width,
                                    int& height,
                                    std::vector<Pixel>& pixels,
                                    std::string* error,
                                    const ImageImportProgressCallback& progress) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_error(error, "Could not open PNG: " + path);
        return false;
    }

    constexpr std::array<unsigned char, 8> kPngSignature = {137, 80, 78, 71, 13, 10, 26, 10};
    std::array<unsigned char, 8> signature{};
    if (!read_exact(file, signature.data(), signature.size()) || signature != kPngSignature) {
        set_error(error, "File is not a PNG: " + path);
        return false;
    }

    StreamingPngState state;
    std::fill(state.palette_alpha.begin(), state.palette_alpha.end(), 255);
    mz_stream stream{};
    bool inflate_started = false;
    bool inflate_finished = false;
    std::vector<unsigned char> input_buffer(1024U * 1024U);
    std::vector<unsigned char> output_buffer(1024U * 1024U);
    memory_trace_vector("decode_png_streaming_rgba.input_buffer", input_buffer);
    memory_trace_vector("decode_png_streaming_rgba.output_buffer", output_buffer);
    publish_image_progress(progress, 0.02f, "Reading PNG header", "Reading header", 0, 0, true);

    auto finish_with_error = [&](const std::string& message) {
        if (inflate_started) {
            mz_inflateEnd(&stream);
        }
        set_error(error, message);
        return false;
    };

    for (;;) {
        std::array<unsigned char, 4> length_bytes{};
        std::array<unsigned char, 4> type_bytes{};
        if (!read_exact(file, length_bytes.data(), length_bytes.size()) ||
            !read_exact(file, type_bytes.data(), type_bytes.size())) {
            return finish_with_error("Unexpected end of PNG");
        }
        const std::uint32_t chunk_length = read_be32_bytes(length_bytes);
        const std::string chunk_type(reinterpret_cast<const char*>(type_bytes.data()), type_bytes.size());

        if (chunk_type == "IHDR") {
            if (chunk_length != 13U) {
                return finish_with_error("Invalid PNG IHDR length");
            }
            std::array<unsigned char, 13> ihdr{};
            if (!read_exact(file, ihdr.data(), ihdr.size())) {
                return finish_with_error("Could not read PNG IHDR");
            }
            state.width = static_cast<int>((static_cast<std::uint32_t>(ihdr[0]) << 24U) |
                                           (static_cast<std::uint32_t>(ihdr[1]) << 16U) |
                                           (static_cast<std::uint32_t>(ihdr[2]) << 8U) |
                                           static_cast<std::uint32_t>(ihdr[3]));
            state.height = static_cast<int>((static_cast<std::uint32_t>(ihdr[4]) << 24U) |
                                            (static_cast<std::uint32_t>(ihdr[5]) << 16U) |
                                            (static_cast<std::uint32_t>(ihdr[6]) << 8U) |
                                            static_cast<std::uint32_t>(ihdr[7]));
            state.bit_depth = ihdr[8];
            state.color_type = ihdr[9];
            const int compression = ihdr[10];
            const int filter = ihdr[11];
            const int interlace = ihdr[12];
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG IHDR CRC");
            }
            if (state.width <= 0 || state.height <= 0 || state.bit_depth != 8 || compression != 0 || filter != 0 || interlace != 0) {
                return finish_with_error("Streaming PNG import supports non-interlaced 8-bit PNG images");
            }
            state.channels = png_channel_count(state.color_type);
            if (state.channels == 0) {
                return finish_with_error("Unsupported PNG color type");
            }
            const std::size_t pixel_count = static_cast<std::size_t>(state.width) * static_cast<std::size_t>(state.height);
            if (pixel_count == 0U || pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(Pixel)) {
                return finish_with_error("PNG dimensions are too large");
            }
            state.row_bytes = static_cast<std::size_t>(state.width) * static_cast<std::size_t>(state.channels);
            state.scanline_bytes = state.row_bytes + 1U;
            state.scanline.assign(state.scanline_bytes, 0);
            state.previous.assign(state.row_bytes, 0);
            state.current.assign(state.row_bytes, 0);
            state.pixels.resize(pixel_count);
            memory_trace_vector("decode_png_streaming_rgba.scanline", state.scanline);
            memory_trace_vector("decode_png_streaming_rgba.previous_row", state.previous);
            memory_trace_vector("decode_png_streaming_rgba.current_row", state.current);
            memory_trace_vector("decode_png_streaming_rgba.pixels", state.pixels);
            width = state.width;
            height = state.height;
            publish_image_progress(progress,
                                   0.05f,
                                   "Allocating " + std::to_string(state.width) + " x " + std::to_string(state.height) + " image",
                                   "Allocating image",
                                   0,
                                   0,
                                   true);
            continue;
        }

        if (chunk_type == "PLTE") {
            if (chunk_length % 3U != 0U || chunk_length > 256U * 3U) {
                return finish_with_error("Invalid PNG palette");
            }
            std::vector<unsigned char> palette_bytes(chunk_length);
            if (!read_exact(file, palette_bytes.data(), palette_bytes.size())) {
                return finish_with_error("Could not read PNG palette");
            }
            state.palette_size = static_cast<int>(chunk_length / 3U);
            for (int i = 0; i < state.palette_size; ++i) {
                const std::size_t src = static_cast<std::size_t>(i) * 3U;
                state.palette[static_cast<std::size_t>(i)] =
                    rgba(palette_bytes[src], palette_bytes[src + 1U], palette_bytes[src + 2U], state.palette_alpha[static_cast<std::size_t>(i)]);
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG palette CRC");
            }
            continue;
        }

        if (chunk_type == "tRNS") {
            std::vector<unsigned char> alpha_bytes(chunk_length);
            if (!read_exact(file, alpha_bytes.data(), alpha_bytes.size())) {
                return finish_with_error("Could not read PNG transparency");
            }
            if (state.color_type == 3) {
                const std::size_t alpha_count = std::min<std::size_t>(alpha_bytes.size(), state.palette_alpha.size());
                for (std::size_t i = 0; i < alpha_count; ++i) {
                    state.palette_alpha[i] = alpha_bytes[i];
                    if (i < static_cast<std::size_t>(state.palette_size)) {
                        state.palette[i] = with_alpha(state.palette[i], alpha_bytes[i]);
                    }
                }
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG transparency CRC");
            }
            continue;
        }

        if (chunk_type == "IDAT") {
            if (state.width <= 0 || state.height <= 0) {
                return finish_with_error("PNG IDAT arrived before IHDR");
            }
            if (!inflate_started) {
                if (mz_inflateInit(&stream) != MZ_OK) {
                    return finish_with_error("Could not initialize PNG inflater");
                }
                inflate_started = true;
                publish_image_progress(progress, 0.08f, "Decoding PNG", "Decoding PNG", 0, state.height);
            }
            std::uint32_t remaining = chunk_length;
            while (remaining > 0U) {
                const std::size_t step = std::min<std::size_t>(input_buffer.size(), remaining);
                if (!read_exact(file, input_buffer.data(), step)) {
                    return finish_with_error("Could not read PNG image data");
                }
                remaining -= static_cast<std::uint32_t>(step);
                stream.next_in = input_buffer.data();
                stream.avail_in = static_cast<unsigned int>(step);
                do {
                    stream.next_out = output_buffer.data();
                    stream.avail_out = static_cast<unsigned int>(output_buffer.size());
                    const int result = mz_inflate(&stream, MZ_NO_FLUSH);
                    const std::size_t produced = output_buffer.size() - static_cast<std::size_t>(stream.avail_out);
                    std::string process_error;
                    if (produced > 0U && !process_png_output_bytes(state, output_buffer.data(), produced, progress, process_error)) {
                        return finish_with_error(process_error);
                    }
                    if (result == MZ_STREAM_END) {
                        inflate_finished = true;
                        break;
                    }
                    if (result != MZ_OK) {
                        return finish_with_error("PNG inflate failed");
                    }
                } while (stream.avail_in > 0U || stream.avail_out == 0U);
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG image data CRC");
            }
            continue;
        }

        if (chunk_type == "IEND") {
            if (!skip_exact(file, chunk_length)) {
                return finish_with_error("Could not read PNG end chunk");
            }
            std::array<unsigned char, 4> crc{};
            if (!read_exact(file, crc.data(), crc.size())) {
                return finish_with_error("Could not read PNG end chunk CRC");
            }
            break;
        }

        if (!skip_exact(file, chunk_length)) {
            return finish_with_error("Could not skip PNG chunk");
        }
        std::array<unsigned char, 4> crc{};
        if (!read_exact(file, crc.data(), crc.size())) {
            return finish_with_error("Could not read PNG chunk CRC");
        }
    }

    if (inflate_started) {
        mz_inflateEnd(&stream);
    }
    if (!inflate_finished || state.row != state.height || state.scanline_offset != 0U) {
        set_error(error, "PNG ended before all image rows were decoded");
        return false;
    }
    memory_trace_vector("decode_png_streaming_rgba.state_pixels_before_move", state.pixels);
    pixels = std::move(state.pixels);
    memory_trace_vector("decode_png_streaming_rgba.output_pixels_after_move", pixels);
    memory_trace_vector("decode_png_streaming_rgba.state_pixels_after_move", state.pixels);
    publish_image_progress(progress, 0.96f, "Publishing decoded pixels", "Publishing image", 0, 0, true);
    return true;
}

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

nlohmann::json pixels_to_binary(const std::vector<Pixel>& pixels) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(pixels.size() * 4U);
    for (Pixel pixel : pixels) {
        bytes.push_back(r(pixel));
        bytes.push_back(g(pixel));
        bytes.push_back(b(pixel));
        bytes.push_back(a(pixel));
    }
    return nlohmann::json::binary(std::move(bytes));
}

bool pixels_from_binary(const nlohmann::json& value, std::vector<Pixel>& pixels) {
    if (value.is_null()) {
        pixels.clear();
        return true;
    }
    if (!value.is_binary()) return false;
    const auto& bytes = value.get_binary();
    if (bytes.size() % 4U != 0U) return false;
    pixels.resize(bytes.size() / 4U);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::size_t offset = index * 4U;
        pixels[index] = rgba(bytes[offset], bytes[offset + 1U], bytes[offset + 2U],
                             bytes[offset + 3U]);
    }
    return true;
}

nlohmann::json bytes_to_binary(const std::vector<std::uint8_t>& bytes) {
    return nlohmann::json::binary(bytes);
}

bool bytes_from_binary(const nlohmann::json& value, std::vector<std::uint8_t>& bytes) {
    if (value.is_null()) {
        bytes.clear();
        return true;
    }
    if (!value.is_binary()) return false;
    const auto& binary = value.get_binary();
    bytes.assign(binary.begin(), binary.end());
    return true;
}

nlohmann::json selection_to_recovery_json(const SelectionMask& selection) {
    return {{"width", selection.width}, {"height", selection.height},
            {"active", selection.active}, {"mask", bytes_to_binary(selection.mask)}};
}

bool selection_from_recovery_json(const nlohmann::json& value, SelectionMask& selection) {
    selection.width = value.value("width", 0);
    selection.height = value.value("height", 0);
    selection.active = value.value("active", false);
    return selection.width >= 0 && selection.height >= 0 &&
           bytes_from_binary(value.at("mask"), selection.mask) &&
           selection.mask.size() == static_cast<std::size_t>(selection.width) *
                                        static_cast<std::size_t>(selection.height);
}

nlohmann::json palette_to_recovery_json(const Palette& palette) {
    return {{"colors", pixels_to_binary(palette.colors)}, {"active", palette.active}};
}

bool palette_from_recovery_json(const nlohmann::json& value, Palette& palette) {
    return pixels_from_binary(value.at("colors"), palette.colors) &&
           (palette.active = value.value("active", 0), true);
}

nlohmann::json layer_to_recovery_json(const Layer& layer) {
    return {{"name", layer.name}, {"visible", layer.visible}, {"opacity", layer.opacity},
            {"blend_mode", layer_blend_mode_name(layer.blend_mode)},
            {"mask_enabled", layer.mask_enabled}, {"clip_to_below", layer.clip_to_below},
            {"mask", bytes_to_binary(layer.mask)}};
}

bool layer_from_recovery_json(const nlohmann::json& value, Layer& layer) {
    layer.name = value.value("name", "Layer");
    layer.visible = value.value("visible", true);
    layer.opacity = value.value("opacity", 1.0f);
    layer.blend_mode = layer_blend_mode_from_json(value.value("blend_mode", "Normal"));
    layer.mask_enabled = value.value("mask_enabled", false);
    layer.clip_to_below = value.value("clip_to_below", false);
    return bytes_from_binary(value.at("mask"), layer.mask);
}

nlohmann::json layers_to_recovery_json(const std::vector<Layer>& layers) {
    nlohmann::json result = nlohmann::json::array();
    for (const Layer& layer : layers) result.push_back(layer_to_recovery_json(layer));
    return result;
}

bool layers_from_recovery_json(const nlohmann::json& value, std::vector<Layer>& layers) {
    if (!value.is_array()) return false;
    layers.clear();
    layers.reserve(value.size());
    for (const auto& item : value) {
        Layer layer;
        if (!layer_from_recovery_json(item, layer)) return false;
        layers.push_back(std::move(layer));
    }
    return true;
}

nlohmann::json frames_to_recovery_json(const std::vector<Frame>& frames) {
    nlohmann::json result = nlohmann::json::array();
    for (const Frame& frame : frames) {
        nlohmann::json cels = nlohmann::json::array();
        for (const Cel& cel : frame.cels) {
            cels.push_back({{"x", cel.x}, {"y", cel.y}, {"pixels", pixels_to_binary(cel.pixels)}});
        }
        result.push_back({{"duration_ms", frame.duration_ms}, {"cels", std::move(cels)}});
    }
    return result;
}

bool frames_from_recovery_json(const nlohmann::json& value, std::vector<Frame>& frames) {
    if (!value.is_array()) return false;
    frames.clear();
    frames.reserve(value.size());
    for (const auto& item : value) {
        Frame frame;
        frame.duration_ms = item.value("duration_ms", 100);
        const auto& cels = item.at("cels");
        if (!cels.is_array()) return false;
        for (const auto& cel_value : cels) {
            Cel cel;
            cel.x = cel_value.value("x", 0);
            cel.y = cel_value.value("y", 0);
            if (!pixels_from_binary(cel_value.at("pixels"), cel.pixels)) return false;
            frame.cels.push_back(std::move(cel));
        }
        frames.push_back(std::move(frame));
    }
    return true;
}

nlohmann::json tags_to_recovery_json(const std::vector<AnimationTag>& tags) {
    nlohmann::json result = nlohmann::json::array();
    for (const AnimationTag& tag : tags) {
        result.push_back({{"name", tag.name}, {"from", tag.from}, {"to", tag.to}});
    }
    return result;
}

bool tags_from_recovery_json(const nlohmann::json& value, std::vector<AnimationTag>& tags) {
    if (!value.is_array()) return false;
    tags.clear();
    for (const auto& item : value) {
        tags.push_back({item.value("name", "Tag"), item.value("from", 0), item.value("to", 0)});
    }
    return true;
}

nlohmann::json floating_to_recovery_json(const FloatingSelection& floating) {
    return {{"active", floating.active}, {"source_x", floating.source_x},
            {"source_y", floating.source_y}, {"offset_x", floating.offset_x},
            {"offset_y", floating.offset_y}, {"width", floating.width},
            {"height", floating.height}, {"pixels", pixels_to_binary(floating.pixels)},
            {"mask", bytes_to_binary(floating.mask)}};
}

bool floating_from_recovery_json(const nlohmann::json& value, FloatingSelection& floating) {
    floating.active = value.value("active", false);
    floating.source_x = value.value("source_x", 0);
    floating.source_y = value.value("source_y", 0);
    floating.offset_x = value.value("offset_x", 0);
    floating.offset_y = value.value("offset_y", 0);
    floating.width = value.value("width", 0);
    floating.height = value.value("height", 0);
    return pixels_from_binary(value.at("pixels"), floating.pixels) &&
           bytes_from_binary(value.at("mask"), floating.mask);
}

bool history_payload_to_recovery_json(const Document& document,
                                      const HistoryPixelPayload& payload,
                                      nlohmann::json& value,
                                      std::string* error) {
    if (payload.empty()) {
        value = nullptr;
        return true;
    }
    std::vector<Pixel> pixels;
    if (!document.materialize_history_payload(payload, pixels)) {
        set_error(error, "Could not read a disk-backed undo history payload");
        return false;
    }
    value = pixels_to_binary(pixels);
    return true;
}

bool history_payload_from_recovery_json(const nlohmann::json& value,
                                        HistoryPixelPayload& payload) {
    payload = {};
    if (value.is_null()) return true;
    if (!pixels_from_binary(value, payload.pixels)) return false;
    payload.pixel_count = static_cast<std::uint64_t>(payload.pixels.size());
    return true;
}

bool command_to_recovery_json(const Document& document, const UndoCommand& command,
                              nlohmann::json& value, std::string* error) {
    value = {{"name", command.name}, {"frame", command.frame}, {"layer", command.layer},
             {"before_active_layer", command.before_active_layer},
             {"before_active_frame", command.before_active_frame},
             {"after_active_layer", command.after_active_layer},
             {"after_active_frame", command.after_active_frame}};
    nlohmann::json diffs = nlohmann::json::array();
    for (const TileDiff& diff : command.pixel_diffs) {
        diffs.push_back({{"frame", diff.frame}, {"layer", diff.layer}, {"x", diff.x},
                         {"y", diff.y}, {"w", diff.w}, {"h", diff.h},
                         {"before", pixels_to_binary(diff.before)},
                         {"after", diff.after.empty() ? nlohmann::json(nullptr)
                                                       : pixels_to_binary(diff.after)}});
    }
    value["pixel_diffs"] = std::move(diffs);
    if (command.dense_pixel_diff) {
        nlohmann::json before;
        nlohmann::json after;
        if (!history_payload_to_recovery_json(document, command.dense_pixel_diff->before,
                                              before, error) ||
            !history_payload_to_recovery_json(document, command.dense_pixel_diff->after,
                                              after, error)) {
            return false;
        }
        value["dense_pixel_diff"] = {{"frame", command.dense_pixel_diff->frame},
                                      {"layer", command.dense_pixel_diff->layer},
                                      {"width", command.dense_pixel_diff->width},
                                      {"height", command.dense_pixel_diff->height},
                                      {"before", std::move(before)},
                                      {"after", std::move(after)}};
    } else {
        value["dense_pixel_diff"] = nullptr;
    }
    value["before_selection"] = command.before_selection
                                      ? selection_to_recovery_json(*command.before_selection)
                                      : nlohmann::json(nullptr);
    value["after_selection"] = command.after_selection
                                     ? selection_to_recovery_json(*command.after_selection)
                                     : nlohmann::json(nullptr);
    value["before_floating_selection"] = command.before_floating_selection
        ? floating_to_recovery_json(*command.before_floating_selection) : nlohmann::json(nullptr);
    value["after_floating_selection"] = command.after_floating_selection
        ? floating_to_recovery_json(*command.after_floating_selection) : nlohmann::json(nullptr);
    value["before_palette"] = command.before_palette
                                    ? palette_to_recovery_json(*command.before_palette)
                                    : nlohmann::json(nullptr);
    value["after_palette"] = command.after_palette
                                   ? palette_to_recovery_json(*command.after_palette)
                                   : nlohmann::json(nullptr);
    value["before_layers"] = command.before_layers
                                   ? layers_to_recovery_json(*command.before_layers)
                                   : nlohmann::json(nullptr);
    value["after_layers"] = command.after_layers
                                  ? layers_to_recovery_json(*command.after_layers)
                                  : nlohmann::json(nullptr);
    value["before_frames"] = command.before_frames
                                   ? frames_to_recovery_json(*command.before_frames)
                                   : nlohmann::json(nullptr);
    value["after_frames"] = command.after_frames
                                  ? frames_to_recovery_json(*command.after_frames)
                                  : nlohmann::json(nullptr);
    value["before_tags"] = command.before_tags ? tags_to_recovery_json(*command.before_tags)
                                                : nlohmann::json(nullptr);
    value["after_tags"] = command.after_tags ? tags_to_recovery_json(*command.after_tags)
                                              : nlohmann::json(nullptr);
    value["before_frame_duration_ms"] = command.before_frame_duration_ms
                                             ? nlohmann::json(*command.before_frame_duration_ms)
                                             : nlohmann::json(nullptr);
    value["after_frame_duration_ms"] = command.after_frame_duration_ms
                                            ? nlohmann::json(*command.after_frame_duration_ms)
                                            : nlohmann::json(nullptr);
    value["before_width"] = command.before_width ? nlohmann::json(*command.before_width)
                                                   : nlohmann::json(nullptr);
    value["before_height"] = command.before_height ? nlohmann::json(*command.before_height)
                                                     : nlohmann::json(nullptr);
    value["after_width"] = command.after_width ? nlohmann::json(*command.after_width)
                                                 : nlohmann::json(nullptr);
    value["after_height"] = command.after_height ? nlohmann::json(*command.after_height)
                                                   : nlohmann::json(nullptr);
    value["before_model"] = command.before_model
        ? nlohmann::json::parse(model_to_json(*command.before_model)) : nlohmann::json(nullptr);
    value["after_model"] = command.after_model
        ? nlohmann::json::parse(model_to_json(*command.after_model)) : nlohmann::json(nullptr);
    return true;
}

bool command_from_recovery_json(const nlohmann::json& value, UndoCommand& command) {
    command = {};
    command.name = value.value("name", "Recovered edit");
    command.frame = value.value("frame", 0);
    command.layer = value.value("layer", 0);
    command.before_active_layer = value.value("before_active_layer", 0);
    command.before_active_frame = value.value("before_active_frame", 0);
    command.after_active_layer = value.value("after_active_layer", 0);
    command.after_active_frame = value.value("after_active_frame", 0);
    for (const auto& diff_value : value.at("pixel_diffs")) {
        TileDiff diff;
        diff.frame = diff_value.value("frame", 0);
        diff.layer = diff_value.value("layer", 0);
        diff.x = diff_value.value("x", 0);
        diff.y = diff_value.value("y", 0);
        diff.w = diff_value.value("w", 0);
        diff.h = diff_value.value("h", 0);
        if (!pixels_from_binary(diff_value.at("before"), diff.before) ||
            !pixels_from_binary(diff_value.at("after"), diff.after)) return false;
        command.pixel_diffs.push_back(std::move(diff));
    }
    if (!value.at("dense_pixel_diff").is_null()) {
        const auto& dense_value = value.at("dense_pixel_diff");
        DensePixelDiff dense;
        dense.frame = dense_value.value("frame", 0);
        dense.layer = dense_value.value("layer", 0);
        dense.width = dense_value.value("width", 0);
        dense.height = dense_value.value("height", 0);
        if (!history_payload_from_recovery_json(dense_value.at("before"), dense.before) ||
            !history_payload_from_recovery_json(dense_value.at("after"), dense.after)) return false;
        command.dense_pixel_diff = std::move(dense);
    }
    const auto decode_selection = [&value](const char* key, std::optional<SelectionMask>& target) {
        if (!value.contains(key) || value.at(key).is_null()) return true;
        SelectionMask selection;
        if (!selection_from_recovery_json(value.at(key), selection)) return false;
        target = std::move(selection);
        return true;
    };
    const auto decode_floating = [&value](const char* key,
                                          std::optional<FloatingSelection>& target) {
        if (!value.contains(key) || value.at(key).is_null()) return true;
        FloatingSelection floating;
        if (!floating_from_recovery_json(value.at(key), floating)) return false;
        target = std::move(floating);
        return true;
    };
    const auto decode_palette = [&value](const char* key, std::optional<Palette>& target) {
        if (value.at(key).is_null()) return true;
        Palette palette;
        if (!palette_from_recovery_json(value.at(key), palette)) return false;
        target = std::move(palette);
        return true;
    };
    const auto decode_layers = [&value](const char* key, std::optional<std::vector<Layer>>& target) {
        if (value.at(key).is_null()) return true;
        std::vector<Layer> layers;
        if (!layers_from_recovery_json(value.at(key), layers)) return false;
        target = std::move(layers);
        return true;
    };
    const auto decode_frames = [&value](const char* key, std::optional<std::vector<Frame>>& target) {
        if (value.at(key).is_null()) return true;
        std::vector<Frame> frames;
        if (!frames_from_recovery_json(value.at(key), frames)) return false;
        target = std::move(frames);
        return true;
    };
    const auto decode_tags = [&value](const char* key, std::optional<std::vector<AnimationTag>>& target) {
        if (value.at(key).is_null()) return true;
        std::vector<AnimationTag> tags;
        if (!tags_from_recovery_json(value.at(key), tags)) return false;
        target = std::move(tags);
        return true;
    };
    if (!decode_selection("before_selection", command.before_selection) ||
        !decode_selection("after_selection", command.after_selection) ||
        !decode_floating("before_floating_selection", command.before_floating_selection) ||
        !decode_floating("after_floating_selection", command.after_floating_selection) ||
        !decode_palette("before_palette", command.before_palette) ||
        !decode_palette("after_palette", command.after_palette) ||
        !decode_layers("before_layers", command.before_layers) ||
        !decode_layers("after_layers", command.after_layers) ||
        !decode_frames("before_frames", command.before_frames) ||
        !decode_frames("after_frames", command.after_frames) ||
        !decode_tags("before_tags", command.before_tags) ||
        !decode_tags("after_tags", command.after_tags)) return false;
    if (!value.at("before_frame_duration_ms").is_null()) {
        command.before_frame_duration_ms = value.at("before_frame_duration_ms").get<int>();
    }
    if (!value.at("after_frame_duration_ms").is_null()) {
        command.after_frame_duration_ms = value.at("after_frame_duration_ms").get<int>();
    }
    const auto decode_int = [&value](const char* key, std::optional<int>& target) {
        if (value.contains(key) && !value.at(key).is_null()) target = value.at(key).get<int>();
    };
    decode_int("before_width", command.before_width);
    decode_int("before_height", command.before_height);
    decode_int("after_width", command.after_width);
    decode_int("after_height", command.after_height);
    const auto decode_model = [&value](const char* key, std::optional<ModelDocument>& target) {
        if (!value.contains(key) || value.at(key).is_null()) return true;
        ModelDocument model;
        if (!model_from_json(value.at(key).dump(), model)) return false;
        target = std::move(model);
        return true;
    };
    if (!decode_model("before_model", command.before_model) ||
        !decode_model("after_model", command.after_model)) return false;
    return true;
}

bool serialize_recovery_history(const Document& document, std::vector<unsigned char>& bytes,
                                std::string* error) {
    try {
        nlohmann::json root = {{"format", "pixelart98-recovery-history"}, {"version", 1},
                               {"selection", selection_to_recovery_json(document.selection)},
                               {"floating_selection", floating_to_recovery_json(document.floating_selection)},
                               {"palette_active", document.palette.active}};
        for (const auto& [key, history] :
             {std::pair{"undo", &document.undo_history_for_recovery()},
              std::pair{"redo", &document.redo_history_for_recovery()}}) {
            nlohmann::json commands = nlohmann::json::array();
            for (const UndoCommand& command : *history) {
                nlohmann::json command_value;
                if (!command_to_recovery_json(document, command, command_value, error)) return false;
                commands.push_back(std::move(command_value));
            }
            root[key] = std::move(commands);
        }
        bytes = nlohmann::json::to_cbor(root);
        return true;
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not serialize recovery history: ") + exception.what());
        return false;
    }
}

bool deserialize_recovery_history(const unsigned char* bytes, std::size_t size,
                                  Document& document, std::string* error) {
    try {
        const nlohmann::json root = nlohmann::json::from_cbor(bytes, bytes + size);
        if (root.value("format", std::string()) != "pixelart98-recovery-history" ||
            root.value("version", 0) != 1) {
            set_error(error, "Unsupported recovery history format");
            return false;
        }
        SelectionMask selection;
        FloatingSelection floating;
        if (!selection_from_recovery_json(root.at("selection"), selection) ||
            !floating_from_recovery_json(root.at("floating_selection"), floating)) {
            set_error(error, "Invalid recovery selection state");
            return false;
        }
        std::deque<UndoCommand> undo;
        std::deque<UndoCommand> redo;
        const auto decode_history = [&root](const char* key, std::deque<UndoCommand>& history) {
            const auto& values = root.at(key);
            if (!values.is_array() || values.size() > 128U) return false;
            for (const auto& command_value : values) {
                UndoCommand command;
                if (!command_from_recovery_json(command_value, command)) return false;
                history.push_back(std::move(command));
            }
            return true;
        };
        if (!decode_history("undo", undo) || !decode_history("redo", redo)) {
            set_error(error, "Invalid recovery undo history");
            return false;
        }
        document.selection = std::move(selection);
        document.floating_selection = std::move(floating);
        document.palette.active = root.value("palette_active", 0);
        document.restore_history_for_recovery(std::move(undo), std::move(redo));
        return true;
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not read recovery history: ") + exception.what());
        return false;
    }
}

} // namespace

Document document_from_pixels(int width, int height, std::vector<Pixel> pixels) {
    MemoryTraceScope trace("document_from_pixels");
    memory_trace_vector("document_from_pixels.input_pixels", pixels);
    Document document = document_from_pixels_impl(width, height, std::move(pixels));
    if (document.has_active_cel()) {
        memory_trace_vector("document_from_pixels.document_pixels", document.active_cel().pixels);
    }
    memory_trace_vector("document_from_pixels.source_after_move", pixels);
    return document;
}

bool decode_png_streaming_rgba(const std::string& path,
                               int& width,
                               int& height,
                               std::vector<Pixel>& pixels,
                               std::string* error,
                               const ImageImportProgressCallback& progress) {
    MemoryTraceScope trace("decode_png_streaming_rgba", path);
    const bool ok = decode_png_streaming_rgba_impl(path, width, height, pixels, error, progress);
    memory_trace_vector("decode_png_streaming_rgba.output", pixels);
    return ok;
}

bool save_project(const std::string& path, const Document& document, const ModelDocument& model, std::string* error) {
    return save_project(std::filesystem::path(path), document, model, error);
}

bool save_project_archive(const std::filesystem::path& path, const Document& document,
                          const ModelDocument& model,
                          const std::vector<unsigned char>* recovery_history,
                          std::string* error) {
    std::FILE* file = open_project_file(path, true);
    if (file == nullptr) {
        set_error(error, "Could not create project archive: " + path_display(path));
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_cfile(&zip, file, 0)) {
        std::fclose(file);
        set_error(error, "Could not create project archive: " + path_display(path));
        return false;
    }

    bool ok = true;
    auto root = document_to_json(document);
    std::string json = root.dump(2);
    ok = ok && mz_zip_writer_add_mem(&zip, "project.json", json.data(), json.size(), MZ_BEST_COMPRESSION);
    std::string model_json = model_to_json(model);
    ok = ok && mz_zip_writer_add_mem(&zip, "model.json", model_json.data(), model_json.size(), MZ_BEST_COMPRESSION);
    if (recovery_history != nullptr) {
        ok = ok && mz_zip_writer_add_mem(&zip, "history.cbor", recovery_history->data(),
                                         recovery_history->size(), MZ_BEST_COMPRESSION);
    }

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
    const bool closed = std::fclose(file) == 0;
    if (!ok) {
        set_error(error, "Could not finish project archive");
        return false;
    }
    if (!closed) {
        set_error(error, "Could not close project archive: " + path_display(path));
        return false;
    }
    return true;
}

bool save_project(const std::filesystem::path& path, const Document& document,
                  const ModelDocument& model, std::string* error) {
    return save_project_archive(path, document, model, nullptr, error);
}

bool save_recovery_project(const std::filesystem::path& path, const Document& document,
                           const ModelDocument& model, std::string* error) {
    std::vector<unsigned char> history;
    if (!serialize_recovery_history(document, history, error)) return false;
    return save_project_archive(path, document, model, &history, error);
}

bool load_project(const std::string& path, ProjectBundle& out_bundle, std::string* error) {
    return load_project(std::filesystem::path(path), out_bundle, error);
}

bool load_project(const std::filesystem::path& path, ProjectBundle& out_bundle, std::string* error) {
    std::FILE* file = open_project_file(path, false);
    if (file == nullptr) {
        set_error(error, "Could not open project archive: " + path_display(path));
        return false;
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file, 0, 0)) {
        std::fclose(file);
        set_error(error, "Could not open project archive: " + path_display(path));
        return false;
    }

    std::size_t json_size = 0;
    void* json_mem = mz_zip_reader_extract_file_to_heap(&zip, "project.json", &json_size, 0);
    if (!json_mem) {
        mz_zip_reader_end(&zip);
        std::fclose(file);
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
    std::fclose(file);
    if (!ok) {
        return false;
    }
    out_bundle = {std::move(document), std::move(model)};
    return true;
}

bool load_recovery_project(const std::filesystem::path& path, ProjectBundle& out_bundle,
                           std::string* error) {
    if (!load_project(path, out_bundle, error)) return false;
    std::FILE* file = open_project_file(path, false);
    if (file == nullptr) {
        set_error(error, "Could not reopen recovery archive: " + path_display(path));
        return false;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_cfile(&zip, file, 0, 0)) {
        std::fclose(file);
        set_error(error, "Could not read recovery archive history");
        return false;
    }
    std::size_t history_size = 0;
    void* history_memory =
        mz_zip_reader_extract_file_to_heap(&zip, "history.cbor", &history_size, 0);
    bool ok = history_memory != nullptr;
    if (!ok) {
        set_error(error, "history.cbor missing from recovery archive");
    } else {
        ok = deserialize_recovery_history(static_cast<unsigned char*>(history_memory),
                                          history_size, out_bundle.document, error);
        mz_free(history_memory);
    }
    mz_zip_reader_end(&zip);
    std::fclose(file);
    return ok;
}

bool import_image(const std::string& path,
                  Document& out_document,
                  std::string* error,
                  const ImageImportProgressCallback& progress) {
    MemoryTraceScope trace("import_image", path);
    publish_image_progress(progress, 0.01f, "Opening image", "Opening file", 0, 0, true);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = nullptr;
    {
        MemoryTraceScope stbi_trace("stbi_load", path);
        pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (pixels != nullptr && width > 0 && height > 0) {
            const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            memory_trace_event("buffer",
                               {},
                               "stbi_load.result",
                               pixels,
                               pixel_count,
                               pixel_count,
                               sizeof(Pixel),
                               "channels=" + std::to_string(channels));
        }
    }
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        const std::string failure = reason == nullptr ? std::string("unknown decoder failure") : std::string(reason);
        std::vector<Pixel> streaming_pixels;
        std::string streaming_error;
        if (decode_png_streaming_rgba(path, width, height, streaming_pixels, &streaming_error, progress)) {
            memory_trace_vector("import_image.streaming_pixels_before_document", streaming_pixels);
            out_document = document_from_pixels(width, height, std::move(streaming_pixels));
            if (out_document.has_active_cel()) {
                memory_trace_vector("import_image.out_document_pixels", out_document.active_cel().pixels);
            }
            publish_image_progress(progress,
                                   1.0f,
                                   "Decoded " + std::to_string(width) + " x " + std::to_string(height) + " PNG",
                                   "Decoded image",
                                   1,
                                   1);
            return true;
        }
        set_error(error, "Could not import image: " + path + " (" + failure + "; streaming PNG fallback: " + streaming_error + ")");
        return false;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<Pixel> imported_pixels(reinterpret_cast<Pixel*>(pixels),
                                       reinterpret_cast<Pixel*>(pixels + pixel_count * sizeof(Pixel)));
    memory_trace_vector("import_image.imported_pixels", imported_pixels);
    stbi_image_free(pixels);
    memory_trace_note("import_image.stbi_buffer_freed");
    out_document = document_from_pixels(width, height, std::move(imported_pixels));
    if (out_document.has_active_cel()) {
        memory_trace_vector("import_image.out_document_pixels", out_document.active_cel().pixels);
    }
    publish_image_progress(progress,
                           1.0f,
                           "Decoded " + std::to_string(width) + " x " + std::to_string(height) + " image",
                           "Decoded image",
                           1,
                           1);
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

} // namespace px
