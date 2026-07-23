// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/GpuEffectRenderer.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <string>

namespace px {

namespace {

int gpu_effect_mode_value(GpuEffectMode mode) {
    return static_cast<int>(mode);
}

float channel_to_float(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

std::array<float, 4> pixel_to_float4(Pixel pixel) {
    return {channel_to_float(r(pixel)), channel_to_float(g(pixel)), channel_to_float(b(pixel)), channel_to_float(a(pixel))};
}

void upload_curve_uniforms(unsigned int program,
                           const char* first_name,
                           const char* second_name,
                           const std::array<float, kMaxCurvePoints>& values) {
    glUniform4f(glGetUniformLocation(program, first_name), values[0], values[1], values[2], values[3]);
    glUniform4f(glGetUniformLocation(program, second_name), values[4], values[5], values[6], values[7]);
}

std::uint64_t texture_footprint(int width, int height, std::uint64_t texture_count) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(Pixel) * texture_count;
}

bool needs_full_image_coordinates(GpuEffectMode mode) {
    switch (mode) {
        case GpuEffectMode::RadialBlur:
        case GpuEffectMode::ZoomBlur:
        case GpuEffectMode::Pixelate:
        case GpuEffectMode::Crystalize:
        case GpuEffectMode::FrostedGlass:
        case GpuEffectMode::Bulge:
        case GpuEffectMode::Twist:
        case GpuEffectMode::TileReflection:
        case GpuEffectMode::Dents:
        case GpuEffectMode::PolarInversion:
        case GpuEffectMode::AddNoise:
        case GpuEffectMode::Vignette:
        case GpuEffectMode::Clouds:
        case GpuEffectMode::JuliaFractal:
        case GpuEffectMode::MandelbrotFractal:
        case GpuEffectMode::Turbulence:
        case GpuEffectMode::AffineTransform:
            return true;
        default:
            return false;
    }
}

int clamped_radius(float value, int fallback, int max_value) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(static_cast<int>(std::ceil(std::abs(value))), fallback, max_value);
}

std::uint64_t conservative_gl_budget_for_texture_limit(int max_texture_size) {
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    if (max_texture_size <= 0) return 0;
    if (max_texture_size <= 4096) return 64ULL * mib;
    if (max_texture_size <= 8192) return 128ULL * mib;
    if (max_texture_size <= 16384) return 256ULL * mib;
    return 512ULL * mib;
}

Document extract_tile_document(const Document& document,
                               int core_x,
                               int core_y,
                               int core_w,
                               int core_h,
                               int halo) {
    const int left = std::min(halo, core_x);
    const int top = std::min(halo, core_y);
    const int right = std::min(halo, document.width - (core_x + core_w));
    const int bottom = std::min(halo, document.height - (core_y + core_h));
    const int tile_x = core_x - left;
    const int tile_y = core_y - top;
    const int tile_w = core_w + left + right;
    const int tile_h = core_h + top + bottom;

    Document tile = Document::create(tile_w, tile_h);
    const auto& source = document.active_cel().pixels;
    auto& dest = tile.active_cel().pixels;
    for (int y = 0; y < tile_h; ++y) {
        const int source_y = tile_y + y;
        for (int x = 0; x < tile_w; ++x) {
            const int source_x = tile_x + x;
            dest[static_cast<std::size_t>(y * tile_w + x)] =
                source[static_cast<std::size_t>(source_y * document.width + source_x)];
        }
    }

    const std::size_t expected_mask_size = static_cast<std::size_t>(document.width * document.height);
    if (document.selection.active && document.selection.mask.size() == expected_mask_size) {
        tile.selection.resize(tile_w, tile_h);
        tile.selection.active = true;
        for (int y = 0; y < tile_h; ++y) {
            const int source_y = tile_y + y;
            for (int x = 0; x < tile_w; ++x) {
                const int source_x = tile_x + x;
                tile.selection.mask[static_cast<std::size_t>(y * tile_w + x)] =
                    document.selection.mask[static_cast<std::size_t>(source_y * document.width + source_x)];
            }
        }
    }
    return tile;
}

std::vector<Pixel> extract_tile_pixels(const std::vector<Pixel>& pixels,
                                       int width,
                                       int height,
                                       int core_x,
                                       int core_y,
                                       int core_w,
                                       int core_h,
                                       int halo) {
    const int left = std::min(halo, core_x);
    const int top = std::min(halo, core_y);
    const int right = std::min(halo, width - (core_x + core_w));
    const int bottom = std::min(halo, height - (core_y + core_h));
    const int tile_x = core_x - left;
    const int tile_y = core_y - top;
    const int tile_w = core_w + left + right;
    const int tile_h = core_h + top + bottom;
    std::vector<Pixel> tile(static_cast<std::size_t>(tile_w * tile_h), 0);
    for (int y = 0; y < tile_h; ++y) {
        for (int x = 0; x < tile_w; ++x) {
            tile[static_cast<std::size_t>(y * tile_w + x)] =
                pixels[static_cast<std::size_t>((tile_y + y) * width + tile_x + x)];
        }
    }
    return tile;
}

const char* shader_type_name(unsigned int type) {
    return type == GL_VERTEX_SHADER ? "vertex" : "fragment";
}

unsigned int compile_shader(unsigned int type, const char* source, std::string& error) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        error = std::string("GPU effect ") + shader_type_name(type) + " shader compile failed: " + log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// OpenGL backend program: shader sources stay separate from resource orchestration.
#include "detail/GpuEffectShaders.inc"

} // namespace

