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
        case GpuEffectMode::Clouds:
        case GpuEffectMode::JuliaFractal:
        case GpuEffectMode::MandelbrotFractal:
        case GpuEffectMode::Turbulence:
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

constexpr const char* kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFragmentShader = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_source;
uniform sampler2D u_mask;
uniform int u_mode;
uniform vec4 u_size;
uniform vec4 u_params;
uniform vec4 u_params2;
uniform vec4 u_primary;
uniform vec4 u_secondary;

float luminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 rgb_to_hsv(vec3 c) {
    vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, k.wz), vec4(c.gb, k.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 0.00001;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv_to_rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 1.0 / 3.0, 2.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amp * value_noise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return value;
}

vec4 sample_source(vec2 uv) {
    return texture(u_source, clamp(uv, vec2(0.0), vec2(1.0)));
}

vec4 blur_box(vec2 uv, int radius) {
    vec4 sum = vec4(0.0);
    float count = 0.0;
    int r = clamp(radius, 1, 16);
    for (int y = -16; y <= 16; ++y) {
        for (int x = -16; x <= 16; ++x) {
            if (abs(x) <= r && abs(y) <= r) {
                sum += sample_source(uv + vec2(float(x) * u_size.z, float(y) * u_size.w));
                count += 1.0;
            }
        }
    }
    return sum / max(1.0, count);
}

vec4 motion_blur(vec2 uv, int radius, float angle) {
    vec2 dir = vec2(cos(angle), sin(angle));
    vec4 sum = vec4(0.0);
    float count = 0.0;
    int r = clamp(radius, 1, 24);
    for (int i = -24; i <= 24; ++i) {
        if (abs(i) <= r) {
            vec2 offset = dir * vec2(u_size.z, u_size.w) * float(i);
            sum += sample_source(uv + offset);
            count += 1.0;
        }
    }
    return sum / max(1.0, count);
}

vec4 radial_zoom_blur(vec2 uv, int amount, bool radial) {
    vec2 center = vec2(0.5);
    vec2 delta = uv - center;
    vec4 sum = vec4(0.0);
    float count = 0.0;
    float strength = clamp(float(amount) / 100.0, 0.0, 1.0);
    for (int i = 0; i < 16; ++i) {
        float t = (float(i) / 15.0 - 0.5) * strength;
        vec2 sample_uv = radial ? center + mat2(cos(t), -sin(t), sin(t), cos(t)) * delta
                                : uv - delta * t;
        sum += sample_source(sample_uv);
        count += 1.0;
    }
    return sum / max(1.0, count);
}

vec4 median_approx(vec2 uv, int radius) {
    vec4 c = sample_source(uv);
    vec4 b = blur_box(uv, radius);
    return mix(c, b, 0.45);
}

vec4 edge_color(vec2 uv) {
    float left = luminance(sample_source(uv + vec2(-u_size.z, 0.0)).rgb);
    float right = luminance(sample_source(uv + vec2(u_size.z, 0.0)).rgb);
    float up = luminance(sample_source(uv + vec2(0.0, -u_size.w)).rgb);
    float down = luminance(sample_source(uv + vec2(0.0, u_size.w)).rgb);
    float edge = clamp(abs(right - left) + abs(down - up), 0.0, 1.0);
    return vec4(vec3(edge), sample_source(uv).a);
}

