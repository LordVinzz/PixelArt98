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
    std::uint32_t curve_point_count = 0;
    simd_float4 params = {};
    simd_float4 params2 = {};
    simd_float4 curve_x0 = {};
    simd_float4 curve_x1 = {};
    simd_float4 curve_y0 = {};
    simd_float4 curve_y1 = {};
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

// Metal backend program: MSL source stays separate from MPS resource orchestration.
#include "detail/MpsEffectShader.inc"

} // namespace

struct MpsEffectRenderer::Impl {
    __strong id<MTLDevice> device = nil;
    __strong id<MTLCommandQueue> queue = nil;
    __strong id<MTLLibrary> library = nil;
    __strong id<MTLComputePipelineState> pipeline = nil;
    __strong id<MTLTexture> source_texture = nil;
    __strong id<MTLTexture> depth_texture = nil;
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

const char* MpsEffectRenderer::metal_shader_source() {
    return kMetalSource;
}

bool MpsEffectRenderer::available() const {
    if (impl_ == nullptr) return false;
    if (impl_->device == nil) impl_->device = MTLCreateSystemDefaultDevice();
    return impl_->device != nil;
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

static std::vector<Pixel> extract_mps_tile_pixels(const std::vector<Pixel>& pixels,
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
        if (request.mode == GpuEffectMode::DepthOfField && request.depth_pixels.size() != pixel_count) {
            last_error_ = "MPS depth of field skipped: depth pixel buffer has the wrong size";
            return false;
        }

        if (impl.width != document.width || impl.height != document.height ||
            impl.source_texture == nil || impl.depth_texture == nil || impl.mask_texture == nil || impl.output_texture == nil) {
            impl.source_texture = make_texture(impl.device, document.width, document.height);
            impl.depth_texture = make_texture(impl.device, document.width, document.height);
            impl.mask_texture = make_texture(impl.device, document.width, document.height);
            impl.output_texture = make_texture(impl.device, document.width, document.height);
            impl.width = document.width;
            impl.height = document.height;
            if (impl.source_texture == nil || impl.depth_texture == nil || impl.mask_texture == nil || impl.output_texture == nil) {
                last_error_ = "MPS backend unavailable: could not allocate textures";
                return false;
            }
        }

        const MTLRegion region = MTLRegionMake2D(0, 0, static_cast<NSUInteger>(document.width), static_cast<NSUInteger>(document.height));
        [impl.source_texture replaceRegion:region
                                mipmapLevel:0
                                  withBytes:pixels.data()
                                bytesPerRow:static_cast<NSUInteger>(document.width * static_cast<int>(sizeof(Pixel)))];
        const Pixel* depth_bytes = request.mode == GpuEffectMode::DepthOfField ? request.depth_pixels.data() : pixels.data();
        [impl.depth_texture replaceRegion:region
                               mipmapLevel:0
                                 withBytes:depth_bytes
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
            uniforms.curve_point_count = static_cast<std::uint32_t>(request.curve_point_count);
            uniforms.params = simd_make_float4(request.params[0], request.params[1], request.params[2], request.params[3]);
            uniforms.params2 = simd_make_float4(request.params2[0], request.params2[1], request.params2[2], request.params2[3]);
            uniforms.curve_x0 = simd_make_float4(request.curve_x[0], request.curve_x[1], request.curve_x[2], request.curve_x[3]);
            uniforms.curve_x1 = simd_make_float4(request.curve_x[4], request.curve_x[5], request.curve_x[6], request.curve_x[7]);
            uniforms.curve_y0 = simd_make_float4(request.curve_y[0], request.curve_y[1], request.curve_y[2], request.curve_y[3]);
            uniforms.curve_y1 = simd_make_float4(request.curve_y[4], request.curve_y[5], request.curve_y[6], request.curve_y[7]);
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
            [encoder setTexture:impl.depth_texture atIndex:3];
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

    const std::uint64_t full_footprint = mps_texture_footprint(document.width, document.height, request.mode == GpuEffectMode::DepthOfField ? 4 : 3);
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
    if (request.mode == GpuEffectMode::DepthOfField && request.depth_pixels.size() != pixel_count) {
        last_error_ = "MPS depth of field skipped: depth pixel buffer has the wrong size";
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
            GpuEffectRequest tile_request = request;
            if (request.mode == GpuEffectMode::DepthOfField) {
                tile_request.depth_pixels = extract_mps_tile_pixels(request.depth_pixels, document.width, document.height, x, y, core_w, core_h, halo);
            }
            if (!render_full_active_cel(tile, tile_request) || !read_output_pixels(tile_output)) {
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