GpuEffectRenderer::~GpuEffectRenderer() {
    destroy();
}

const char* GpuEffectRenderer::vertex_shader_source() {
    return kVertexShader;
}

const char* GpuEffectRenderer::fragment_shader_source() {
    return combined_fragment_shader_source().c_str();
}

void GpuEffectRenderer::set_error(std::string value) {
    last_error_ = std::move(value);
}

GpuBackendCapabilities GpuEffectRenderer::capabilities() const {
    GpuBackendCapabilities result;
    int max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    result.max_texture_size = max_texture_size;
    result.working_texture_budget = conservative_gl_budget_for_texture_limit(result.max_texture_size);
    result.supports_chunking = result.max_texture_size > 0;
    return result;
}

bool GpuEffectRenderer::effect_supports_chunking(const GpuEffectRequest& request) {
    return !needs_full_image_coordinates(request.mode);
}

int GpuEffectRenderer::effect_chunk_halo(const GpuEffectRequest& request) {
    switch (request.mode) {
        case GpuEffectMode::OilPainting:
        case GpuEffectMode::GaussianBlur:
        case GpuEffectMode::MedianBlur:
        case GpuEffectMode::SurfaceBlur:
        case GpuEffectMode::ReduceNoise:
        case GpuEffectMode::Glow:
        case GpuEffectMode::SoftenPortrait:
            return clamped_radius(request.params[0], 1, 16);
        case GpuEffectMode::DepthOfField:
            return clamped_radius(request.params[3], 1, 32);
        case GpuEffectMode::MotionBlur:
            return clamped_radius(request.params[0], 1, 24);
        case GpuEffectMode::InkSketch:
        case GpuEffectMode::PencilSketch:
            return std::max(1, clamped_radius(request.params[0] * 0.5f, 1, 16));
        case GpuEffectMode::Sharpen:
        case GpuEffectMode::EdgeDetect:
        case GpuEffectMode::Emboss:
        case GpuEffectMode::Outline:
        case GpuEffectMode::Relief:
            return 1;
        default:
            return 0;
    }
}

