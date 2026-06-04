#pragma once

#include <span>
#include <string_view>

namespace px::assets {

constexpr int kSplashWidth = 1728;
constexpr int kSplashHeight = 1080;

[[nodiscard]] std::span<const unsigned char> splash_png() noexcept;
[[nodiscard]] std::string_view default_imgui_ini() noexcept;

} // namespace px::assets
