// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Document.hpp"

#include <QString>

#include <string>
#include <vector>

namespace px {

enum class RasterTextAlignment {
    Left,
    Center,
    Right
};

struct RasterTextOptions {
    QString text;
    QString font_family;
    int pixel_size = 16;
    int box_width = 128;
    int box_height = 0;
    RasterTextAlignment alignment = RasterTextAlignment::Left;
    bool antialias = false;
    bool bold = false;
    bool italic = false;
};

struct RasterTextImage {
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels;
};

[[nodiscard]] RasterTextImage rasterize_text(const RasterTextOptions& options, Pixel color,
                                              int maximum_width, int maximum_height);
[[nodiscard]] bool stamp_raster_text(Document& document, int x, int y,
                                     const RasterTextOptions& options, Pixel color,
                                     std::string* error = nullptr);

} // namespace px
