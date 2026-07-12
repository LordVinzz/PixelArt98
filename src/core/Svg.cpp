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

struct ParsedPaint {
    Pixel color = rgba(0, 0, 0, 255);
    bool enabled = false;
};

struct PathToken {
    bool command = false;
    char command_value = '\0';
    float number_value = 0.0f;
};

std::string ascii_lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

std::string trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool is_name_char(char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '-' || ch == '_' || ch == ':' || ch == '.';
}

std::string tag_name(std::string_view tag) {
    std::size_t i = 0;
    while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])) != 0) {
        ++i;
    }
    const std::size_t start = i;
    while (i < tag.size() && is_name_char(tag[i])) {
        ++i;
    }
    return ascii_lower(tag.substr(start, i - start));
}

std::unordered_map<std::string, std::string> parse_attributes(std::string_view tag) {
    std::unordered_map<std::string, std::string> attrs;
    std::size_t i = 0;
    while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])) != 0) {
        ++i;
    }
    while (i < tag.size() && is_name_char(tag[i])) {
        ++i;
    }
    while (i < tag.size()) {
        while (i < tag.size() && (std::isspace(static_cast<unsigned char>(tag[i])) != 0 || tag[i] == '/')) {
            ++i;
        }
        const std::size_t name_start = i;
        while (i < tag.size() && is_name_char(tag[i])) {
            ++i;
        }
        if (i == name_start) {
            break;
        }
        std::string name = ascii_lower(tag.substr(name_start, i - name_start));
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])) != 0) {
            ++i;
        }
        if (i >= tag.size() || tag[i] != '=') {
            attrs.emplace(std::move(name), std::string{});
            continue;
        }
        ++i;
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])) != 0) {
            ++i;
        }
        if (i >= tag.size()) {
            attrs.emplace(std::move(name), std::string{});
            break;
        }
        std::string value;
        if (tag[i] == '"' || tag[i] == '\'') {
            const char quote = tag[i++];
            const std::size_t value_start = i;
            while (i < tag.size() && tag[i] != quote) {
                ++i;
            }
            value = std::string(tag.substr(value_start, i - value_start));
            if (i < tag.size()) {
                ++i;
            }
        } else {
            const std::size_t value_start = i;
            while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i])) == 0 && tag[i] != '/') {
                ++i;
            }
            value = std::string(tag.substr(value_start, i - value_start));
        }
        attrs[std::move(name)] = std::move(value);
    }
    return attrs;
}

std::unordered_map<std::string, std::string> parse_style(std::string_view style) {
    std::unordered_map<std::string, std::string> out;
    std::size_t start = 0;
    while (start < style.size()) {
        std::size_t end = style.find(';', start);
        if (end == std::string_view::npos) {
            end = style.size();
        }
        const std::string_view entry = style.substr(start, end - start);
        const std::size_t colon = entry.find(':');
        if (colon != std::string_view::npos) {
            out[ascii_lower(trim(entry.substr(0, colon)))] = trim(entry.substr(colon + 1));
        }
        start = end + 1;
    }
    return out;
}

std::string attr_value(const std::unordered_map<std::string, std::string>& attrs,
                       const std::unordered_map<std::string, std::string>& style,
                       const char* name,
                       std::string_view fallback = {}) {
    const std::string key = ascii_lower(name);
    if (auto attr = attrs.find(key); attr != attrs.end()) {
        return attr->second;
    }
    if (auto styled = style.find(key); styled != style.end()) {
        return styled->second;
    }
    return std::string(fallback);
}

