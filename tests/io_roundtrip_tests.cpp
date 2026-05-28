#include "core/Document.hpp"
#include "core/Model.hpp"
#include "io/ProjectIO.hpp"

#include <nlohmann/json.hpp>
#include <stb_image_write.h>

#undef NDEBUG
#include <cassert>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace px;

namespace {

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() / ("PixelArt98 IO Tests " + std::to_string(stamp));
    std::filesystem::create_directories(root / "nested folder");
    return root;
}

std::vector<unsigned char> rgba_bytes(const std::vector<Pixel>& pixels) {
    std::vector<unsigned char> bytes;
    bytes.reserve(pixels.size() * 4U);
    for (Pixel pixel : pixels) {
        bytes.push_back(r(pixel));
        bytes.push_back(g(pixel));
        bytes.push_back(b(pixel));
        bytes.push_back(a(pixel));
    }
    return bytes;
}

std::vector<unsigned char> rgb_bytes(const std::vector<Pixel>& pixels) {
    std::vector<unsigned char> bytes;
    bytes.reserve(pixels.size() * 3U);
    for (Pixel pixel : pixels) {
        bytes.push_back(r(pixel));
        bytes.push_back(g(pixel));
        bytes.push_back(b(pixel));
    }
    return bytes;
}

std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    assert(file);
    return nlohmann::json::parse(file);
}

bool bytes_start_with(const std::vector<unsigned char>& bytes, std::string_view expected) {
    if (bytes.size() < expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (bytes[i] != static_cast<unsigned char>(expected[i])) {
            return false;
        }
    }
    return true;
}

bool file_contains(const std::filesystem::path& path, std::string_view needle) {
    const auto bytes = read_bytes(path);
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

Pixel expected_pixel(int frame, int layer, int x, int y) {
    return rgba(static_cast<std::uint8_t>(20 + frame * 60 + layer * 25 + x * 7),
                static_cast<std::uint8_t>(30 + frame * 20 + layer * 50 + y * 11),
                static_cast<std::uint8_t>(40 + frame * 30 + layer * 15 + x * 3 + y * 5),
                255);
}

Document make_document() {
    Document document = Document::create(5, 4);
    document.layers[0].name = "Base";
    document.layers[0].blend_mode = LayerBlendMode::Normal;
    document.palette.colors = {
        rgba(20, 30, 40),
        rgba(80, 50, 70),
        rgba(45, 80, 55),
        rgba(105, 100, 85),
        rgba(170, 120, 130),
        rgba(0, 0, 0),
        rgba(255, 255, 255)
    };

    document.add_layer("Highlights");
    document.layers[1].opacity = 1.0f;
    document.layers[1].blend_mode = LayerBlendMode::Normal;
    document.add_frame(false);
    document.frames[0].duration_ms = 120;
    document.frames[1].duration_ms = 240;
    document.add_tag("Blink", 0, 1);
    document.playback_mode = PlaybackMode::PingPong;

    for (int frame = 0; frame < 2; ++frame) {
        for (int layer = 0; layer < 2; ++layer) {
            Cel& cel = document.cel(frame, layer);
            for (int y = 0; y < document.height; ++y) {
                for (int x = 0; x < document.width; ++x) {
                    cel.pixels[static_cast<std::size_t>(document.pixel_index(x, y))] = expected_pixel(frame, layer, x, y);
                }
            }
        }
    }
    document.active_frame = 0;
    document.active_layer = 0;
    document.clear_history();
    return document;
}

ModelDocument make_model() {
    ModelDocument model;
    model.texture_width = 32;
    model.texture_height = 16;
    Cuboid cuboid;
    cuboid.name = "Panel";
    cuboid.from = {1.0f, 2.0f, 3.0f};
    cuboid.to = {12.0f, 8.0f, 15.0f};
    cuboid.rotation_origin = {4.0f, 5.0f, 6.0f};
    cuboid.rotation_axis = 2;
    cuboid.rotation_angle = 22.5f;
    cuboid.rotation_rescale = true;
    cuboid.uv[0] = {0, 0, 5, 4};
    cuboid.uv[1] = {5, 0, 5, 4};
    cuboid.uv[2] = {10, 0, 4, 4};
    cuboid.uv[3] = {14, 0, 4, 4};
    cuboid.uv[4] = {0, 4, 8, 8};
    cuboid.uv[5] = {8, 4, 8, 8};
    model.cuboids.push_back(cuboid);
    model.selected_cuboid = 0;
    model.selected_face = 4;
    clamp_model_uvs(model);
    return model;
}

void assert_same_pixel(Pixel actual, Pixel expected) {
    if (r(actual) != r(expected) || g(actual) != g(expected) || b(actual) != b(expected) || a(actual) != a(expected)) {
        std::cerr << "pixel mismatch: actual=("
                  << static_cast<int>(r(actual)) << ", "
                  << static_cast<int>(g(actual)) << ", "
                  << static_cast<int>(b(actual)) << ", "
                  << static_cast<int>(a(actual)) << ") expected=("
                  << static_cast<int>(r(expected)) << ", "
                  << static_cast<int>(g(expected)) << ", "
                  << static_cast<int>(b(expected)) << ", "
                  << static_cast<int>(a(expected)) << ")\n";
        assert(false);
    }
}

void assert_document_shape(const Document& document) {
    assert(document.width == 5);
    assert(document.height == 4);
    assert(document.frames.size() == 2);
    assert(document.layers.size() == 2);
}

void test_project_roundtrip(const std::filesystem::path& root) {
    const Document source = make_document();
    const ModelDocument model = make_model();
    const auto path = root / "nested folder" / "project roundtrip.pixart";

    std::string error;
    assert(save_project(path.string(), source, model, &error));

    ProjectBundle loaded;
    assert(load_project(path.string(), loaded, &error));
    assert_document_shape(loaded.document);
    assert(loaded.document.layers[0].name == "Base");
    assert(loaded.document.layers[1].name == "Highlights");
    assert(loaded.document.frames[0].duration_ms == 120);
    assert(loaded.document.frames[1].duration_ms == 240);
    assert(loaded.document.tags.size() == 1);
    assert(loaded.document.tags[0].name == "Blink");
    assert(loaded.document.playback_mode == PlaybackMode::PingPong);
    assert_same_pixel(loaded.document.frames[1].cels[1].pixels[0], expected_pixel(1, 1, 0, 0));
    assert(loaded.model.cuboids.size() == 1);
    assert(loaded.model.cuboids[0].name == "Panel");
    assert(loaded.model.cuboids[0].rotation_axis == 2);
    assert(loaded.model.cuboids[0].rotation_rescale);
}

void write_import_fixture(const std::filesystem::path& path, int width, int height, std::string_view extension) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(width * height), rgba(0, 0, 0, 255));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[static_cast<std::size_t>(y * width + x)] =
                rgba(static_cast<std::uint8_t>(10 + x * 40),
                     static_cast<std::uint8_t>(20 + y * 30),
                     static_cast<std::uint8_t>(30 + x * 10 + y * 20),
                     255);
        }
    }

    if (extension == ".png") {
        const auto bytes = rgba_bytes(pixels);
        assert(stbi_write_png(path.string().c_str(), width, height, 4, bytes.data(), width * 4) != 0);
    } else if (extension == ".bmp") {
        const auto bytes = rgb_bytes(pixels);
        assert(stbi_write_bmp(path.string().c_str(), width, height, 3, bytes.data()) != 0);
    } else if (extension == ".tga") {
        const auto bytes = rgba_bytes(pixels);
        assert(stbi_write_tga(path.string().c_str(), width, height, 4, bytes.data()) != 0);
    } else if (extension == ".jpg" || extension == ".jpeg") {
        const auto bytes = rgb_bytes(pixels);
        assert(stbi_write_jpg(path.string().c_str(), width, height, 3, bytes.data(), 100) != 0);
    } else {
        assert(false);
    }
}

