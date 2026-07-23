// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Tools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <queue>
#include <string_view>

namespace px {

namespace {

[[nodiscard]] int positive_when_zero_sign(int value) noexcept {
    return value < 0 ? -1 : 1;
}

} // namespace

const char* tool_name(ToolType tool) {
    switch (tool) {
        case ToolType::Pencil: return "Pencil";
        case ToolType::Brush: return "Brush";
        case ToolType::Eraser: return "Eraser";
        case ToolType::Line: return "Line";
        case ToolType::Rectangle: return "Rectangle";
        case ToolType::Ellipse: return "Ellipse";
        case ToolType::Bucket: return "Bucket";
        case ToolType::Gradient: return "Gradient";
        case ToolType::Eyedropper: return "Eyedropper";
        case ToolType::CloneStamp: return "Clone Stamp";
        case ToolType::RectSelect: return "Rectangle Select";
        case ToolType::EllipseSelect: return "Ellipse Select";
        case ToolType::LassoSelect: return "Lasso Select";
        case ToolType::MagicWand: return "Magic Wand";
        case ToolType::MovePixels: return "Move Pixels";
        case ToolType::Text: return "Text";
    }
    return "Tool";
}

std::array<int, 2> constrained_tool_endpoint(ToolType tool,
                                             int start_x,
                                             int start_y,
                                             int current_x,
                                             int current_y,
                                             bool constrain) {
    if (!constrain) {
        return {current_x, current_y};
    }

    const int dx = current_x - start_x;
    const int dy = current_y - start_y;
    if (dx == 0 && dy == 0) {
        return {current_x, current_y};
    }

    switch (tool) {
        case ToolType::Rectangle:
        case ToolType::Ellipse:
        case ToolType::RectSelect:
        case ToolType::EllipseSelect: {
            const int side = std::max(std::abs(dx), std::abs(dy));
            return {start_x + positive_when_zero_sign(dx) * side,
                    start_y + positive_when_zero_sign(dy) * side};
        }
        case ToolType::Line: {
            constexpr double kFortyFiveDegreesRadians = 0.78539816339744830962;
            const double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
            const int octant =
                ((static_cast<int>(std::lround(angle / kFortyFiveDegreesRadians)) % 8) + 8) % 8;
            const int side = std::max(std::abs(dx), std::abs(dy));
            switch (octant) {
                case 0:
                    return {current_x, start_y};
                case 1:
                    return {start_x + side, start_y + side};
                case 2:
                    return {start_x, current_y};
                case 3:
                    return {start_x - side, start_y + side};
                case 4:
                    return {current_x, start_y};
                case 5:
                    return {start_x - side, start_y - side};
                case 6:
                    return {start_x, current_y};
                case 7:
                    return {start_x + side, start_y - side};
                default:
                    break;
            }
            return {current_x, current_y};
        }
        default:
            return {current_x, current_y};
    }
}

void put_pixel(Document& doc, int x, int y, Pixel color, bool erase, float opacity) {
    if (!doc.in_bounds(x, y) || !doc.selection.contains(x, y)) {
        return;
    }
    auto& pixels = doc.active_cel().pixels;
    std::size_t i = static_cast<std::size_t>(doc.pixel_index(x, y));
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (erase) {
        const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(
            static_cast<float>(a(pixels[i])) * (1.0f - opacity), 0.0f, 255.0f) + 0.5f);
        pixels[i] = alpha == 0 ? 0 : with_alpha(pixels[i], alpha);
    } else {
        pixels[i] = blend_over(pixels[i], color, opacity);
    }
}

