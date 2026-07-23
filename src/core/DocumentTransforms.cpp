// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "core/DocumentTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

namespace px {

namespace {

constexpr std::int64_t kMaximumDocumentPixels = 268'435'456;

bool valid_size(int width, int height) {
    return width > 0 && height > 0 &&
           static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) <=
               kMaximumDocumentPixels;
}

Pixel cel_pixel_at(const Cel& cel, int canvas_x, int canvas_y, int width, int height) {
    const int source_x = canvas_x - cel.x;
    const int source_y = canvas_y - cel.y;
    if (source_x < 0 || source_y < 0 || source_x >= width || source_y >= height) return 0;
    const std::size_t index = static_cast<std::size_t>(source_y * width + source_x);
    return index < cel.pixels.size() ? cel.pixels[index] : 0;
}

float cubic(float p0, float p1, float p2, float p3, float t) {
    const float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float a1 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float a2 = -0.5f * p0 + 0.5f * p2;
    return ((a0 * t + a1) * t + a2) * t + p1;
}

Pixel sample_cel(const Cel& cel, float x, float y, int width, int height,
                 ResamplingMode resampling) {
    const auto bounded_pixel = [&](int sample_x, int sample_y) {
        return cel_pixel_at(cel, std::clamp(sample_x, 0, width - 1),
                            std::clamp(sample_y, 0, height - 1), width, height);
    };
    if (resampling == ResamplingMode::Nearest) {
        return bounded_pixel(static_cast<int>(std::floor(x + 0.5f)),
                             static_cast<int>(std::floor(y + 0.5f)));
    }
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    if (resampling == ResamplingMode::Bilinear) {
        const Pixel samples[4] = {
            bounded_pixel(x0, y0), bounded_pixel(x0 + 1, y0),
            bounded_pixel(x0, y0 + 1), bounded_pixel(x0 + 1, y0 + 1)};
        const auto channel = [&](auto component) {
            const float top = static_cast<float>(component(samples[0])) * (1.0f - tx) +
                              static_cast<float>(component(samples[1])) * tx;
            const float bottom = static_cast<float>(component(samples[2])) * (1.0f - tx) +
                                 static_cast<float>(component(samples[3])) * tx;
            return static_cast<std::uint8_t>(std::clamp(top * (1.0f - ty) + bottom * ty,
                                                        0.0f, 255.0f) + 0.5f);
        };
        return rgba(channel(r), channel(g), channel(b), channel(a));
    }

    const auto channel = [&](auto component) {
        float rows[4]{};
        for (int row = -1; row <= 2; ++row) {
            float values[4]{};
            for (int column = -1; column <= 2; ++column) {
                values[column + 1] = static_cast<float>(component(
                    bounded_pixel(x0 + column, y0 + row)));
            }
            rows[row + 1] = cubic(values[0], values[1], values[2], values[3], tx);
        }
        return static_cast<std::uint8_t>(std::clamp(
            cubic(rows[0], rows[1], rows[2], rows[3], ty), 0.0f, 255.0f) + 0.5f);
    };
    return rgba(channel(r), channel(g), channel(b), channel(a));
}

std::uint8_t byte_at(const std::vector<std::uint8_t>& values, int x, int y,
                     int width, int height, std::uint8_t outside) {
    if (x < 0 || y < 0 || x >= width || y >= height) return outside;
    const std::size_t index = static_cast<std::size_t>(y * width + x);
    return index < values.size() ? values[index] : outside;
}

using CoordinateMap = std::function<std::pair<float, float>(int, int)>;

bool remap_document(Document& document, int new_width, int new_height,
                    const CoordinateMap& source_coordinate, const char* name,
                    ModelDocument* model, bool interpolate_pixels,
                    ResamplingMode resampling = ResamplingMode::Nearest) {
    if (!valid_size(new_width, new_height) || !document.valid()) return false;
    const int old_width = document.width;
    const int old_height = document.height;
    auto before_layers = document.layers;
    auto before_frames = document.frames;
    auto before_selection = document.selection;
    auto before_floating = document.floating_selection;
    const std::optional<ModelDocument> before_model = model != nullptr
        ? std::optional<ModelDocument>(*model) : std::nullopt;

    for (Frame& frame : document.frames) {
        for (Cel& cel : frame.cels) {
            const Cel source = cel;
            cel.x = 0;
            cel.y = 0;
            cel.pixels.assign(static_cast<std::size_t>(new_width * new_height), 0);
            for (int y = 0; y < new_height; ++y) {
                for (int x = 0; x < new_width; ++x) {
                    const auto [source_x, source_y] = source_coordinate(x, y);
                    cel.pixels[static_cast<std::size_t>(y * new_width + x)] =
                        interpolate_pixels
                            ? sample_cel(source, source_x, source_y, old_width, old_height,
                                         resampling)
                            : cel_pixel_at(source, static_cast<int>(source_x),
                                           static_cast<int>(source_y), old_width, old_height);
                }
            }
        }
    }

    for (Layer& layer : document.layers) {
        if (layer.mask.empty()) continue;
        const std::vector<std::uint8_t> source = layer.mask;
        layer.mask.assign(static_cast<std::size_t>(new_width * new_height), 255);
        for (int y = 0; y < new_height; ++y) {
            for (int x = 0; x < new_width; ++x) {
                const auto [source_x, source_y] = source_coordinate(x, y);
                layer.mask[static_cast<std::size_t>(y * new_width + x)] = byte_at(
                    source, static_cast<int>(std::floor(source_x + 0.5f)),
                    static_cast<int>(std::floor(source_y + 0.5f)), old_width, old_height, 255);
            }
        }
    }

    document.selection.width = new_width;
    document.selection.height = new_height;
    document.selection.mask.assign(static_cast<std::size_t>(new_width * new_height), 0);
    for (int y = 0; y < new_height; ++y) {
        for (int x = 0; x < new_width; ++x) {
            const auto [source_x, source_y] = source_coordinate(x, y);
            document.selection.mask[static_cast<std::size_t>(y * new_width + x)] = byte_at(
                before_selection.mask, static_cast<int>(std::floor(source_x + 0.5f)),
                static_cast<int>(std::floor(source_y + 0.5f)), old_width, old_height, 0);
        }
    }
    document.selection.active = std::any_of(document.selection.mask.begin(),
                                             document.selection.mask.end(),
                                             [](std::uint8_t value) { return value != 0; });
    document.floating_selection.clear();
    document.width = new_width;
    document.height = new_height;
    if (model != nullptr) {
        model->texture_width = new_width;
        model->texture_height = new_height;
        clamp_model_uvs(*model);
    }
    const std::optional<ModelDocument> after_model = model != nullptr
        ? std::optional<ModelDocument>(*model) : std::nullopt;
    document.commit_document_edit(name, old_width, old_height, std::move(before_layers),
                                  std::move(before_frames), std::move(before_selection),
                                  std::move(before_floating), before_model, after_model);
    return true;
}

} // namespace

