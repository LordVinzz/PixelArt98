// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Svg.hpp"

#include "core/Tools.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace px {

namespace {

void publish_progress(const SvgProgressCallback& progress,
                      float fraction,
                      const char* phase,
                      const char* status,
                      int done,
                      int total,
                      bool indeterminate = false) {
    if (progress) {
        progress({std::clamp(fraction, 0.0f, 1.0f), done, total, indeterminate, phase, status});
    }
}

SvgShape scaled_shape(const SvgShape& shape, float sx, float sy) {
    SvgShape scaled = shape;
    scaled.x *= sx;
    scaled.y *= sy;
    scaled.w *= sx;
    scaled.h *= sy;
    scaled.stroke_width *= std::max(sx, sy);
    for (SvgPoint& point : scaled.points) {
        point.x *= sx;
        point.y *= sy;
    }
    return scaled;
}

void blend_pixel(std::vector<Pixel>& pixels, int width, int height, int x, int y, Pixel color) {
    if (x < 0 || y < 0 || x >= width || y >= height || a(color) == 0) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(y * width + x);
    pixels[index] = blend_over(pixels[index], color);
}

float distance_to_segment(float px, float py, SvgPoint a_point, SvgPoint b_point) {
    const float dx = b_point.x - a_point.x;
    const float dy = b_point.y - a_point.y;
    const float length_sq = dx * dx + dy * dy;
    if (length_sq <= 0.000001f) {
        const float tx = px - a_point.x;
        const float ty = py - a_point.y;
        return std::sqrt(tx * tx + ty * ty);
    }
    const float t = std::clamp(((px - a_point.x) * dx + (py - a_point.y) * dy) / length_sq, 0.0f, 1.0f);
    const float sx = a_point.x + dx * t;
    const float sy = a_point.y + dy * t;
    const float tx = px - sx;
    const float ty = py - sy;
    return std::sqrt(tx * tx + ty * ty);
}

void draw_stroked_segment(std::vector<Pixel>& pixels,
                          int width,
                          int height,
                          SvgPoint a_point,
                          SvgPoint b_point,
                          Pixel color,
                          float stroke_width) {
    const float half = std::max(0.5f, stroke_width * 0.5f);
    const int min_x = std::max(0, static_cast<int>(std::floor(std::min(a_point.x, b_point.x) - half - 1.0f)));
    const int min_y = std::max(0, static_cast<int>(std::floor(std::min(a_point.y, b_point.y) - half - 1.0f)));
    const int max_x = std::min(width - 1, static_cast<int>(std::ceil(std::max(a_point.x, b_point.x) + half + 1.0f)));
    const int max_y = std::min(height - 1, static_cast<int>(std::ceil(std::max(a_point.y, b_point.y) + half + 1.0f)));
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            if (distance_to_segment(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, a_point, b_point) <= half) {
                blend_pixel(pixels, width, height, x, y, color);
            }
        }
    }
}

bool point_in_polygon(float x, float y, const std::vector<SvgPoint>& points) {
    if (points.size() < 3) {
        return false;
    }
    bool inside = false;
    std::size_t j = points.size() - 1;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const SvgPoint pi = points[i];
        const SvgPoint pj = points[j];
        const bool intersects = ((pi.y > y) != (pj.y > y)) &&
                                (x < (pj.x - pi.x) * (y - pi.y) / std::max(0.000001f, pj.y - pi.y) + pi.x);
        if (intersects) {
            inside = !inside;
        }
        j = i;
    }
    return inside;
}

void fill_polygon(std::vector<Pixel>& pixels, int width, int height, const std::vector<SvgPoint>& points, Pixel color) {
    if (points.size() < 3) {
        return;
    }
    float min_x = points[0].x;
    float min_y = points[0].y;
    float max_x = points[0].x;
    float max_y = points[0].y;
    for (SvgPoint point : points) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(min_x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(min_y)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(max_x)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(max_y)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (point_in_polygon(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, points)) {
                blend_pixel(pixels, width, height, x, y, color);
            }
        }
    }
}

