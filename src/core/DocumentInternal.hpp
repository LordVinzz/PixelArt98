// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Document.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace px {

Pixel apply_layer_blend_mode(Pixel base, Pixel blend, LayerBlendMode mode);

namespace history_detail {

struct TileDiffStats {
    std::size_t total_tiles = 0;
    std::size_t changed_tiles = 0;
    std::size_t payload_pixels = 0;

    [[nodiscard]] bool any_changed() const {
        return changed_tiles > 0;
    }
};

TileDiffStats analyze_tile_diff_stats(const std::vector<Pixel>& before,
                                      const std::vector<Pixel>& after,
                                      int width,
                                      int height,
                                      int tile_size = 16);
bool should_use_dense_pixel_history(const TileDiffStats& stats,
                                    std::size_t full_pixel_count);
bool apply_history_payload(Document& doc,
                           const DensePixelDiff& diff,
                           const HistoryPixelPayload& payload);
bool capture_dense_after_pixels(Document& doc, DensePixelDiff& diff);
void release_dense_after_pixels(DensePixelDiff& diff);
DensePixelDiff make_dense_pixel_diff(const std::vector<Pixel>& before,
                                     int frame,
                                     int layer,
                                     int width,
                                     int height,
                                     std::string_view label);
void trace_tile_diff_storage(std::string_view prefix,
                             const std::vector<TileDiff>& diffs);
void trace_dense_diff_storage(std::string_view prefix,
                              const DensePixelDiff& diff);

} // namespace history_detail
} // namespace px