void plot_brush_raw(Document& doc, int cx, int cy, Pixel color, int size, bool erase,
                    float opacity, float hardness) {
    const int diameter = std::max(1, size);
    const int half = diameter / 2;
    const float radius = std::max(0.5f, static_cast<float>(diameter) * 0.5f);
    const float center = (static_cast<float>(diameter) - 1.0f) * 0.5f;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    hardness = std::clamp(hardness, 0.0f, 1.0f);
    for (int yy = 0; yy < diameter; ++yy) {
        for (int xx = 0; xx < diameter; ++xx) {
            const float dx = static_cast<float>(xx) - center;
            const float dy = static_cast<float>(yy) - center;
            const float normalized_distance = std::sqrt(dx * dx + dy * dy) / radius;
            if (normalized_distance > 1.0f) continue;
            float coverage = 1.0f;
            if (hardness < 0.999f && normalized_distance > hardness) {
                coverage = std::clamp((1.0f - normalized_distance) /
                                      std::max(0.001f, 1.0f - hardness), 0.0f, 1.0f);
            }
            if (coverage > 0.0f) {
                put_pixel(doc, cx + xx - half, cy + yy - half, color, erase,
                          opacity * coverage);
            }
        }
    }
}

void draw_line_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool erase) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        plot_brush_raw(doc, x0, y0, color, size, erase);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_brush(Document& doc, int x, int y, const ToolContext& ctx, bool erase) {
    auto before = doc.snapshot_active_cel();
    plot_brush_raw(doc, x, y, ctx.primary, std::max(1, ctx.brush_size), erase,
                   ctx.brush_opacity, ctx.brush_hardness);
    doc.commit_active_cel_edit(erase ? "Erase" : "Brush", std::move(before));
}

void draw_line(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size) {
    auto before = doc.snapshot_active_cel();
    draw_line_raw(doc, x0, y0, x1, y1, color, std::max(1, size), false);
    doc.commit_active_cel_edit("Line", std::move(before));
}

void draw_rect_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled) {
    int min_x = std::min(x0, x1);
    int max_x = std::max(x0, x1);
    int min_y = std::min(y0, y1);
    int max_y = std::max(y0, y1);
    if (filled) {
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                put_pixel(doc, x, y, color, false);
            }
        }
    } else {
        for (int i = 0; i < std::max(1, size); ++i) {
            draw_line_raw(doc, min_x, min_y + i, max_x, min_y + i, color, 1, false);
            draw_line_raw(doc, min_x, max_y - i, max_x, max_y - i, color, 1, false);
            draw_line_raw(doc, min_x + i, min_y, min_x + i, max_y, color, 1, false);
            draw_line_raw(doc, max_x - i, min_y, max_x - i, max_y, color, 1, false);
        }
    }
}

void draw_rect(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled) {
    auto before = doc.snapshot_active_cel();
    draw_rect_raw(doc, x0, y0, x1, y1, color, size, filled);
    doc.commit_active_cel_edit(filled ? "Filled Rectangle" : "Rectangle", std::move(before));
}

