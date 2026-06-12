// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Pixel.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace px {

enum class DepthAccelerationPreference {
    Cpu,
    Gpu,
    Metal
};

struct DepthExtractionSettings {
    std::filesystem::path cache_dir;
    int tile_size = 1024;
    int overlap = 128;
    bool allow_cpu_fallback = true;
    DepthAccelerationPreference acceleration = DepthAccelerationPreference::Gpu;
};

struct DepthExtractionProgress {
    float fraction = 0.0f;
    std::string status = "Waiting";
};

struct DepthExtractionResult {
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels;
    std::string backend_name;
    std::string status;
};

struct DepthExtractionError {
    std::string message;
};

struct DepthTile {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int expanded_x = 0;
    int expanded_y = 0;
    int expanded_width = 0;
    int expanded_height = 0;
};

struct DepthTilePrediction {
    DepthTile tile;
    std::vector<float> depth;
};

using DepthProgressCallback = std::function<void(const DepthExtractionProgress&)>;

[[nodiscard]] std::vector<DepthTile> build_depth_tiles(int width, int height, int tile_size, int overlap);
[[nodiscard]] std::vector<float> stitch_depth_tiles(const std::vector<DepthTilePrediction>& predictions, int width, int height);
[[nodiscard]] std::vector<Pixel> normalize_depth_to_pixels(const std::vector<float>& values,
                                                           const std::vector<Pixel>& source,
                                                           int width,
                                                           int height);
[[nodiscard]] bool depth_pixels_have_full_range(const std::vector<Pixel>& pixels);
[[nodiscard]] std::filesystem::path default_depth_model_cache_dir();
[[nodiscard]] bool depth_backend_compiled();
[[nodiscard]] std::string depth_backend_build_description();

class DepthMapExtractor {
public:
    [[nodiscard]] bool extract(const std::vector<Pixel>& source,
                               int width,
                               int height,
                               const DepthExtractionSettings& settings,
                               const std::atomic_bool& cancel_requested,
                               const DepthProgressCallback& progress,
                               DepthExtractionResult& result,
                               DepthExtractionError& error) const;
};

} // namespace px
