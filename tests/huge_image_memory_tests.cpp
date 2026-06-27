// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "io/ProjectIO.hpp"
#include "render/GLTiledCanvasTexture.hpp"
#include "ui/EditorApp.hpp"

// Keep unit-test assertions active even when the project is configured as Release.
#undef NDEBUG
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#endif

using namespace px;

namespace {

std::filesystem::path make_test_root() {
    auto root = std::filesystem::temp_directory_path() / "pixelart_huge_image_memory_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void set_test_env(const char* name, const char* value) {
#if defined(_WIN32)
    assert(_putenv_s(name, value) == 0);
#else
    assert(setenv(name, value, 1) == 0);
#endif
}

std::vector<Pixel> make_pixels(int width, int height) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                rgba(static_cast<std::uint8_t>((x * 13 + y * 3) & 255),
                     static_cast<std::uint8_t>((x * 5 + y * 17) & 255),
                     static_cast<std::uint8_t>((x * 11 + y * 7) & 255),
                     255);
        }
    }
    return pixels;
}

void test_document_from_pixels_moves_buffer() {
    constexpr int width = 32;
    constexpr int height = 16;
    std::vector<Pixel> pixels = make_pixels(width, height);
    const Pixel* original_data = pixels.data();

    Document document = document_from_pixels(width, height, std::move(pixels));

    assert(pixels.empty());
    assert(document.width == width);
    assert(document.height == height);
    assert(document.has_active_cel());
    assert(document.active_cel().pixels.size() == static_cast<std::size_t>(width * height));
    assert(document.active_cel().pixels.data() == original_data);
}

void test_streaming_png_decode_hands_off_single_buffer(const std::filesystem::path& root) {
    constexpr int width = 23;
    constexpr int height = 17;
    Document source = document_from_pixels(width, height, make_pixels(width, height));
    const auto png = root / "streaming_source.png";
    std::string error;
    assert(export_png(png.string(), source, 0, &error));

    int decoded_width = 0;
    int decoded_height = 0;
    std::vector<Pixel> decoded_pixels;
    assert(decode_png_streaming_rgba(png.string(), decoded_width, decoded_height, decoded_pixels, &error));
    assert(decoded_width == width);
    assert(decoded_height == height);
    assert(decoded_pixels.size() == static_cast<std::size_t>(width * height));
    assert(decoded_pixels.capacity() == decoded_pixels.size());
    const Pixel* decoded_data = decoded_pixels.data();

    Document decoded = document_from_pixels(decoded_width, decoded_height, std::move(decoded_pixels));

    assert(decoded_pixels.empty());
    assert(decoded.active_cel().pixels.data() == decoded_data);
    assert(decoded.active_cel().pixels == source.active_cel().pixels);
}

void test_cpu_pyramid_dimensions_and_move_handoff() {
    constexpr int width = 1025;
    constexpr int height = 513;
    const std::vector<Pixel> pixels = make_pixels(width, height);

    GLTiledCanvasTexture::CpuPyramid pyramid =
        GLTiledCanvasTexture::build_cpu_pyramid_with_min_pixels(width, height, pixels, 1U);

    assert(pyramid.width == width);
    assert(pyramid.height == height);
    assert(pyramid.levels.size() == 2U);
    assert(pyramid.levels[0].width == 513);
    assert(pyramid.levels[0].height == 257);
    assert(pyramid.levels[1].width == 257);
    assert(pyramid.levels[1].height == 129);
    const std::size_t expected_pixels =
        static_cast<std::size_t>(513 * 257) + static_cast<std::size_t>(257 * 129);
    assert(pyramid.pixel_count() == expected_pixels);

    GLTiledCanvasTexture texture;
    texture.set_prepared_pyramid(std::move(pyramid));

    assert(pyramid.empty());
    assert(texture.has_prepared_pyramid());
    assert(texture.prepared_pyramid_pixel_count() == expected_pixels);
    assert(texture.scratch_capacity_pixels() == 0U);
    assert(GLTiledCanvasTexture::max_tile_scratch_pixels() == 512U * 512U);
}

void test_pixel_edit_history_uses_tile_diffs() {
    constexpr int width = 64;
    constexpr int height = 64;
    Document document = document_from_pixels(width, height, make_pixels(width, height));
    const std::size_t changed_index = static_cast<std::size_t>(document.pixel_index(31, 29));
    const Pixel original = document.active_cel().pixels[changed_index];
    std::vector<Pixel> before = document.snapshot_active_cel();
    document.active_cel().pixels[changed_index] = rgba(255, 0, 0, 255);
    document.commit_active_cel_edit("Synthetic Huge Pixel Edit", std::move(before));

    assert(document.undo_stack_full_frame_pixel_capacity() == 0U);
    assert(document.undo_stack_pixel_diff_capacity() > 0U);
    assert(document.undo_stack_pixel_diff_capacity() < static_cast<std::size_t>(width * height));
    const std::size_t undo_only_capacity = document.undo_stack_pixel_diff_capacity();
    assert(document.undo());
    assert(document.active_cel().pixels[changed_index] == original);
    assert(document.undo_stack_pixel_diff_capacity() == undo_only_capacity * 2U);
    assert(document.redo());
    assert(document.active_cel().pixels[changed_index] == rgba(255, 0, 0, 255));
    assert(document.undo_stack_pixel_diff_capacity() == undo_only_capacity);
}