bool resize_document_image(Document& document, int width, int height,
                           ResamplingMode resampling, ModelDocument* model) {
    if (width == document.width && height == document.height) return false;
    const float scale_x = static_cast<float>(document.width) / static_cast<float>(width);
    const float scale_y = static_cast<float>(document.height) / static_cast<float>(height);
    return remap_document(document, width, height,
        [scale_x, scale_y](int x, int y) {
            return std::pair{(static_cast<float>(x) + 0.5f) * scale_x - 0.5f,
                             (static_cast<float>(y) + 0.5f) * scale_y - 0.5f};
        }, "Resize Image", model, true, resampling);
}

bool resize_document_canvas(Document& document, int width, int height,
                            int offset_x, int offset_y, ModelDocument* model,
                            const char* undo_name) {
    if (width == document.width && height == document.height && offset_x == 0 && offset_y == 0)
        return false;
    return remap_document(document, width, height,
        [offset_x, offset_y](int x, int y) {
            return std::pair{static_cast<float>(x - offset_x),
                             static_cast<float>(y - offset_y)};
        }, undo_name, model, false);
}

bool crop_document(Document& document, int x, int y, int width, int height,
                   ModelDocument* model) {
    if (!valid_size(width, height) || x < 0 || y < 0 || x + width > document.width ||
        y + height > document.height) return false;
    return resize_document_canvas(document, width, height, -x, -y, model, "Crop Image");
}

bool flip_document_horizontal(Document& document, ModelDocument* model) {
    const int width = document.width;
    return remap_document(document, width, document.height,
        [width](int x, int y) { return std::pair{static_cast<float>(width - 1 - x),
                                                static_cast<float>(y)}; },
        "Flip Image Horizontally", model, false);
}

bool flip_document_vertical(Document& document, ModelDocument* model) {
    const int height = document.height;
    return remap_document(document, document.width, height,
        [height](int x, int y) { return std::pair{static_cast<float>(x),
                                                 static_cast<float>(height - 1 - y)}; },
        "Flip Image Vertically", model, false);
}

bool rotate_document_90_clockwise(Document& document, ModelDocument* model) {
    const int old_height = document.height;
    return remap_document(document, document.height, document.width,
        [old_height](int x, int y) { return std::pair{static_cast<float>(y),
                                                     static_cast<float>(old_height - 1 - x)}; },
        "Rotate Image 90 Clockwise", model, false);
}

bool rotate_document_90_counter_clockwise(Document& document, ModelDocument* model) {
    const int old_width = document.width;
    return remap_document(document, document.height, document.width,
        [old_width](int x, int y) { return std::pair{static_cast<float>(old_width - 1 - y),
                                                    static_cast<float>(x)}; },
        "Rotate Image 90 Counter-Clockwise", model, false);
}

bool rotate_document_180(Document& document, ModelDocument* model) {
    const int width = document.width;
    const int height = document.height;
    return remap_document(document, width, height,
        [width, height](int x, int y) {
            return std::pair{static_cast<float>(width - 1 - x),
                             static_cast<float>(height - 1 - y)};
        }, "Rotate Image 180", model, false);
}

} // namespace px
