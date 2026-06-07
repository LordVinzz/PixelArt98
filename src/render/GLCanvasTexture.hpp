// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Pixel.hpp"

#include <vector>

namespace px {

class GLCanvasTexture {
public:
    GLCanvasTexture() = default;
    ~GLCanvasTexture();

    GLCanvasTexture(const GLCanvasTexture&) = delete;
    GLCanvasTexture& operator=(const GLCanvasTexture&) = delete;

    void update(int width, int height, const std::vector<Pixel>& pixels);
    void update_region(int x, int y, int width, int height, const Pixel* pixels);
    void bind_nearest() const;
    void bind_linear_repeat() const;
    void destroy();

    unsigned int id() const { return texture_id_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int texture_id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace px
