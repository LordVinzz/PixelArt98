// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"

#include "core/MemoryTrace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace px {

void SelectionMask::resize(int w, int h) {
    width = w;
    height = h;
    mask.assign(static_cast<std::size_t>(w * h), 0);
    active = false;
}

void SelectionMask::clear() {
    std::fill(mask.begin(), mask.end(), 0);
    active = false;
}

void SelectionMask::select_all() {
    std::fill(mask.begin(), mask.end(), 1);
    active = true;
}

void SelectionMask::select_rect(int x0, int y0, int x1, int y1, bool replace) {
    select_rect(x0, y0, x1, y1, replace ? SelectionCombineMode::Replace : SelectionCombineMode::Add);
}

void SelectionMask::select_rect(int x0, int y0, int x1, int y1, SelectionCombineMode mode) {
    if (width <= 0 || height <= 0) {
        active = false;
        return;
    }
    std::vector<std::uint8_t> source(mask.size(), 0);
    const int requested_min_x = std::min(x0, x1);
    const int requested_max_x = std::max(x0, x1);
    const int requested_min_y = std::min(y0, y1);
    const int requested_max_y = std::max(y0, y1);
    if (requested_max_x < 0 || requested_min_x >= width ||
        requested_max_y < 0 || requested_min_y >= height) {
        combine_with_mask(source, mode);
        return;
    }
    const int min_x = std::clamp(requested_min_x, 0, width - 1);
    const int max_x = std::clamp(requested_max_x, 0, width - 1);
    const int min_y = std::clamp(requested_min_y, 0, height - 1);
    const int max_y = std::clamp(requested_max_y, 0, height - 1);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            source[static_cast<std::size_t>(y * width + x)] = 1;
        }
    }
    combine_with_mask(source, mode);
}

void SelectionMask::select_ellipse(int x0, int y0, int x1, int y1,
                                   SelectionCombineMode mode) {
    if (width <= 0 || height <= 0) {
        active = false;
        return;
    }
    std::vector<std::uint8_t> source(mask.size(), 0);
    const int min_x = std::min(x0, x1);
    const int max_x = std::max(x0, x1);
    const int min_y = std::min(y0, y1);
    const int max_y = std::max(y0, y1);
    const double center_x = (static_cast<double>(min_x) + static_cast<double>(max_x) + 1.0) * 0.5;
    const double center_y = (static_cast<double>(min_y) + static_cast<double>(max_y) + 1.0) * 0.5;
    const double radius_x = std::max(0.5, static_cast<double>(max_x - min_x + 1) * 0.5);
    const double radius_y = std::max(0.5, static_cast<double>(max_y - min_y + 1) * 0.5);
    for (int y = std::max(0, min_y); y <= std::min(height - 1, max_y); ++y) {
        for (int x = std::max(0, min_x); x <= std::min(width - 1, max_x); ++x) {
            const double normalized_x = (static_cast<double>(x) + 0.5 - center_x) / radius_x;
            const double normalized_y = (static_cast<double>(y) + 0.5 - center_y) / radius_y;
            if (normalized_x * normalized_x + normalized_y * normalized_y <= 1.0) {
                source[static_cast<std::size_t>(y * width + x)] = 1;
            }
        }
    }
    combine_with_mask(source, mode);
}

void SelectionMask::select_polygon(const std::vector<std::array<int, 2>>& points, bool replace) {
    select_polygon(points, replace ? SelectionCombineMode::Replace : SelectionCombineMode::Add);
}

void SelectionMask::select_polygon(const std::vector<std::array<int, 2>>& points, SelectionCombineMode mode) {
    if (points.size() < 3 || width <= 0 || height <= 0) {
        active = selected_count() > 0;
        return;
    }

    std::vector<std::uint8_t> source(mask.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float py = static_cast<float>(y) + 0.5f;
            bool inside = false;
            for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                float xi = static_cast<float>(points[i][0]);
                float yi = static_cast<float>(points[i][1]);
                float xj = static_cast<float>(points[j][0]);
                float yj = static_cast<float>(points[j][1]);
                bool crosses = ((yi > py) != (yj > py)) &&
                               (px < (xj - xi) * (py - yi) / ((yj - yi) + 0.00001f) + xi);
                if (crosses) {
                    inside = !inside;
                }
            }
            if (inside) {
                source[static_cast<std::size_t>(y * width + x)] = 1;
            }
        }
    }
    combine_with_mask(source, mode);
}