int GpuEffectRenderer::choose_chunk_extent(int width,
                                           int height,
                                           int halo,
                                           const GpuBackendCapabilities& caps) {
    if (width <= 0 || height <= 0 || !caps.supports_chunking) {
        return 0;
    }

    const int max_texture_size = caps.max_texture_size > 0 ? caps.max_texture_size : std::max(width, height);
    const int max_core_by_texture = std::max(1, max_texture_size - halo * 2);
    if (max_core_by_texture < 128) {
        return max_core_by_texture;
    }
    if (caps.working_texture_budget == 0) {
        return std::min({max_core_by_texture, width, height});
    }

    constexpr std::uint64_t textures_per_pass = 4;
    const double max_pixels = static_cast<double>(caps.working_texture_budget) /
                              static_cast<double>(sizeof(Pixel) * textures_per_pass);
    int extent = static_cast<int>(std::floor(std::sqrt(std::max(1.0, max_pixels)))) - halo * 2;
    extent = std::clamp(extent, 128, max_core_by_texture);
    return std::min({extent, std::max(width, height), max_core_by_texture});
}

bool GpuEffectRenderer::ensure_program() {
    if (shader_program_ != 0) {
        return true;
    }

    unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, kVertexShader, last_error_);
    if (vertex_shader == 0) {
        return false;
    }
    unsigned int fragment_shader =
        compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source(), last_error_);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return false;
    }

    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex_shader);
    glAttachShader(shader_program_, fragment_shader);
    glLinkProgram(shader_program_);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    int ok = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &ok);
    if (ok == 0) {
        char log[1024] = {};
        glGetProgramInfoLog(shader_program_, sizeof(log), nullptr, log);
        set_error(std::string("GPU effect shader link failed: ") + log);
        glDeleteProgram(shader_program_);
        shader_program_ = 0;
        return false;
    }
    return true;
}

bool GpuEffectRenderer::ensure_geometry() {
    if (vertex_array_ != 0 && vertex_buffer_ != 0) {
        return true;
    }
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(vertices)), vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<GLsizei>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<GLsizei>(sizeof(float)),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    return true;
}

bool GpuEffectRenderer::ensure_texture(unsigned int& texture, int width, int height, const Pixel* pixels) {
    if (width <= 0 || height <= 0) {
        set_error("GPU effect texture has invalid dimensions");
        return false;
    }
    if (texture == 0) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return true;
}

