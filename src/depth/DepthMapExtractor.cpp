// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "depth/DepthMapExtractor.hpp"

#include "ui/AppSettings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <thread>

#if PIXELART_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#if defined(__APPLE__) && __has_include(<onnxruntime/core/providers/coreml/coreml_provider_factory.h>)
#include <onnxruntime/core/providers/coreml/coreml_provider_factory.h>
#define PIXELART_DEPTH_HAS_COREML_PROVIDER 1
#endif
#if __has_include(<onnxruntime/core/providers/cuda/cuda_provider_factory.h>)
#include <onnxruntime/core/providers/cuda/cuda_provider_factory.h>
#define PIXELART_DEPTH_HAS_CUDA_PROVIDER 1
#endif
#endif
#if PIXELART_HAS_OPENCV_DNN
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#endif

namespace px {

namespace {

// Preprocessing pipeline: model acquisition, sampling, and tile alignment helpers.
#include "detail/DepthMapPipeline.inc"

} // namespace

std::vector<float> stitch_depth_tiles(const std::vector<DepthTilePrediction>& predictions, int width, int height) {
    std::vector<float> accumulated(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), 0.0f);
    std::vector<float> weights(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), 0.0f);
    if (width <= 0 || height <= 0) {
        return accumulated;
    }
    for (const DepthTilePrediction& prediction : predictions) {
        const DepthTile& tile = prediction.tile;
        if (tile.expanded_width <= 0 || tile.expanded_height <= 0 ||
            prediction.depth.size() != static_cast<std::size_t>(tile.expanded_width * tile.expanded_height)) {
            continue;
        }
        const DepthTileAdjustment adjustment = estimate_depth_tile_adjustment(prediction, accumulated, weights, width, height);
        const int y_end = std::min(height, tile.expanded_y + tile.expanded_height);
        const int x_end = std::min(width, tile.expanded_x + tile.expanded_width);
        for (int y = std::max(0, tile.expanded_y); y < y_end; ++y) {
            for (int x = std::max(0, tile.expanded_x); x < x_end; ++x) {
                const int expanded_x = x - tile.expanded_x;
                const int expanded_y = y - tile.expanded_y;
                const float raw_value = prediction.depth[static_cast<std::size_t>(expanded_y * tile.expanded_width + expanded_x)];
                if (!std::isfinite(raw_value)) {
                    continue;
                }
                const float value = raw_value * adjustment.scale + adjustment.bias;
                const float weight = tile_weight(tile, x, y);
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                accumulated[index] += value * weight;
                weights[index] += weight;
            }
        }
    }
    for (std::size_t i = 0; i < accumulated.size(); ++i) {
        if (weights[i] > 0.0f) {
            accumulated[i] /= weights[i];
        }
    }
    return accumulated;
}

