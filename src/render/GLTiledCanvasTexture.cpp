// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/GLTiledCanvasTexture.hpp"

#include "core/MemoryTrace.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace px {

namespace {

constexpr const char* kTiledCanvasVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)GLSL";

constexpr const char* kTiledCanvasFragmentShader = R"GLSL(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_texture;
out vec4 frag_color;
void main() {
    frag_color = texture(u_texture, v_uv);
}
)GLSL";

int ceil_div(int value, int divisor) {
    return (value + divisor - 1) / divisor;
}

Pixel average_pixels(const std::vector<Pixel>& pixels, int width, int height, int x, int y) {
    const int x0 = std::min(width - 1, x * 2);
    const int y0 = std::min(height - 1, y * 2);
    const int x1 = std::min(width - 1, x0 + 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const Pixel p0 = pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x0)];
    const Pixel p1 = pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x1)];
    const Pixel p2 = pixels[static_cast<std::size_t>(y1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x0)];
    const Pixel p3 = pixels[static_cast<std::size_t>(y1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x1)];
    return rgba(static_cast<std::uint8_t>((static_cast<unsigned int>(r(p0)) + r(p1) + r(p2) + r(p3) + 2U) / 4U),
                static_cast<std::uint8_t>((static_cast<unsigned int>(g(p0)) + g(p1) + g(p2) + g(p3) + 2U) / 4U),
                static_cast<std::uint8_t>((static_cast<unsigned int>(b(p0)) + b(p1) + b(p2) + b(p3) + 2U) / 4U),
                static_cast<std::uint8_t>((static_cast<unsigned int>(a(p0)) + a(p1) + a(p2) + a(p3) + 2U) / 4U));
}

int pyramid_level_count(int width, int height) {
    constexpr int kMinimumLargestDimension = 512;
    int levels = 0;
    while (std::max(width, height) > kMinimumLargestDimension) {
        width = std::max(1, ceil_div(width, 2));
        height = std::max(1, ceil_div(height, 2));
        ++levels;
    }
    return levels;
}

} // namespace

std::size_t GLTiledCanvasTexture::CpuPyramid::pixel_count() const {
    std::size_t total = 0;
    for (const CpuPyramidLevel& level : levels) {
        total += level.pixels.size();
    }
    return total;
}

GLTiledCanvasTexture::~GLTiledCanvasTexture() {
    destroy();
}

const char* GLTiledCanvasTexture::vertex_shader_source() {
    return kTiledCanvasVertexShader;
}

const char* GLTiledCanvasTexture::fragment_shader_source() {
    return kTiledCanvasFragmentShader;
}

bool GLTiledCanvasTexture::should_use_pyramid(int document_width, int document_height) {
    if (document_width <= 0 || document_height <= 0) {
        return false;
    }
    return static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height) >= kPyramidMinPixels;
}

GLTiledCanvasTexture::CpuPyramid GLTiledCanvasTexture::build_cpu_pyramid(int document_width,
                                                                         int document_height,
                                                                         const std::vector<Pixel>& pixels,
                                                                         const PyramidProgressCallback& progress) {
    return build_cpu_pyramid_with_min_pixels(document_width, document_height, pixels, kPyramidMinPixels, progress);
}

