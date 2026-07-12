// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/QtModelPreviewWidget.hpp"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QWheelEvent>

#include <glad/gl.h>

#include <algorithm>
#include <vector>

namespace px {

QtModelPreviewWidget::QtModelPreviewWidget(EditorController& controller, QWidget* parent)
    : QOpenGLWidget(parent), controller_(controller) {
    setMinimumSize(320, 240);
    setMouseTracking(true);
}

QtModelPreviewWidget::~QtModelPreviewWidget() {
    makeCurrent();
    renderer_.destroy();
    texture_.destroy();
    doneCurrent();
}

void* QtModelPreviewWidget::load_gl_proc(const char* name) {
    return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
}

void QtModelPreviewWidget::initializeGL() {
    ready_ = gladLoadGL(load_gl_proc) != 0 && renderer_.init();
}

void QtModelPreviewWidget::paintGL() {
    glViewport(0, 0, width(), height());
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!ready_) return;
    const auto& pixels = controller_.display_pixels();
    texture_.update(controller_.document().width, controller_.document().height, pixels);
    const bool rendered = renderer_.render_model_to_texture(controller_.model(), texture_.id(), controller_.document().width,
                                                            controller_.document().height, viewport_, std::max(1, width()),
                                                            std::max(1, height()), pixels);
    if (!rendered) return;
    std::vector<Pixel> output(static_cast<std::size_t>(width()) * static_cast<std::size_t>(height()));
    glBindTexture(GL_TEXTURE_2D, renderer_.texture_id());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, output.data());
    QImage image(reinterpret_cast<const uchar*>(output.data()), width(), height(), width() * static_cast<int>(sizeof(Pixel)), QImage::Format_RGBA8888);
    QPainter painter(this);
    painter.translate(0.0, static_cast<double>(height()));
    painter.scale(1.0, -1.0);
    painter.drawImage(rect(), image);
    painter.resetTransform();
    painter.setPen(QColor(235, 238, 243));
    painter.drawText(10, 20, tr("Drag to orbit, wheel to zoom, click to select a face"));
}

void QtModelPreviewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = false;
    drag_start_ = event->position();
    drag_yaw_ = viewport_.yaw_degrees;
    drag_pitch_ = viewport_.pitch_degrees;
}

void QtModelPreviewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    const QPointF delta = event->position() - drag_start_;
    if (delta.manhattanLength() > 2.0) dragging_ = true;
    viewport_.yaw_degrees = drag_yaw_ + static_cast<float>(delta.x()) * 0.45f;
    viewport_.pitch_degrees = std::clamp(drag_pitch_ + static_cast<float>(delta.y()) * 0.35f, -85.0f, 85.0f);
    update();
}

void QtModelPreviewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || dragging_ || !ready_) return;
    const FaceHit hit = renderer_.pick_face(controller_.model(), viewport_, width(), height(), static_cast<float>(event->position().x()), static_cast<float>(event->position().y()));
    if (hit.hit) {
        controller_.model().selected_cuboid = hit.cuboid;
        controller_.model().selected_face = hit.face;
        controller_.model().selected_mesh = hit.mesh;
        if (model_changed) model_changed();
        update();
    }
}

void QtModelPreviewWidget::wheelEvent(QWheelEvent* event) {
    viewport_.distance = std::clamp(viewport_.distance * (event->angleDelta().y() > 0 ? 0.88f : 1.12f), 2.0f, 300.0f);
    update();
    event->accept();
}

} // namespace px
