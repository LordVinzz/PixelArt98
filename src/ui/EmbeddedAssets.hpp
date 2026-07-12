// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include <span>

namespace px::assets {

constexpr int kSplashWidth = 1728;
constexpr int kSplashHeight = 1080;

[[nodiscard]] std::span<const unsigned char> splash_png() noexcept;
} // namespace px::assets
