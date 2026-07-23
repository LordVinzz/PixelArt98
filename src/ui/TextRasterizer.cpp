// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/TextRasterizer.hpp"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>

#include <algorithm>

namespace px {

namespace {

Qt::Alignment text_alignment(RasterTextAlignment alignment) {
    switch (alignment) {
        case RasterTextAlignment::Center: return Qt::AlignHCenter;
        case RasterTextAlignment::Right: return Qt::AlignRight;
        case RasterTextAlignment::Left: return Qt::AlignLeft;
    }
    return Qt::AlignLeft;
}

QFont raster_font(const RasterTextOptions& options) {
    QFont font(options.font_family);
    font.setPixelSize(std::max(1, options.pixel_size));
    font.setBold(options.bold);
    font.setItalic(options.italic);
    font.setStyleStrategy(options.antialias ? QFont::PreferAntialias : QFont::NoAntialias);
    return font;
}

} // namespace

RasterTextImage rasterize_text(const RasterTextOptions& options, Pixel color,
                               int maximum_width, int maximum_height) {
    RasterTextImage result;
    if (options.text.isEmpty() || maximum_width <= 0 || maximum_height <= 0) return result;

    const int width = std::clamp(options.box_width, 1, maximum_width);
    const QFont font = raster_font(options);
    const QFontMetrics metrics(font);
    const int flags = static_cast<int>(Qt::AlignTop | text_alignment(options.alignment) |
                                       Qt::TextWordWrap);
    const QRect measured = metrics.boundingRect(QRect(0, 0, width, maximum_height), flags,
                                                options.text);
    const int measured_height = std::max(metrics.height(), measured.height());
    const int height = options.box_height > 0
                           ? std::clamp(options.box_height, 1, maximum_height)
                           : std::clamp(measured_height, 1, maximum_height);

    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, options.antialias);
    painter.setFont(font);
    painter.setPen(QColor(r(color), g(color), b(color), a(color)));
    painter.drawText(QRect(0, 0, width, height), flags, options.text);
    painter.end();

    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        const auto* line = reinterpret_cast<const Pixel*>(image.constScanLine(y));
        std::copy_n(line, width, result.pixels.begin() + static_cast<std::ptrdiff_t>(y * width));
    }
    return result;
}

bool stamp_raster_text(Document& document, int x, int y, const RasterTextOptions& options,
                       Pixel color, std::string* error) {
    if (!document.in_bounds(x, y) || !document.ensure_active_cel()) {
        if (error != nullptr) *error = "The text origin is outside the canvas.";
        return false;
    }
    if (options.text.trimmed().isEmpty()) {
        if (error != nullptr) *error = "Enter text before applying it.";
        return false;
    }

    const RasterTextImage raster = rasterize_text(options, color, document.width - x,
                                                   document.height - y);
    if (raster.pixels.empty()) {
        if (error != nullptr) *error = "The selected font produced no raster pixels.";
        return false;
    }

    std::vector<Pixel> before = document.snapshot_active_cel();
    auto& destination = document.active_cel().pixels;
    for (int source_y = 0; source_y < raster.height; ++source_y) {
        for (int source_x = 0; source_x < raster.width; ++source_x) {
            const int destination_x = x + source_x;
            const int destination_y = y + source_y;
            if (!document.in_bounds(destination_x, destination_y) ||
                (document.selection.active &&
                 !document.selection.contains(destination_x, destination_y))) {
                continue;
            }
            const Pixel source = raster.pixels[static_cast<std::size_t>(
                source_y * raster.width + source_x)];
            if (a(source) == 0) continue;
            const std::size_t index = static_cast<std::size_t>(
                document.pixel_index(destination_x, destination_y));
            destination[index] = blend_over(destination[index], source);
        }
    }
    document.commit_active_cel_edit("Raster Text", std::move(before));
    return true;
}

} // namespace px