GLTiledCanvasTexture::CpuPyramid GLTiledCanvasTexture::build_cpu_pyramid_with_min_pixels(int document_width,
                                                                                         int document_height,
                                                                                         const std::vector<Pixel>& pixels,
                                                                                         std::size_t min_pixels,
                                                                                         const PyramidProgressCallback& progress) {
    MemoryTraceScope trace("GLTiledCanvasTexture::build_cpu_pyramid");
    memory_trace_vector("build_cpu_pyramid.source", pixels);
    CpuPyramid pyramid;
    pyramid.width = document_width;
    pyramid.height = document_height;
    const std::size_t expected_size = static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height);
    if (document_width <= 0 || document_height <= 0 || expected_size < min_pixels || pixels.size() != expected_size) {
        return pyramid;
    }

    const int total = pyramid_level_count(document_width, document_height);
    int previous_width = document_width;
    int previous_height = document_height;
    const std::vector<Pixel>* previous_pixels = &pixels;
    pyramid.levels.reserve(static_cast<std::size_t>(total));

    for (int level = 1; level <= total; ++level) {
        const int next_width = std::max(1, ceil_div(previous_width, 2));
        const int next_height = std::max(1, ceil_div(previous_height, 2));
        CpuPyramidLevel next;
        next.width = next_width;
        next.height = next_height;
        next.pixels.resize(static_cast<std::size_t>(next_width) * static_cast<std::size_t>(next_height));
        memory_trace_vector("build_cpu_pyramid.next_level_allocated", next.pixels);
        if (progress) {
            progress(level - 1, total, "Building image pyramid");
        }
        for (int y = 0; y < next_height; ++y) {
            for (int x = 0; x < next_width; ++x) {
                next.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(next_width) + static_cast<std::size_t>(x)] =
                    average_pixels(*previous_pixels, previous_width, previous_height, x, y);
            }
        }
        pyramid.levels.push_back(std::move(next));
        memory_trace_vector("build_cpu_pyramid.stored_level", pyramid.levels.back().pixels);
        previous_width = pyramid.levels.back().width;
        previous_height = pyramid.levels.back().height;
        previous_pixels = &pyramid.levels.back().pixels;
        if (progress) {
            progress(level, total, "Building image pyramid");
        }
    }

    memory_trace_event("counter",
                       {},
                       "build_cpu_pyramid.total_pixels",
                       nullptr,
                       pyramid.pixel_count(),
                       pyramid.pixel_count(),
                       sizeof(Pixel));
    return pyramid;
}

void GLTiledCanvasTexture::invalidate() {
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
        for (Level& level : levels_) {
            for (Tile& tile : level.tiles) {
                tile.generation = 0;
            }
            if (level.downsample == 1) {
                level.source_generation = generation_;
            }
        }
    }
    if (!levels_.empty()) {
        levels_.front().source_generation = generation_;
    }
}

void GLTiledCanvasTexture::upload_region_now(int document_width,
                                             int document_height,
                                             const std::vector<Pixel>& pixels,
                                             int x,
                                             int y,
                                             int width,
                                             int height) {
    MemoryTraceScope trace("GLTiledCanvasTexture::upload_region_now");
    memory_trace_vector("upload_region_now.pixels", pixels);
    const std::size_t expected_size = static_cast<std::size_t>(document_width) * static_cast<std::size_t>(document_height);
    if (document_width <= 0 || document_height <= 0 || width <= 0 || height <= 0 || pixels.size() != expected_size) {
        return;
    }
    if (document_width != document_width_ || document_height != document_height_ || levels_.empty()) {
        reset_levels(document_width, document_height);
    }
    if (levels_.empty()) {
        return;
    }

    Level& level = levels_.front();
    if (level.downsample != 1 || level.columns <= 0 || level.rows <= 0) {
        return;
    }
    const int min_x = std::clamp(x, 0, document_width);
    const int min_y = std::clamp(y, 0, document_height);
    const int max_x = std::clamp(x + width, 0, document_width);
    const int max_y = std::clamp(y + height, 0, document_height);
    if (max_x <= min_x || max_y <= min_y) {
        return;
    }

    const int first_tile_x = std::clamp(min_x / kTileSize, 0, level.columns - 1);
    const int first_tile_y = std::clamp(min_y / kTileSize, 0, level.rows - 1);
    const int last_tile_x = std::clamp((max_x - 1) / kTileSize, 0, level.columns - 1);
    const int last_tile_y = std::clamp((max_y - 1) / kTileSize, 0, level.rows - 1);
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            Tile& tile = tile_at(level, tile_x, tile_y);
            upload_tile(level, tile, pixels, document_width, generation_);
        }
    }
}

