// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#define GLFW_INCLUDE_NONE

#include "core/Document.hpp"
#include "core/Pixel.hpp"
#include "render/GpuEffectRenderer.hpp"
#include "render/MpsEffectRenderer.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <vector>

using namespace px;

namespace {

constexpr int kA4Width1200Dpi = 9921;
constexpr int kA4Height1200Dpi = 14031;
constexpr std::uint64_t kRandomSeed = 0x98a4'1200'd1ULL;

struct BenchResult {
    std::string name;
    bool ran = false;
    double seconds = 0.0;
    std::uint64_t rss_delta = 0;
    std::uint64_t peak_rss = 0;
    std::uint64_t estimated_gpu_memory = 0;
    std::string note;
};

void* load_glfw_gl_proc(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

std::uint64_t max_rss_bytes() {
    rusage usage = {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(std::max<decltype(usage.ru_maxrss)>(0, usage.ru_maxrss));
#else
    return static_cast<std::uint64_t>(std::max<decltype(usage.ru_maxrss)>(0, usage.ru_maxrss)) * 1024ULL;
#endif
}

std::string human_bytes(std::uint64_t bytes) {
    const char* units[] = {"B", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << value << ' ' << units[unit];
    return out.str();
}

std::filesystem::path default_image_path() {
    if (const char* path = std::getenv("PIXELART_BENCH_IMAGE")) {
        return std::filesystem::path(path);
    }
    return std::filesystem::current_path() / "pixelart_a4_1200dpi_random_rgba.bin";
}

std::uint64_t xorshift64(std::uint64_t& state) {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

std::vector<Pixel> generate_random_pixels(int width, int height) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::uint64_t state = kRandomSeed;
    for (Pixel& pixel : pixels) {
        const std::uint64_t value = xorshift64(state);
        pixel = rgba(static_cast<std::uint8_t>(value & 0xffU),
                     static_cast<std::uint8_t>((value >> 8U) & 0xffU),
                     static_cast<std::uint8_t>((value >> 16U) & 0xffU),
                     255);
    }
    return pixels;
}

std::vector<Pixel> load_or_generate_pixels(const std::filesystem::path& path, int width, int height) {
    const std::uint64_t expected_bytes =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(Pixel);
    std::error_code error;
    if (std::filesystem::exists(path, error) && std::filesystem::file_size(path, error) == expected_bytes) {
        std::vector<Pixel> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        std::ifstream file(path, std::ios::binary);
        file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(expected_bytes));
        if (file.gcount() == static_cast<std::streamsize>(expected_bytes)) {
            std::cout << "Loaded cached random image: " << path << "\n";
            return pixels;
        }
    }

    std::cout << "Generating random A4 1200 DPI image: " << width << " x " << height << "\n";
    std::vector<Pixel> pixels = generate_random_pixels(width, height);
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(expected_bytes));
    std::cout << "Cached random image: " << path << "\n";
    return pixels;
}

std::uint8_t to_u8(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}

Pixel cpu_brightness(Pixel p) {
    constexpr float brightness = 32.0f;
    constexpr float contrast = 1.25f;
    return rgba(to_u8((static_cast<float>(r(p)) - 127.5f) * contrast + 127.5f + brightness),
                to_u8((static_cast<float>(g(p)) - 127.5f) * contrast + 127.5f + brightness),
                to_u8((static_cast<float>(b(p)) - 127.5f) * contrast + 127.5f + brightness),
                a(p));
}

Pixel cpu_grayscale(Pixel p) {
    const std::uint8_t value = to_u8(static_cast<float>(r(p)) * 0.299f +
                                    static_cast<float>(g(p)) * 0.587f +
                                    static_cast<float>(b(p)) * 0.114f);
    return rgba(value, value, value, a(p));
}

Pixel cpu_posterize(Pixel p) {
    constexpr int levels = 8;
    auto quantize = [](std::uint8_t channel) {
        return static_cast<std::uint8_t>((static_cast<int>(channel) * (levels - 1) + 127) / 255 * 255 / (levels - 1));
    };
    return rgba(quantize(r(p)), quantize(g(p)), quantize(b(p)), a(p));
}

std::vector<Pixel> cpu_blur(const std::vector<Pixel>& source, int width, int height) {
    constexpr int radius = 2;
    std::vector<Pixel> out(source.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int red = 0;
            int green = 0;
            int blue = 0;
            int alpha = 0;
            int count = 0;
            for (int oy = -radius; oy <= radius; ++oy) {
                for (int ox = -radius; ox <= radius; ++ox) {
                    const int sx = std::clamp(x + ox, 0, width - 1);
                    const int sy = std::clamp(y + oy, 0, height - 1);
                    const Pixel p = source[static_cast<std::size_t>(sy * width + sx)];
                    red += r(p);
                    green += g(p);
                    blue += b(p);
                    alpha += a(p);
                    ++count;
                }
            }
            out[static_cast<std::size_t>(y * width + x)] =
                rgba(static_cast<std::uint8_t>(red / count),
                     static_cast<std::uint8_t>(green / count),
                     static_cast<std::uint8_t>(blue / count),
                     static_cast<std::uint8_t>(alpha / count));
        }
    }
    return out;
}

std::vector<GpuEffectRequest> effect_sequence() {
    GpuEffectRequest brightness;
    brightness.mode = GpuEffectMode::BrightnessContrast;
    brightness.params = {32.0f / 255.0f, 32.0f / 255.0f, 0.0f, 0.0f};

    GpuEffectRequest grayscale;
    grayscale.mode = GpuEffectMode::Grayscale;

    GpuEffectRequest posterize;
    posterize.mode = GpuEffectMode::Posterize;
    posterize.params = {8.0f, 0.0f, 0.0f, 0.0f};

    GpuEffectRequest blur;
    blur.mode = GpuEffectMode::GaussianBlur;
    blur.params = {2.0f, 0.0f, 0.0f, 0.0f};

    return {brightness, grayscale, posterize, blur};
}

BenchResult benchmark_cpu(const std::vector<Pixel>& input, int width, int height) {
    BenchResult result;
    result.name = "CPU default pipeline";
    const std::uint64_t rss_before = max_rss_bytes();
    const auto start = std::chrono::steady_clock::now();

    std::vector<Pixel> pixels = input;
    for (Pixel& pixel : pixels) pixel = cpu_brightness(pixel);
    for (Pixel& pixel : pixels) pixel = cpu_grayscale(pixel);
    for (Pixel& pixel : pixels) pixel = cpu_posterize(pixel);
    pixels = cpu_blur(pixels, width, height);

    const auto end = std::chrono::steady_clock::now();
    const std::uint64_t rss_after = max_rss_bytes();
    result.ran = true;
    result.seconds = std::chrono::duration<double>(end - start).count();
    result.rss_delta = rss_after > rss_before ? rss_after - rss_before : 0;
    result.peak_rss = rss_after;
    result.estimated_gpu_memory = 0;
    return result;
}

Document document_from_pixels(const std::vector<Pixel>& pixels, int width, int height) {
    Document document = Document::create(width, height);
    document.active_cel().pixels = pixels;
    return document;
}

BenchResult benchmark_opengl(const std::vector<Pixel>& input, int width, int height) {
    BenchResult result;
    result.name = "OpenGL Heavy GPU Optimization";

    if (!glfwInit()) {
        result.note = "Skipped: GLFW init failed";
        return result;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(64, 64, "pixelart gpu benchmark", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        result.note = "Skipped: hidden OpenGL context unavailable";
        return result;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(load_glfw_gl_proc)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        result.note = "Skipped: GLAD load failed";
        return result;
    }

    const std::uint64_t rss_before = max_rss_bytes();
    const auto start = std::chrono::steady_clock::now();
    Document document = document_from_pixels(input, width, height);
    GpuEffectRenderer renderer;
    std::vector<Pixel> output;
    bool ok = true;
    for (const GpuEffectRequest& request : effect_sequence()) {
        ok = renderer.render_active_cel(document, request) && renderer.read_output_pixels(output);
        if (!ok) {
            result.note = "Failed: " + renderer.last_error();
            break;
        }
        document.active_cel().pixels = output;
    }
    const auto end = std::chrono::steady_clock::now();
    const std::uint64_t rss_after = max_rss_bytes();

    gladLoaderUnloadGL();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (ok) {
        result.ran = true;
        result.seconds = std::chrono::duration<double>(end - start).count();
        result.rss_delta = rss_after > rss_before ? rss_after - rss_before : 0;
        result.peak_rss = rss_after;
        result.estimated_gpu_memory = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(Pixel) * 3ULL;
    }
    return result;
}

BenchResult benchmark_mps(const std::vector<Pixel>& input, int width, int height) {
    BenchResult result;
    result.name = "Metal / MPS Backend";
#if defined(__APPLE__)
    const std::uint64_t rss_before = max_rss_bytes();
    const auto start = std::chrono::steady_clock::now();
    Document document = document_from_pixels(input, width, height);
    MpsEffectRenderer renderer;
    std::vector<Pixel> output;
    bool ok = true;
    for (const GpuEffectRequest& request : effect_sequence()) {
        ok = renderer.render_active_cel(document, request) && renderer.read_output_pixels(output);
        if (!ok) {
            result.note = "Skipped/failed: " + renderer.last_error();
            break;
        }
        document.active_cel().pixels = output;
    }
    const auto end = std::chrono::steady_clock::now();
    const std::uint64_t rss_after = max_rss_bytes();
    if (ok) {
        result.ran = true;
        result.seconds = std::chrono::duration<double>(end - start).count();
        result.rss_delta = rss_after > rss_before ? rss_after - rss_before : 0;
        result.peak_rss = rss_after;
        result.estimated_gpu_memory = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(Pixel) * 3ULL;
    }
#else
    result.note = "Skipped: MPS is only available on macOS";
#endif
    return result;
}

void print_result(const BenchResult& result) {
    std::cout << "\n" << result.name << "\n";
    std::cout << "  Status: " << (result.ran ? "ran" : "not run") << "\n";
    if (result.ran) {
        std::cout << "  Time: " << std::fixed << std::setprecision(3) << result.seconds << " s\n";
        std::cout << "  Peak RSS: " << human_bytes(result.peak_rss) << "\n";
        std::cout << "  RSS delta: " << human_bytes(result.rss_delta) << "\n";
        std::cout << "  Estimated GPU texture memory: " << human_bytes(result.estimated_gpu_memory) << "\n";
    }
    if (!result.note.empty()) {
        std::cout << "  Note: " << result.note << "\n";
    }
}

} // namespace

int main() {
    const int width = [] {
        if (const char* value = std::getenv("PIXELART_BENCH_WIDTH")) return std::max(1, std::atoi(value));
        return kA4Width1200Dpi;
    }();
    const int height = [] {
        if (const char* value = std::getenv("PIXELART_BENCH_HEIGHT")) return std::max(1, std::atoi(value));
        return kA4Height1200Dpi;
    }();

    const std::filesystem::path image_path = default_image_path();
    std::cout << "PixelArt98 manual GPU backend benchmark\n";
    std::cout << "Image: " << width << " x " << height << " RGBA (" <<
        human_bytes(static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(Pixel)) << ")\n";
    std::cout << "Cache file: " << image_path << "\n";
    std::cout << "Effects: Brightness/Contrast -> Grayscale -> Posterize -> Gaussian Blur\n";

    const std::vector<Pixel> pixels = load_or_generate_pixels(image_path, width, height);
    std::vector<BenchResult> results;
    results.push_back(benchmark_cpu(pixels, width, height));
    results.push_back(benchmark_opengl(pixels, width, height));
    results.push_back(benchmark_mps(pixels, width, height));

    std::cout << "\n=== Results ===\n";
    for (const BenchResult& result : results) {
        print_result(result);
    }

    auto fastest = std::min_element(results.begin(), results.end(), [](const BenchResult& lhs, const BenchResult& rhs) {
        if (!lhs.ran) return false;
        if (!rhs.ran) return true;
        return lhs.seconds < rhs.seconds;
    });
    if (fastest != results.end() && fastest->ran) {
        std::cout << "\nFastest: " << fastest->name << " (" << std::fixed << std::setprecision(3) << fastest->seconds << " s)\n";
    }
    return 0;
}
