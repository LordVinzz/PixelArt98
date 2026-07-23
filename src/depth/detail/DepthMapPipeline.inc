// Internal implementation fragment for DepthMapExtractor preprocessing and tile stitching.
// This file is included by its owning translation unit; do not compile it separately.

[[maybe_unused]] constexpr int kDepthAnythingInputSize = 518;
[[maybe_unused]] constexpr int kOpenCvMidasInputSize = 384;
[[maybe_unused]] constexpr std::string_view kDepthAnythingModelFileUrl =
    "https://huggingface.co/onnx-community/depth-anything-v2-small-ONNX/resolve/main/onnx/model.onnx";
[[maybe_unused]] constexpr std::string_view kDepthAnythingModelDataUrl =
    "https://huggingface.co/onnx-community/depth-anything-v2-small-ONNX/resolve/main/onnx/model.onnx_data";
[[maybe_unused]] constexpr std::string_view kOpenCvMidasModelUrl =
    "https://huggingface.co/julienkay/sentis-MiDaS/resolve/main/onnx/midas_v21_384.onnx";

void publish_progress(const DepthProgressCallback& progress, float fraction, std::string status) {
    if (progress) {
        progress({std::clamp(fraction, 0.0f, 1.0f), std::move(status)});
    }
}

std::string shell_quote(const std::filesystem::path& path) {
    const std::string value = path.string();
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

std::string shell_quote_text(std::string_view value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += '"';
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

bool file_exists_with_content(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && std::filesystem::file_size(path, error) > 0;
}

bool download_file(std::string_view url, const std::filesystem::path& path, std::string& error) {
    std::error_code fs_error;
    std::filesystem::create_directories(path.parent_path(), fs_error);
    if (fs_error) {
        error = "Could not create model cache: " + fs_error.message();
        return false;
    }

    const std::filesystem::path tmp_path = path.string() + ".download";
    std::filesystem::remove(tmp_path, fs_error);

    const std::string command = "curl -L --fail --retry 2 --silent --show-error --create-dirs -o " +
                                shell_quote(tmp_path) + " " + shell_quote_text(url);
    const int exit_code = std::system(command.c_str());
    if (exit_code != 0 || !file_exists_with_content(tmp_path)) {
        error = "Could not download " + std::string(url) + ". Install curl or place the model files in " + path.parent_path().string();
        std::filesystem::remove(tmp_path, fs_error);
        return false;
    }

    std::filesystem::rename(tmp_path, path, fs_error);
    if (fs_error) {
        std::filesystem::copy_file(tmp_path, path, std::filesystem::copy_options::overwrite_existing, fs_error);
        std::filesystem::remove(tmp_path, fs_error);
    }
    if (fs_error) {
        error = "Could not finalize model download: " + fs_error.message();
        return false;
    }
    return true;
}

[[maybe_unused]] bool ensure_depth_anything_model_cache(const std::filesystem::path& cache_dir,
                                                        const std::atomic_bool& cancel_requested,
                                                        const DepthProgressCallback& progress,
                                                        std::filesystem::path& model_path,
                                                        std::string& error) {
    model_path = cache_dir / "model.onnx";
    const std::filesystem::path data_path = cache_dir / "model.onnx_data";
    publish_progress(progress, 0.01f, "Checking Depth Anything V2 Small model cache");
    if (file_exists_with_content(model_path) && file_exists_with_content(data_path)) {
        return true;
    }
    if (cancel_requested.load()) {
        error = "Depth extraction canceled";
        return false;
    }
    publish_progress(progress, 0.03f, "Downloading Depth Anything V2 Small model.onnx");
    if (!file_exists_with_content(model_path) && !download_file(kDepthAnythingModelFileUrl, model_path, error)) {
        return false;
    }
    if (cancel_requested.load()) {
        error = "Depth extraction canceled";
        return false;
    }
    publish_progress(progress, 0.08f, "Downloading Depth Anything V2 Small model weights");
    if (!file_exists_with_content(data_path) && !download_file(kDepthAnythingModelDataUrl, data_path, error)) {
        return false;
    }
    return true;
}

[[maybe_unused]] bool ensure_opencv_midas_model_cache(const std::filesystem::path& cache_dir,
                                                      const std::atomic_bool& cancel_requested,
                                                      const DepthProgressCallback& progress,
                                                      std::filesystem::path& model_path,
                                                      std::string& error) {
    model_path = cache_dir / "midas_v21_384.onnx";
    publish_progress(progress, 0.01f, "Checking OpenCV MiDaS depth model cache");
    if (file_exists_with_content(model_path)) {
        return true;
    }
    if (cancel_requested.load()) {
        error = "Depth extraction canceled";
        return false;
    }
    publish_progress(progress, 0.05f, "Downloading OpenCV-compatible MiDaS v2.1 depth model");
    return download_file(kOpenCvMidasModelUrl, model_path, error);
}

float sample_depth_bilinear(const std::vector<float>& values, int width, int height, float x, float y) {
    if (width <= 0 || height <= 0 || values.empty()) {
        return 0.0f;
    }
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a00 = values[static_cast<std::size_t>(y0 * width + x0)];
    const float a10 = values[static_cast<std::size_t>(y0 * width + x1)];
    const float a01 = values[static_cast<std::size_t>(y1 * width + x0)];
    const float a11 = values[static_cast<std::size_t>(y1 * width + x1)];
    return ((a00 * (1.0f - tx) + a10 * tx) * (1.0f - ty)) + ((a01 * (1.0f - tx) + a11 * tx) * ty);
}

[[maybe_unused]] Pixel sample_pixel_bilinear(const std::vector<Pixel>& pixels, int width, int height, float x, float y) {
    if (width <= 0 || height <= 0 || pixels.empty()) {
        return 0;
    }
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    auto channel = [&](auto getter) {
        const float a00 = static_cast<float>(getter(pixels[static_cast<std::size_t>(y0 * width + x0)]));
        const float a10 = static_cast<float>(getter(pixels[static_cast<std::size_t>(y0 * width + x1)]));
        const float a01 = static_cast<float>(getter(pixels[static_cast<std::size_t>(y1 * width + x0)]));
        const float a11 = static_cast<float>(getter(pixels[static_cast<std::size_t>(y1 * width + x1)]));
        return static_cast<std::uint8_t>(std::clamp(((a00 * (1.0f - tx) + a10 * tx) * (1.0f - ty)) +
                                                        ((a01 * (1.0f - tx) + a11 * tx) * ty),
                                                    0.0f,
                                                    255.0f) +
                                         0.5f);
    };
    return rgba(channel(r), channel(g), channel(b), channel(a));
}

[[maybe_unused]] std::vector<float> resize_depth(const std::vector<float>& values, int source_width, int source_height, int width, int height) {
    std::vector<float> resized(static_cast<std::size_t>(std::max(0, width) * std::max(0, height)), 0.0f);
    if (source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0) {
        return resized;
    }
    const float sx = static_cast<float>(source_width) / static_cast<float>(width);
    const float sy = static_cast<float>(source_height) / static_cast<float>(height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            resized[static_cast<std::size_t>(y * width + x)] =
                sample_depth_bilinear(values, source_width, source_height, (static_cast<float>(x) + 0.5f) * sx - 0.5f, (static_cast<float>(y) + 0.5f) * sy - 0.5f);
        }
    }
    return resized;
}

float tile_weight(const DepthTile& tile, int x, int y) {
    const int local_x = x - tile.expanded_x;
    const int local_y = y - tile.expanded_y;
    const int dist_x = std::min(local_x + 1, tile.expanded_width - local_x);
    const int dist_y = std::min(local_y + 1, tile.expanded_height - local_y);
    const int distance = std::max(1, std::min(dist_x, dist_y));
    return static_cast<float>(distance);
}

struct DepthTileAdjustment {
    float scale = 1.0f;
    float bias = 0.0f;
};

DepthTileAdjustment estimate_depth_tile_adjustment(const DepthTilePrediction& prediction,
                                                   const std::vector<float>& accumulated,
                                                   const std::vector<float>& weights,
                                                   int width,
                                                   int height) {
    const DepthTile& tile = prediction.tile;
    double sum_w = 0.0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;
    int samples = 0;

    const int y_end = std::min(height, tile.expanded_y + tile.expanded_height);
    const int x_end = std::min(width, tile.expanded_x + tile.expanded_width);
    for (int y = std::max(0, tile.expanded_y); y < y_end; ++y) {
        for (int x = std::max(0, tile.expanded_x); x < x_end; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            if (weights[index] <= 0.0f) {
                continue;
            }
            const int expanded_x = x - tile.expanded_x;
            const int expanded_y = y - tile.expanded_y;
            const float local_value = prediction.depth[static_cast<std::size_t>(expanded_y * tile.expanded_width + expanded_x)];
            const float global_value = accumulated[index] / weights[index];
            if (!std::isfinite(local_value) || !std::isfinite(global_value)) {
                continue;
            }
            const double sample_weight = static_cast<double>(std::min(tile_weight(tile, x, y), weights[index]));
            sum_w += sample_weight;
            sum_x += sample_weight * static_cast<double>(local_value);
            sum_y += sample_weight * static_cast<double>(global_value);
            sum_xx += sample_weight * static_cast<double>(local_value) * static_cast<double>(local_value);
            sum_xy += sample_weight * static_cast<double>(local_value) * static_cast<double>(global_value);
            ++samples;
        }
    }

    if (samples < 4 || sum_w <= 0.0) {
        return {};
    }
    const double denominator = sum_w * sum_xx - sum_x * sum_x;
    if (std::abs(denominator) < 1.0e-9) {
        return {};
    }

    const double scale = (sum_w * sum_xy - sum_x * sum_y) / denominator;
    const double bias = (sum_y - scale * sum_x) / sum_w;
    if (!std::isfinite(scale) || !std::isfinite(bias) || scale <= 0.0) {
        return {};
    }

    DepthTileAdjustment adjustment;
    adjustment.scale = static_cast<float>(std::clamp(scale, 0.125, 8.0));
    adjustment.bias = static_cast<float>(bias);
    return adjustment;
}