namespace {

#if PIXELART_HAS_ONNXRUNTIME || PIXELART_HAS_OPENCV_DNN
std::vector<float> make_model_input(const std::vector<Pixel>& source, int width, int height, const DepthTile& tile, int input_size) {
    std::vector<float> input(static_cast<std::size_t>(3 * input_size * input_size), 0.0f);
    constexpr std::array<float, 3> mean = {0.485f, 0.456f, 0.406f};
    constexpr std::array<float, 3> stdev = {0.229f, 0.224f, 0.225f};
    for (int y = 0; y < input_size; ++y) {
        for (int x = 0; x < input_size; ++x) {
            const float sx = static_cast<float>(tile.expanded_width) / static_cast<float>(input_size);
            const float sy = static_cast<float>(tile.expanded_height) / static_cast<float>(input_size);
            const float source_x = static_cast<float>(tile.expanded_x) + (static_cast<float>(x) + 0.5f) * sx - 0.5f;
            const float source_y = static_cast<float>(tile.expanded_y) + (static_cast<float>(y) + 0.5f) * sy - 0.5f;
            const Pixel pixel = sample_pixel_bilinear(source, width, height, source_x, source_y);
            const std::size_t offset = static_cast<std::size_t>(y * input_size + x);
            input[offset] = ((static_cast<float>(r(pixel)) / 255.0f) - mean[0]) / stdev[0];
            input[static_cast<std::size_t>(input_size * input_size) + offset] = ((static_cast<float>(g(pixel)) / 255.0f) - mean[1]) / stdev[1];
            input[static_cast<std::size_t>(2 * input_size * input_size) + offset] = ((static_cast<float>(b(pixel)) / 255.0f) - mean[2]) / stdev[2];
        }
    }
    return input;
}
#endif

#if PIXELART_HAS_ONNXRUNTIME
std::string configure_execution_provider(Ort::SessionOptions& options,
                                         DepthAccelerationPreference preference,
                                         bool allow_cpu_fallback) {
#if !PIXELART_DEPTH_HAS_COREML_PROVIDER && !PIXELART_DEPTH_HAS_CUDA_PROVIDER
    (void)options;
#endif
    if (preference == DepthAccelerationPreference::Metal) {
#if PIXELART_DEPTH_HAS_COREML_PROVIDER
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(options, 0));
        return "ONNX Runtime CoreML/Metal";
#else
        if (!allow_cpu_fallback) {
            throw std::runtime_error("ONNX Runtime CoreML provider is not available in this build");
        }
#endif
    }
    if (preference == DepthAccelerationPreference::Gpu) {
#if PIXELART_DEPTH_HAS_CUDA_PROVIDER
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CUDA(options, 0));
        return "ONNX Runtime CUDA";
#else
        if (!allow_cpu_fallback) {
            throw std::runtime_error("ONNX Runtime GPU provider is not available in this build");
        }
#endif
    }
    return "ONNX Runtime CPU";
}

std::vector<float> run_onnx_tile(Ort::Session& session, const std::vector<Pixel>& source, int width, int height, const DepthTile& tile) {
    std::vector<float> input = make_model_input(source, width, height, tile, kDepthAnythingInputSize);
    std::array<int64_t, 4> input_shape = {1, 3, kDepthAnythingInputSize, kDepthAnythingInputSize};
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, input.data(), input.size(), input_shape.data(), input_shape.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name = session.GetInputNameAllocated(0, allocator);
    auto output_name = session.GetOutputNameAllocated(0, allocator);
    std::array<const char*, 1> input_names = {input_name.get()};
    std::array<const char*, 1> output_names = {output_name.get()};
    std::vector<Ort::Value> outputs = session.Run(Ort::RunOptions{nullptr},
                                                  input_names.data(),
                                                  &input_tensor,
                                                  1,
                                                  output_names.data(),
                                                  1);
    if (outputs.empty() || !outputs[0].IsTensor()) {
        throw std::runtime_error("Depth model did not return a tensor");
    }
    const float* values = outputs[0].GetTensorData<float>();
    const auto info = outputs[0].GetTensorTypeAndShapeInfo();
    const std::vector<int64_t> shape = info.GetShape();
    int out_width = kDepthAnythingInputSize;
    int out_height = kDepthAnythingInputSize;
    if (shape.size() >= 2 && shape[shape.size() - 1] > 0 && shape[shape.size() - 2] > 0) {
        out_width = static_cast<int>(shape[shape.size() - 1]);
        out_height = static_cast<int>(shape[shape.size() - 2]);
    }
    const std::size_t count = static_cast<std::size_t>(out_width * out_height);
    std::vector<float> depth(values, values + count);
    return resize_depth(depth, out_width, out_height, tile.expanded_width, tile.expanded_height);
}
#endif

#if PIXELART_HAS_OPENCV_DNN
std::vector<float> run_opencv_tile(cv::dnn::Net& net, const std::vector<Pixel>& source, int width, int height, const DepthTile& tile) {
    std::vector<float> input = make_model_input(source, width, height, tile, kOpenCvMidasInputSize);
    int blob_shape[] = {1, 3, kOpenCvMidasInputSize, kOpenCvMidasInputSize};
    cv::Mat blob(4, blob_shape, CV_32F, input.data());
    net.setInput(blob);
    cv::Mat output = net.forward();
    if (output.empty()) {
        throw std::runtime_error("OpenCV DNN depth model returned no output");
    }

    int out_width = kOpenCvMidasInputSize;
    int out_height = kOpenCvMidasInputSize;
    if (output.dims >= 2) {
        out_width = output.size[output.dims - 1];
        out_height = output.size[output.dims - 2];
    } else if (output.cols > 0 && output.rows > 0) {
        out_width = output.cols;
        out_height = output.rows;
    }
    const std::size_t count = static_cast<std::size_t>(std::max(0, out_width) * std::max(0, out_height));
    if (count == 0 || output.total() < count) {
        throw std::runtime_error("OpenCV DNN depth model output has an invalid shape");
    }
    cv::Mat output_float;
    output.reshape(1, 1).convertTo(output_float, CV_32F);
    const float* values = output_float.ptr<float>(0);
    std::vector<float> depth(values, values + count);
    return resize_depth(depth, out_width, out_height, tile.expanded_width, tile.expanded_height);
}
#endif

} // namespace

