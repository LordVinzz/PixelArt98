// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "ui/AppSettings.hpp"

#include <QSaveFile>
#include <QSettings>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>

namespace px {

namespace {

void set_error(std::string* error, const std::string& value) {
    if (error != nullptr) {
        *error = value;
    }
}

QString qstring_from_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    return QString::fromStdWString(path.wstring());
#else
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
#endif
}

void apply_private_permissions(const std::filesystem::path& path, bool directory) {
    std::error_code error;
    const auto permissions = directory
                                 ? std::filesystem::perms::owner_all
                                 : (std::filesystem::perms::owner_read |
                                    std::filesystem::perms::owner_write);
    std::filesystem::permissions(path, permissions, std::filesystem::perm_options::replace, error);
}

} // namespace

std::filesystem::path default_app_data_directory() {
    if (const char* override_path = std::getenv("PIXELART98_HOME");
        override_path != nullptr && override_path[0] != '\0') {
        return std::filesystem::path(override_path);
    }
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".pixelart98";
    }
    std::error_code error;
    const std::filesystem::path fallback = std::filesystem::temp_directory_path(error);
    return (error ? std::filesystem::path(".") : fallback) / ".pixelart98";
}

std::filesystem::path default_app_settings_path() {
    return default_app_data_directory() / "settings.json";
}

std::filesystem::path default_recovery_directory() {
    return default_app_data_directory() / "recovery";
}

std::filesystem::path default_recovery_session_path() {
    return default_recovery_directory() / "session.pixart-recovery";
}

void configure_qt_settings_storage() {
    std::error_code error;
    const std::filesystem::path directory = default_app_data_directory();
    std::filesystem::create_directories(directory, error);
    apply_private_permissions(directory, true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, qstring_from_path(directory));
}

AppSettings load_app_settings() {
    const std::filesystem::path path = default_app_settings_path();
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        const std::filesystem::path legacy = std::filesystem::current_path(error) /
                                             "pixelart98_settings.json";
        if (!error && std::filesystem::exists(legacy, error)) {
            AppSettings migrated = load_app_settings(legacy);
            static_cast<void>(save_app_settings(migrated, path));
            return migrated;
        }
    }
    return load_app_settings(path);
}

AppSettings load_app_settings(const std::filesystem::path& path, std::string* error) {
    AppSettings settings;
    std::ifstream file(path);
    if (!file.is_open()) {
        return settings;
    }

    try {
        nlohmann::json root;
        file >> root;
        if (root.contains("show_splash_screen")) {
            settings.show_splash_screen = root.at("show_splash_screen").get<bool>();
        }
        if (root.contains("auto_open_error_console")) {
            settings.auto_open_error_console = root.at("auto_open_error_console").get<bool>();
        } else if (root.contains("show_error_console")) {
            settings.auto_open_error_console = root.at("show_error_console").get<bool>();
        }
        if (root.contains("heavy_gpu_optimization")) {
            settings.heavy_gpu_optimization = root.at("heavy_gpu_optimization").get<bool>();
        }
        if (root.contains("mps_backend")) {
            settings.mps_backend = root.at("mps_backend").get<bool>();
        }
        if (root.contains("depth_allow_cpu_fallback")) {
            settings.depth_allow_cpu_fallback = root.at("depth_allow_cpu_fallback").get<bool>();
        }
        if (root.contains("depth_tile_size")) {
            settings.depth_tile_size = std::clamp(root.at("depth_tile_size").get<int>(), 64, 4096);
        }
        if (root.contains("depth_tile_overlap")) {
            settings.depth_tile_overlap = std::clamp(root.at("depth_tile_overlap").get<int>(), 0, settings.depth_tile_size / 2);
        }
        if (root.contains("show_tools_panel")) {
            settings.show_tools_panel = root.at("show_tools_panel").get<bool>();
        }
        if (root.contains("show_colors_panel")) {
            settings.show_colors_panel = root.at("show_colors_panel").get<bool>();
        }
        if (root.contains("show_layers_panel")) {
            settings.show_layers_panel = root.at("show_layers_panel").get<bool>();
        }
        if (root.contains("show_adjustments_panel")) {
            settings.show_adjustments_panel = root.at("show_adjustments_panel").get<bool>();
        }
        if (root.contains("show_animation_panel")) {
            settings.show_animation_panel = root.at("show_animation_panel").get<bool>();
        }
        if (root.contains("show_history_panel")) {
            settings.show_history_panel = root.at("show_history_panel").get<bool>();
        }
        if (root.contains("show_model_uv_panel")) {
            settings.show_model_uv_panel = root.at("show_model_uv_panel").get<bool>();
        }
        if (root.contains("show_3d_preview")) {
            settings.show_3d_preview = root.at("show_3d_preview").get<bool>();
        }
        if (root.contains("show_canvas_cuboid_uv_overlay")) {
            settings.show_canvas_cuboid_uv_overlay = root.at("show_canvas_cuboid_uv_overlay").get<bool>();
        }
        settings.ffmpeg_path = root.value("ffmpeg_path", std::string());
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not load settings: ") + exception.what());
    }
    return settings;
}

bool save_app_settings(const AppSettings& settings, std::string* error) {
    return save_app_settings(settings, default_app_settings_path(), error);
}

bool save_app_settings(const AppSettings& settings, const std::filesystem::path& path, std::string* error) {
    try {
        nlohmann::json root;
        root["show_splash_screen"] = settings.show_splash_screen;
        root["auto_open_error_console"] = settings.auto_open_error_console;
        root["heavy_gpu_optimization"] = settings.heavy_gpu_optimization;
        root["mps_backend"] = settings.mps_backend;
        root["depth_allow_cpu_fallback"] = settings.depth_allow_cpu_fallback;
        root["depth_tile_size"] = settings.depth_tile_size;
        root["depth_tile_overlap"] = settings.depth_tile_overlap;
        root["show_tools_panel"] = settings.show_tools_panel;
        root["show_colors_panel"] = settings.show_colors_panel;
        root["show_layers_panel"] = settings.show_layers_panel;
        root["show_adjustments_panel"] = settings.show_adjustments_panel;
        root["show_animation_panel"] = settings.show_animation_panel;
        root["show_history_panel"] = settings.show_history_panel;
        root["show_model_uv_panel"] = settings.show_model_uv_panel;
        root["show_3d_preview"] = settings.show_3d_preview;
        root["show_canvas_cuboid_uv_overlay"] = settings.show_canvas_cuboid_uv_overlay;
        root["ffmpeg_path"] = settings.ffmpeg_path;

        std::error_code directory_error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), directory_error);
            if (directory_error) {
                set_error(error, "Could not create settings directory: " + path.parent_path().string());
                return false;
            }
            apply_private_permissions(path.parent_path(), true);
        }
        QSaveFile file(qstring_from_path(path));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            set_error(error, "Could not save settings: " + path.string());
            return false;
        }
        const std::string contents = root.dump(2) + '\n';
        if (file.write(contents.data(), static_cast<qint64>(contents.size())) !=
                static_cast<qint64>(contents.size()) ||
            !file.commit()) {
            set_error(error, "Could not atomically save settings: " + path.string());
            return false;
        }
        apply_private_permissions(path, false);
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not save settings: ") + exception.what());
        return false;
    }
    return true;
}

} // namespace px
