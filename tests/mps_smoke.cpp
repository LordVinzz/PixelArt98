// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "render/MpsEffectRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace px;

static std::uint8_t to_u8(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
}

static bool close_channel(std::uint8_t lhs, std::uint8_t rhs) {
    return std::abs(static_cast<int>(lhs) - static_cast<int>(rhs)) <= 1;
}

static bool close_pixel(Pixel lhs, Pixel rhs) {
    return close_channel(r(lhs), r(rhs)) &&
           close_channel(g(lhs), g(rhs)) &&
           close_channel(b(lhs), b(rhs)) &&
           close_channel(a(lhs), a(rhs));
}

static Document make_reference_document() {
    Document doc = Document::create(32, 32);
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            doc.active_cel().pixels[static_cast<std::size_t>(y * doc.width + x)] =
                rgba(static_cast<std::uint8_t>((x * 9 + y * 5) & 0xff),
                     static_cast<std::uint8_t>((x * 3 + y * 13) & 0xff),
                     static_cast<std::uint8_t>((x * 17 + y * 7) & 0xff),
                     static_cast<std::uint8_t>(96 + ((x * 11 + y * 19) & 0x7f)));
        }
    }
    return doc;
}

static Pixel reference_pixel(Pixel src, const GpuEffectRequest& request) {
    switch (request.mode) {
        case GpuEffectMode::BrightnessContrast: {
            const float brightness = request.params[0] * 255.0f;
            const float contrast = 1.0f + request.params[1] * 2.0f;
            return rgba(to_u8((static_cast<float>(r(src)) - 127.5f) * contrast + 127.5f + brightness),
                        to_u8((static_cast<float>(g(src)) - 127.5f) * contrast + 127.5f + brightness),
                        to_u8((static_cast<float>(b(src)) - 127.5f) * contrast + 127.5f + brightness),
                        a(src));
        }
        case GpuEffectMode::Grayscale: {
            const std::uint8_t value = to_u8(static_cast<float>(r(src)) * 0.299f +
                                            static_cast<float>(g(src)) * 0.587f +
                                            static_cast<float>(b(src)) * 0.114f);
            return rgba(value, value, value, a(src));
        }
        case GpuEffectMode::Sepia:
            return rgba(to_u8(static_cast<float>(r(src)) * 0.393f +
                              static_cast<float>(g(src)) * 0.769f +
                              static_cast<float>(b(src)) * 0.189f),
                        to_u8(static_cast<float>(r(src)) * 0.349f +
                              static_cast<float>(g(src)) * 0.686f +
                              static_cast<float>(b(src)) * 0.168f),
                        to_u8(static_cast<float>(r(src)) * 0.272f +
                              static_cast<float>(g(src)) * 0.534f +
                              static_cast<float>(b(src)) * 0.131f),
                        a(src));
        case GpuEffectMode::InvertColors:
            return rgba(static_cast<std::uint8_t>(255 - r(src)),
                        static_cast<std::uint8_t>(255 - g(src)),
                        static_cast<std::uint8_t>(255 - b(src)),
                        a(src));
        case GpuEffectMode::InvertAlpha:
            return rgba(r(src), g(src), b(src), static_cast<std::uint8_t>(255 - a(src)));
        case GpuEffectMode::Posterize: {
            const int levels = std::max(2, static_cast<int>(request.params[0]));
            auto quantize = [levels](std::uint8_t channel) {
                return static_cast<std::uint8_t>((static_cast<int>(channel) * (levels - 1) + 127) / 255 * 255 / (levels - 1));
            };
            return rgba(quantize(r(src)), quantize(g(src)), quantize(b(src)), a(src));
        }
        default:
            return src;
    }
}