void test_image_import_formats(const std::filesystem::path& root) {
    const std::vector<std::string> extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    std::string error;
    for (const auto& extension : extensions) {
        const auto path = root / "nested folder" / ("import fixture" + extension);
        write_import_fixture(path, 3, 2, extension);

        Document imported;
        assert(import_image(path.string(), imported, &error));
        assert(imported.width == 3);
        assert(imported.height == 2);
        assert(imported.layers.size() == 1);
        assert(imported.frames.size() == 1);
        assert(a(imported.active_cel().pixels[0]) == 255);
    }
}

void test_import_image_as_layer(const std::filesystem::path& root) {
    const auto path = root / "layer source.png";
    write_import_fixture(path, 3, 2, ".png");

    Document document = Document::create(5, 4);
    std::string error;
    assert(import_image_as_layer(path.string(), document, "Imported Layer", &error));
    assert(document.layers.size() == 2);
    assert(document.layers[1].name == "Imported Layer");
    assert(document.active_layer == 1);
    assert(a(document.active_cel().pixels[static_cast<std::size_t>(document.pixel_index(2, 1))]) == 255);
    assert(a(document.active_cel().pixels[static_cast<std::size_t>(document.pixel_index(3, 1))]) == 0);
}

void test_png_export_roundtrip(const std::filesystem::path& root) {
    const Document source = make_document();
    const auto path = root / "export frame.png";
    std::string error;
    assert(export_png(path.string(), source, 0, &error));
    assert(bytes_start_with(read_bytes(path), "\x89PNG\r\n\x1A\n"));

    Document imported;
    assert(import_image(path.string(), imported, &error));
    assert(imported.width == source.width);
    assert(imported.height == source.height);
    assert_same_pixel(imported.active_cel().pixels[0], source.composite_frame(0)[0]);
}

