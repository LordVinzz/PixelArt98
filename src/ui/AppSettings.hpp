#pragma once

#include <filesystem>
#include <string>

namespace px {

struct AppSettings {
    bool show_splash_screen = true;
};

[[nodiscard]] std::filesystem::path default_app_settings_path();
[[nodiscard]] AppSettings load_app_settings();
[[nodiscard]] AppSettings load_app_settings(const std::filesystem::path& path, std::string* error = nullptr);
[[nodiscard]] bool save_app_settings(const AppSettings& settings, std::string* error = nullptr);
[[nodiscard]] bool save_app_settings(const AppSettings& settings, const std::filesystem::path& path, std::string* error = nullptr);

} // namespace px