static bool validate_exact_mps_effect(MpsEffectRenderer& renderer,
                                      const Document& doc,
                                      const GpuEffectRequest& request,
                                      const std::string& name) {
    if (!renderer.render_active_cel(doc, request)) {
        std::cerr << "MPS smoke failed: " << name << " render failed: " << renderer.last_error() << "\n";
        return false;
    }
    std::vector<Pixel> pixels;
    if (!renderer.read_output_pixels(pixels)) {
        std::cerr << "MPS smoke failed: " << name << " readback failed\n";
        return false;
    }
    const auto& source = doc.active_cel().pixels;
    if (pixels.size() != source.size()) {
        std::cerr << "MPS smoke failed: " << name << " output size mismatch\n";
        return false;
    }
    for (std::size_t i = 0; i < source.size(); ++i) {
        const Pixel expected = reference_pixel(source[i], request);
        if (!close_pixel(pixels[i], expected)) {
            std::cerr << "MPS smoke failed: " << name << " pixel mismatch at " << i << "\n";
            return false;
        }
    }
    std::cout << "[PASS] MPS exact effect matches CPU reference: " << name << "\n";
    return true;
}

int main() {
    Document doc = make_reference_document();

    MpsEffectRenderer renderer;
    const GpuBackendCapabilities caps = renderer.capabilities();
    std::cout << "MPS capabilities: max_texture_size=" << caps.max_texture_size
              << ", working_texture_budget=" << caps.working_texture_budget
              << ", supports_chunking=" << (caps.supports_chunking ? "true" : "false") << "\n";
    GpuEffectRequest request;
    request.mode = GpuEffectMode::Grayscale;
    if (!renderer.render_active_cel(doc, request)) {
        std::cout << "MPS smoke skipped: " << renderer.last_error() << "\n";
        return 0;
    }

    std::vector<Pixel> pixels;
    if (!renderer.read_output_pixels(pixels)) {
        std::cerr << "MPS smoke failed: could not read grayscale output\n";
        return 1;
    }
    bool grayscale_ok = pixels.size() == doc.active_cel().pixels.size() &&
                         std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
                             return a(p) > 0 && r(p) > 0 && r(p) == g(p) && g(p) == b(p);
                         });
    if (!grayscale_ok) {
        std::cerr << "MPS smoke failed: grayscale output was invalid\n";
        return 1;
    }
    std::cout << "[PASS] MPS grayscale smoke output is plausible\n";

    GpuEffectRequest brightness;
    brightness.mode = GpuEffectMode::BrightnessContrast;
    brightness.params = {32.0f / 255.0f, 24.0f / 255.0f, 0.0f, 0.0f};
    if (!validate_exact_mps_effect(renderer, doc, brightness, "Brightness / Contrast")) return 1;

    GpuEffectRequest grayscale;
    grayscale.mode = GpuEffectMode::Grayscale;
    if (!validate_exact_mps_effect(renderer, doc, grayscale, "Grayscale")) return 1;

    GpuEffectRequest sepia;
    sepia.mode = GpuEffectMode::Sepia;
    if (!validate_exact_mps_effect(renderer, doc, sepia, "Sepia")) return 1;

    GpuEffectRequest invert_colors;
    invert_colors.mode = GpuEffectMode::InvertColors;
    if (!validate_exact_mps_effect(renderer, doc, invert_colors, "Invert Colors")) return 1;

    GpuEffectRequest invert_alpha;
    invert_alpha.mode = GpuEffectMode::InvertAlpha;
    if (!validate_exact_mps_effect(renderer, doc, invert_alpha, "Invert Alpha")) return 1;

    GpuEffectRequest posterize;
    posterize.mode = GpuEffectMode::Posterize;
    posterize.params = {6.0f, 0.0f, 0.0f, 0.0f};
    if (!validate_exact_mps_effect(renderer, doc, posterize, "Posterize")) return 1;

    request.mode = GpuEffectMode::GaussianBlur;
    request.params = {2.0f, 0.0f, 0.0f, 0.0f};
    if (!renderer.render_active_cel(doc, request) || !renderer.read_output_pixels(pixels)) {
        std::cerr << "MPS smoke failed: Gaussian blur failed: " << renderer.last_error() << "\n";
        return 1;
    }
    bool blur_ok = pixels.size() == doc.active_cel().pixels.size() &&
                   std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
                       return a(p) > 0 && (r(p) > 0 || g(p) > 0 || b(p) > 0);
                   });
    if (!blur_ok) {
        std::cerr << "MPS smoke failed: blur output was invalid\n";
        return 1;
    }

    std::cout << "MPS smoke passed\n";
    return 0;
}
