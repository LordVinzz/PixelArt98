// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "render/MpsEffectRenderer.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace px;

int main() {
    Document doc = Document::create(32, 32);
    std::fill(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(), rgba(255, 0, 0, 255));

    MpsEffectRenderer renderer;
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
    bool grayscale_ok = std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
        return a(p) == 255 && r(p) > 0 && r(p) == g(p) && g(p) == b(p);
    });
    if (!grayscale_ok) {
        std::cerr << "MPS smoke failed: grayscale output was invalid\n";
        return 1;
    }

    request.mode = GpuEffectMode::GaussianBlur;
    request.params = {2.0f, 0.0f, 0.0f, 0.0f};
    if (!renderer.render_active_cel(doc, request) || !renderer.read_output_pixels(pixels)) {
        std::cerr << "MPS smoke failed: Gaussian blur failed: " << renderer.last_error() << "\n";
        return 1;
    }
    bool blur_ok = std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
        return a(p) == 255 && r(p) > 0;
    });
    if (!blur_ok) {
        std::cerr << "MPS smoke failed: blur output was invalid\n";
        return 1;
    }

    std::cout << "MPS smoke passed\n";
    return 0;
}
