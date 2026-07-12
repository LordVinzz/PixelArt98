// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "render/GLCanvasTexture.hpp"
#include "render/Renderer3D.hpp"
#include "ui/EditorController.hpp"

#include <QImage>
#include <QOpenGLWidget>
#include <QPointF>

#include <functional>

namespace px {

class QtModelPreviewWidget final : public QOpenGLWidget {
public:
    explicit QtModelPreviewWidget(EditorController& controller, QWidget* parent = nullptr);
    ~QtModelPreviewWidget() override;
    std::function<void()> model_changed;

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    static void* load_gl_proc(const char* name);

    EditorController& controller_;
    GLCanvasTexture texture_;
    Renderer3D renderer_;
    ModelViewportState viewport_;
    QPointF drag_start_;
    float drag_yaw_ = 0.0f;
    float drag_pitch_ = 0.0f;
    bool ready_ = false;
    bool dragging_ = false;
};

} // namespace px