void rasterize_shape(const SvgShape& shape, std::vector<Pixel>& pixels, int width, int height) {
    if (shape.type == SvgShapeType::Rect) {
        const float x0 = std::min(shape.x, shape.x + shape.w);
        const float y0 = std::min(shape.y, shape.y + shape.h);
        const float x1 = std::max(shape.x, shape.x + shape.w);
        const float y1 = std::max(shape.y, shape.y + shape.h);
        if (shape.fill_enabled) {
            for (int y = std::max(0, static_cast<int>(std::floor(y0))); y <= std::min(height - 1, static_cast<int>(std::ceil(y1))); ++y) {
                for (int x = std::max(0, static_cast<int>(std::floor(x0))); x <= std::min(width - 1, static_cast<int>(std::ceil(x1))); ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    if (px >= x0 && px <= x1 && py >= y0 && py <= y1) {
                        blend_pixel(pixels, width, height, x, y, shape.fill);
                    }
                }
            }
        }
        if (shape.stroke_enabled) {
            draw_stroked_segment(pixels, width, height, {x0, y0}, {x1, y0}, shape.stroke, shape.stroke_width);
            draw_stroked_segment(pixels, width, height, {x1, y0}, {x1, y1}, shape.stroke, shape.stroke_width);
            draw_stroked_segment(pixels, width, height, {x1, y1}, {x0, y1}, shape.stroke, shape.stroke_width);
            draw_stroked_segment(pixels, width, height, {x0, y1}, {x0, y0}, shape.stroke, shape.stroke_width);
        }
        return;
    }
    if (shape.type == SvgShapeType::Ellipse) {
        const float cx = shape.x + shape.w * 0.5f;
        const float cy = shape.y + shape.h * 0.5f;
        const float rx = std::max(0.01f, std::abs(shape.w) * 0.5f);
        const float ry = std::max(0.01f, std::abs(shape.h) * 0.5f);
        const float half = shape.stroke_width * 0.5f;
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - rx - half - 1.0f)));
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - ry - half - 1.0f)));
        const int x1 = std::min(width - 1, static_cast<int>(std::ceil(cx + rx + half + 1.0f)));
        const int y1 = std::min(height - 1, static_cast<int>(std::ceil(cy + ry + half + 1.0f)));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float nx = (static_cast<float>(x) + 0.5f - cx) / rx;
                const float ny = (static_cast<float>(y) + 0.5f - cy) / ry;
                const float dist = nx * nx + ny * ny;
                if (shape.fill_enabled && dist <= 1.0f) {
                    blend_pixel(pixels, width, height, x, y, shape.fill);
                } else if (shape.stroke_enabled) {
                    const float edge = std::abs(std::sqrt(std::max(0.0f, dist)) - 1.0f) * std::max(rx, ry);
                    if (edge <= std::max(0.5f, half)) {
                        blend_pixel(pixels, width, height, x, y, shape.stroke);
                    }
                }
            }
        }
        return;
    }
    if (shape.type == SvgShapeType::Line && shape.points.size() >= 2) {
        draw_stroked_segment(pixels, width, height, shape.points[0], shape.points[1], shape.stroke, shape.stroke_width);
        return;
    }
    if (shape.type == SvgShapeType::Text) {
        const std::vector<std::array<int, 2>> points =
            raster_text_points(static_cast<int>(std::lround(shape.x)), static_cast<int>(std::lround(shape.y)), shape.text);
        const Pixel color = shape.fill_enabled ? shape.fill : shape.stroke;
        for (const auto& point : points) {
            blend_pixel(pixels, width, height, point[0], point[1], color);
        }
        return;
    }
    const bool closed = shape.type == SvgShapeType::Polygon || shape.closed;
    if (shape.fill_enabled && closed) {
        fill_polygon(pixels, width, height, shape.points, shape.fill);
    }
    if (shape.stroke_enabled || !shape.fill_enabled) {
        const Pixel stroke = shape.stroke_enabled ? shape.stroke : shape.fill;
        for (std::size_t i = 1; i < shape.points.size(); ++i) {
            draw_stroked_segment(pixels, width, height, shape.points[i - 1], shape.points[i], stroke, shape.stroke_width);
        }
        if (closed && shape.points.size() > 2) {
            draw_stroked_segment(pixels, width, height, shape.points.back(), shape.points.front(), stroke, shape.stroke_width);
        }
    }
}

