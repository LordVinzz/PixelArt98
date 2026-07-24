#include "ui/QtCanvasWidget.hpp"

#include "ui/CanvasViewport.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTabletEvent>
#include <QWheelEvent>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace px {

namespace {

constexpr QColor kSelectionFill(78, 178, 255, 72);
constexpr QColor kSelectionStroke(78, 178, 255, 230);

QRectF pixel_rect(const QRectF& target, double zoom, int x, int y, int w = 1, int h = 1) {
    return QRectF(target.left() + static_cast<double>(x) * zoom,
                  target.top() + static_cast<double>(y) * zoom,
                  static_cast<double>(w) * zoom,
                  static_cast<double>(h) * zoom);
}

void draw_pixel_buffer(QPainter& painter,
                       const QRectF& target,
                       double zoom,
                       const CanvasPixelBounds& visible,
                       const std::vector<Pixel>& pixels,
                       int image_width,
                       int image_height,
                       qreal opacity = 1.0) {
    const std::size_t expected_size = static_cast<std::size_t>(std::max(0, image_width)) *
                                      static_cast<std::size_t>(std::max(0, image_height));
    if (pixels.size() != expected_size || visible.empty()) return;

    const QImage image(reinterpret_cast<const uchar*>(pixels.data()),
                       image_width,
                       image_height,
                       image_width * static_cast<int>(sizeof(Pixel)),
                       QImage::Format_RGBA8888);
    const QRectF source(static_cast<double>(visible.left),
                        static_cast<double>(visible.top),
                        static_cast<double>(visible.width()),
                        static_cast<double>(visible.height()));
    painter.save();
    painter.setOpacity(opacity);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(pixel_rect(target, zoom, visible.left, visible.top, visible.width(), visible.height()),
                      image,
                      source);
    painter.restore();
}

void draw_selection_mask(QPainter& painter,
                         const QRectF& target,
                         double zoom,
                         const SelectionMask& selection,
                         const CanvasPixelBounds& visible) {
    if (!selection.active || selection.width <= 0 || selection.height <= 0 ||
        selection.mask.size() != static_cast<std::size_t>(selection.width) * static_cast<std::size_t>(selection.height)) {
        return;
    }

    const int left = std::clamp(visible.left, 0, selection.width);
    const int top = std::clamp(visible.top, 0, selection.height);
    const int right = std::clamp(visible.right, left, selection.width);
    const int bottom = std::clamp(visible.bottom, top, selection.height);
    if (left >= right || top >= bottom) return;

    painter.setPen(Qt::NoPen);
    painter.setBrush(kSelectionFill);
    for (int y = top; y < bottom; ++y) {
        int run_start = -1;
        for (int x = left; x <= right; ++x) {
            const bool selected = x < right && selection.mask[static_cast<std::size_t>(y * selection.width + x)] != 0;
            if (selected && run_start < 0) {
                run_start = x;
            } else if (!selected && run_start >= 0) {
                painter.drawRect(pixel_rect(target, zoom, run_start, y, x - run_start, 1));
                run_start = -1;
            }
        }
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kSelectionStroke, 1.25));
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if (selection.mask[static_cast<std::size_t>(y * selection.width + x)] == 0) {
                continue;
            }
            if (x == 0 || selection.mask[static_cast<std::size_t>(y * selection.width + x - 1)] == 0) {
                painter.drawLine(pixel_rect(target, zoom, x, y).topLeft(), pixel_rect(target, zoom, x, y).bottomLeft());
            }
            if (x == selection.width - 1 || selection.mask[static_cast<std::size_t>(y * selection.width + x + 1)] == 0) {
                painter.drawLine(pixel_rect(target, zoom, x, y).topRight(), pixel_rect(target, zoom, x, y).bottomRight());
            }
            if (y == 0 || selection.mask[static_cast<std::size_t>((y - 1) * selection.width + x)] == 0) {
                painter.drawLine(pixel_rect(target, zoom, x, y).topLeft(), pixel_rect(target, zoom, x, y).topRight());
            }
            if (y == selection.height - 1 || selection.mask[static_cast<std::size_t>((y + 1) * selection.width + x)] == 0) {
                painter.drawLine(pixel_rect(target, zoom, x, y).bottomLeft(), pixel_rect(target, zoom, x, y).bottomRight());
            }
        }
    }
}

