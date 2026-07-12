// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Pixel.hpp"

#include <functional>
#include <string>
#include <vector>

namespace px {

enum class SvgShapeType {
    Rect,
    Ellipse,
    Line,
    Polyline,
    Polygon,
    Path,
    Text
};

struct SvgPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct SvgShape {
    SvgShapeType type = SvgShapeType::Path;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::vector<SvgPoint> points;
    std::string text;
    Pixel fill = rgba(0, 0, 0, 255);
    Pixel stroke = rgba(0, 0, 0, 255);
    float stroke_width = 1.0f;
    bool fill_enabled = true;
    bool stroke_enabled = false;
    bool closed = false;
};

struct SvgDocument {
    int width = 64;
    int height = 64;
    std::vector<SvgShape> shapes;
};

struct SvgOperationProgress {
    float fraction = 0.0f;
    int done = 0;
    int total = 0;
    bool indeterminate = false;
    std::string phase;
    std::string status;
};

using SvgProgressCallback = std::function<void(const SvgOperationProgress&)>;

[[nodiscard]] const char* svg_shape_type_name(SvgShapeType type);
[[nodiscard]] std::vector<Pixel> rasterize_svg_document(const SvgDocument& svg,
                                                        int target_width,
                                                        int target_height,
                                                        const SvgProgressCallback& progress = {});
[[nodiscard]] SvgDocument trace_pixels_to_svg(const std::vector<Pixel>& pixels,
                                              int width,
                                              int height,
                                              const SvgProgressCallback& progress = {});
[[nodiscard]] std::string svg_to_string(const SvgDocument& svg);
[[nodiscard]] bool svg_from_string(const std::string& text, SvgDocument& out_svg, std::string* error = nullptr);
[[nodiscard]] bool load_svg_file(const std::string& path, SvgDocument& out_svg, std::string* error = nullptr);
[[nodiscard]] bool save_svg_file(const std::string& path, const SvgDocument& svg, std::string* error = nullptr);

} // namespace px