void GLTiledCanvasTexture::set_prepared_pyramid(CpuPyramid&& pyramid) {
    MemoryTraceScope trace("GLTiledCanvasTexture::set_prepared_pyramid");
    memory_trace_event("counter",
                       {},
                       "set_prepared_pyramid.source_total_pixels",
                       nullptr,
                       pyramid.pixel_count(),
                       pyramid.pixel_count(),
                       sizeof(Pixel));
    destroy();
    if (pyramid.width <= 0 || pyramid.height <= 0) {
        return;
    }
    document_width_ = pyramid.width;
    document_height_ = pyramid.height;
    add_level(document_width_, document_height_, 1, {}, generation_);
    int downsample = 2;
    for (CpuPyramidLevel& level : pyramid.levels) {
        memory_trace_vector("set_prepared_pyramid.source_level_before_move", level.pixels);
        add_level(level.width, level.height, downsample, std::move(level.pixels), generation_);
        downsample = std::min(downsample * 2, 1 << 30);
    }
    pyramid.levels.clear();
    memory_trace_event("counter",
                       {},
                       "set_prepared_pyramid.stored_total_pixels",
                       nullptr,
                       prepared_pyramid_pixel_count(),
                       prepared_pyramid_pixel_count(),
                       sizeof(Pixel));
}

void GLTiledCanvasTexture::clear_prepared_pyramid() {
    const int width = document_width_;
    const int height = document_height_;
    destroy();
    if (width > 0 && height > 0) {
        reset_levels(width, height);
    }
}

bool GLTiledCanvasTexture::has_prepared_pyramid() const {
    return levels_.size() > 1U;
}

std::size_t GLTiledCanvasTexture::prepared_pyramid_pixel_count() const {
    std::size_t total = 0;
    for (std::size_t i = 1; i < levels_.size(); ++i) {
        total += levels_[i].pixels.size();
    }
    return total;
}

std::size_t GLTiledCanvasTexture::scratch_capacity_pixels() const {
    return scratch_.capacity();
}

void GLTiledCanvasTexture::destroy() {
    for (Level& level : levels_) {
        for (Tile& tile : level.tiles) {
            if (tile.texture_id != 0) {
                glDeleteTextures(1, &tile.texture_id);
                tile.texture_id = 0;
            }
        }
    }
    levels_.clear();
    upload_queue_.clear();
    scratch_.clear();
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) glDeleteVertexArrays(1, &vertex_array_);
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    vertex_buffer_ = 0;
    vertex_array_ = 0;
    shader_program_ = 0;
    document_width_ = 0;
    document_height_ = 0;
    generation_ = 1;
    last_error_.clear();
}

GLTiledCanvasTexture::Tile& GLTiledCanvasTexture::tile_at(Level& level, int tile_x, int tile_y) {
    return level.tiles[static_cast<std::size_t>(tile_y * level.columns + tile_x)];
}

const GLTiledCanvasTexture::Tile& GLTiledCanvasTexture::tile_at(const Level& level, int tile_x, int tile_y) const {
    return level.tiles[static_cast<std::size_t>(tile_y * level.columns + tile_x)];
}

void GLTiledCanvasTexture::reset_levels(int document_width, int document_height) {
    destroy();
    document_width_ = document_width;
    document_height_ = document_height;
    add_level(document_width_, document_height_, 1, {}, generation_);
}

void GLTiledCanvasTexture::add_level(int width,
                                     int height,
                                     int downsample,
                                     std::vector<Pixel> pixels,
                                     std::uint64_t source_generation) {
    Level level;
    level.width = width;
    level.height = height;
    level.downsample = std::max(1, downsample);
    level.columns = ceil_div(level.width, kTileSize);
    level.rows = ceil_div(level.height, kTileSize);
    level.source_generation = source_generation;
    level.pixels = std::move(pixels);
    level.tiles.resize(static_cast<std::size_t>(level.columns * level.rows));
    for (int tile_y = 0; tile_y < level.rows; ++tile_y) {
        for (int tile_x = 0; tile_x < level.columns; ++tile_x) {
            Tile& tile = tile_at(level, tile_x, tile_y);
            tile.x = tile_x * kTileSize;
            tile.y = tile_y * kTileSize;
            tile.width = std::min(kTileSize, level.width - tile.x);
            tile.height = std::min(kTileSize, level.height - tile.y);
        }
    }
    levels_.push_back(std::move(level));
}

