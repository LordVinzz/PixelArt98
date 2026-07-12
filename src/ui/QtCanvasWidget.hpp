// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "ui/EditorController.hpp"

#include <QOpenGLWidget>
#include <QPoint>

#include <functional>

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
    std::function<void()> editor_changed;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF image_rect() const;
    [[nodiscard]] QPoint pixel_at(const QPointF& position) const;
    [[nodiscard]] bool valid_pixel(const QPoint& pixel) const;
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
    bool panning_ = false;
    bool fit_requested_ = false;
    bool space_down_ = false;
};

} // namespace px