void test_spritesheet_export(const std::filesystem::path& root) {
    const Document source = make_document();
    const auto png_path = root / "sprite sheet.png";
    const auto json_path = root / "sprite sheet.json";
    std::string error;
    assert(export_spritesheet(png_path.string(), json_path.string(), source, &error));

    Document sheet;
    assert(import_image(png_path.string(), sheet, &error));
    assert(sheet.width == source.width * 2);
    assert(sheet.height == source.height);
    assert_same_pixel(sheet.active_cel().pixels[0], source.composite_frame(0)[0]);
    assert_same_pixel(sheet.active_cel().pixels[static_cast<std::size_t>(source.width)], source.composite_frame(1)[0]);

    const auto meta = read_json(json_path);
    assert(meta.at("frame_width").get<int>() == source.width);
    assert(meta.at("frame_height").get<int>() == source.height);
    assert(meta.at("frames").size() == source.frames.size());
    assert(meta.at("frames")[1].at("duration_ms").get<int>() == 240);
}

void test_gif_export(const std::filesystem::path& root) {
    const Document source = make_document();
    const auto path = root / "animation.gif";
    std::string error;
    assert(export_gif(path.string(), source, &error));
    assert(bytes_start_with(read_bytes(path), "GIF89a"));

    Document imported;
    assert(import_image(path.string(), imported, &error));
    assert(imported.width == source.width);
    assert(imported.height == source.height);
}

void test_apng_export(const std::filesystem::path& root) {
    const Document source = make_document();
    const auto path = root / "animation.png";
    std::string error;
    assert(export_apng(path.string(), source, &error));
    assert(bytes_start_with(read_bytes(path), "\x89PNG\r\n\x1A\n"));
    assert(file_contains(path, "acTL"));
    assert(file_contains(path, "fcTL"));
    assert(file_contains(path, "fdAT"));

    Document imported;
    assert(import_image(path.string(), imported, &error));
    assert(imported.width == source.width);
    assert(imported.height == source.height);
}

void test_aseprite_roundtrip(const std::filesystem::path& root) {
    const Document source = make_document();
    const auto path = root / "sprite.aseprite";
    std::string error;
    assert(export_aseprite(path.string(), source, &error));

    Document imported;
    assert(import_aseprite(path.string(), imported, &error));
    assert_document_shape(imported);
    assert(imported.layers[0].name == "Base");
    assert(imported.layers[1].name == "Highlights");
    assert(imported.frames[0].duration_ms == 120);
    assert(imported.frames[1].duration_ms == 240);
    assert(imported.tags.size() == 1);
    assert(imported.tags[0].name == "Blink");
    assert(imported.playback_mode == PlaybackMode::PingPong);
    assert_same_pixel(imported.frames[1].cels[1].pixels[0], expected_pixel(1, 1, 0, 0));
}

void test_model_json_roundtrip(const std::filesystem::path& root) {
    const ModelDocument source = make_model();
    const auto path = root / "model.json";
    std::string error;
    assert(export_model_json(path.string(), source, &error));

    ModelDocument imported;
    assert(import_model_json(path.string(), imported, &error));
    assert(imported.texture_width == 32);
    assert(imported.texture_height == 16);
    assert(imported.selected_face == 4);
    assert(imported.cuboids.size() == 1);
    assert(imported.cuboids[0].name == "Panel");
    assert(imported.cuboids[0].uv[4].w == 8);
}

void test_minecraft_model_roundtrip(const std::filesystem::path& root) {
    const ModelDocument source = make_model();
    const auto path = root / "minecraft block model.json";
    std::string error;
    assert(export_minecraft_model(path.string(), source, "block/panel_texture", &error));

    const auto json = read_json(path);
    assert(json.at("textures").at("texture").get<std::string>() == "block/panel_texture");
    assert(json.at("elements").size() == 1);

    ModelDocument imported;
    assert(import_minecraft_model(path.string(), imported, &error));
    assert(imported.texture_width == 32);
    assert(imported.texture_height == 16);
    assert(imported.cuboids.size() == 1);
    assert(imported.cuboids[0].rotation_axis == 2);
    assert(std::abs(imported.cuboids[0].rotation_angle - 22.5f) < 0.001f);
    assert(imported.cuboids[0].rotation_rescale);
    assert(imported.cuboids[0].uv[0].w == 5);
}

void test_import_failures_are_reported(const std::filesystem::path& root) {
    std::string error;
    Document document;
    assert(!import_image((root / "missing.png").string(), document, &error));
    assert(!error.empty());

    const auto bad_model = root / "bad model.json";
    {
        std::ofstream file(bad_model);
        file << "{not json";
    }
    ModelDocument model;
    error.clear();
    assert(!import_model_json(bad_model.string(), model, &error));
    assert(!error.empty());
}

} // namespace

int main() {
    const auto root = make_test_root();
    test_project_roundtrip(root);
    test_image_import_formats(root);
    test_import_image_as_layer(root);
    test_png_export_roundtrip(root);
    test_spritesheet_export(root);
    test_gif_export(root);
    test_apng_export(root);
    test_aseprite_roundtrip(root);
    test_model_json_roundtrip(root);
    test_minecraft_model_roundtrip(root);
    test_import_failures_are_reported(root);
    std::filesystem::remove_all(root);
    std::cout << "pixelart import/export tests passed\n";
    return 0;
}