struct ActiveTraceRun {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    Pixel color = 0;
    bool touched = false;
};

SvgShape rect_shape_from_run(const ActiveTraceRun& run) {
    SvgShape shape;
    shape.type = SvgShapeType::Rect;
    shape.x = static_cast<float>(run.x);
    shape.y = static_cast<float>(run.y);
    shape.w = static_cast<float>(run.w);
    shape.h = static_cast<float>(run.h);
    shape.fill = run.color;
    shape.fill_enabled = true;
    shape.stroke_enabled = false;
    return shape;
}

void flush_untouched_runs(std::vector<ActiveTraceRun>& active, SvgDocument& svg) {
    std::vector<ActiveTraceRun> kept;
    kept.reserve(active.size());
    for (const ActiveTraceRun& run : active) {
        if (run.touched) {
            ActiveTraceRun copy = run;
            copy.touched = false;
            kept.push_back(copy);
        } else {
            svg.shapes.push_back(rect_shape_from_run(run));
        }
    }
    active = std::move(kept);
}

} // namespace
std::vector<Pixel> rasterize_svg_document(const SvgDocument& svg,
                                          int target_width,
                                          int target_height,
                                          const SvgProgressCallback& progress) {
    const int width = std::max(1, target_width);
    const int height = std::max(1, target_height);
    std::vector<Pixel> pixels(static_cast<std::size_t>(width * height), 0);
    const float sx = static_cast<float>(width) / static_cast<float>(std::max(1, svg.width));
    const float sy = static_cast<float>(height) / static_cast<float>(std::max(1, svg.height));
    const int total = static_cast<int>(svg.shapes.size());
    publish_progress(progress, 0.0f, "Rasterizing SVG", "Preparing vector shapes", 0, total);
    for (std::size_t i = 0; i < svg.shapes.size(); ++i) {
        rasterize_shape(scaled_shape(svg.shapes[i], sx, sy), pixels, width, height);
        publish_progress(progress,
                         total <= 0 ? 1.0f : static_cast<float>(i + 1U) / static_cast<float>(total),
                         "Rasterizing SVG",
                         "Drawing vector shapes",
                         static_cast<int>(i + 1U),
                         total);
    }
    publish_progress(progress, 1.0f, "Rasterizing SVG", "SVG rasterized", total, total);
    return pixels;
}

SvgDocument trace_pixels_to_svg(const std::vector<Pixel>& pixels,
                                int width,
                                int height,
                                const SvgProgressCallback& progress) {
    SvgDocument svg;
    svg.width = std::max(1, width);
    svg.height = std::max(1, height);
    if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width * height)) {
        return svg;
    }

    std::vector<ActiveTraceRun> active;
    publish_progress(progress, 0.0f, "Vectorizing pixels", "Scanning opaque pixel runs", 0, height);
    for (int y = 0; y < height; ++y) {
        int x = 0;
        while (x < width) {
            const Pixel color = pixels[static_cast<std::size_t>(y * width + x)];
            if (a(color) == 0) {
                ++x;
                continue;
            }
            const int start_x = x;
            while (x < width && pixels[static_cast<std::size_t>(y * width + x)] == color) {
                ++x;
            }
            const int run_w = x - start_x;
            auto match = std::find_if(active.begin(), active.end(), [&](const ActiveTraceRun& run) {
                return !run.touched && run.x == start_x && run.w == run_w && run.color == color && run.y + run.h == y;
            });
            if (match != active.end()) {
                match->h += 1;
                match->touched = true;
            } else {
                active.push_back({start_x, y, run_w, 1, color, true});
            }
        }
        flush_untouched_runs(active, svg);
        publish_progress(progress,
                         static_cast<float>(y + 1) / static_cast<float>(height),
                         "Vectorizing pixels",
                         "Merging same-color runs",
                         y + 1,
                         height);
    }
    for (const ActiveTraceRun& run : active) {
        svg.shapes.push_back(rect_shape_from_run(run));
    }
    publish_progress(progress, 1.0f, "Vectorizing pixels", "Vector document ready", height, height);
    return svg;
}
} // namespace px
