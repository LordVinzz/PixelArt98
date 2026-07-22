// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

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
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

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
    setMinimumSize(320, 240);
}

void QtCanvasWidget::initializeGL() {
    QOpenGLContext::currentContext()->functions()->glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
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

    const auto& pixels = controller_.display_pixels();
    draw_pixel_buffer(painter,
                      target,
                      zoom_,
                      visible,
                      pixels,
                      controller_.document().width,
                      controller_.document().height);

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
        if (tool == ToolType::Line || tool == ToolType::Rectangle || tool == ToolType::Ellipse || tool == ToolType::RectSelect) {
            const auto endpoint = constrained_tool_endpoint(tool, drag_start_pixel_.x(), drag_start_pixel_.y(), drag_current_pixel_.x(), drag_current_pixel_.y(), QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) || QApplication::keyboardModifiers().testFlag(Qt::MetaModifier));
            const QPointF start(target.left() + (static_cast<double>(drag_start_pixel_.x()) + 0.5) * zoom_, target.top() + (static_cast<double>(drag_start_pixel_.y()) + 0.5) * zoom_);
            const QPointF end(target.left() + (static_cast<double>(endpoint[0]) + 0.5) * zoom_, target.top() + (static_cast<double>(endpoint[1]) + 0.5) * zoom_);
            if (tool == ToolType::RectSelect) {
                const int min_x = std::min(drag_start_pixel_.x(), endpoint[0]);
                const int max_x = std::max(drag_start_pixel_.x(), endpoint[0]);
                const int min_y = std::min(drag_start_pixel_.y(), endpoint[1]);
                const int max_y = std::max(drag_start_pixel_.y(), endpoint[1]);
                const QRectF preview = pixel_rect(target, zoom_, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
                painter.fillRect(preview, kSelectionFill);
                painter.setPen(QPen(kSelectionStroke, 1.5));
                painter.drawRect(preview);
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
}

QPoint QtCanvasWidget::pixel_at(const QPointF& position) const {
    const QRectF target = image_rect();
    return {static_cast<int>(std::floor((position.x() - target.left()) / zoom_)),
            static_cast<int>(std::floor((position.y() - target.top()) / zoom_))};
}

bool QtCanvasWidget::valid_pixel(const QPoint& pixel) const {
    return controller_.document().in_bounds(pixel.x(), pixel.y());
}

SelectionCombineMode QtCanvasWidget::selection_mode(Qt::KeyboardModifiers modifiers, bool secondary) const {
    const bool action = modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    if (secondary && action) return SelectionCombineMode::Invert;
    if (secondary && alt) return SelectionCombineMode::Intersect;
    if (action) return SelectionCombineMode::Add;
    if (alt) return SelectionCombineMode::Subtract;
    return SelectionCombineMode::Replace;
}

void QtCanvasWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const bool space_pan = space_down_ && event->button() == Qt::LeftButton;
    if (event->button() == Qt::MiddleButton || space_pan) {
        panning_ = true;
        pan_anchor_ = event->position();
        pan_start_ = pan_;
        return;
    }
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) return;
    const QPoint pixel = pixel_at(event->position());
    if (!valid_pixel(pixel)) return;
    drawing_ = true;
    drag_start_pixel_ = drag_current_pixel_ = pixel;
    const bool secondary = event->button() == Qt::RightButton;
    controller_.begin_stroke(pixel.x(), pixel.y(), secondary, selection_mode(event->modifiers(), secondary));
    update();
}

void QtCanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        pan_ = pan_start_ + event->position() - pan_anchor_;
        update();
        return;
    }
    if (!drawing_) return;
    const QPoint pixel = pixel_at(event->position());
    if (valid_pixel(pixel)) {
        drag_current_pixel_ = pixel;
        controller_.update_stroke(pixel.x(), pixel.y(), event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier));
        update();
    }
}

void QtCanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        return;
    }
    if (!drawing_) return;
    const QPoint pixel = valid_pixel(pixel_at(event->position())) ? pixel_at(event->position()) : drag_current_pixel_;
    controller_.end_stroke(pixel.x(), pixel.y(), event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier));
    drawing_ = false;
    notify_changed();
}

void QtCanvasWidget::wheelEvent(QWheelEvent* event) {
    const double factor = std::pow(1.25, static_cast<double>(event->angleDelta().y()) / 120.0);
    change_zoom(zoom_ * factor, event->position());
    event->accept();
}

void QtCanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) { space_down_ = true; event->accept(); return; }
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        controller_.cancel_interaction();
        controller_.clear_selection();
        drawing_ = false;
        notify_changed();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) { controller_.delete_selection(); notify_changed(); return; }
    if (event->key() == Qt::Key_BracketLeft) { controller_.tool().brush_size = std::max(1, controller_.tool().brush_size - 1); notify_changed(); return; }
    if (event->key() == Qt::Key_BracketRight) { controller_.tool().brush_size = std::min(32, controller_.tool().brush_size + 1); notify_changed(); return; }
    int dx = 0;
    int dy = 0;
    if (event->key() == Qt::Key_Left) dx = -1;
    if (event->key() == Qt::Key_Right) dx = 1;
    if (event->key() == Qt::Key_Up) dy = -1;
    if (event->key() == Qt::Key_Down) dy = 1;
    if (dx != 0 || dy != 0) { controller_.nudge_selection(dx * (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1), dy * (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1)); notify_changed(); return; }
    QOpenGLWidget::keyPressEvent(event);
}

void QtCanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) { space_down_ = false; event->accept(); return; }
    QOpenGLWidget::keyReleaseEvent(event);
}

void QtCanvasWidget::change_zoom(double next_zoom, const QPointF& anchor) {
    next_zoom = std::clamp(next_zoom, 0.02, 128.0);
    const QRectF before = image_rect();
    const QPointF document_point((anchor.x() - before.left()) / zoom_, (anchor.y() - before.top()) / zoom_);
    zoom_ = next_zoom;
    const QRectF after = image_rect();
    pan_ += anchor - QPointF(after.left() + document_point.x() * zoom_, after.top() + document_point.y() * zoom_);
    fit_requested_ = false;
    update();
}

void QtCanvasWidget::zoom_in() { change_zoom(zoom_ * 1.25, rect().center()); }
void QtCanvasWidget::zoom_out() { change_zoom(zoom_ / 1.25, rect().center()); }
void QtCanvasWidget::actual_size() { zoom_ = 1.0; pan_ = {}; fit_requested_ = false; update(); }
void QtCanvasWidget::fit_to_canvas() {
    const double horizontal = std::max(1.0, static_cast<double>(width() - 32)) / static_cast<double>(controller_.document().width);
    const double vertical = std::max(1.0, static_cast<double>(height() - 32)) / static_cast<double>(controller_.document().height);
    zoom_ = std::clamp(std::min(horizontal, vertical), 0.02, 128.0);
    pan_ = {};
    fit_requested_ = true;
    update();
}
void QtCanvasWidget::set_grid_visible(bool visible) { grid_visible_ = visible; update(); }
void QtCanvasWidget::set_checker_visible(bool visible) { checker_visible_ = visible; update(); }
void QtCanvasWidget::set_onion_visible(bool visible) { onion_visible_ = visible; update(); }
void QtCanvasWidget::notify_changed() { update(); if (editor_changed) editor_changed(); }

} // namespace px