float parse_svg_float(std::string_view text, float fallback = 0.0f) {
    std::string trimmed = trim(text);
    if (trimmed.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float value = std::strtof(trimmed.c_str(), &end);
    return end == trimmed.c_str() ? fallback : value;
}

int parse_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

std::uint8_t opacity_channel(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

ParsedPaint parse_paint(std::string_view text, bool default_enabled) {
    std::string value = trim(text);
    if (value.empty()) {
        return {rgba(0, 0, 0, 255), default_enabled};
    }
    const std::string lower = ascii_lower(value);
    if (lower == "none") {
        return {0, false};
    }
    if (lower == "transparent") {
        return {rgba(0, 0, 0, 0), true};
    }
    if (starts_with(lower, "#")) {
        if (lower.size() == 4) {
            const int rr = parse_hex_digit(lower[1]);
            const int gg = parse_hex_digit(lower[2]);
            const int bb = parse_hex_digit(lower[3]);
            if (rr >= 0 && gg >= 0 && bb >= 0) {
                return {rgba(static_cast<std::uint8_t>(rr * 17),
                             static_cast<std::uint8_t>(gg * 17),
                             static_cast<std::uint8_t>(bb * 17),
                             255),
                        true};
            }
        }
        if (lower.size() == 7 || lower.size() == 9) {
            int values[8] = {};
            bool ok = true;
            for (std::size_t i = 1; i < lower.size(); ++i) {
                values[i - 1] = parse_hex_digit(lower[i]);
                ok = ok && values[i - 1] >= 0;
            }
            if (ok) {
                const auto pair = [&](std::size_t offset) {
                    return static_cast<std::uint8_t>(values[offset] * 16 + values[offset + 1]);
                };
                return {rgba(pair(0), pair(2), pair(4), lower.size() == 9 ? pair(6) : 255), true};
            }
        }
    }
    if (starts_with(lower, "rgb(") && lower.back() == ')') {
        std::array<int, 3> values = {};
        std::size_t start = 4;
        bool ok = true;
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::size_t end = lower.find(i == values.size() - 1 ? ')' : ',', start);
            if (end == std::string::npos) {
                ok = false;
                break;
            }
            values[i] = static_cast<int>(std::lround(parse_svg_float(std::string_view(lower).substr(start, end - start), 0.0f)));
            start = end + 1;
        }
        if (ok) {
            return {rgba(static_cast<std::uint8_t>(std::clamp(values[0], 0, 255)),
                         static_cast<std::uint8_t>(std::clamp(values[1], 0, 255)),
                         static_cast<std::uint8_t>(std::clamp(values[2], 0, 255)),
                         255),
                    true};
        }
    }
    if (lower == "white") return {rgba(255, 255, 255, 255), true};
    if (lower == "black") return {rgba(0, 0, 0, 255), true};
    if (lower == "red") return {rgba(255, 0, 0, 255), true};
    if (lower == "green") return {rgba(0, 128, 0, 255), true};
    if (lower == "blue") return {rgba(0, 0, 255, 255), true};
    if (lower == "yellow") return {rgba(255, 255, 0, 255), true};
    if (lower == "cyan" || lower == "aqua") return {rgba(0, 255, 255, 255), true};
    if (lower == "magenta" || lower == "fuchsia") return {rgba(255, 0, 255, 255), true};
    if (lower == "gray" || lower == "grey") return {rgba(128, 128, 128, 255), true};
    return {rgba(0, 0, 0, 255), default_enabled};
}

Pixel multiply_alpha(Pixel color, float opacity) {
    return with_alpha(color, opacity_channel((static_cast<float>(a(color)) / 255.0f) * opacity));
}

void apply_paint(SvgShape& shape,
                 const std::unordered_map<std::string, std::string>& attrs,
                 bool default_fill,
                 bool default_stroke) {
    const auto style = parse_style(attr_value(attrs, {}, "style"));
    const float opacity = parse_svg_float(attr_value(attrs, style, "opacity", "1"), 1.0f);
    const float fill_opacity = parse_svg_float(attr_value(attrs, style, "fill-opacity", "1"), 1.0f);
    const float stroke_opacity = parse_svg_float(attr_value(attrs, style, "stroke-opacity", "1"), 1.0f);
    const ParsedPaint fill = parse_paint(attr_value(attrs, style, "fill", default_fill ? "black" : "none"), default_fill);
    const ParsedPaint stroke = parse_paint(attr_value(attrs, style, "stroke", default_stroke ? "black" : "none"), default_stroke);
    shape.fill = multiply_alpha(fill.color, opacity * fill_opacity);
    shape.stroke = multiply_alpha(stroke.color, opacity * stroke_opacity);
    shape.fill_enabled = fill.enabled && a(shape.fill) > 0;
    shape.stroke_enabled = stroke.enabled && a(shape.stroke) > 0;
    shape.stroke_width = std::max(0.1f, parse_svg_float(attr_value(attrs, style, "stroke-width", "1"), 1.0f));
}

std::vector<SvgPoint> parse_points(std::string_view text) {
    std::vector<SvgPoint> points;
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char ch : text) {
        cleaned.push_back(ch == ',' ? ' ' : ch);
    }
    std::istringstream stream(cleaned);
    float x = 0.0f;
    float y = 0.0f;
    while (stream >> x >> y) {
        points.push_back({x, y});
    }
    return points;
}