void SelectionMask::combine_with_mask(const std::vector<std::uint8_t>& source, SelectionCombineMode mode) {
    if (source.size() != mask.size()) {
        return;
    }
    switch (mode) {
        case SelectionCombineMode::Replace:
            mask = source;
            break;
        case SelectionCombineMode::Add:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                mask[i] = static_cast<std::uint8_t>(mask[i] != 0 || source[i] != 0);
            }
            break;
        case SelectionCombineMode::Subtract:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                if (source[i] != 0) {
                    mask[i] = 0;
                }
            }
            break;
        case SelectionCombineMode::Intersect:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                mask[i] = static_cast<std::uint8_t>(mask[i] != 0 && source[i] != 0);
            }
            break;
        case SelectionCombineMode::Invert:
            for (std::size_t i = 0; i < mask.size(); ++i) {
                if (source[i] != 0) {
                    mask[i] = static_cast<std::uint8_t>(mask[i] == 0);
                }
            }
            break;
    }
    active = selected_count() > 0;
}

void SelectionMask::invert() {
    for (auto& value : mask) {
        value = static_cast<std::uint8_t>(value == 0);
    }
    active = selected_count() > 0;
}

void SelectionMask::translate(int dx, int dy) {
    if (!active || (dx == 0 && dy == 0) || width <= 0 || height <= 0) {
        return;
    }
    std::vector<std::uint8_t> translated(mask.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int src_x = x - dx;
            const int src_y = y - dy;
            if (src_x < 0 || src_y < 0 || src_x >= width || src_y >= height) {
                continue;
            }
            translated[static_cast<std::size_t>(y * width + x)] =
                mask[static_cast<std::size_t>(src_y * width + src_x)];
        }
    }
    mask = std::move(translated);
    active = selected_count() > 0;
}

void SelectionMask::expand(int radius) {
    radius = std::max(0, radius);
    if (!active || radius == 0 || width <= 0 || height <= 0) return;
    const std::vector<std::uint8_t> source = mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool selected = false;
            for (int dy = -radius; dy <= radius && !selected; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sample_x = x + dx;
                    const int sample_y = y + dy;
                    if (sample_x >= 0 && sample_y >= 0 && sample_x < width && sample_y < height &&
                        source[static_cast<std::size_t>(sample_y * width + sample_x)] != 0) {
                        selected = true;
                        break;
                    }
                }
            }
            mask[static_cast<std::size_t>(y * width + x)] = selected ? 1 : 0;
        }
    }
    active = selected_count() > 0;
}

void SelectionMask::contract(int radius) {
    radius = std::max(0, radius);
    if (!active || radius == 0 || width <= 0 || height <= 0) return;
    const std::vector<std::uint8_t> source = mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool selected = true;
            for (int dy = -radius; dy <= radius && selected; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sample_x = x + dx;
                    const int sample_y = y + dy;
                    if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height ||
                        source[static_cast<std::size_t>(sample_y * width + sample_x)] == 0) {
                        selected = false;
                        break;
                    }
                }
            }
            mask[static_cast<std::size_t>(y * width + x)] = selected ? 1 : 0;
        }
    }
    active = selected_count() > 0;
}

void SelectionMask::select_border(int radius) {
    radius = std::max(1, radius);
    if (!active) return;
    SelectionMask expanded = *this;
    SelectionMask contracted = *this;
    expanded.expand(radius);
    contracted.contract(radius);
    for (std::size_t index = 0; index < mask.size(); ++index) {
        mask[index] = expanded.mask[index] != 0 && contracted.mask[index] == 0 ? 1 : 0;
    }
    active = selected_count() > 0;
}

void SelectionMask::smooth(int radius) {
    radius = std::max(0, radius);
    if (!active || radius == 0 || width <= 0 || height <= 0) return;
    const std::vector<std::uint8_t> source = mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int selected = 0;
            int samples = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sample_x = x + dx;
                    const int sample_y = y + dy;
                    if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height)
                        continue;
                    ++samples;
                    if (source[static_cast<std::size_t>(sample_y * width + sample_x)] != 0) ++selected;
                }
            }
            mask[static_cast<std::size_t>(y * width + x)] = selected * 2 >= samples ? 1 : 0;
        }
    }
    active = selected_count() > 0;
}

bool SelectionMask::contains(int x, int y) const {
    if (!active) {
        return true;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return mask[static_cast<std::size_t>(y * width + x)] != 0;
}

int SelectionMask::selected_count() const {
    return static_cast<int>(std::count_if(mask.begin(), mask.end(),
                                         [](std::uint8_t value) { return value != 0; }));
}

std::optional<std::array<int, 4>> SelectionMask::bounds() const {
    if (!active || selected_count() == 0) {
        return std::nullopt;
    }
    int min_x = width;
    int min_y = height;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (mask[static_cast<std::size_t>(y * width + x)] == 0) {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    return std::array<int, 4>{min_x, min_y, max_x, max_y};
}

void FloatingSelection::clear() {
    active = false;
    source_x = source_y = offset_x = offset_y = 0;
    width = height = 0;
    pixels.clear();
    mask.clear();
}

bool FloatingSelection::contains_local(int x, int y) const {
    if (!active || x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return mask[static_cast<std::size_t>(y * width + x)] != 0;
}

} // namespace px