void test_replace_active_pixels_uses_before_only_tile_diffs() {
    constexpr int width = 64;
    constexpr int height = 64;
    Document document = document_from_pixels(width, height, make_pixels(width, height));
    const std::vector<Pixel> original = document.active_cel().pixels;
    std::vector<Pixel> replacement = original;
    for (Pixel& pixel : replacement) {
        pixel = rgba(r(pixel), r(pixel), r(pixel), a(pixel));
    }

    document.replace_active_pixels(std::move(replacement), "Synthetic Huge Replacement");

    assert(document.undo_stack_full_frame_pixel_capacity() == 0U);
    assert(document.undo_stack_pixel_diff_capacity() > 0U);
    assert(document.undo_stack_pixel_diff_capacity() <= static_cast<std::size_t>(width * height));
    const std::size_t undo_only_capacity = document.undo_stack_pixel_diff_capacity();
    assert(document.undo());
    assert(document.active_cel().pixels == original);
    assert(document.undo_stack_pixel_diff_capacity() == undo_only_capacity * 2U);
    assert(document.redo());
    assert(document.active_cel().pixels != original);
    assert(document.undo_stack_pixel_diff_capacity() == undo_only_capacity);
}

void test_dense_history_spills_to_private_journal(const std::filesystem::path& root) {
    const auto history_root = root / "history";
    set_test_env("PIXELART_DENSE_HISTORY_MIN_BYTES", "1");
    set_test_env("PIXELART_HISTORY_SPILL_BYTES", "1");
    set_test_env("PIXELART_HISTORY_DIR", history_root.string().c_str());

    constexpr int width = 64;
    constexpr int height = 64;
    Document document = document_from_pixels(width, height, make_pixels(width, height));
    const std::vector<Pixel> original = document.active_cel().pixels;
    std::vector<Pixel> replacement = original;
    for (Pixel& pixel : replacement) {
        pixel = rgba(255 - r(pixel), 255 - g(pixel), 255 - b(pixel), a(pixel));
    }

    document.replace_active_pixels(std::move(replacement), "Dense Synthetic Replacement");

    const std::size_t full_pixels = static_cast<std::size_t>(width * height);
    assert(document.undo_stack_full_frame_pixel_capacity() == 0U);
    assert(document.undo_stack_pixel_diff_capacity() == 0U);
    assert(document.undo_stack_disk_history_pixel_count() == full_pixels);
    assert(std::filesystem::exists(history_root));

    bool found_journal = false;
    for (const auto& entry : std::filesystem::directory_iterator(history_root)) {
        if (entry.path().extension() == ".pxhist") {
            found_journal = true;
            const auto permissions = entry.status().permissions();
            assert((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
            assert((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
            assert((permissions & std::filesystem::perms::group_read) == std::filesystem::perms::none);
            assert((permissions & std::filesystem::perms::others_read) == std::filesystem::perms::none);
        }
    }
    assert(found_journal);

    assert(document.undo());
    assert(document.active_cel().pixels == original);
    assert(document.undo_stack_pixel_diff_capacity() == 0U);
    assert(document.undo_stack_disk_history_pixel_count() == full_pixels * 2U);

    assert(document.redo());
    assert(document.active_cel().pixels != original);
    assert(document.undo_stack_pixel_diff_capacity() == 0U);
    assert(document.undo_stack_disk_history_pixel_count() == full_pixels);
}

void test_editor_huge_history_and_refresh_are_lightweight() {
    set_test_env("PIXELART_HUGE_DOCUMENT_PIXEL_THRESHOLD", "1024");
    set_test_env("PIXELART_HISTOGRAM_SAMPLE_LIMIT", "256");

    constexpr int width = 64;
    constexpr int height = 64;
    Document document = document_from_pixels(width, height, make_pixels(width, height));
    const std::size_t live_pixels = document.active_cel().pixels.capacity();

    EditorApp app(nullptr, AppSettings{});
    app.debug_replace_document_for_memory_test(std::move(document));

    assert(app.debug_huge_document_history_mode());
    assert(app.debug_canvas_uses_active_cel());
    assert(app.debug_composite_pixel_capacity() == 0U);
    assert(app.debug_history_document_pixel_capacity() == 0U);
    app.debug_update_histogram_cache_for_memory_test();
    assert(app.debug_histogram_cache_approximate());
    assert(app.debug_composite_pixel_capacity() == 0U);
    assert(live_pixels == static_cast<std::size_t>(width * height));
}

} // namespace

int main() {
    const auto root = make_test_root();
    test_document_from_pixels_moves_buffer();
    test_streaming_png_decode_hands_off_single_buffer(root);
    test_cpu_pyramid_dimensions_and_move_handoff();
    test_pixel_edit_history_uses_tile_diffs();
    test_replace_active_pixels_uses_before_only_tile_diffs();
    test_dense_history_spills_to_private_journal(root);
    test_editor_huge_history_and_refresh_are_lightweight();
    std::filesystem::remove_all(root);
    std::cout << "pixelart huge image memory tests passed\n";
    return 0;
}
