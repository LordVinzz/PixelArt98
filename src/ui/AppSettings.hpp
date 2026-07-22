// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include <filesystem>
#include <string>

namespace px {

struct AppSettings {
    bool show_splash_screen = true;
    bool auto_open_error_console = true;
    bool heavy_gpu_optimization = true;
    bool mps_backend = false;
    bool depth_allow_cpu_fallback = true;
    int depth_tile_size = 1024;
    int depth_tile_overlap = 128;
    bool show_tools_panel = true;
    bool show_colors_panel = true;
    bool show_layers_panel = true;
    bool show_adjustments_panel = true;
    bool show_animation_panel = false;
    bool show_history_panel = false;
    bool show_model_uv_panel = false;
    bool show_3d_preview = false;
    bool show_canvas_cuboid_uv_overlay = false;
    std::string ffmpeg_path;
};

[[nodiscard]] std::filesystem::path default_app_data_directory();
[[nodiscard]] std::filesystem::path default_app_settings_path();
[[nodiscard]] std::filesystem::path default_recovery_directory();
[[nodiscard]] std::filesystem::path default_recovery_session_path();
void configure_qt_settings_storage();
[[nodiscard]] AppSettings load_app_settings();
[[nodiscard]] AppSettings load_app_settings(const std::filesystem::path& path, std::string* error = nullptr);
[[nodiscard]] bool save_app_settings(const AppSettings& settings, std::string* error = nullptr);
[[nodiscard]] bool save_app_settings(const AppSettings& settings, const std::filesystem::path& path, std::string* error = nullptr);

} // namespace px