bool GpuEffectRenderer::ensure_target(int width, int height) {
    int max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    if (max_texture_size > 0 && (width > max_texture_size || height > max_texture_size)) {
        set_error("GPU effect skipped: image exceeds GL_MAX_TEXTURE_SIZE");
        return false;
    }
    if (!ensure_texture(output_texture_, width, height, nullptr)) {
        return false;
    }
    if (framebuffer_ == 0) {
        glGenFramebuffers(1, &framebuffer_);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "GPU effect framebuffer incomplete: 0x%04x", static_cast<unsigned int>(status));
        set_error(buffer);
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void GpuEffectRenderer::upload_selection_mask(const Document& document) {
    const std::size_t size = static_cast<std::size_t>(document.width * document.height);
    mask_pixels_.assign(size, rgba(255, 255, 255, 255));
    if (document.selection.active && document.selection.mask.size() == size) {
        for (std::size_t i = 0; i < size; ++i) {
            const std::uint8_t value = document.selection.mask[i] != 0 ? 255 : 0;
            mask_pixels_[i] = rgba(value, value, value, 255);
        }
    }
    ensure_texture(mask_texture_, document.width, document.height, mask_pixels_.data());
}

bool GpuEffectRenderer::render_active_cel(const Document& document,
                                          const GpuEffectRequest& request,
                                          const GpuBackendCapabilities* capability_override) {
    last_error_.clear();
    if (!document.valid()) {
        set_error("GPU effect skipped: document is invalid");
        return false;
    }
    const GpuBackendCapabilities caps = capability_override != nullptr ? *capability_override : capabilities();
    const std::uint64_t full_footprint = texture_footprint(document.width, document.height, request.mode == GpuEffectMode::DepthOfField ? 4 : 3);
    const bool exceeds_texture_size = caps.max_texture_size > 0 &&
                                      (document.width > caps.max_texture_size || document.height > caps.max_texture_size);
    const bool exceeds_budget = caps.working_texture_budget > 0 && full_footprint > caps.working_texture_budget;
    if (exceeds_texture_size || exceeds_budget) {
        if (!effect_supports_chunking(request)) {
            set_error("GPU effect skipped: this effect requires full-image coordinates and the image exceeds the current GPU budget");
            return false;
        }
        return render_chunked_active_cel(document, request, caps);
    }
    chunked_output_.clear();
    used_chunking_ = false;
    return render_full_active_cel(document, request);
}

bool GpuEffectRenderer::render_full_active_cel(const Document& document, const GpuEffectRequest& request) {
    if (!ensure_program() || !ensure_geometry() || !ensure_target(document.width, document.height)) {
        return false;
    }

    const auto& pixels = document.active_cel().pixels;
    if (pixels.size() != static_cast<std::size_t>(document.width * document.height)) {
        set_error("GPU effect skipped: active cel pixel buffer has the wrong size");
        return false;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(document.width * document.height);
    if (request.mode == GpuEffectMode::DepthOfField && request.depth_pixels.size() != pixel_count) {
        set_error("GPU depth of field skipped: depth pixel buffer has the wrong size");
        return false;
    }
    if (!ensure_texture(source_texture_, document.width, document.height, pixels.data())) {
        return false;
    }
    if (request.mode == GpuEffectMode::DepthOfField &&
        !ensure_texture(depth_texture_, document.width, document.height, request.depth_pixels.data())) {
        return false;
    }
    upload_selection_mask(document);

    int previous_fbo = 0;
    int previous_viewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGetIntegerv(GL_VIEWPORT, previous_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, document.width, document.height);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shader_program_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "u_source"), 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, depth_texture_ != 0 ? depth_texture_ : source_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "u_depth"), 1);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, mask_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "u_mask"), 2);

    const auto primary = pixel_to_float4(request.primary);
    const auto secondary = pixel_to_float4(request.secondary);
    glUniform1i(glGetUniformLocation(shader_program_, "u_mode"), gpu_effect_mode_value(request.mode));
    glUniform4f(glGetUniformLocation(shader_program_, "u_size"),
                static_cast<float>(document.width),
                static_cast<float>(document.height),
                1.0f / static_cast<float>(document.width),
                1.0f / static_cast<float>(document.height));
    glUniform4f(glGetUniformLocation(shader_program_, "u_params"),
                request.params[0], request.params[1], request.params[2], request.params[3]);
    glUniform4f(glGetUniformLocation(shader_program_, "u_params2"),
                request.params2[0], request.params2[1], request.params2[2], request.params2[3]);
    glUniform1i(glGetUniformLocation(shader_program_, "u_curve_point_count"), request.curve_point_count);
    upload_curve_uniforms(shader_program_, "u_curve_x0", "u_curve_x1", request.curve_x);
    upload_curve_uniforms(shader_program_, "u_curve_y0", "u_curve_y1", request.curve_y);
    glUniform4f(glGetUniformLocation(shader_program_, "u_primary"), primary[0], primary[1], primary[2], primary[3]);
    glUniform4f(glGetUniformLocation(shader_program_, "u_secondary"), secondary[0], secondary[1], secondary[2], secondary[3]);

    glBindVertexArray(vertex_array_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous_fbo));
    glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
    return true;
}

