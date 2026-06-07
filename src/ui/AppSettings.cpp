// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "ui/AppSettings.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace px {

namespace {

void set_error(std::string* error, const std::string& value) {
    if (error != nullptr) {
        *error = value;
    }
}

} // namespace

std::filesystem::path default_app_settings_path() {
    std::error_code error;
    const std::filesystem::path base = std::filesystem::current_path(error);
    if (error) {
        return std::filesystem::path("pixelart98_settings.json");
    }
    return base / "pixelart98_settings.json";
}

AppSettings load_app_settings() {
    return load_app_settings(default_app_settings_path());
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
        if (!settings.heavy_gpu_optimization) {
            settings.mps_backend = false;
        }
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
        root["mps_backend"] = settings.heavy_gpu_optimization && settings.mps_backend;

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open()) {
            set_error(error, "Could not save settings: " + path.string());
            return false;
        }
        file << root.dump(2) << '\n';
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not save settings: ") + exception.what());
        return false;
    }
    return true;
}

} // namespace px