std::vector<DepthTile> build_depth_tiles(int width, int height, int tile_size, int overlap) {
    std::vector<DepthTile> tiles;
    if (width <= 0 || height <= 0) {
        return tiles;
    }
    tile_size = std::clamp(tile_size, 64, std::max(width, height));
    overlap = std::clamp(overlap, 0, tile_size / 2);
    for (int y = 0; y < height; y += tile_size) {
        const int core_height = std::min(tile_size, height - y);
        for (int x = 0; x < width; x += tile_size) {
            const int core_width = std::min(tile_size, width - x);
            DepthTile tile;
            tile.x = x;
            tile.y = y;
            tile.width = core_width;
            tile.height = core_height;
            tile.expanded_x = std::max(0, x - overlap);
            tile.expanded_y = std::max(0, y - overlap);
            const int expanded_right = std::min(width, x + core_width + overlap);
            const int expanded_bottom = std::min(height, y + core_height + overlap);
            tile.expanded_width = expanded_right - tile.expanded_x;
            tile.expanded_height = expanded_bottom - tile.expanded_y;
            tiles.push_back(tile);
        }
    }
    return tiles;
}

std::vector<Pixel> normalize_depth_to_pixels(const std::vector<float>& values,
                                             const std::vector<Pixel>& source,
                                             int width,
                                             int height) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), rgba(0, 0, 0, 255));
    if (values.size() != pixels.size() || source.size() != pixels.size() || pixels.empty()) {
        return pixels;
    }
    std::vector<float> valid;
    std::vector<std::size_t> valid_indices;
    valid.reserve(values.size());
    valid_indices.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (a(source[i]) != 0 && std::isfinite(values[i])) {
            valid.push_back(values[i]);
            valid_indices.push_back(i);
        }
    }
    if (valid.empty()) {
        return pixels;
    }
    std::sort(valid.begin(), valid.end());
    const auto percentile = [&](float p) {
        const std::size_t index = static_cast<std::size_t>(std::round(std::clamp(p, 0.0f, 1.0f) * static_cast<float>(valid.size() - 1)));
        return valid[index];
    };
    float low = percentile(0.02f);
    float high = percentile(0.98f);
    if (std::abs(high - low) < std::numeric_limits<float>::epsilon()) {
        low = valid.front();
        high = valid.back();
    }
    if (std::abs(high - low) < std::numeric_limits<float>::epsilon()) {
        return pixels;
    }

    const float scale = 255.0f / (high - low);
    int actual_low = 255;
    int actual_high = 0;
    for (std::size_t index : valid_indices) {
        const float normalized = (values[index] - low) * scale;
        const int gray = static_cast<int>(std::clamp(normalized, 0.0f, 255.0f) + 0.5f);
        actual_low = std::min(actual_low, gray);
        actual_high = std::max(actual_high, gray);
        pixels[index] = rgba(static_cast<std::uint8_t>(gray),
                             static_cast<std::uint8_t>(gray),
                             static_cast<std::uint8_t>(gray),
                             255);
    }

    if (actual_high <= actual_low) {
        return pixels;
    }
    const float final_scale = 255.0f / static_cast<float>(actual_high - actual_low);
    for (std::size_t index : valid_indices) {
        const int gray = static_cast<int>(r(pixels[index]));
        const int stretched = static_cast<int>(std::clamp((static_cast<float>(gray - actual_low) * final_scale), 0.0f, 255.0f) + 0.5f);
        pixels[index] = rgba(static_cast<std::uint8_t>(stretched),
                             static_cast<std::uint8_t>(stretched),
                             static_cast<std::uint8_t>(stretched),
                             255);
    }
    return pixels;
}

