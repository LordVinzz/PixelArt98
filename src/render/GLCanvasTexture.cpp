// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/GLCanvasTexture.hpp"

#include <glad/gl.h>

namespace px {

GLCanvasTexture::~GLCanvasTexture() {
    destroy();
}

void GLCanvasTexture::update(int width, int height, const std::vector<Pixel>& pixels) {
    if (width <= 0 || height <= 0 || pixels.empty()) {
        return;
    }
    if (texture_id_ == 0) {
        glGenTextures(1, &texture_id_);
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
}

void GLCanvasTexture::bind_nearest() const {
    if (texture_id_ == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void GLCanvasTexture::bind_linear_repeat() const {
    if (texture_id_ == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void GLCanvasTexture::destroy() {
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
        width_ = 0;
        height_ = 0;
    }
}

} // namespace px