bool GpuEffectRenderer::render_chunked_active_cel(const Document& document,
                                                  const GpuEffectRequest& request,
                                                  const GpuBackendCapabilities& caps) {
    if (!ensure_program() || !ensure_geometry()) {
        return false;
    }
    const auto& source = document.active_cel().pixels;
    const std::size_t pixel_count = static_cast<std::size_t>(document.width * document.height);
    if (source.size() != pixel_count) {
        set_error("GPU effect skipped: active cel pixel buffer has the wrong size");
        return false;
    }
    if (request.mode == GpuEffectMode::DepthOfField && request.depth_pixels.size() != pixel_count) {
        set_error("GPU depth of field skipped: depth pixel buffer has the wrong size");
        return false;
    }

    const int halo = effect_chunk_halo(request);
    const int chunk_extent = choose_chunk_extent(document.width, document.height, halo, caps);
    if (chunk_extent <= 0) {
        set_error("GPU effect skipped: could not choose a valid GPU chunk size");
        return false;
    }

    used_chunking_ = false;
    chunked_output_.assign(pixel_count, rgba(0, 0, 0, 0));
    std::vector<Pixel> tile_output;
    for (int y = 0; y < document.height; y += chunk_extent) {
        const int core_h = std::min(chunk_extent, document.height - y);
        for (int x = 0; x < document.width; x += chunk_extent) {
            const int core_w = std::min(chunk_extent, document.width - x);
            const int left = std::min(halo, x);
            const int top = std::min(halo, y);
            Document tile = extract_tile_document(document, x, y, core_w, core_h, halo);
            GpuEffectRequest tile_request = request;
            if (request.mode == GpuEffectMode::DepthOfField) {
                tile_request.depth_pixels = extract_tile_pixels(request.depth_pixels, document.width, document.height, x, y, core_w, core_h, halo);
            }
            if (!render_full_active_cel(tile, tile_request) || !read_output_pixels(tile_output)) {
                if (last_error_.empty()) {
                    set_error("GPU effect skipped: chunk render failed");
                }
                chunked_output_.clear();
                used_chunking_ = false;
                return false;
            }
            for (int row = 0; row < core_h; ++row) {
                const std::size_t src_offset = static_cast<std::size_t>((row + top) * tile.width + left);
                const std::size_t dst_offset = static_cast<std::size_t>((y + row) * document.width + x);
                std::copy_n(tile_output.begin() + static_cast<std::ptrdiff_t>(src_offset),
                            static_cast<std::size_t>(core_w),
                            chunked_output_.begin() + static_cast<std::ptrdiff_t>(dst_offset));
            }
        }
    }

    width_ = document.width;
    height_ = document.height;
    used_chunking_ = true;
    return true;
}

bool GpuEffectRenderer::read_output_pixels(std::vector<Pixel>& pixels) const {
    if (used_chunking_ && !chunked_output_.empty() && width_ > 0 && height_ > 0 &&
        chunked_output_.size() == static_cast<std::size_t>(width_ * height_)) {
        pixels = chunked_output_;
        return true;
    }
    if (output_texture_ == 0 || width_ <= 0 || height_ <= 0) {
        return false;
    }
    pixels.assign(static_cast<std::size_t>(width_ * height_), 0);
    glBindTexture(GL_TEXTURE_2D, output_texture_);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return true;
}

void GpuEffectRenderer::destroy() {
    if (framebuffer_ != 0) glDeleteFramebuffers(1, &framebuffer_);
    if (output_texture_ != 0) glDeleteTextures(1, &output_texture_);
    if (source_texture_ != 0) glDeleteTextures(1, &source_texture_);
    if (depth_texture_ != 0) glDeleteTextures(1, &depth_texture_);
    if (mask_texture_ != 0) glDeleteTextures(1, &mask_texture_);
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) glDeleteVertexArrays(1, &vertex_array_);
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    framebuffer_ = 0;
    output_texture_ = 0;
    source_texture_ = 0;
    depth_texture_ = 0;
    mask_texture_ = 0;
    vertex_buffer_ = 0;
    vertex_array_ = 0;
    shader_program_ = 0;
    width_ = 0;
    height_ = 0;
    chunked_output_.clear();
    used_chunking_ = false;
}

} // namespace px