void draw_ellipse_outline_raw(Document& doc, int min_x, int min_y, int max_x, int max_y, Pixel color, int size) {
    const int stroke_size = std::max(1, size);
    if (min_y == max_y) {
        draw_line_raw(doc, min_x, min_y, max_x, min_y, color, stroke_size, false);
        return;
    }
    if (min_x == max_x) {
        draw_line_raw(doc, min_x, min_y, min_x, max_y, color, stroke_size, false);
        return;
    }

    const float cx = (static_cast<float>(min_x) + static_cast<float>(max_x)) * 0.5f;
    const float cy = (static_cast<float>(min_y) + static_cast<float>(max_y)) * 0.5f;
    const float rx = std::max(0.5f, static_cast<float>(max_x - min_x) * 0.5f);
    const float ry = std::max(0.5f, static_cast<float>(max_y - min_y) * 0.5f);

    if (rx < ry) {
        auto ellipse_x_at = [&](int y, float sign) {
            const float ny = std::clamp((static_cast<float>(y) - cy) / ry, -1.0f, 1.0f);
            const float x_offset = rx * std::sqrt(std::max(0.0f, 1.0f - ny * ny));
            return static_cast<int>(std::lround(cx + sign * x_offset));
        };

        int previous_left_x = ellipse_x_at(min_y, -1.0f);
        int previous_left_y = min_y;
        int previous_right_x = ellipse_x_at(min_y, 1.0f);
        int previous_right_y = min_y;
        for (int y = min_y + 1; y <= max_y; ++y) {
            const int left_x = ellipse_x_at(y, -1.0f);
            const int right_x = ellipse_x_at(y, 1.0f);
            draw_line_raw(doc, previous_left_x, previous_left_y, left_x, y, color, stroke_size, false);
            draw_line_raw(doc, previous_right_x, previous_right_y, right_x, y, color, stroke_size, false);
            previous_left_x = left_x;
            previous_left_y = y;
            previous_right_x = right_x;
            previous_right_y = y;
        }
        return;
    }

    auto ellipse_y_at = [&](int x, float sign) {
        const float nx = std::clamp((static_cast<float>(x) - cx) / rx, -1.0f, 1.0f);
        const float y_offset = ry * std::sqrt(std::max(0.0f, 1.0f - nx * nx));
        return static_cast<int>(std::lround(cy + sign * y_offset));
    };

    int previous_top_x = min_x;
    int previous_top_y = ellipse_y_at(min_x, -1.0f);
    int previous_bottom_x = min_x;
    int previous_bottom_y = ellipse_y_at(min_x, 1.0f);
    for (int x = min_x + 1; x <= max_x; ++x) {
        const int top_y = ellipse_y_at(x, -1.0f);
        const int bottom_y = ellipse_y_at(x, 1.0f);
        draw_line_raw(doc, previous_top_x, previous_top_y, x, top_y, color, stroke_size, false);
        draw_line_raw(doc, previous_bottom_x, previous_bottom_y, x, bottom_y, color, stroke_size, false);
        previous_top_x = x;
        previous_top_y = top_y;
        previous_bottom_x = x;
        previous_bottom_y = bottom_y;
    }
}

void draw_ellipse_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled) {
    int min_x = std::min(x0, x1);
    int max_x = std::max(x0, x1);
    int min_y = std::min(y0, y1);
    int max_y = std::max(y0, y1);
    if (!filled) {
        draw_ellipse_outline_raw(doc, min_x, min_y, max_x, max_y, color, size);
        return;
    }

    float cx = (static_cast<float>(x0) + static_cast<float>(x1)) * 0.5f;
    float cy = (static_cast<float>(y0) + static_cast<float>(y1)) * 0.5f;
    float rx = std::max(0.5f, std::abs(static_cast<float>(x1 - x0)) * 0.5f);
    float ry = std::max(0.5f, std::abs(static_cast<float>(y1 - y0)) * 0.5f);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            float nx = (static_cast<float>(x) - cx) / rx;
            float ny = (static_cast<float>(y) - cy) / ry;
            float d = nx * nx + ny * ny;
            if (d <= 1.0f) {
                put_pixel(doc, x, y, color, false);
            }
        }
    }
}

void draw_ellipse(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled) {
    auto before = doc.snapshot_active_cel();
    draw_ellipse_raw(doc, x0, y0, x1, y1, color, size, filled);
    doc.commit_active_cel_edit(filled ? "Filled Ellipse" : "Ellipse", std::move(before));
}

static bool color_matches(Pixel p, Pixel target, int tolerance) {
    return color_distance(p, target, false) <= tolerance;
}