std::vector<PathToken> tokenize_path(std::string_view text) {
    std::vector<PathToken> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        const char ch = text[i];
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ',') {
            ++i;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
            tokens.push_back({true, ch, 0.0f});
            ++i;
            continue;
        }
        std::string tail(text.substr(i));
        char* end = nullptr;
        const float value = std::strtof(tail.c_str(), &end);
        if (end == tail.c_str()) {
            ++i;
            continue;
        }
        tokens.push_back({false, '\0', value});
        i += static_cast<std::size_t>(end - tail.c_str());
    }
    return tokens;
}

bool token_is_number(const std::vector<PathToken>& tokens, std::size_t index) {
    return index < tokens.size() && !tokens[index].command;
}

bool read_number(const std::vector<PathToken>& tokens, std::size_t& index, float& out) {
    if (!token_is_number(tokens, index)) {
        return false;
    }
    out = tokens[index].number_value;
    ++index;
    return true;
}

SvgPoint relative_point(SvgPoint current, float x, float y, bool relative) {
    return relative ? SvgPoint{current.x + x, current.y + y} : SvgPoint{x, y};
}

std::vector<SvgPoint> parse_path_points(std::string_view path, bool& out_closed) {
    const std::vector<PathToken> tokens = tokenize_path(path);
    std::vector<SvgPoint> points;
    SvgPoint current{};
    SvgPoint start{};
    char command = '\0';
    out_closed = false;
    std::size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].command) {
            command = tokens[i].command_value;
            ++i;
        }
        if (command == '\0') {
            break;
        }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char normalized = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        if (normalized == 'M') {
            bool first = true;
            while (token_is_number(tokens, i)) {
                float x = 0.0f;
                float y = 0.0f;
                if (!read_number(tokens, i, x) || !read_number(tokens, i, y)) {
                    break;
                }
                current = relative_point(current, x, y, relative);
                if (first) {
                    start = current;
                    first = false;
                }
                points.push_back(current);
            }
            command = relative ? 'l' : 'L';
        } else if (normalized == 'L') {
            while (token_is_number(tokens, i)) {
                float x = 0.0f;
                float y = 0.0f;
                if (!read_number(tokens, i, x) || !read_number(tokens, i, y)) {
                    break;
                }
                current = relative_point(current, x, y, relative);
                points.push_back(current);
            }
        } else if (normalized == 'H') {
            while (token_is_number(tokens, i)) {
                float x = 0.0f;
                if (!read_number(tokens, i, x)) {
                    break;
                }
                current.x = relative ? current.x + x : x;
                points.push_back(current);
            }
        } else if (normalized == 'V') {
            while (token_is_number(tokens, i)) {
                float y = 0.0f;
                if (!read_number(tokens, i, y)) {
                    break;
                }
                current.y = relative ? current.y + y : y;
                points.push_back(current);
            }
        } else if (normalized == 'C') {
            while (token_is_number(tokens, i)) {
                float x1 = 0.0f;
                float y1 = 0.0f;
                float x2 = 0.0f;
                float y2 = 0.0f;
                float x = 0.0f;
                float y = 0.0f;
                if (!read_number(tokens, i, x1) || !read_number(tokens, i, y1) ||
                    !read_number(tokens, i, x2) || !read_number(tokens, i, y2) ||
                    !read_number(tokens, i, x) || !read_number(tokens, i, y)) {
                    break;
                }
                const SvgPoint p0 = current;
                const SvgPoint p1 = relative_point(current, x1, y1, relative);
                const SvgPoint p2 = relative_point(current, x2, y2, relative);
                const SvgPoint p3 = relative_point(current, x, y, relative);
                for (int step = 1; step <= 16; ++step) {
                    const float t = static_cast<float>(step) / 16.0f;
                    const float inv = 1.0f - t;
                    points.push_back({inv * inv * inv * p0.x + 3.0f * inv * inv * t * p1.x + 3.0f * inv * t * t * p2.x + t * t * t * p3.x,
                                      inv * inv * inv * p0.y + 3.0f * inv * inv * t * p1.y + 3.0f * inv * t * t * p2.y + t * t * t * p3.y});
                }
                current = p3;
            }
        } else if (normalized == 'Q') {
            while (token_is_number(tokens, i)) {
                float x1 = 0.0f;
                float y1 = 0.0f;
                float x = 0.0f;
                float y = 0.0f;
                if (!read_number(tokens, i, x1) || !read_number(tokens, i, y1) ||
                    !read_number(tokens, i, x) || !read_number(tokens, i, y)) {
                    break;
                }
                const SvgPoint p0 = current;
                const SvgPoint p1 = relative_point(current, x1, y1, relative);
                const SvgPoint p2 = relative_point(current, x, y, relative);
                for (int step = 1; step <= 12; ++step) {
                    const float t = static_cast<float>(step) / 12.0f;
                    const float inv = 1.0f - t;
                    points.push_back({inv * inv * p0.x + 2.0f * inv * t * p1.x + t * t * p2.x,
                                      inv * inv * p0.y + 2.0f * inv * t * p1.y + t * t * p2.y});
                }
                current = p2;
            }
        } else if (normalized == 'Z') {
            if (!points.empty()) {
                points.push_back(start);
                current = start;
            }
            out_closed = true;
        } else {
            while (i < tokens.size() && !tokens[i].command) {
                ++i;
            }
        }
    }
    return points;
}

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