QPointF pixel_center(const QRectF& target, double zoom, int x, int y) {
    return QPointF(target.left() + (static_cast<double>(x) + 0.5) * zoom,
                   target.top() + (static_cast<double>(y) + 0.5) * zoom);
}

QRectF clone_source_rect(const QRectF& target, double zoom, const ToolContext& tool) {
    const int size = std::max(1, tool.brush_size);
    const int half = size / 2;
    return pixel_rect(target, zoom, tool.clone_source_x - half, tool.clone_source_y - half, size, size);
}

void draw_clone_source_preview(QPainter& painter, const QRectF& target, double zoom, const ToolContext& tool) {
    if (tool.tool != ToolType::CloneStamp || tool.clone_source_x < 0 || tool.clone_source_y < 0) {
        return;
    }

    painter.save();
    painter.setClipRect(target);
    const QRectF source = clone_source_rect(target, zoom, tool);
    painter.fillRect(source, kSelectionFill);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kSelectionStroke, 1.5));
    painter.drawRect(source);
    painter.drawLine(QPointF(source.center().x(), source.top()), QPointF(source.center().x(), source.bottom()));
    painter.drawLine(QPointF(source.left(), source.center().y()), QPointF(source.right(), source.center().y()));
    painter.restore();
}

} // namespace

QtCanvasWidget::QtCanvasWidget(EditorController& controller, QWidget* parent)
    : QOpenGLWidget(parent), controller_(controller) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setTabletTracking(true);
    setMinimumSize(320, 240);
}

QtCanvasWidget::~QtCanvasWidget() {
    if (context() != nullptr && isValid()) {
        makeCurrent();
        tiled_texture_.destroy();
        doneCurrent();
    }
}

void* QtCanvasWidget::load_gl_proc(const char* name) {
    return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
}

void QtCanvasWidget::initializeGL() {
    gl_ready_ = gladLoadGL(load_gl_proc) != 0;
    if (gl_ready_) glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
}

void QtCanvasWidget::resizeGL(int, int) {
    if (fit_requested_) fit_to_canvas();
}

QRectF QtCanvasWidget::image_rect() const {
    const double image_width = static_cast<double>(controller_.document().width) * zoom_;
    const double image_height = static_cast<double>(controller_.document().height) * zoom_;
    const QPointF centered((static_cast<double>(width()) - image_width) * 0.5,
                           (static_cast<double>(height()) - image_height) * 0.5);
    return {centered + pan_, QSizeF(image_width, image_height)};
}