void fill_bucket(Document& doc, int x, int y, Pixel color, int tolerance, bool contiguous) {
    if (!doc.in_bounds(x, y)) {
        return;
    }
    auto before = doc.snapshot_active_cel();
    auto& pixels = doc.active_cel().pixels;
    Pixel target = pixels[static_cast<std::size_t>(doc.pixel_index(x, y))];
    if (color_matches(color, target, 0)) {
        return;
    }

    if (!contiguous) {
        for (int py = 0; py < doc.height; ++py) {
            for (int px_i = 0; px_i < doc.width; ++px_i) {
                std::size_t i = static_cast<std::size_t>(doc.pixel_index(px_i, py));
                if (doc.selection.contains(px_i, py) && color_matches(pixels[i], target, tolerance)) {
                    pixels[i] = color;
                }
            }
        }
        doc.commit_active_cel_edit("Global Fill", std::move(before));
        return;
    }

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(doc.width * doc.height), 0);
    std::queue<std::pair<int, int>> q;
    q.push({x, y});
    visited[static_cast<std::size_t>(doc.pixel_index(x, y))] = 1;
    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        std::size_t i = static_cast<std::size_t>(doc.pixel_index(cx, cy));
        if (!doc.selection.contains(cx, cy) || !color_matches(pixels[i], target, tolerance)) {
            continue;
        }
        pixels[i] = color;
        constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& dir : dirs) {
            int nx = cx + dir[0];
            int ny = cy + dir[1];
            if (doc.in_bounds(nx, ny)) {
                std::size_t ni = static_cast<std::size_t>(doc.pixel_index(nx, ny));
                if (!visited[ni]) {
                    visited[ni] = 1;
                    q.push({nx, ny});
                }
            }
        }
    }
    doc.commit_active_cel_edit("Fill", std::move(before));
}

void fill_gradient_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel ca, Pixel cb) {
    float vx = static_cast<float>(x1 - x0);
    float vy = static_cast<float>(y1 - y0);
    float denom = std::max(1.0f, vx * vx + vy * vy);
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            if (!doc.selection.contains(x, y)) {
                continue;
            }
            float t = ((static_cast<float>(x - x0) * vx) + (static_cast<float>(y - y0) * vy)) / denom;
            t = std::clamp(t, 0.0f, 1.0f);
            Pixel c = rgba(
                static_cast<std::uint8_t>((1.0f - t) * r(ca) + t * r(cb) + 0.5f),
                static_cast<std::uint8_t>((1.0f - t) * g(ca) + t * g(cb) + 0.5f),
                static_cast<std::uint8_t>((1.0f - t) * b(ca) + t * b(cb) + 0.5f),
                static_cast<std::uint8_t>((1.0f - t) * a(ca) + t * a(cb) + 0.5f));
            doc.active_cel().pixels[static_cast<std::size_t>(doc.pixel_index(x, y))] = c;
        }
    }
}

void fill_gradient(Document& doc, int x0, int y0, int x1, int y1, Pixel ca, Pixel cb) {
    auto before = doc.snapshot_active_cel();
    fill_gradient_raw(doc, x0, y0, x1, y1, ca, cb);
    doc.commit_active_cel_edit("Gradient", std::move(before));
}

std::optional<Pixel> pick_color(const Document& doc, int x, int y) {
    if (!doc.in_bounds(x, y)) {
        return std::nullopt;
    }
    auto comp = doc.composite_active();
    return comp[static_cast<std::size_t>(doc.pixel_index(x, y))];
}

void magic_wand(Document& doc, int x, int y, int tolerance, bool contiguous, bool replace) {
    magic_wand(doc, x, y, tolerance, contiguous, replace ? SelectionCombineMode::Replace : SelectionCombineMode::Add);
}

void magic_wand(Document& doc, int x, int y, int tolerance, bool contiguous, SelectionCombineMode mode) {
    if (!doc.in_bounds(x, y)) {
        return;
    }

    const auto& pixels = doc.active_cel().pixels;
    Pixel target = pixels[static_cast<std::size_t>(doc.pixel_index(x, y))];
    std::vector<std::uint8_t> source(static_cast<std::size_t>(doc.width * doc.height), 0);

    if (!contiguous) {
        for (int py = 0; py < doc.height; ++py) {
            for (int px_i = 0; px_i < doc.width; ++px_i) {
                std::size_t i = static_cast<std::size_t>(doc.pixel_index(px_i, py));
                if (color_matches(pixels[i], target, tolerance)) {
                    source[i] = 1;
                }
            }
        }
        doc.selection.combine_with_mask(source, mode);
        return;
    }

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(doc.width * doc.height), 0);
    std::queue<std::pair<int, int>> q;
    q.push({x, y});
    visited[static_cast<std::size_t>(doc.pixel_index(x, y))] = 1;
    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();
        std::size_t i = static_cast<std::size_t>(doc.pixel_index(cx, cy));
        if (!color_matches(pixels[i], target, tolerance)) {
            continue;
        }
        source[i] = 1;
        constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& dir : dirs) {
            int nx = cx + dir[0];
            int ny = cy + dir[1];
            if (doc.in_bounds(nx, ny)) {
                std::size_t ni = static_cast<std::size_t>(doc.pixel_index(nx, ny));
                if (!visited[ni]) {
                    visited[ni] = 1;
                    q.push({nx, ny});
                }
            }
        }
    }
    doc.selection.combine_with_mask(source, mode);
}