vec4 apply_effect(vec2 uv, vec4 src) {
    vec2 pixel = uv * u_size.xy;
    int mode = u_mode;

    if (mode == 0) {
        float brightness = u_params.x;
        float contrast = 1.0 + u_params.y * 2.0;
        vec3 color = (src.rgb - 0.5) * contrast + 0.5 + brightness;
        return vec4(clamp(color, 0.0, 1.0), src.a);
    }
    if (mode == 1) {
        vec3 hsv = rgb_to_hsv(src.rgb);
        hsv.x = fract(hsv.x + u_params.x);
        hsv.y = clamp(hsv.y + u_params.y, 0.0, 1.0);
        hsv.z = clamp(hsv.z + u_params.z, 0.0, 1.0);
        return vec4(hsv_to_rgb(hsv), src.a);
    }
    if (mode == 2) {
        float in_black = clamp(u_params.x, 0.0, 0.99);
        float in_white = max(in_black + 0.01, u_params.y);
        float gamma = max(0.05, u_params.z);
        float out_black = clamp(u_params.w, 0.0, 1.0);
        float out_white = clamp(u_params2.x, 0.0, 1.0);
        vec3 normalized = clamp((src.rgb - in_black) / (in_white - in_black), 0.0, 1.0);
        normalized = pow(normalized, vec3(1.0 / gamma));
        return vec4(mix(vec3(out_black), vec3(out_white), normalized), src.a);
    }
    if (mode == 3 || mode == 4) {
        float steps = max(2.0, u_params.x);
        vec3 color = floor(src.rgb * (steps - 1.0) + 0.5) / (steps - 1.0);
        if (mode == 4) {
            float n = hash12(pixel) - 0.5;
            color = floor(clamp(src.rgb + n / steps, 0.0, 1.0) * (steps - 1.0) + 0.5) / (steps - 1.0);
        }
        return vec4(color, src.a);
    }
    if (mode == 5) {
        vec3 color = src.rgb;
        float lo = min(min(color.r, color.g), color.b);
        float hi = max(max(color.r, color.g), color.b);
        color = clamp((color - lo) / max(0.01, hi - lo), 0.0, 1.0);
        return vec4(color, src.a);
    }
    if (mode == 6) return vec4(vec3(luminance(src.rgb)), src.a);
    if (mode == 7) {
        return vec4(vec3(dot(src.rgb, vec3(0.393, 0.769, 0.189)),
                         dot(src.rgb, vec3(0.349, 0.686, 0.168)),
                         dot(src.rgb, vec3(0.272, 0.534, 0.131))), src.a);
    }
    if (mode == 8) return vec4(1.0 - src.rgb, src.a);
    if (mode == 9) return vec4(src.rgb, 1.0 - src.a);
    if (mode == 10) {
        float levels = max(2.0, u_params.x);
        return vec4(floor(src.rgb * (levels - 1.0) + 0.5) / (levels - 1.0), src.a);
    }
    if (mode == 11) return mix(src, blur_box(uv, int(u_params.x)), 0.65);
    if (mode == 12) {
        vec4 edge = edge_color(uv);
        vec4 soft = blur_box(uv, max(1, int(u_params.x / 2.0)));
        return vec4(mix(edge.rgb, soft.rgb, clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 13) {
        vec4 gray = vec4(vec3(luminance(src.rgb)), src.a);
        vec4 edge = edge_color(uv);
        return vec4(clamp(gray.rgb - edge.rgb * clamp(u_params.y / 100.0, 0.0, 1.0), 0.0, 1.0), src.a);
    }
    if (mode == 14) return blur_box(uv, int(u_params.x));
    if (mode == 15) return motion_blur(uv, int(u_params.x), u_params.z);
    if (mode == 16) return radial_zoom_blur(uv, int(u_params.y), true);
    if (mode == 17) return radial_zoom_blur(uv, int(u_params.y), false);
    if (mode == 18) return median_approx(uv, int(u_params.x));
    if (mode == 19) {
        vec4 b = blur_box(uv, int(u_params.x));
        float diff = length(src.rgb - b.rgb);
        return diff < u_params.y / 255.0 ? b : src;
    }
    if (mode == 20 || mode == 21) {
        float cell = max(1.0, u_params.x);
        vec2 snapped = (floor(pixel / cell) * cell + cell * 0.5) / u_size.xy;
        return sample_source(snapped);
    }
    if (mode == 22) {
        float radius = max(1.0, u_params.x);
        vec2 jitter = vec2(hash12(pixel), hash12(pixel + 19.7)) * 2.0 - 1.0;
        return sample_source(uv + jitter * vec2(u_size.z, u_size.w) * radius);
    }
    if (mode == 23) {
        vec2 center = vec2(0.5);
        vec2 delta = uv - center;
        float dist = length(delta);
        vec2 sample_uv = center + delta * (1.0 - u_params.x * 0.35 * (1.0 - dist));
        return sample_source(sample_uv);
    }
    if (mode == 24) {
        vec2 center = vec2(0.5);
        vec2 delta = uv - center;
        float angle = u_params.x * 6.28318 * (1.0 - length(delta));
        vec2 sample_uv = center + mat2(cos(angle), -sin(angle), sin(angle), cos(angle)) * delta;
        return sample_source(sample_uv);
    }
    if (mode == 25) {
        float cell = max(1.0, u_params.x);
        vec2 local = mod(pixel, cell) / cell;
        vec2 tile = floor(pixel / cell) * cell;
        vec2 mirrored = mix(local, 1.0 - local, step(0.5, local));
        return sample_source((tile + mirrored * cell) / u_size.xy);
    }
    if (mode == 26) {
        float n = fbm(pixel / max(1.0, u_params.x));
        vec2 offset = (vec2(n, value_noise(pixel.yx / max(1.0, u_params.x))) - 0.5) * u_params.y * vec2(u_size.z, u_size.w);
        return sample_source(uv + offset);
    }
    if (mode == 27) {
        vec2 center = vec2(0.5);
        vec2 delta = uv - center;
        float radius = length(delta);
        float angle = atan(delta.y, delta.x);
        vec2 sample_uv = center + vec2(cos(angle), sin(angle)) * (1.0 - radius) * u_params.x;
        return sample_source(sample_uv);
    }
    if (mode == 28) {
        float n = hash12(pixel + u_params.x);
        float coverage = clamp(u_params.y / 100.0, 0.0, 1.0);
        if (n > coverage) return src;
        vec3 noise = mix(vec3(n), vec3(hash12(pixel + 2.0), hash12(pixel + 7.0), hash12(pixel + 13.0)), clamp(u_params.z / 100.0, 0.0, 1.0));
        return vec4(mix(src.rgb, noise, clamp(u_params.x / 255.0, 0.0, 1.0)), src.a);
    }
    if (mode == 29) return median_approx(uv, int(u_params.x));
    if (mode == 30) {
        vec4 b = blur_box(uv, int(u_params.x));
        vec3 color = src.rgb + b.rgb * clamp(u_params.y / 100.0, 0.0, 1.0);
        return vec4(clamp(color, 0.0, 1.0), src.a);
    }
    if (mode == 31) {
        vec3 hsv = rgb_to_hsv(src.rgb);
        if (hsv.x < 0.06 || hsv.x > 0.94) {
            hsv.y *= 1.0 - clamp(u_params.y / 100.0, 0.0, 1.0);
        }
        return vec4(hsv_to_rgb(hsv), src.a);
    }
    if (mode == 32) {
        vec4 n = src * 5.0 - sample_source(uv + vec2(u_size.z, 0.0)) - sample_source(uv - vec2(u_size.z, 0.0))
                 - sample_source(uv + vec2(0.0, u_size.w)) - sample_source(uv - vec2(0.0, u_size.w));
        return vec4(mix(src.rgb, clamp(n.rgb, 0.0, 1.0), clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 33) {
        vec4 b = blur_box(uv, int(u_params.x));
        return vec4(mix(src.rgb, b.rgb * vec3(1.05, 1.0, 0.94), clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    if (mode == 34) {
        float d = distance(uv, vec2(0.5));
        float v = smoothstep(0.9, 0.2, d + u_params.y / 400.0);
        return vec4(src.rgb * v, src.a);
    }
    if (mode == 35) {
        float n = fbm(pixel / max(1.0, u_params.x));
        return vec4(mix(u_primary.rgb, u_secondary.rgb, n), src.a);
    }
    if (mode == 36 || mode == 37) {
        vec2 z = (uv - 0.5) * max(0.1, 3.0 / max(0.1, u_params.x));
        vec2 c = mode == 36 ? vec2(-0.8, 0.156) : z;
        if (mode == 36) z = vec2(cos(u_params.z), sin(u_params.z)) * 0.2 + z;
        float iter = 0.0;
        for (int i = 0; i < 64; ++i) {
            z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
            if (dot(z, z) > 4.0) break;
            iter += 1.0;
        }
        vec3 color = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + iter * 0.18);
        return vec4(color, src.a);
    }
    if (mode == 38) {
        float n = fbm(pixel / max(1.0, u_params.x));
        return vec4(mix(src.rgb, vec3(n), 0.65), src.a);
    }
    if (mode == 39) return edge_color(uv);
    if (mode == 40 || mode == 42) {
        float l0 = luminance(sample_source(uv - vec2(u_size.z, u_size.w)).rgb);
        float l1 = luminance(sample_source(uv + vec2(u_size.z, u_size.w)).rgb);
        float e = clamp((l0 - l1) + 0.5, 0.0, 1.0);
        return vec4(vec3(e), src.a);
    }
    if (mode == 41) {
        vec4 e = edge_color(uv);
        return vec4(mix(src.rgb, vec3(0.0), e.r * clamp(u_params.y / 100.0, 0.0, 1.0)), src.a);
    }
    return src;
}

void main() {
    vec4 src = sample_source(v_uv);
    float mask = texture(u_mask, v_uv).r;
    if (mask < 0.5) {
        frag_color = src;
        return;
    }
    frag_color = clamp(apply_effect(v_uv, src), 0.0, 1.0);
}
)GLSL";

} // namespace

GpuEffectRenderer::~GpuEffectRenderer() {
    destroy();
}

void GpuEffectRenderer::set_error(std::string value) {
    last_error_ = std::move(value);
}

GpuBackendCapabilities GpuEffectRenderer::capabilities() const {
    GpuBackendCapabilities result;
    int max_texture_size = 0;
    int max_renderbuffer_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_renderbuffer_size);
    if (max_texture_size > 0 && max_renderbuffer_size > 0) {
        result.max_texture_size = std::min(max_texture_size, max_renderbuffer_size);
    } else {
        result.max_texture_size = std::max(max_texture_size, max_renderbuffer_size);
    }
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

    constexpr std::uint64_t textures_per_pass = 3;
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
    unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader, last_error_);
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
    const std::uint64_t full_footprint = texture_footprint(document.width, document.height, 3);
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
    if (!ensure_texture(source_texture_, document.width, document.height, pixels.data())) {
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
    glBindTexture(GL_TEXTURE_2D, mask_texture_);
    glUniform1i(glGetUniformLocation(shader_program_, "u_mask"), 1);

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
            if (!render_full_active_cel(tile, request) || !read_output_pixels(tile_output)) {
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
    if (mask_texture_ != 0) glDeleteTextures(1, &mask_texture_);
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (vertex_array_ != 0) glDeleteVertexArrays(1, &vertex_array_);
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    framebuffer_ = 0;
    output_texture_ = 0;
    source_texture_ = 0;
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
