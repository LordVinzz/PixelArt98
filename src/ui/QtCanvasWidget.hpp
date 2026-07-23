// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "ui/EditorController.hpp"
#include "ui/TextRasterizer.hpp"

#include <QOpenGLWidget>
#include <QPoint>
#include <QRect>
#include <QRectF>

#include <functional>
#include <optional>

class QEvent;
class QPainter;
class QTabletEvent;

namespace px {

class QtCanvasWidget final : public QOpenGLWidget {
public:
    explicit QtCanvasWidget(EditorController& controller, QWidget* parent = nullptr);

    void set_grid_visible(bool visible);
    void set_checker_visible(bool visible);
    void set_onion_visible(bool visible);
    void zoom_in();
    void zoom_out();
    void actual_size();
    void fit_to_canvas();
    [[nodiscard]] double zoom() const noexcept { return zoom_; }
    [[nodiscard]] bool onion_visible() const noexcept { return onion_visible_; }
    void set_raster_text_preview(int x, int y, RasterTextImage preview);
    void clear_raster_text_preview();
    void set_raster_text_box(int x, int y, int width, int height);
    void clear_raster_text_box();
    [[nodiscard]] bool has_raster_text_preview() const noexcept {
        return !raster_text_preview_.pixels.empty();
    }
    [[nodiscard]] bool has_raster_text_box() const noexcept {
        return raster_text_box_active_;
    }
    [[nodiscard]] QRect raster_text_box() const noexcept {
        return QRect(raster_text_box_x_, raster_text_box_y_,
                     raster_text_box_width_, raster_text_box_height_);
    }
    [[nodiscard]] const RasterTextImage& raster_text_preview() const noexcept {
        return raster_text_preview_;
    }
    [[nodiscard]] int raster_text_preview_revision() const noexcept {
        return raster_text_preview_revision_;
    }
    static void paint_selection_transform_frame(QPainter& painter, const QRectF& box);
    std::function<void()> editor_changed;
    std::function<void(std::optional<QPoint>)> pointer_coordinates_changed;
    std::function<void()> selection_geometry_changed;
    std::function<void(std::optional<QRect>)> selection_preview_changed;
    std::function<void(int, int, bool)> text_requested;
    std::function<void(int, int, int, int)> raster_text_box_changed;
    std::function<void()> raster_text_box_resize_finished;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF image_rect() const;
    [[nodiscard]] QPoint pixel_at(const QPointF& position) const;
    [[nodiscard]] bool valid_pixel(const QPoint& pixel) const;
    [[nodiscard]] std::optional<QRect> active_selection_preview() const;
    [[nodiscard]] std::optional<QRect> selection_transform_bounds() const;
    [[nodiscard]] SelectionTransformHandle selection_transform_handle_at(const QPointF& position) const;
    [[nodiscard]] SelectionTransformHandle raster_text_box_handle_at(const QPointF& position) const;
    void draw_selection_transform_handles(QPainter& painter) const;
    void draw_raster_text_box_handles(QPainter& painter) const;
    void update_raster_text_box_resize(int x, int y);
    void notify_pointer_coordinates(const QPointF& position);
    void notify_selection_preview();
    [[nodiscard]] SelectionCombineMode selection_mode(Qt::KeyboardModifiers modifiers, bool secondary) const;
    void change_zoom(double next_zoom, const QPointF& anchor);
    void notify_changed();

    EditorController& controller_;
    double zoom_ = 12.0;
    QPointF pan_;
    QPointF pan_anchor_;
    QPointF pan_start_;
    QPoint drag_start_pixel_;
    QPoint drag_current_pixel_;
    bool grid_visible_ = true;
    bool checker_visible_ = true;
    bool onion_visible_ = true;
    bool drawing_ = false;
    bool transforming_selection_ = false;
    bool resizing_raster_text_box_ = false;
    bool panning_ = false;
    bool fit_requested_ = false;
    bool space_down_ = false;
    int raster_text_preview_x_ = 0;
    int raster_text_preview_y_ = 0;
    int raster_text_preview_revision_ = 0;
    RasterTextImage raster_text_preview_;
    bool raster_text_box_active_ = false;
    int raster_text_box_x_ = 0;
    int raster_text_box_y_ = 0;
    int raster_text_box_width_ = 1;
    int raster_text_box_height_ = 1;
    SelectionTransformHandle raster_text_box_handle_ = SelectionTransformHandle::None;
    QRect raster_text_box_before_resize_;
};

} // namespace px