bool depth_pixels_have_full_range(const std::vector<Pixel>& pixels) {
    bool has_black = false;
    bool has_white = false;
    for (Pixel pixel : pixels) {
        if (a(pixel) == 0) {
            continue;
        }
        has_black = has_black || r(pixel) == 0;
        has_white = has_white || r(pixel) == 255;
        if (has_black && has_white) {
            return true;
        }
    }
    return false;
}

[[maybe_unused]] std::filesystem::path default_opencv_depth_model_cache_dir() {
    return default_app_settings_path().parent_path() / "models" / "midas-v21-384-onnx";
}

[[maybe_unused]] std::filesystem::path opencv_depth_model_cache_dir_for_settings(const std::filesystem::path& requested_cache_dir) {
    if (requested_cache_dir.empty() || requested_cache_dir.filename() == "depth-anything-v2-small-onnx") {
        return default_opencv_depth_model_cache_dir();
    }
    return requested_cache_dir / "midas-v21-384-onnx";
}

std::filesystem::path default_depth_model_cache_dir() {
    return default_app_settings_path().parent_path() / "models" / "depth-anything-v2-small-onnx";
}

bool depth_backend_compiled() {
#if PIXELART_HAS_ONNXRUNTIME || PIXELART_HAS_OPENCV_DNN
    return true;
#else
    return false;
#endif
}

std::string depth_backend_build_description() {
    std::string text;
#if PIXELART_HAS_ONNXRUNTIME
    text += "ONNX Runtime";
#if PIXELART_DEPTH_HAS_COREML_PROVIDER
    text += " + CoreML/Metal";
#endif
#if PIXELART_DEPTH_HAS_CUDA_PROVIDER
    text += " + CUDA";
#endif
#endif
#if PIXELART_HAS_OPENCV_DNN
    if (!text.empty()) {
        text += "; ";
    }
    text += "OpenCV DNN";
#endif
    return text.empty() ? "No real depth model backend found at configure time" : text;
}

bool DepthMapExtractor::extract(const std::vector<Pixel>& source,
                                int width,
                                int height,
                                const DepthExtractionSettings& settings,
                                const std::atomic_bool& cancel_requested,
                                const DepthProgressCallback& progress,
                                DepthExtractionResult& result,
                                DepthExtractionError& error) const {
    if (width <= 0 || height <= 0 || source.size() != static_cast<std::size_t>(width * height)) {
        error.message = "Depth extraction needs a valid source layer";
        return false;
    }
    const std::vector<DepthTile> tiles = build_depth_tiles(width, height, settings.tile_size, settings.overlap);
    if (tiles.empty()) {
        error.message = "Depth extraction could not build image chunks";
        return false;
    }

#if !PIXELART_HAS_ONNXRUNTIME && !PIXELART_HAS_OPENCV_DNN
    (void)settings;
    (void)cancel_requested;
    (void)progress;
    (void)result;
    error.message = "Depth extraction requires a real model backend. Build with ONNX Runtime or OpenCV DNN; fake grayscale fallback is disabled.";
    return false;
#else
    auto finish_from_predictions = [&](const std::vector<DepthTilePrediction>& predictions, const std::string& backend_name) {
        publish_progress(progress, 0.93f, "Stitching depth chunks");
        const std::vector<float> stitched = stitch_depth_tiles(predictions, width, height);
        publish_progress(progress, 0.98f, "Normalizing depth map: 255 closest, 0 furthest");
        result.width = width;
        result.height = height;
        result.pixels = normalize_depth_to_pixels(stitched, source, width, height);
        if (!depth_pixels_have_full_range(result.pixels)) {
            throw std::runtime_error("Depth model output has no usable depth range. The generated map did not contain both 0 and 255.");
        }
        result.backend_name = backend_name;
        result.status = "Depth map generated with " + backend_name;
        publish_progress(progress, 1.0f, result.status);
    };

    std::vector<std::string> backend_errors;

#if PIXELART_HAS_ONNXRUNTIME
    try {
        std::filesystem::path model_path;
        std::string cache_error;
        if (!ensure_depth_anything_model_cache(settings.cache_dir.empty() ? default_depth_model_cache_dir() : settings.cache_dir,
                                               cancel_requested,
                                               progress,
                                               model_path,
                                               cache_error)) {
            throw std::runtime_error(cache_error);
        }
        if (cancel_requested.load()) {
            error.message = "Depth extraction canceled";
            return false;
        }
        publish_progress(progress, 0.12f, "Loading ONNX Runtime depth model");
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "pixelart98-depth");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        options.SetIntraOpNumThreads(std::max(1, static_cast<int>(std::thread::hardware_concurrency() / 2U)));
        const std::string backend_name = configure_execution_provider(options, settings.acceleration, settings.allow_cpu_fallback);
#ifdef _WIN32
        const std::wstring model_path_text = model_path.wstring();
        Ort::Session session(env, model_path_text.c_str(), options);
#else
        Ort::Session session(env, model_path.c_str(), options);
#endif

        std::vector<DepthTilePrediction> predictions;
        predictions.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            if (cancel_requested.load()) {
                error.message = "Depth extraction canceled";
                return false;
            }
            publish_progress(progress,
                             0.12f + 0.78f * (static_cast<float>(i) / static_cast<float>(tiles.size())),
                             "Running depth tile " + std::to_string(i + 1) + " / " + std::to_string(tiles.size()));
            predictions.push_back({tiles[i], run_onnx_tile(session, source, width, height, tiles[i])});
        }
        finish_from_predictions(predictions, backend_name);
        return true;
    } catch (const std::exception& exception) {
        backend_errors.push_back(std::string("ONNX Runtime: ") + exception.what());
        if (!settings.allow_cpu_fallback && settings.acceleration != DepthAccelerationPreference::Cpu) {
            error.message = backend_errors.back();
            return false;
        }
    }