void clone_stamp(Document& doc, int sx, int sy, int dx, int dy, int brush_size) {
    if (!doc.in_bounds(sx, sy) || !doc.in_bounds(dx, dy)) {
        return;
    }
    auto before = doc.snapshot_active_cel();
    auto source = doc.active_cel().pixels;
    clone_stamp_raw(doc, source, sx, sy, dx, dy, brush_size);
    doc.commit_active_cel_edit("Clone Stamp", std::move(before));
}

void clone_stamp_raw(Document& doc, const std::vector<Pixel>& source, int sx, int sy, int dx, int dy, int brush_size) {
    if (source.size() != doc.active_cel().pixels.size()) {
        return;
    }
    int size = std::max(1, brush_size);
    int half = size / 2;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int src_x = sx + x - half;
            int src_y = sy + y - half;
            int dst_x = dx + x - half;
            int dst_y = dy + y - half;
            if (!doc.in_bounds(src_x, src_y) || !doc.in_bounds(dst_x, dst_y) || !doc.selection.contains(dst_x, dst_y)) {
                continue;
            }
            doc.active_cel().pixels[static_cast<std::size_t>(doc.pixel_index(dst_x, dst_y))] =
                source[static_cast<std::size_t>(doc.pixel_index(src_x, src_y))];
        }
    }
}

static std::array<std::uint8_t, 7> glyph_for(char input) {
    char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
    switch (ch) {
        case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
        case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
        case 'C': return {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
        case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
        case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
        case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
        case 'G': return {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e};
        case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
        case 'I': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
        case 'J': return {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e};
        case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
        case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
        case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
        case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
        case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
        case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
        case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
        case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
        case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
        case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
        case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
        case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
        case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
        case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
        case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
        case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
        case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
        case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
        case '6': return {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
        case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
        case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
        case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
        case ',': return {0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08};
        case ':': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
        case '!': return {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
        case '?': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
        case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
        case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f};
        case '+': return {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00};
        case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
        case '\\': return {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01};
        case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        default: return {0x1f, 0x11, 0x15, 0x15, 0x11, 0x11, 0x1f};
    }
}

std::vector<std::array<int, 2>> raster_text_points(int x, int y, const std::string& text) {
    std::vector<std::array<int, 2>> points;
    int cursor_x = x;
    int cursor_y = y;
    for (char ch : text) {
        if (ch == '\n') {
            cursor_y += 8;
            cursor_x = x;
            continue;
        }
        auto glyph = glyph_for(ch);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if ((glyph[static_cast<std::size_t>(gy)] & (1u << (4 - gx))) != 0) {
                    points.push_back({cursor_x + gx, cursor_y + gy});
                }
            }
        }
        cursor_x += 6;
    }
    return points;
}

void stamp_text(Document& doc, int x, int y, const std::string& text, Pixel color) {
    auto before = doc.snapshot_active_cel();
    for (const auto& point : raster_text_points(x, y, text)) {
        put_pixel(doc, point[0], point[1], color, false);
    }
    doc.commit_active_cel_edit("Text", std::move(before));
}

void stamp_text_blocks(Document& doc, int x, int y, const std::string& text, Pixel color) {
    stamp_text(doc, x, y, text, color);
}