int GLTiledCanvasTexture::choose_level(float zoom) const {
    if (levels_.empty()) {
        return 0;
    }
    int best = 0;
    float best_score = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(levels_.size()); ++i) {
        const float screen_pixels_per_level_pixel = zoom * static_cast<float>(levels_[static_cast<std::size_t>(i)].downsample);
        const float score = std::abs(std::log2(std::max(0.0001f, screen_pixels_per_level_pixel)));
        if (score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

std::uint64_t GLTiledCanvasTexture::target_generation_for_level(int level_index) const {
    if (level_index < 0 || level_index >= static_cast<int>(levels_.size())) {
        return 0;
    }
    const Level& level = levels_[static_cast<std::size_t>(level_index)];
    return level.downsample == 1 ? generation_ : level.source_generation;
}

void GLTiledCanvasTexture::enqueue_tile(int level_index, int tile_index) {
    for (const TileKey& key : upload_queue_) {
        if (key.level == level_index && key.tile == tile_index) {
            return;
        }
    }
    upload_queue_.push_back({level_index, tile_index});
}

bool GLTiledCanvasTexture::tile_ready(int level_index, int tile_index) const {
    if (level_index < 0 || level_index >= static_cast<int>(levels_.size())) {
        return false;
    }
    const Level& level = levels_[static_cast<std::size_t>(level_index)];
    if (tile_index < 0 || tile_index >= static_cast<int>(level.tiles.size())) {
        return false;
    }
    const Tile& tile = level.tiles[static_cast<std::size_t>(tile_index)];
    return tile.texture_id != 0 && tile.generation == target_generation_for_level(level_index);
}

void GLTiledCanvasTexture::process_upload_queue(const std::vector<Pixel>& base_pixels, int document_width) {
    MemoryTraceScope trace("GLTiledCanvasTexture::process_upload_queue");
    memory_trace_vector("process_upload_queue.base_pixels", base_pixels);
    const auto start = std::chrono::steady_clock::now();
    int uploaded = 0;
    auto it = upload_queue_.begin();
    while (it != upload_queue_.end() && uploaded < kMaxUploadsPerFrame) {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (uploaded > 0 && elapsed >= kUploadBudgetSeconds) {
            break;
        }
        if (it->level < 0 || it->level >= static_cast<int>(levels_.size())) {
            it = upload_queue_.erase(it);
            continue;
        }
        Level& level = levels_[static_cast<std::size_t>(it->level)];
        if (it->tile < 0 || it->tile >= static_cast<int>(level.tiles.size())) {
            it = upload_queue_.erase(it);
            continue;
        }
        Tile& tile = level.tiles[static_cast<std::size_t>(it->tile)];
        const std::uint64_t target_generation = target_generation_for_level(it->level);
        if (tile.generation == target_generation) {
            it = upload_queue_.erase(it);
            continue;
        }
        const std::vector<Pixel>& source = level.downsample == 1 ? base_pixels : level.pixels;
        const int source_width = level.downsample == 1 ? document_width : level.width;
        if (source.empty() || !upload_tile(level, tile, source, source_width, target_generation)) {
            it = upload_queue_.erase(it);
            continue;
        }
        ++uploaded;
        ++last_draw_stats_.tiles_uploaded;
        if (it->level > 0) {
            last_draw_stats_.lod_uploaded = true;
        }
        it = upload_queue_.erase(it);
    }
}

bool GLTiledCanvasTexture::upload_tile(Level& level,
                                       Tile& tile,
                                       const std::vector<Pixel>& source,
                                       int source_width,
                                       std::uint64_t generation) {
    MemoryTraceScope trace("GLTiledCanvasTexture::upload_tile");
    memory_trace_vector("upload_tile.source", source);
    if (tile.width <= 0 || tile.height <= 0 || source_width <= 0) {
        return false;
    }
    scratch_.resize(static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height));
    memory_trace_vector("upload_tile.scratch", scratch_);
    for (int row = 0; row < tile.height; ++row) {
        const std::size_t src = static_cast<std::size_t>(tile.y + row) * static_cast<std::size_t>(source_width) +
                                static_cast<std::size_t>(tile.x);
        const std::size_t dst = static_cast<std::size_t>(row) * static_cast<std::size_t>(tile.width);
        if (src + static_cast<std::size_t>(tile.width) > source.size()) {
            return false;
        }
        std::copy_n(source.data() + src, static_cast<std::size_t>(tile.width), scratch_.data() + dst);
    }

    if (tile.texture_id == 0) {
        glGenTextures(1, &tile.texture_id);
        glBindTexture(GL_TEXTURE_2D, tile.texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile.width, tile.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, tile.texture_id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tile.width, tile.height, GL_RGBA, GL_UNSIGNED_BYTE, scratch_.data());
    }
    tile.generation = generation;
    (void)level;
    return true;
}

bool GLTiledCanvasTexture::ensure_program() {
    if (shader_program_ != 0) return true;
    const auto compile = [this](unsigned int type, const char* source) {
        const unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        int ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok != 0) return shader;
        std::array<char, 1024> log{};
        glGetShaderInfoLog(shader, static_cast<int>(log.size()), nullptr, log.data());
        last_error_ = std::string("Tiled canvas shader compile failed: ") + log.data();
        glDeleteShader(shader);
        return 0U;
    };
    const unsigned int vertex = compile(GL_VERTEX_SHADER, vertex_shader_source());
    if (vertex == 0) return false;
    const unsigned int fragment = compile(GL_FRAGMENT_SHADER, fragment_shader_source());
    if (fragment == 0) {
        glDeleteShader(vertex);
        return false;
    }
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex);
    glAttachShader(shader_program_, fragment);
    glLinkProgram(shader_program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    int ok = 0;
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &ok);
    if (ok != 0) return true;
    std::array<char, 1024> log{};
    glGetProgramInfoLog(shader_program_, static_cast<int>(log.size()), nullptr, log.data());
    last_error_ = std::string("Tiled canvas shader link failed: ") + log.data();
    glDeleteProgram(shader_program_);
    shader_program_ = 0;
    return false;
}

