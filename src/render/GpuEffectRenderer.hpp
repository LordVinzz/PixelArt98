// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Document.hpp"
#include "core/Pixel.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace px {

enum class GpuEffectMode {
    BrightnessContrast,
    Hsv,
    Levels,
    PaletteQuantize,
    PaletteDither,
    AutoLevel,
    Grayscale,
    Sepia,
    InvertColors,
    InvertAlpha,
    Posterize,
    OilPainting,
    InkSketch,
    PencilSketch,
    GaussianBlur,
    MotionBlur,
    RadialBlur,
    ZoomBlur,
    MedianBlur,
    SurfaceBlur,
    Pixelate,
    Crystalize,
    FrostedGlass,
    Bulge,
    Twist,
    TileReflection,
    Dents,
    PolarInversion,
    AddNoise,
    ReduceNoise,
    Glow,
    RedEyeRemoval,
    Sharpen,
    SoftenPortrait,
    Vignette,
    Clouds,
    JuliaFractal,
    MandelbrotFractal,
    Turbulence,
    EdgeDetect,
    Emboss,
    Outline,
    Relief
};

struct GpuEffectRequest {
    GpuEffectMode mode = GpuEffectMode::BrightnessContrast;
    std::array<float, 4> params = {};
    std::array<float, 4> params2 = {};
    Pixel primary = rgba(0, 0, 0, 255);
    Pixel secondary = rgba(255, 255, 255, 255);
};

struct GpuBackendCapabilities {
    int max_texture_size = 0;
    std::uint64_t working_texture_budget = 0;
    bool supports_chunking = false;
};

class GpuEffectRenderer {
public:
    GpuEffectRenderer() = default;
    ~GpuEffectRenderer();

    GpuEffectRenderer(const GpuEffectRenderer&) = delete;
    GpuEffectRenderer& operator=(const GpuEffectRenderer&) = delete;

    bool render_active_cel(const Document& document,
                           const GpuEffectRequest& request,
                           const GpuBackendCapabilities* capability_override = nullptr);
    bool read_output_pixels(std::vector<Pixel>& pixels) const;
    void destroy();

    [[nodiscard]] GpuBackendCapabilities capabilities() const;
    [[nodiscard]] bool used_chunking() const noexcept { return used_chunking_; }
    [[nodiscard]] static bool effect_supports_chunking(const GpuEffectRequest& request);
    [[nodiscard]] static int effect_chunk_halo(const GpuEffectRequest& request);
    [[nodiscard]] static int choose_chunk_extent(int width,
                                                 int height,
                                                 int halo,
                                                 const GpuBackendCapabilities& capabilities);

    unsigned int texture_id() const { return output_texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    bool render_full_active_cel(const Document& document, const GpuEffectRequest& request);
    bool render_chunked_active_cel(const Document& document,
                                   const GpuEffectRequest& request,
                                   const GpuBackendCapabilities& capabilities);
    bool ensure_program();
    bool ensure_geometry();
    bool ensure_texture(unsigned int& texture, int width, int height, const Pixel* pixels);
    bool ensure_target(int width, int height);
    void upload_selection_mask(const Document& document);
    void set_error(std::string value);

    unsigned int framebuffer_ = 0;
    unsigned int output_texture_ = 0;
    unsigned int source_texture_ = 0;
    unsigned int mask_texture_ = 0;
    unsigned int vertex_array_ = 0;
    unsigned int vertex_buffer_ = 0;
    unsigned int shader_program_ = 0;
    int width_ = 0;
    int height_ = 0;
    std::vector<Pixel> mask_pixels_;
    std::vector<Pixel> chunked_output_;
    bool used_chunking_ = false;
    std::string last_error_;
};

} // namespace px