FloatingSelection scale_floating_selection(const FloatingSelection& source, int left, int top,
                                           int width, int height) {
    if (!source.active || source.width <= 0 || source.height <= 0 || width <= 0 || height <= 0 ||
        source.pixels.size() != static_cast<std::size_t>(source.width * source.height) ||
        source.mask.size() != source.pixels.size()) return {};
    FloatingSelection result;
    result.active = true;
    result.source_x = left;
    result.source_y = top;
    result.width = width;
    result.height = height;
    result.pixels.assign(static_cast<std::size_t>(width * height), 0);
    result.mask.assign(result.pixels.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int source_x = std::clamp(static_cast<int>(std::floor(
                (static_cast<double>(x) + 0.5) * source.width / width)), 0, source.width - 1);
            const int source_y = std::clamp(static_cast<int>(std::floor(
                (static_cast<double>(y) + 0.5) * source.height / height)), 0, source.height - 1);
            const std::size_t source_index = static_cast<std::size_t>(source_y * source.width + source_x);
            const std::size_t destination = static_cast<std::size_t>(y * width + x);
            result.pixels[destination] = source.pixels[source_index];
            result.mask[destination] = source.mask[source_index];
        }
    }
    return result;
}

FloatingSelection rotate_floating_selection(const FloatingSelection& source, float angle_degrees) {
    if (!source.active || source.width <= 0 || source.height <= 0 ||
        source.pixels.size() != static_cast<std::size_t>(source.width * source.height) ||
        source.mask.size() != source.pixels.size()) return {};
    const double radians = static_cast<double>(angle_degrees) * std::numbers::pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    // Keep exact quarter turns exact despite the tiny floating-point residue
    // produced by sin/cos (for example ceil(1.0000000000000002) must be 1).
    constexpr double kBoundingBoxEpsilon = 1.0e-9;
    const int width = std::max(1, static_cast<int>(std::ceil(
        std::abs(source.width * cosine) + std::abs(source.height * sine) -
        kBoundingBoxEpsilon)));
    const int height = std::max(1, static_cast<int>(std::ceil(
        std::abs(source.width * sine) + std::abs(source.height * cosine) -
        kBoundingBoxEpsilon)));
    const int source_left = source.source_x + source.offset_x;
    const int source_top = source.source_y + source.offset_y;
    const int left = static_cast<int>(std::lround(
        static_cast<double>(source_left) +
        static_cast<double>(source.width - width) * 0.5));
    const int top = static_cast<int>(std::lround(
        static_cast<double>(source_top) +
        static_cast<double>(source.height - height) * 0.5));
    const double source_center_x = static_cast<double>(source.width - 1) * 0.5;
    const double source_center_y = static_cast<double>(source.height - 1) * 0.5;
    const double destination_center_x = static_cast<double>(width - 1) * 0.5;
    const double destination_center_y = static_cast<double>(height - 1) * 0.5;
    FloatingSelection result;
    result.active = true;
    result.source_x = left;
    result.source_y = top;
    result.width = width;
    result.height = height;
    result.pixels.assign(static_cast<std::size_t>(width * height), 0);
    result.mask.assign(result.pixels.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double destination_x = static_cast<double>(x) - destination_center_x;
            const double destination_y = static_cast<double>(y) - destination_center_y;
            const int source_x = static_cast<int>(std::lround(
                destination_x * cosine + destination_y * sine + source_center_x));
            const int source_y = static_cast<int>(std::lround(
                -destination_x * sine + destination_y * cosine + source_center_y));
            if (source_x < 0 || source_y < 0 || source_x >= source.width || source_y >= source.height)
                continue;
            const std::size_t source_index = static_cast<std::size_t>(source_y * source.width + source_x);
            const std::size_t destination = static_cast<std::size_t>(y * width + x);
            result.pixels[destination] = source.pixels[source_index];
            result.mask[destination] = source.mask[source_index];
        }
    }
    return result;
}

void ensure_active_layer_mask(Document& doc, std::uint8_t value) {
    if (doc.active_layer < 0 || doc.active_layer >= static_cast<int>(doc.layers.size())) {
        return;
    }
    Layer& layer = doc.layers[static_cast<std::size_t>(doc.active_layer)];
    const std::size_t size = static_cast<std::size_t>(doc.width * doc.height);
    if (layer.mask.size() != size) {
        layer.mask.assign(size, value);
    }
    layer.mask_enabled = true;
}