bool GLTiledCanvasTexture::ensure_geometry() {
    if (vertex_array_ != 0 && vertex_buffer_ != 0) return true;
    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glBindVertexArray(vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(16 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * static_cast<int>(sizeof(float)),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    return vertex_array_ != 0 && vertex_buffer_ != 0;
}

void GLTiledCanvasTexture::draw_tile(const Level& level,
                                     const Tile& tile,
                                     float zoom,
                                     float target_left,
                                     float target_top,
                                     int viewport_width,
                                     int viewport_height) {
    const float document_x = static_cast<float>(tile.x * level.downsample);
    const float document_y = static_cast<float>(tile.y * level.downsample);
    const float document_right = static_cast<float>(
        std::min(document_width_, (tile.x + tile.width) * level.downsample));
    const float document_bottom = static_cast<float>(
        std::min(document_height_, (tile.y + tile.height) * level.downsample));
    const float left = target_left + document_x * zoom;
    const float top = target_top + document_y * zoom;
    const float right = target_left + document_right * zoom;
    const float bottom = target_top + document_bottom * zoom;
    const auto ndc_x = [viewport_width](float x) {
        return x * 2.0f / static_cast<float>(viewport_width) - 1.0f;
    };
    const auto ndc_y = [viewport_height](float y) {
        return 1.0f - y * 2.0f / static_cast<float>(viewport_height);
    };
    const float vertices[] = {
        ndc_x(left),  ndc_y(top),    0.0f, 0.0f,
        ndc_x(right), ndc_y(top),    1.0f, 0.0f,
        ndc_x(left),  ndc_y(bottom), 0.0f, 1.0f,
        ndc_x(right), ndc_y(bottom), 1.0f, 1.0f
    };
    glBindTexture(GL_TEXTURE_2D, tile.texture_id);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(vertices)), vertices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool GLTiledCanvasTexture::draw(const std::vector<Pixel>& base_pixels,
                                int document_width,
                                int document_height,
                                float zoom,
                                float target_left,
                                float target_top,
                                int viewport_width,
                                int viewport_height,
                                int visible_left,
                                int visible_top,
                                int visible_right,
                                int visible_bottom) {
    last_draw_stats_ = {};
    last_error_.clear();
    const std::size_t expected = static_cast<std::size_t>(std::max(0, document_width)) *
                                 static_cast<std::size_t>(std::max(0, document_height));
    if (document_width <= 0 || document_height <= 0 || zoom <= 0.0f ||
        viewport_width <= 0 || viewport_height <= 0 || base_pixels.size() != expected) {
        last_error_ = "Tiled canvas draw received invalid input";
        return false;
    }
    if (document_width != document_width_ || document_height != document_height_ || levels_.empty()) {
        reset_levels(document_width, document_height);
    }

    const int selected = choose_level(zoom);
    Level& level = levels_[static_cast<std::size_t>(selected)];
    last_draw_stats_.selected_level = selected;
    const int left = std::clamp(visible_left / level.downsample, 0, level.width);
    const int top = std::clamp(visible_top / level.downsample, 0, level.height);
    const int right = std::clamp(ceil_div(visible_right, level.downsample), left, level.width);
    const int bottom = std::clamp(ceil_div(visible_bottom, level.downsample), top, level.height);
    if (left >= right || top >= bottom) return false;

    const int first_tile_x = left / kTileSize;
    const int first_tile_y = top / kTileSize;
    const int last_tile_x = (right - 1) / kTileSize;
    const int last_tile_y = (bottom - 1) / kTileSize;
    upload_queue_.clear();
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            const int tile_index = tile_y * level.columns + tile_x;
            ++last_draw_stats_.visible_tiles;
            if (!tile_ready(selected, tile_index)) enqueue_tile(selected, tile_index);
        }
    }
    process_upload_queue(base_pixels, document_width);
    last_draw_stats_.pending_tiles = static_cast<int>(upload_queue_.size());
    if (!ensure_program() || !ensure_geometry()) return false;

    int previous_program = 0;
    int previous_array_buffer = 0;
    int previous_vertex_array = 0;
    int previous_active_texture = 0;
    int previous_texture_2d = 0;
    int previous_blend_src_rgb = 0;
    int previous_blend_dst_rgb = 0;
    int previous_blend_src_alpha = 0;
    int previous_blend_dst_alpha = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture_2d);
    glGetIntegerv(GL_BLEND_SRC_RGB, &previous_blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &previous_blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previous_blend_src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previous_blend_dst_alpha);
    const bool blend_enabled = glIsEnabled(GL_BLEND) != 0;
    const bool depth_enabled = glIsEnabled(GL_DEPTH_TEST) != 0;
    const bool scissor_enabled = glIsEnabled(GL_SCISSOR_TEST) != 0;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(shader_program_);
    glUniform1i(glGetUniformLocation(shader_program_, "u_texture"), 0);
    glBindVertexArray(vertex_array_);
    bool drew = false;
    for (int tile_y = first_tile_y; tile_y <= last_tile_y; ++tile_y) {
        for (int tile_x = first_tile_x; tile_x <= last_tile_x; ++tile_x) {
            const int tile_index = tile_y * level.columns + tile_x;
            if (!tile_ready(selected, tile_index)) continue;
            draw_tile(level, level.tiles[static_cast<std::size_t>(tile_index)], zoom,
                      target_left, target_top, viewport_width, viewport_height);
            drew = true;
        }
    }
    last_draw_stats_.drew_lod = drew && selected > 0;

    glBindVertexArray(static_cast<unsigned int>(previous_vertex_array));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned int>(previous_array_buffer));
    glUseProgram(static_cast<unsigned int>(previous_program));
    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previous_texture_2d));
    glActiveTexture(static_cast<unsigned int>(previous_active_texture));
    glBlendFuncSeparate(static_cast<unsigned int>(previous_blend_src_rgb),
                        static_cast<unsigned int>(previous_blend_dst_rgb),
                        static_cast<unsigned int>(previous_blend_src_alpha),
                        static_cast<unsigned int>(previous_blend_dst_alpha));
    if (!blend_enabled) glDisable(GL_BLEND);
    if (depth_enabled) glEnable(GL_DEPTH_TEST);
    if (scissor_enabled) glEnable(GL_SCISSOR_TEST);
    return drew;
}

} // namespace px