std::string pixel_hex(Pixel pixel) {
    std::ostringstream out;
    out << '#';
    constexpr char digits[] = "0123456789abcdef";
    const std::array<std::uint8_t, 3> channels = {r(pixel), g(pixel), b(pixel)};
    for (std::uint8_t channel : channels) {
        out << digits[channel >> 4U] << digits[channel & 0x0fU];
    }
    return out.str();
}

std::string xml_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string xml_unescape(std::string_view text) {
    std::string out(text);
    auto replace_all = [&](std::string_view from, std::string_view to) {
        std::size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all("&lt;", "<");
    replace_all("&gt;", ">");
    replace_all("&quot;", "\"");
    replace_all("&apos;", "'");
    replace_all("&amp;", "&");
    return out;
}

void write_paint_attrs(std::ostringstream& out, const SvgShape& shape) {
    out << " fill=\"" << (shape.fill_enabled ? pixel_hex(shape.fill) : std::string("none")) << '"';
    out << " stroke=\"" << (shape.stroke_enabled ? pixel_hex(shape.stroke) : std::string("none")) << '"';
    out << " stroke-width=\"" << shape.stroke_width << '"';
    if (shape.fill_enabled && a(shape.fill) != 255U) {
        out << " fill-opacity=\"" << (static_cast<float>(a(shape.fill)) / 255.0f) << '"';
    }
    if (shape.stroke_enabled && a(shape.stroke) != 255U) {
        out << " stroke-opacity=\"" << (static_cast<float>(a(shape.stroke)) / 255.0f) << '"';
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

const char* svg_shape_type_name(SvgShapeType type) {
    switch (type) {
        case SvgShapeType::Rect: return "Rectangle";
        case SvgShapeType::Ellipse: return "Ellipse";
        case SvgShapeType::Line: return "Line";
        case SvgShapeType::Polyline: return "Polyline";
        case SvgShapeType::Polygon: return "Polygon";
        case SvgShapeType::Path: return "Path";
        case SvgShapeType::Text: return "Text";
    }
    return "Shape";
}

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

std::string svg_to_string(const SvgDocument& svg) {
    std::ostringstream out;
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << svg.width << "\" height=\"" << svg.height
        << "\" viewBox=\"0 0 " << svg.width << ' ' << svg.height << "\">\n";
    for (const SvgShape& shape : svg.shapes) {
        switch (shape.type) {
            case SvgShapeType::Rect:
                out << "  <rect x=\"" << shape.x << "\" y=\"" << shape.y << "\" width=\"" << shape.w
                    << "\" height=\"" << shape.h << '"';
                write_paint_attrs(out, shape);
                out << "/>\n";
                break;
            case SvgShapeType::Ellipse:
                out << "  <ellipse cx=\"" << (shape.x + shape.w * 0.5f) << "\" cy=\"" << (shape.y + shape.h * 0.5f)
                    << "\" rx=\"" << (std::abs(shape.w) * 0.5f) << "\" ry=\"" << (std::abs(shape.h) * 0.5f) << '"';
                write_paint_attrs(out, shape);
                out << "/>\n";
                break;
            case SvgShapeType::Line:
                if (shape.points.size() >= 2) {
                    out << "  <line x1=\"" << shape.points[0].x << "\" y1=\"" << shape.points[0].y
                        << "\" x2=\"" << shape.points[1].x << "\" y2=\"" << shape.points[1].y << '"';
                    write_paint_attrs(out, shape);
                    out << "/>\n";
                }
                break;
            case SvgShapeType::Polyline:
            case SvgShapeType::Polygon:
            case SvgShapeType::Path: {
                out << "  <" << (shape.type == SvgShapeType::Polygon ? "polygon" : "polyline") << " points=\"";
                for (std::size_t i = 0; i < shape.points.size(); ++i) {
                    if (i != 0U) {
                        out << ' ';
                    }
                    out << shape.points[i].x << ',' << shape.points[i].y;
                }
                out << '"';
                write_paint_attrs(out, shape);
                out << "/>\n";
                break;
            }
            case SvgShapeType::Text:
                out << "  <text x=\"" << shape.x << "\" y=\"" << shape.y << '"';
                write_paint_attrs(out, shape);
                out << '>' << xml_escape(shape.text) << "</text>\n";
                break;
        }
    }
    out << "</svg>\n";
    return out.str();
}

bool svg_from_string(const std::string& text, SvgDocument& out_svg, std::string* error) {
    SvgDocument svg;
    std::size_t pos = 0;
    bool saw_svg = false;
    while ((pos = text.find('<', pos)) != std::string::npos) {
        if (pos + 1U >= text.size()) {
            break;
        }
        const char first = text[pos + 1U];
        if (first == '/' || first == '!' || first == '?') {
            ++pos;
            continue;
        }
        const std::size_t end = text.find('>', pos + 1U);
        if (end == std::string::npos) {
            break;
        }
        const std::string_view tag(text.data() + pos + 1U, end - pos - 1U);
        const std::string name = tag_name(tag);
        const auto attrs = parse_attributes(tag);
        if (name == "svg") {
            saw_svg = true;
            svg.width = std::max(1, static_cast<int>(std::lround(parse_svg_float(attr_value(attrs, {}, "width", "64"), 64.0f))));
            svg.height = std::max(1, static_cast<int>(std::lround(parse_svg_float(attr_value(attrs, {}, "height", "64"), 64.0f))));
            const std::string view_box = attr_value(attrs, {}, "viewbox");
            if (!view_box.empty()) {
                const std::vector<SvgPoint> values = parse_points(view_box);
                if (values.size() >= 2) {
                    svg.width = std::max(1, static_cast<int>(std::lround(values[1].x)));
                    svg.height = std::max(1, static_cast<int>(std::lround(values[1].y)));
                }
            }
        } else if (name == "rect") {
            SvgShape shape;
            shape.type = SvgShapeType::Rect;
            shape.x = parse_svg_float(attr_value(attrs, {}, "x"), 0.0f);
            shape.y = parse_svg_float(attr_value(attrs, {}, "y"), 0.0f);
            shape.w = parse_svg_float(attr_value(attrs, {}, "width"), 0.0f);
            shape.h = parse_svg_float(attr_value(attrs, {}, "height"), 0.0f);
            apply_paint(shape, attrs, true, false);
            svg.shapes.push_back(std::move(shape));
        } else if (name == "circle" || name == "ellipse") {
            SvgShape shape;
            shape.type = SvgShapeType::Ellipse;
            const float cx = parse_svg_float(attr_value(attrs, {}, "cx"), 0.0f);
            const float cy = parse_svg_float(attr_value(attrs, {}, "cy"), 0.0f);
            const float rx = name == "circle" ? parse_svg_float(attr_value(attrs, {}, "r"), 0.0f)
                                               : parse_svg_float(attr_value(attrs, {}, "rx"), 0.0f);
            const float ry = name == "circle" ? rx : parse_svg_float(attr_value(attrs, {}, "ry"), rx);
            shape.x = cx - rx;
            shape.y = cy - ry;
            shape.w = rx * 2.0f;
            shape.h = ry * 2.0f;
            apply_paint(shape, attrs, true, false);
            svg.shapes.push_back(std::move(shape));
        } else if (name == "line") {
            SvgShape shape;
            shape.type = SvgShapeType::Line;
            shape.points.push_back({parse_svg_float(attr_value(attrs, {}, "x1"), 0.0f),
                                    parse_svg_float(attr_value(attrs, {}, "y1"), 0.0f)});
            shape.points.push_back({parse_svg_float(attr_value(attrs, {}, "x2"), 0.0f),
                                    parse_svg_float(attr_value(attrs, {}, "y2"), 0.0f)});
            apply_paint(shape, attrs, false, true);
            svg.shapes.push_back(std::move(shape));
        } else if (name == "polyline" || name == "polygon") {
            SvgShape shape;
            shape.type = name == "polygon" ? SvgShapeType::Polygon : SvgShapeType::Polyline;
            shape.points = parse_points(attr_value(attrs, {}, "points"));
            shape.closed = name == "polygon";
            apply_paint(shape, attrs, name == "polygon", true);
            svg.shapes.push_back(std::move(shape));
        } else if (name == "path") {
            SvgShape shape;
            shape.type = SvgShapeType::Path;
            shape.points = parse_path_points(attr_value(attrs, {}, "d"), shape.closed);
            apply_paint(shape, attrs, shape.closed, true);
            svg.shapes.push_back(std::move(shape));
        } else if (name == "text") {
            const std::string close_tag = "</text>";
            const std::size_t close = text.find(close_tag, end + 1U);
            SvgShape shape;
            shape.type = SvgShapeType::Text;
            shape.x = parse_svg_float(attr_value(attrs, {}, "x"), 0.0f);
            shape.y = parse_svg_float(attr_value(attrs, {}, "y"), 0.0f);
            shape.text = close == std::string::npos ? std::string{} : xml_unescape(std::string_view(text.data() + end + 1U, close - end - 1U));
            apply_paint(shape, attrs, true, false);
            svg.shapes.push_back(std::move(shape));
            if (close != std::string::npos) {
                pos = close + close_tag.size();
                continue;
            }
        }
        pos = end + 1U;
    }
    if (!saw_svg) {
        if (error != nullptr) {
            *error = "File is not an SVG document";
        }
        return false;
    }
    out_svg = std::move(svg);
    return true;
}

bool load_svg_file(const std::string& path, SvgDocument& out_svg, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "Unable to open SVG file";
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return svg_from_string(buffer.str(), out_svg, error);
}

bool save_svg_file(const std::string& path, const SvgDocument& svg, std::string* error) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "Unable to write SVG file";
        }
        return false;
    }
    file << svg_to_string(svg);
    if (!file) {
        if (error != nullptr) {
            *error = "Failed while writing SVG file";
        }
        return false;
    }
    return true;
}

} // namespace px
