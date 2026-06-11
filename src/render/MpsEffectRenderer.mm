// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "render/MpsEffectRenderer.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>

#include <algorithm>
#include <cstdint>
#include <simd/simd.h>

namespace px {

namespace {

struct MpsUniforms {
    std::int32_t mode = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pad = 0;
    simd_float4 params = {};
    simd_float4 params2 = {};
    simd_float4 primary = {};
    simd_float4 secondary = {};
};

int mode_value(GpuEffectMode mode) {
    return static_cast<int>(mode);
}

simd_float4 pixel_to_float4(Pixel pixel) {
    return simd_make_float4(static_cast<float>(r(pixel)) / 255.0f,
                            static_cast<float>(g(pixel)) / 255.0f,
                            static_cast<float>(b(pixel)) / 255.0f,
                            static_cast<float>(a(pixel)) / 255.0f);
}

std::string nsstring_to_string(NSString* value) {
    if (value == nil) {
        return {};
    }
    const char* text = [value UTF8String];
    return text != nullptr ? std::string(text) : std::string();
}

constexpr const char* kMetalSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    int mode;
    uint width;
    uint height;
    uint pad;
    float4 params;
    float4 params2;
    float4 primary;
    float4 secondary;
};

float luma(float3 color) {
    return dot(color, float3(0.299, 0.587, 0.114));
}

float3 rgb_to_hsv(float3 c) {
    float4 k = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = mix(float4(c.bg, k.wz), float4(c.gb, k.xy), step(c.b, c.g));
    float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 0.00001;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 hsv_to_rgb(float3 c) {
    float3 p = abs(fract(c.xxx + float3(0.0, 1.0 / 3.0, 2.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(float3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float hash12(float2 p) {
    float3 p3 = fract(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + float2(1.0, 0.0));
    float c = hash12(i + float2(0.0, 1.0));
    float d = hash12(i + float2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(float2 p) {
    float value = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amp * value_noise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return value;
}

float4 read_px(texture2d<float, access::read> source, int2 p, constant Uniforms& u) {
    int2 clamped = clamp(p, int2(0), int2(int(u.width) - 1, int(u.height) - 1));
    return source.read(uint2(clamped));
}

float4 blur_box(texture2d<float, access::read> source, int2 p, constant Uniforms& u, int radius) {
    float4 sum = float4(0.0);
    float count = 0.0;
    int r = clamp(radius, 1, 16);
    for (int y = -16; y <= 16; ++y) {
        for (int x = -16; x <= 16; ++x) {
            if (abs(x) <= r && abs(y) <= r) {
                sum += read_px(source, p + int2(x, y), u);
                count += 1.0;
            }
        }
    }
    return sum / max(1.0, count);
}

float4 edge_color(texture2d<float, access::read> source, int2 p, constant Uniforms& u) {
    float left = luma(read_px(source, p + int2(-1, 0), u).rgb);
    float right = luma(read_px(source, p + int2(1, 0), u).rgb);
    float up = luma(read_px(source, p + int2(0, -1), u).rgb);
    float down = luma(read_px(source, p + int2(0, 1), u).rgb);
    float edge = clamp(abs(right - left) + abs(down - up), 0.0, 1.0);
    return float4(float3(edge), read_px(source, p, u).a);
}

kernel void pixelart_effect_kernel(texture2d<float, access::read> source [[texture(0)]],
                                   texture2d<float, access::read> mask_tex [[texture(1)]],
                                   texture2d<float, access::write> dest [[texture(2)]],
                                   constant Uniforms& u [[buffer(0)]],
                                   uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= u.width || gid.y >= u.height) {
        return;
    }
    int2 p = int2(gid);
    float4 src = source.read(gid);
    if (mask_tex.read(gid).r < 0.5) {
        dest.write(src, gid);
        return;
    }

    float4 out = src;
    if (u.mode == 0) {
        float contrast = 1.0 + u.params.y * 2.0;
        out = float4(clamp((src.rgb - 0.5) * contrast + 0.5 + u.params.x, 0.0, 1.0), src.a);
    } else if (u.mode == 1) {
        float3 hsv = rgb_to_hsv(src.rgb);
        hsv.x = fract(hsv.x + u.params.x);
        hsv.y = clamp(hsv.y + u.params.y, 0.0, 1.0);
        hsv.z = clamp(hsv.z + u.params.z, 0.0, 1.0);
        out = float4(hsv_to_rgb(hsv), src.a);
    } else if (u.mode == 2) {
        float in_black = clamp(u.params.x, 0.0, 0.99);
        float in_white = max(in_black + 0.01, u.params.y);
        float3 normalized = clamp((src.rgb - in_black) / (in_white - in_black), 0.0, 1.0);
        normalized = pow(normalized, float3(1.0 / max(0.05, u.params.z)));
        out = float4(mix(float3(u.params.w), float3(u.params2.x), normalized), src.a);
    } else if (u.mode == 3 || u.mode == 4 || u.mode == 10) {
        float steps = max(2.0, u.params.x);
        float3 color = src.rgb;
        if (u.mode == 4) {
            color = clamp(color + (hash12(float2(gid)) - 0.5) / steps, 0.0, 1.0);
        }
        out = float4(floor(color * (steps - 1.0) + 0.5) / (steps - 1.0), src.a);
    } else if (u.mode == 5) {
        float lo = min(min(src.r, src.g), src.b);
        float hi = max(max(src.r, src.g), src.b);
        out = float4(clamp((src.rgb - lo) / max(0.01, hi - lo), 0.0, 1.0), src.a);
    } else if (u.mode == 6) {
        out = float4(float3(luma(src.rgb)), src.a);
    } else if (u.mode == 7) {
        out = float4(float3(dot(src.rgb, float3(0.393, 0.769, 0.189)),
                            dot(src.rgb, float3(0.349, 0.686, 0.168)),
                            dot(src.rgb, float3(0.272, 0.534, 0.131))), src.a);
    } else if (u.mode == 8) {
        out = float4(1.0 - src.rgb, src.a);
    } else if (u.mode == 9) {
        out = float4(src.rgb, 1.0 - src.a);
    } else if (u.mode == 11 || u.mode == 14) {
        out = blur_box(source, p, u, int(u.params.x));
    } else if (u.mode == 12 || u.mode == 13 || u.mode == 39 || u.mode == 41) {
        float4 edge = edge_color(source, p, u);
        out = u.mode == 39 ? edge : float4(mix(src.rgb, edge.rgb, clamp(u.params.y / 100.0, 0.0, 1.0)), src.a);
    } else if (u.mode == 18 || u.mode == 29) {
        out = mix(src, blur_box(source, p, u, int(u.params.x)), 0.45);
    } else if (u.mode == 19) {
        float4 b = blur_box(source, p, u, int(u.params.x));
        out = length(src.rgb - b.rgb) < u.params.y / 255.0 ? b : src;
    } else if (u.mode == 20 || u.mode == 21) {
        int cell = max(1, int(u.params.x));
        int2 snapped = (p / cell) * cell + int2(cell / 2);
        out = read_px(source, snapped, u);
    } else if (u.mode == 22) {
        float radius = max(1.0, u.params.x);
        float2 jitter = float2(hash12(float2(gid)), hash12(float2(gid) + 19.7)) * 2.0 - 1.0;
        out = read_px(source, p + int2(jitter * radius), u);
    } else if (u.mode == 28) {
        float n = hash12(float2(gid) + u.params.x);
        if (n <= clamp(u.params.y / 100.0, 0.0, 1.0)) {
            float3 noise = mix(float3(n), float3(hash12(float2(gid) + 2.0), hash12(float2(gid) + 7.0), hash12(float2(gid) + 13.0)), clamp(u.params.z / 100.0, 0.0, 1.0));
            out = float4(mix(src.rgb, noise, clamp(u.params.x / 255.0, 0.0, 1.0)), src.a);
        }
    } else if (u.mode == 30) {
        float4 b = blur_box(source, p, u, int(u.params.x));
        out = float4(clamp(src.rgb + b.rgb * clamp(u.params.y / 100.0, 0.0, 1.0), 0.0, 1.0), src.a);
    } else if (u.mode == 31) {
        float3 hsv = rgb_to_hsv(src.rgb);
        if (hsv.x < 0.06 || hsv.x > 0.94) {
            hsv.y *= 1.0 - clamp(u.params.y / 100.0, 0.0, 1.0);
        }
        out = float4(hsv_to_rgb(hsv), src.a);
    } else if (u.mode == 32) {
        float4 sharp = src * 5.0 - read_px(source, p + int2(1, 0), u) - read_px(source, p - int2(1, 0), u) - read_px(source, p + int2(0, 1), u) - read_px(source, p - int2(0, 1), u);
        out = float4(mix(src.rgb, clamp(sharp.rgb, 0.0, 1.0), clamp(u.params.y / 100.0, 0.0, 1.0)), src.a);
    } else if (u.mode == 33) {
        float4 b = blur_box(source, p, u, int(u.params.x));
        out = float4(mix(src.rgb, b.rgb * float3(1.05, 1.0, 0.94), clamp(u.params.y / 100.0, 0.0, 1.0)), src.a);
    } else if (u.mode == 34) {
        float2 uv = float2(gid) / float2(max(1u, u.width), max(1u, u.height));
        float v = smoothstep(0.9, 0.2, distance(uv, float2(0.5)) + u.params.y / 400.0);
        out = float4(src.rgb * v, src.a);
    } else if (u.mode == 35) {
        float n = fbm(float2(gid) / max(1.0, u.params.x));
        out = float4(mix(u.primary.rgb, u.secondary.rgb, n), src.a);
    } else if (u.mode == 38) {
        float n = fbm(float2(gid) / max(1.0, u.params.x));
        out = float4(mix(src.rgb, float3(n), 0.65), src.a);
    } else if (u.mode == 40 || u.mode == 42) {
        float l0 = luma(read_px(source, p - int2(1, 1), u).rgb);
        float l1 = luma(read_px(source, p + int2(1, 1), u).rgb);
        out = float4(float3(clamp((l0 - l1) + 0.5, 0.0, 1.0)), src.a);
    } else if (u.mode == 44) {
        float white_point = clamp(u.params.x, -1.0, 1.0);
        float highlights = clamp(u.params.y, -1.0, 1.0);
        float shadows = clamp(u.params.z, -1.0, 1.0);
        float black_point = clamp(u.params.w, -1.0, 1.0);
        float black_anchor = clamp(black_point * 0.20, -0.20, 0.20);
        float white_anchor = clamp(1.0 - white_point * 0.20, 0.80, 1.20);
        float3 color = clamp((src.rgb - float3(black_anchor)) / max(0.05, white_anchor - black_anchor), 0.0, 1.0);
        float lum = clamp(luma(src.rgb), 0.0, 1.0);
        float shadow_weight = pow(1.0 - lum, 1.6);
        float highlight_weight = pow(lum, 1.6);
        color += shadows * 0.45 * shadow_weight * (shadows >= 0.0 ? (float3(1.0) - color) : color);
        color += highlights * 0.45 * highlight_weight * (highlights >= 0.0 ? (float3(1.0) - color) : color);
        out = float4(clamp(color, 0.0, 1.0), src.a);
    } else {
        float2 uv = float2(gid) / float2(max(1u, u.width), max(1u, u.height));
        float2 center = float2(0.5);
        float2 delta = uv - center;
        if (u.mode == 23) {
            float dist = length(delta);
            float2 sample_uv = center + delta * (1.0 - u.params.x * 0.35 * (1.0 - dist));
            out = read_px(source, int2(sample_uv * float2(u.width, u.height)), u);
        } else if (u.mode == 24) {
            float angle = u.params.x * 6.28318 * (1.0 - length(delta));
            float2 sample_uv = center + float2(cos(angle) * delta.x - sin(angle) * delta.y, sin(angle) * delta.x + cos(angle) * delta.y);
            out = read_px(source, int2(sample_uv * float2(u.width, u.height)), u);
        } else if (u.mode == 27) {
            float radius = length(delta);
            float angle = atan2(delta.y, delta.x);
            float2 sample_uv = center + float2(cos(angle), sin(angle)) * (1.0 - radius) * u.params.x;
            out = read_px(source, int2(sample_uv * float2(u.width, u.height)), u);
        } else if (u.mode == 36 || u.mode == 37) {
            float2 z = (uv - 0.5) * max(0.1, 3.0 / max(0.1, u.params.x));
            float2 c = u.mode == 36 ? float2(-0.8, 0.156) : z;
            float iter = 0.0;
            for (int i = 0; i < 64; ++i) {
                z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
                if (dot(z, z) > 4.0) break;
                iter += 1.0;
            }
            out = float4(0.5 + 0.5 * cos(float3(0.0, 2.0, 4.0) + iter * 0.18), src.a);
        } else if (u.mode == 15 || u.mode == 16 || u.mode == 17 || u.mode == 25 || u.mode == 26) {
            out = blur_box(source, p, u, max(1, int(u.params.x)));
        }
    }
    dest.write(clamp(out, 0.0, 1.0), gid);
}
)MSL";

} // namespace

struct MpsEffectRenderer::Impl {
    __strong id<MTLDevice> device = nil;
    __strong id<MTLCommandQueue> queue = nil;
    __strong id<MTLLibrary> library = nil;
    __strong id<MTLComputePipelineState> pipeline = nil;
    __strong id<MTLTexture> source_texture = nil;
    __strong id<MTLTexture> mask_texture = nil;
    __strong id<MTLTexture> output_texture = nil;
    int width = 0;
    int height = 0;
    std::vector<Pixel> mask_pixels;
    std::vector<Pixel> chunked_output;
    bool used_chunking = false;
};

MpsEffectRenderer::MpsEffectRenderer()
    : impl_(std::make_unique<Impl>()) {}

MpsEffectRenderer::~MpsEffectRenderer() = default;

bool MpsEffectRenderer::available() const {
    return impl_ != nullptr && impl_->device != nil;
}

void MpsEffectRenderer::destroy() {
    impl_ = std::make_unique<Impl>();
}

static id<MTLTexture> make_texture(id<MTLDevice> device, int width, int height) {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModeShared;
    return [device newTextureWithDescriptor:descriptor];
}

static std::string metal_device_summary(id<MTLDevice> device) {
    if (device == nil) {
        return "device=nil";
    }
    std::string summary = "device=" + nsstring_to_string([device name]);
    if ([device respondsToSelector:@selector(isLowPower)]) {
        summary += std::string(", low_power=") + ([device isLowPower] ? "true" : "false");
    }
    if ([device respondsToSelector:@selector(hasUnifiedMemory)]) {
        summary += std::string(", unified_memory=") + ([device hasUnifiedMemory] ? "true" : "false");
    }
    if ([device respondsToSelector:@selector(recommendedMaxWorkingSetSize)]) {
        summary += ", recommended_working_set=" + std::to_string(static_cast<std::uint64_t>([device recommendedMaxWorkingSetSize]));
    }
    if ([device respondsToSelector:@selector(currentAllocatedSize)]) {
        summary += ", current_allocated=" + std::to_string(static_cast<std::uint64_t>([device currentAllocatedSize]));
    }
    return summary;
}

static id<MTLCommandBuffer> make_command_buffer(id<MTLCommandQueue> queue) {
    if (queue == nil) {
        return nil;
    }
    id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
    if (command_buffer == nil && [queue respondsToSelector:@selector(commandBufferWithUnretainedReferences)]) {
        command_buffer = [queue commandBufferWithUnretainedReferences];
    }
    return command_buffer;
}

static std::uint64_t mps_texture_footprint(int width, int height, std::uint64_t texture_count) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(Pixel) * texture_count;
}

static GpuBackendCapabilities mps_capabilities_for_device(id<MTLDevice> device) {
    GpuBackendCapabilities caps;
    if (device == nil) {
        return caps;
    }
    caps.max_texture_size = 16384;
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    std::uint64_t recommended = 0;
    if ([device respondsToSelector:@selector(recommendedMaxWorkingSetSize)]) {
        recommended = static_cast<std::uint64_t>([device recommendedMaxWorkingSetSize]);
    }
    if (recommended > 0) {
        caps.working_texture_budget = std::clamp(recommended / 4ULL, 128ULL * mib, 768ULL * mib);
    } else if ([device respondsToSelector:@selector(isLowPower)] && [device isLowPower]) {
        caps.working_texture_budget = 256ULL * mib;
    } else {
        caps.working_texture_budget = 512ULL * mib;
    }
    caps.supports_chunking = caps.max_texture_size > 0;
    return caps;
}

static Document extract_mps_tile_document(const Document& document,
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

bool MpsEffectRenderer::render_full_active_cel(const Document& document, const GpuEffectRequest& request) {
    auto& impl = *impl_;
    @autoreleasepool {
        last_error_.clear();
        if (!document.valid()) {
            last_error_ = "MPS backend skipped: document is invalid";
            return false;
        }

        if (impl.device == nil || impl.queue == nil || impl.library == nil || impl.pipeline == nil) {
            impl.device = MTLCreateSystemDefaultDevice();
            if (impl.device == nil) {
                last_error_ = "MPS backend unavailable: no Metal device";
                return false;
            }
            impl.queue = [impl.device newCommandQueue];
            if (impl.queue == nil) {
                impl.queue = [impl.device newCommandQueueWithMaxCommandBufferCount:8];
            }
            if (impl.queue == nil) {
                last_error_ = "MPS backend unavailable: could not create command queue (" + metal_device_summary(impl.device) + ")";
                return false;
            }

            NSError* error = nil;
            NSString* source = [NSString stringWithUTF8String:kMetalSource];
            impl.library = [impl.device newLibraryWithSource:source options:nil error:&error];
            if (impl.library == nil) {
                last_error_ = "MPS backend shader compile failed: " + nsstring_to_string([error localizedDescription]);
                return false;
            }
            id<MTLFunction> function = [impl.library newFunctionWithName:@"pixelart_effect_kernel"];
            impl.pipeline = [impl.device newComputePipelineStateWithFunction:function error:&error];
            if (impl.pipeline == nil) {
                last_error_ = "MPS backend pipeline creation failed: " + nsstring_to_string([error localizedDescription]);
                return false;
            }
        }

        const GpuBackendCapabilities caps = mps_capabilities_for_device(impl.device);
        if (caps.max_texture_size > 0 &&
            (document.width > caps.max_texture_size || document.height > caps.max_texture_size)) {
            last_error_ = "MPS backend skipped: image exceeds Metal max texture size";
            return false;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(document.width * document.height);
        const auto& pixels = document.active_cel().pixels;
        if (pixels.size() != pixel_count) {
            last_error_ = "MPS backend skipped: active cel pixel buffer has the wrong size";
            return false;
        }

        if (impl.width != document.width || impl.height != document.height ||
            impl.source_texture == nil || impl.mask_texture == nil || impl.output_texture == nil) {
            impl.source_texture = make_texture(impl.device, document.width, document.height);
            impl.mask_texture = make_texture(impl.device, document.width, document.height);
            impl.output_texture = make_texture(impl.device, document.width, document.height);
            impl.width = document.width;
            impl.height = document.height;
            if (impl.source_texture == nil || impl.mask_texture == nil || impl.output_texture == nil) {
                last_error_ = "MPS backend unavailable: could not allocate textures";
                return false;
            }
        }

        const MTLRegion region = MTLRegionMake2D(0, 0, static_cast<NSUInteger>(document.width), static_cast<NSUInteger>(document.height));
        [impl.source_texture replaceRegion:region
                                mipmapLevel:0
                                  withBytes:pixels.data()
                                bytesPerRow:static_cast<NSUInteger>(document.width * static_cast<int>(sizeof(Pixel)))];

        impl.mask_pixels.assign(pixel_count, rgba(255, 255, 255, 255));
        if (document.selection.active && document.selection.mask.size() == pixel_count) {
            for (std::size_t i = 0; i < pixel_count; ++i) {
                const std::uint8_t value = document.selection.mask[i] != 0 ? 255 : 0;
                impl.mask_pixels[i] = rgba(value, value, value, 255);
            }
        }
        [impl.mask_texture replaceRegion:region
                              mipmapLevel:0
                                withBytes:impl.mask_pixels.data()
                              bytesPerRow:static_cast<NSUInteger>(document.width * static_cast<int>(sizeof(Pixel)))];

        id<MTLCommandBuffer> command_buffer = make_command_buffer(impl.queue);
        if (command_buffer == nil) {
            last_error_ = "MPS backend unavailable: could not create command buffer (" +
                          metal_device_summary(impl.device) +
                          ", queue=" + (impl.queue == nil ? std::string("nil") : std::string("created")) + ")";
            return false;
        }

        if (request.mode == GpuEffectMode::GaussianBlur) {
            const float sigma = std::max(0.01f, request.params[0]);
            MPSImageGaussianBlur* blur = [[MPSImageGaussianBlur alloc] initWithDevice:impl.device sigma:sigma];
            [blur encodeToCommandBuffer:command_buffer
                          sourceTexture:impl.source_texture
                     destinationTexture:impl.output_texture];
        } else {
            MpsUniforms uniforms;
            uniforms.mode = mode_value(request.mode);
            uniforms.width = static_cast<std::uint32_t>(document.width);
            uniforms.height = static_cast<std::uint32_t>(document.height);
            uniforms.params = simd_make_float4(request.params[0], request.params[1], request.params[2], request.params[3]);
            uniforms.params2 = simd_make_float4(request.params2[0], request.params2[1], request.params2[2], request.params2[3]);
            uniforms.primary = pixel_to_float4(request.primary);
            uniforms.secondary = pixel_to_float4(request.secondary);

            id<MTLComputeCommandEncoder> encoder = [command_buffer computeCommandEncoder];
            if (encoder == nil) {
                last_error_ = "MPS backend unavailable: could not create compute encoder";
                return false;
            }
            [encoder setComputePipelineState:impl.pipeline];
            [encoder setTexture:impl.source_texture atIndex:0];
            [encoder setTexture:impl.mask_texture atIndex:1];
            [encoder setTexture:impl.output_texture atIndex:2];
            [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:0];

            const NSUInteger thread_width = std::min<NSUInteger>(16, impl.pipeline.threadExecutionWidth);
            const NSUInteger thread_height = std::max<NSUInteger>(1, std::min<NSUInteger>(16, impl.pipeline.maxTotalThreadsPerThreadgroup / thread_width));
            const MTLSize threads_per_group = MTLSizeMake(thread_width, thread_height, 1);
            const MTLSize threads = MTLSizeMake(static_cast<NSUInteger>(document.width), static_cast<NSUInteger>(document.height), 1);
            [encoder dispatchThreads:threads threadsPerThreadgroup:threads_per_group];
            [encoder endEncoding];
        }

        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        if ([command_buffer status] == MTLCommandBufferStatusError) {
            last_error_ = "MPS backend command failed: " + nsstring_to_string([[command_buffer error] localizedDescription]);
            return false;
        }
        return true;
    }
}

GpuBackendCapabilities MpsEffectRenderer::capabilities() const {
    if (impl_ == nullptr) {
        return {};
    }
    if (impl_->device == nil) {
        impl_->device = MTLCreateSystemDefaultDevice();
    }
    return mps_capabilities_for_device(impl_->device);
}

bool MpsEffectRenderer::used_chunking() const {
    return impl_ != nullptr && impl_->used_chunking;
}

bool MpsEffectRenderer::render_active_cel(const Document& document, const GpuEffectRequest& request) {
    last_error_.clear();
    if (!document.valid()) {
        last_error_ = "MPS backend skipped: document is invalid";
        return false;
    }
    const GpuBackendCapabilities caps = capabilities();
    if (impl_ == nullptr || impl_->device == nil) {
        last_error_ = "MPS backend unavailable: no Metal device";
        return false;
    }

    const std::uint64_t full_footprint = mps_texture_footprint(document.width, document.height, 3);
    const bool exceeds_texture_size = caps.max_texture_size > 0 &&
                                      (document.width > caps.max_texture_size || document.height > caps.max_texture_size);
    const bool exceeds_budget = caps.working_texture_budget > 0 && full_footprint > caps.working_texture_budget;
    if (!exceeds_texture_size && !exceeds_budget) {
        impl_->chunked_output.clear();
        impl_->used_chunking = false;
        return render_full_active_cel(document, request);
    }

    if (!GpuEffectRenderer::effect_supports_chunking(request)) {
        last_error_ = "MPS backend skipped: this effect requires full-image coordinates and the image exceeds the current Metal budget";
        return false;
    }

    const auto& source = document.active_cel().pixels;
    const std::size_t pixel_count = static_cast<std::size_t>(document.width * document.height);
    if (source.size() != pixel_count) {
        last_error_ = "MPS backend skipped: active cel pixel buffer has the wrong size";
        return false;
    }

    const int halo = GpuEffectRenderer::effect_chunk_halo(request);
    const int chunk_extent = GpuEffectRenderer::choose_chunk_extent(document.width, document.height, halo, caps);
    if (chunk_extent <= 0) {
        last_error_ = "MPS backend skipped: could not choose a valid chunk size";
        return false;
    }

    impl_->used_chunking = false;
    impl_->chunked_output.assign(pixel_count, rgba(0, 0, 0, 0));
    std::vector<Pixel> tile_output;
    for (int y = 0; y < document.height; y += chunk_extent) {
        const int core_h = std::min(chunk_extent, document.height - y);
        for (int x = 0; x < document.width; x += chunk_extent) {
            const int core_w = std::min(chunk_extent, document.width - x);
            const int left = std::min(halo, x);
            const int top = std::min(halo, y);
            Document tile = extract_mps_tile_document(document, x, y, core_w, core_h, halo);
            if (!render_full_active_cel(tile, request) || !read_output_pixels(tile_output)) {
                if (last_error_.empty()) {
                    last_error_ = "MPS backend skipped: chunk render failed";
                }
                impl_->chunked_output.clear();
                impl_->used_chunking = false;
                return false;
            }
            for (int row = 0; row < core_h; ++row) {
                const std::size_t src_offset = static_cast<std::size_t>((row + top) * tile.width + left);
                const std::size_t dst_offset = static_cast<std::size_t>((y + row) * document.width + x);
                std::copy_n(tile_output.begin() + static_cast<std::ptrdiff_t>(src_offset),
                            static_cast<std::size_t>(core_w),
                            impl_->chunked_output.begin() + static_cast<std::ptrdiff_t>(dst_offset));
            }
        }
    }

    impl_->width = document.width;
    impl_->height = document.height;
    impl_->used_chunking = true;
    last_error_.clear();
    return true;
}

bool MpsEffectRenderer::read_output_pixels(std::vector<Pixel>& pixels) const {
    if (impl_ != nullptr && impl_->used_chunking && !impl_->chunked_output.empty() &&
        impl_->width > 0 && impl_->height > 0 &&
        impl_->chunked_output.size() == static_cast<std::size_t>(impl_->width * impl_->height)) {
        pixels = impl_->chunked_output;
        return true;
    }
    if (impl_ == nullptr || impl_->output_texture == nil || impl_->width <= 0 || impl_->height <= 0) {
        return false;
    }
    pixels.assign(static_cast<std::size_t>(impl_->width * impl_->height), 0);
    const MTLRegion region = MTLRegionMake2D(0, 0, static_cast<NSUInteger>(impl_->width), static_cast<NSUInteger>(impl_->height));
    [impl_->output_texture getBytes:pixels.data()
                        bytesPerRow:static_cast<NSUInteger>(impl_->width * static_cast<int>(sizeof(Pixel)))
                         fromRegion:region
                        mipmapLevel:0];
    return true;
}

} // namespace px
