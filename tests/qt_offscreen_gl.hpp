// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <glad/gl.h>

#include <memory>

class QtOffscreenGlContext {
public:
    QtOffscreenGlContext() {
        if (QGuiApplication::instance() == nullptr) {
            static char application_name[] = "pixelart-gl-test";
            static char* arguments[] = {application_name, nullptr};
            static int argument_count = 1;
            application_ = std::make_unique<QGuiApplication>(argument_count, arguments);
        }
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        surface_.setFormat(format);
        surface_.create();
        context_.setFormat(format);
        if (!surface_.isValid() || !context_.create() || !context_.makeCurrent(&surface_)) return;
        active_context_ = &context_;
        ready_ = gladLoadGL(load_proc) != 0;
    }

    ~QtOffscreenGlContext() {
        if (ready_) gladLoaderUnloadGL();
        context_.doneCurrent();
        active_context_ = nullptr;
    }

    QtOffscreenGlContext(const QtOffscreenGlContext&) = delete;
    QtOffscreenGlContext& operator=(const QtOffscreenGlContext&) = delete;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    static void* load_proc(const char* name) {
        return reinterpret_cast<void*>(active_context_->getProcAddress(name));
    }

    inline static QOpenGLContext* active_context_ = nullptr;
    std::unique_ptr<QGuiApplication> application_;
    QOffscreenSurface surface_;
    QOpenGLContext context_;
    bool ready_ = false;
};
