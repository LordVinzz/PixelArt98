#pragma once

#include "core/Document.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace px {

enum class ToolType {
    Pencil,
    Brush,
    Eraser,
    Line,
    Rectangle,
    Ellipse,
    Bucket,
    Gradient,
    Eyedropper,
    CloneStamp,
    RectSelect,
    LassoSelect,
    MagicWand,
    MovePixels,
    Text
};

struct ToolContext {
    ToolType tool = ToolType::Pencil;
    Pixel primary = rgba(0, 0, 0, 255);
    Pixel secondary = rgba(255, 255, 255, 255);
    int brush_size = 1;
    int tolerance = 32;
    bool contiguous = true;
    bool erase_to_transparent = true;
    int clone_source_x = -1;
    int clone_source_y = -1;
};

struct TextBox {
    bool active = false;
    int x = 0;
    int y = 0;
    std::string text;
    Pixel color = rgba(0, 0, 0, 255);
};

const char* tool_name(ToolType tool);
[[nodiscard]] std::array<int, 2> constrained_tool_endpoint(ToolType tool,
                                                           int start_x,
                                                           int start_y,
                                                           int current_x,
                                                           int current_y,
                                                           bool constrain);

void put_pixel(Document& doc, int x, int y, Pixel color, bool erase);
void plot_brush_raw(Document& doc, int cx, int cy, Pixel color, int size, bool erase);
void draw_line_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool erase);
void draw_rect_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled);
void draw_ellipse_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled);
void fill_gradient_raw(Document& doc, int x0, int y0, int x1, int y1, Pixel a, Pixel b);

void draw_brush(Document& doc, int x, int y, const ToolContext& ctx, bool erase = false);
void draw_line(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size);
void draw_rect(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled);
void draw_ellipse(Document& doc, int x0, int y0, int x1, int y1, Pixel color, int size, bool filled);
void fill_bucket(Document& doc, int x, int y, Pixel color, int tolerance, bool contiguous);
void fill_gradient(Document& doc, int x0, int y0, int x1, int y1, Pixel a, Pixel b);
std::optional<Pixel> pick_color(const Document& doc, int x, int y);
void magic_wand(Document& doc, int x, int y, int tolerance, bool contiguous, bool replace = true);
void clone_stamp_raw(Document& doc, const std::vector<Pixel>& source, int sx, int sy, int dx, int dy, int brush_size);
void clone_stamp(Document& doc, int sx, int sy, int dx, int dy, int brush_size);
std::vector<std::array<int, 2>> raster_text_points(int x, int y, const std::string& text);
void stamp_text(Document& doc, int x, int y, const std::string& text, Pixel color);
void stamp_text_blocks(Document& doc, int x, int y, const std::string& text, Pixel color);

} // namespace px
