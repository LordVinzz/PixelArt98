// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include <QByteArray>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QtGlobal>

#include <glad/gl.h>

#include <cctype>
#include <cstdlib>
#include <memory>

class QtOffscreenGlContext {
public:
    QtOffscreenGlContext() {
#if defined(__linux__)
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
        }
#endif
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

inline int qt_offscreen_glsl_version_number() {
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (version == nullptr) {
        return 0;
    }
    const char* cursor = version;
    while (*cursor != '\0' && !std::isdigit(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
    if (*cursor == '\0') {
        return 0;
    }
    const int major = std::atoi(cursor);
    while (std::isdigit(static_cast<unsigned char>(*cursor))) {
        ++cursor;
    }
    if (*cursor == '.') {
        ++cursor;
    }
    const int minor = std::atoi(cursor);
    return major * 100 + minor;
}

inline bool qt_offscreen_gl_supports_glsl_330() {
    return qt_offscreen_glsl_version_number() >= 330;
}