void put_mask_pixel(Document& doc, int x, int y, std::uint8_t value) {
    if (!doc.in_bounds(x, y) || !doc.selection.contains(x, y)) {
        return;
    }
    ensure_active_layer_mask(doc, 255);
    Layer& layer = doc.layers[static_cast<std::size_t>(doc.active_layer)];
    layer.mask[static_cast<std::size_t>(doc.pixel_index(x, y))] = value;
}

void plot_mask_brush_raw(Document& doc, int cx, int cy, std::uint8_t value, int size) {
    int radius = std::max(1, size);
    int half = radius / 2;
    if (radius <= 2) {
        for (int y = cy - half; y <= cy - half + radius - 1; ++y) {
            for (int x = cx - half; x <= cx - half + radius - 1; ++x) {
                put_mask_pixel(doc, x, y, value);
            }
        }
        return;
    }

    const float r2 = (static_cast<float>(radius) * 0.5f) * (static_cast<float>(radius) * 0.5f);
    const float center = (static_cast<float>(radius) - 1.0f) * 0.5f;
    for (int yy = 0; yy < radius; ++yy) {
        for (int xx = 0; xx < radius; ++xx) {
            const float dx = static_cast<float>(xx) - center;
            const float dy = static_cast<float>(yy) - center;
            if (dx * dx + dy * dy <= r2 + 0.1f) {
                put_mask_pixel(doc, cx + xx - half, cy + yy - half, value);
            }
        }
    }
}

void draw_mask_line_raw(Document& doc, int x0, int y0, int x1, int y1, std::uint8_t value, int size) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        plot_mask_brush_raw(doc, x0, y0, value, size);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void fill_mask_bucket(Document& doc, int x, int y, std::uint8_t value, int tolerance, bool contiguous) {
    if (!doc.in_bounds(x, y) || !doc.selection.contains(x, y)) {
        return;
    }
    ensure_active_layer_mask(doc, 255);
    Layer& layer = doc.layers[static_cast<std::size_t>(doc.active_layer)];
    const std::size_t start_index = static_cast<std::size_t>(doc.pixel_index(x, y));
    const int start_value = layer.mask[start_index];
    const int tol = std::max(0, tolerance);
    auto matches = [&](int px, int py) {
        if (!doc.in_bounds(px, py) || !doc.selection.contains(px, py)) {
            return false;
        }
        const int current = layer.mask[static_cast<std::size_t>(doc.pixel_index(px, py))];
        return std::abs(current - start_value) <= tol;
    };

    if (!contiguous) {
        for (int py = 0; py < doc.height; ++py) {
            for (int px = 0; px < doc.width; ++px) {
                if (matches(px, py)) {
                    layer.mask[static_cast<std::size_t>(doc.pixel_index(px, py))] = value;
                }
            }
        }
        return;
    }

    std::vector<std::uint8_t> visited(static_cast<std::size_t>(doc.width * doc.height), 0);
    std::queue<std::array<int, 2>> queue;
    queue.push({x, y});
    visited[start_index] = 1;
    while (!queue.empty()) {
        const auto point = queue.front();
        queue.pop();
        const int px = point[0];
        const int py = point[1];
        if (!matches(px, py)) {
            continue;
        }
        layer.mask[static_cast<std::size_t>(doc.pixel_index(px, py))] = value;
        constexpr std::array<std::array<int, 2>, 4> dirs = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        for (const auto& dir : dirs) {
            const int nx = px + dir[0];
            const int ny = py + dir[1];
            if (!doc.in_bounds(nx, ny)) {
                continue;
            }
            const std::size_t ni = static_cast<std::size_t>(doc.pixel_index(nx, ny));
            if (visited[ni] == 0) {
                visited[ni] = 1;
                queue.push({nx, ny});
            }
        }
    }
}

} // namespace px
