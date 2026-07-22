// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/CanvasViewport.hpp"

#undef NDEBUG
#include <cassert>
#include <iostream>

using namespace px;

int main() {
    // Regression: at high zoom, rendering work must depend on the viewport and
    // not on every pixel of a very large document.
    const auto large = visible_canvas_pixels(-2'000'000.0, -1'000'000.0, 128.0, 32'768, 32'768, 1'920, 1'080);
    assert(!large.empty());
    assert(large.width() <= 16);
    assert(large.height() <= 10);
    assert(large.pixel_count() <= 160);

    // Increasing the document dimensions must not increase the visible work.
    const auto larger = visible_canvas_pixels(-2'000'000.0, -1'000'000.0, 128.0, 65'536, 65'536, 1'920, 1'080);
    assert(larger.width() == large.width());
    assert(larger.height() == large.height());

    const auto clipped_corner = visible_canvas_pixels(1'900.0, 1'070.0, 16.0, 512, 512, 1'920, 1'080);
    assert(clipped_corner.left == 0);
    assert(clipped_corner.top == 0);
    assert(clipped_corner.right == 2);
    assert(clipped_corner.bottom == 1);

    const auto outside = visible_canvas_pixels(2'000.0, 2'000.0, 64.0, 32'768, 32'768, 1'920, 1'080);
    assert(outside.empty());
    assert(outside.pixel_count() == 0);

    std::cout << "Canvas viewport culling tests passed\n";
    return 0;
}