#else
    backend_errors.push_back("ONNX Runtime: not compiled");
#endif

#if PIXELART_HAS_OPENCV_DNN
    if (!settings.allow_cpu_fallback && settings.acceleration != DepthAccelerationPreference::Cpu) {
        error.message = "Depth extraction could not use the requested accelerated backend, and real CPU model fallback is disabled.";
        return false;
    }
    try {
        std::filesystem::path model_path;
        std::string cache_error;
        if (!ensure_opencv_midas_model_cache(opencv_depth_model_cache_dir_for_settings(settings.cache_dir),
                                             cancel_requested,
                                             progress,
                                             model_path,
                                             cache_error)) {
            throw std::runtime_error(cache_error);
        }
        if (cancel_requested.load()) {
            error.message = "Depth extraction canceled";
            return false;
        }
        publish_progress(progress, 0.12f, "Loading OpenCV DNN MiDaS depth model");
        cv::dnn::Net net = cv::dnn::readNetFromONNX(model_path.string());
        if (net.empty()) {
            throw std::runtime_error("OpenCV DNN could not load the MiDaS depth model");
        }
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

        std::vector<DepthTilePrediction> predictions;
        predictions.reserve(tiles.size());
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            if (cancel_requested.load()) {
                error.message = "Depth extraction canceled";
                return false;
            }
            publish_progress(progress,
                             0.12f + 0.78f * (static_cast<float>(i) / static_cast<float>(tiles.size())),
                             "Running OpenCV DNN MiDaS depth tile " + std::to_string(i + 1) + " / " + std::to_string(tiles.size()));
            predictions.push_back({tiles[i], run_opencv_tile(net, source, width, height, tiles[i])});
        }
        finish_from_predictions(predictions, "OpenCV DNN CPU (MiDaS v2.1 384)");
        return true;
    } catch (const std::exception& exception) {
        backend_errors.push_back(std::string("OpenCV DNN: ") + exception.what());
    }
#else
    backend_errors.push_back("OpenCV DNN: not compiled");
#endif

    error.message = "No real depth model backend could generate a valid depth map.";
    if (!backend_errors.empty()) {
        error.message += " ";
        for (std::size_t i = 0; i < backend_errors.size(); ++i) {
            if (i > 0) {
                error.message += "; ";
            }
            error.message += backend_errors[i];
        }
    }
    return false;
#endif
}

} // namespace px
