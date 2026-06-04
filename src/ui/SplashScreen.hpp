// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "render/GLCanvasTexture.hpp"

#include <span>
#include <string>

namespace px {

class SplashScreen {
public:
    [[nodiscard]] bool load(std::span<const unsigned char> png_bytes, std::string* error = nullptr);
    void render_full_window() const;

private:
    GLCanvasTexture texture_;
};

} // namespace px