void QtCanvasWidget::paintGL() {
    const int document_width = controller_.document().width;
    const int document_height = controller_.document().height;
    const auto& pixels = controller_.display_pixels();
    if (gl_ready_) {
        const bool dimensions_changed = document_width != tiled_document_width_ ||
                                        document_height != tiled_document_height_;
        if (dimensions_changed) {
            tiled_texture_.destroy();
            tiled_document_width_ = document_width;
            tiled_document_height_ = document_height;
            tiled_display_revision_ = 0;
            pyramid_display_revision_ = 0;
        }
        const std::uint64_t revision = controller_.display_revision();
        if (revision != tiled_display_revision_) {
            if (tiled_texture_.has_prepared_pyramid()) tiled_texture_.clear_prepared_pyramid();
            tiled_texture_.invalidate();
            tiled_display_revision_ = revision;
        }
        if (zoom_ < 1.0 &&
            GLTiledCanvasTexture::should_use_pyramid(document_width, document_height) &&
            pyramid_display_revision_ != revision) {
            auto pyramid = GLTiledCanvasTexture::build_cpu_pyramid(
                document_width, document_height, pixels);
            if (!pyramid.empty()) {
                tiled_texture_.set_prepared_pyramid(std::move(pyramid));
                pyramid_display_revision_ = revision;
            }
        }
    }

    QPainter painter(this);
    painter.fillRect(rect(), QColor(25, 28, 33));
    const QRectF target = image_rect();
    const auto visible = visible_canvas_pixels(target.left(),
                                               target.top(),
                                               zoom_,
                                               controller_.document().width,
                                               controller_.document().height,
                                               width(),
                                               height());
    if (visible.empty()) return;

    const QRectF visible_target = target.intersected(QRectF(rect()));
    painter.save();
    painter.setClipRect(visible_target, Qt::IntersectClip);
    if (checker_visible_) {
        const int group = std::max(1, static_cast<int>(std::ceil(4.0 / zoom_)));
        const int start_y = (visible.top / group) * group;
        const int start_x = (visible.left / group) * group;
        for (int py = start_y; py < visible.bottom; py += group) {
            const int h = std::min(group, controller_.document().height - py);
            for (int px = start_x; px < visible.right; px += group) {
                const int w = std::min(group, controller_.document().width - px);
                const QRectF cell(target.left() + static_cast<double>(px) * zoom_,
                                  target.top() + static_cast<double>(py) * zoom_,
                                  static_cast<double>(w) * zoom_,
                                  static_cast<double>(h) * zoom_);
                painter.fillRect(cell, (((px / group) + (py / group)) & 1) == 0 ? QColor(178, 178, 178) : QColor(220, 220, 220));
            }
        }
    } else {
        painter.fillRect(visible_target, Qt::white);
    }

    bool used_tiled_renderer = false;
    if (gl_ready_) {
        painter.beginNativePainting();
        used_tiled_renderer = tiled_texture_.draw(
            pixels, document_width, document_height, static_cast<float>(zoom_),
            static_cast<float>(target.left()), static_cast<float>(target.top()),
            width(), height(), visible.left, visible.top, visible.right, visible.bottom);
        painter.endNativePainting();
        if (tiled_texture_.has_pending_uploads()) update();
    }
    if (!used_tiled_renderer) {
        draw_pixel_buffer(painter,
                          target,
                          zoom_,
                          visible,
                          pixels,
                          document_width,
                          document_height);
    }

    if (onion_visible_ && controller_.document().active_frame > 0) {
        constexpr qreal onion_opacity = 0.30;
        const auto& onion_pixels = controller_.onion_skin_pixels();
        draw_pixel_buffer(painter,
                          target,
                          zoom_,
                          visible,
                          onion_pixels,
                          controller_.document().width,
                          controller_.document().height,
                          onion_opacity);
    }

    const FloatingSelection& floating = controller_.document().floating_selection;
    if (floating.active && floating.width > 0 && floating.height > 0 && !floating.pixels.empty()) {
        const QImage floating_image(reinterpret_cast<const uchar*>(floating.pixels.data()),
                                    floating.width,
                                    floating.height,
                                    floating.width * static_cast<int>(sizeof(Pixel)),
                                    QImage::Format_RGBA8888);
        const QRectF floating_target(target.left() + static_cast<double>(floating.source_x + floating.offset_x) * zoom_,
                                     target.top() + static_cast<double>(floating.source_y + floating.offset_y) * zoom_,
                                     static_cast<double>(floating.width) * zoom_,
                                     static_cast<double>(floating.height) * zoom_);
        const auto floating_visible = visible_canvas_pixels(floating_target.left(),
                                                            floating_target.top(),
                                                            zoom_,
                                                            floating.width,
                                                            floating.height,
                                                            width(),
                                                            height());
        if (!floating_visible.empty()) {
            const QRectF floating_source(static_cast<double>(floating_visible.left),
                                         static_cast<double>(floating_visible.top),
                                         static_cast<double>(floating_visible.width()),
                                         static_cast<double>(floating_visible.height()));
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(pixel_rect(floating_target,
                                         zoom_,
                                         floating_visible.left,
                                         floating_visible.top,
                                         floating_visible.width(),
                                         floating_visible.height()),
                              floating_image,
                              floating_source);
        }
    }

    if (raster_text_preview_.width > 0 && raster_text_preview_.height > 0 &&
        raster_text_preview_.pixels.size() ==
            static_cast<std::size_t>(raster_text_preview_.width *
                                     raster_text_preview_.height)) {
        const QImage preview_image(
            reinterpret_cast<const uchar*>(raster_text_preview_.pixels.data()),
            raster_text_preview_.width,
            raster_text_preview_.height,
            raster_text_preview_.width * static_cast<int>(sizeof(Pixel)),
            QImage::Format_RGBA8888);
        const QRectF preview_target = pixel_rect(
            target, zoom_, raster_text_preview_x_, raster_text_preview_y_,
            raster_text_preview_.width, raster_text_preview_.height);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(preview_target, preview_image);
    }

    if (grid_visible_ && zoom_ >= 4.0) {
        painter.setPen(QPen(QColor(0, 0, 0, 58), 1.0));
        for (int x = visible.left; x <= visible.right; ++x) {
            const double px = target.left() + static_cast<double>(x) * zoom_;
            painter.drawLine(QPointF(px, visible_target.top()), QPointF(px, visible_target.bottom()));
        }
        for (int y = visible.top; y <= visible.bottom; ++y) {
            const double py = target.top() + static_cast<double>(y) * zoom_;
            painter.drawLine(QPointF(visible_target.left(), py), QPointF(visible_target.right(), py));
        }
    }

    draw_selection_mask(painter, target, zoom_, controller_.document().selection, visible);
    draw_clone_source_preview(painter, target, zoom_, controller_.tool());

    if (drawing_) {
        const ToolType tool = controller_.tool().tool;
        if (tool == ToolType::Line || tool == ToolType::Rectangle || tool == ToolType::Ellipse ||
            tool == ToolType::RectSelect || tool == ToolType::EllipseSelect) {
            const auto endpoint = constrained_tool_endpoint(tool, drag_start_pixel_.x(), drag_start_pixel_.y(), drag_current_pixel_.x(), drag_current_pixel_.y(), QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) || QApplication::keyboardModifiers().testFlag(Qt::MetaModifier));
            const QPointF start(target.left() + (static_cast<double>(drag_start_pixel_.x()) + 0.5) * zoom_, target.top() + (static_cast<double>(drag_start_pixel_.y()) + 0.5) * zoom_);
            const QPointF end(target.left() + (static_cast<double>(endpoint[0]) + 0.5) * zoom_, target.top() + (static_cast<double>(endpoint[1]) + 0.5) * zoom_);
            if (tool == ToolType::RectSelect || tool == ToolType::EllipseSelect) {
                const int min_x = std::min(drag_start_pixel_.x(), endpoint[0]);
                const int max_x = std::max(drag_start_pixel_.x(), endpoint[0]);
                const int min_y = std::min(drag_start_pixel_.y(), endpoint[1]);
                const int max_y = std::max(drag_start_pixel_.y(), endpoint[1]);
                const QRectF preview = pixel_rect(target, zoom_, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
                if (tool == ToolType::EllipseSelect) painter.setBrush(kSelectionFill);
                else painter.fillRect(preview, kSelectionFill);
                painter.setPen(QPen(kSelectionStroke, 1.5));
                if (tool == ToolType::EllipseSelect) painter.drawEllipse(preview);
                else painter.drawRect(preview);
            } else {
                painter.setPen(QPen(QColor(255, 255, 255, 220), 1.5, Qt::DashLine));
                if (tool == ToolType::Line) painter.drawLine(start, end);
                else if (tool == ToolType::Ellipse) painter.drawEllipse(QRectF(start, end).normalized());
                else painter.drawRect(QRectF(start, end).normalized());
            }
        } else if (tool == ToolType::LassoSelect) {
            QPolygonF polygon;
            for (const auto& point : controller_.lasso_points()) {
                polygon << pixel_center(target, zoom_, point[0], point[1]);
            }
            if (polygon.size() >= 2) {
                polygon << pixel_center(target, zoom_, drag_current_pixel_.x(), drag_current_pixel_.y());
                painter.setPen(QPen(kSelectionStroke, 1.5));
                painter.setBrush(Qt::NoBrush);
                painter.drawPolyline(polygon);
                if (polygon.size() >= 3) {
                    QPainterPath path;
                    path.addPolygon(polygon);
                    path.closeSubpath();
                    painter.fillPath(path, kSelectionFill);
                    painter.setPen(QPen(kSelectionStroke, 1.5));
                    painter.drawPath(path);
                }
            }
        }
    }
    painter.restore();
    draw_selection_transform_handles(painter);
    draw_raster_text_box_handles(painter);
}
