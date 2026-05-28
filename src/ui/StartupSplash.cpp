#include "ui/StartupSplash.hpp"

#include "ui/EmbeddedAssets.hpp"
#include "ui/SplashScreen.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/gl.h>
#include <imgui.h>
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace px {

namespace {

constexpr int kSplashDurationSeconds = 1;
constexpr int kScreenShareDivisor = 4;

struct SplashWindowConfig {
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
};

void* load_glfw_gl_proc(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

[[nodiscard]] SplashWindowConfig splash_window_config(GLFWmonitor* monitor, int image_width, int image_height) {
    int screen_x = 0;
    int screen_y = 0;
    int screen_width = 0;
    int screen_height = 0;
    glfwGetMonitorWorkarea(monitor, &screen_x, &screen_y, &screen_width, &screen_height);
    if (screen_width <= 0 || screen_height <= 0) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode != nullptr) {
            glfwGetMonitorPos(monitor, &screen_x, &screen_y);
            screen_width = mode->width;
            screen_height = mode->height;
        }
    }

    const int target_width = std::max(1, screen_width / kScreenShareDivisor);
    const int target_height = std::max(1, screen_height / kScreenShareDivisor);
    const int scale = std::max(1, std::min(target_width / image_width, target_height / image_height));
    const int width = image_width * scale;
    const int height = image_height * scale;
    return {width, height, screen_x + (screen_width - width) / 2, screen_y + (screen_height - height) / 2};
}

void configure_splash_imgui() {
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.AntiAliasedLines = false;
    style.AntiAliasedLinesUseTex = false;
    style.AntiAliasedFill = false;
}

void export_default_imgui_ini_if_missing() {
    const std::filesystem::path path{"imgui.ini"};
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return;
    }
    if (error) {
        std::fprintf(stderr, "Unable to check imgui.ini: %s\n", error.message().c_str());
        return;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Unable to create imgui.ini\n");
        return;
    }
    const std::string_view config = assets::default_imgui_ini();
    file.write(config.data(), static_cast<std::streamsize>(config.size()));
    if (!file) {
        std::fprintf(stderr, "Unable to write imgui.ini\n");
    }
}

void render_splash_frame(GLFWwindow* window, const SplashScreen& splash) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    splash.render_full_window();
    ImGui::Render();

    int display_width = 0;
    int display_height = 0;
    glfwGetFramebufferSize(window, &display_width, &display_height);
    glViewport(0, 0, display_width, display_height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

} // namespace

void show_startup_splash() {
    export_default_imgui_ini_if_missing();

    if (!glfwInit()) {
        return;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        glfwTerminate();
        return;
    }
    const SplashWindowConfig config = splash_window_config(monitor, assets::kSplashWidth, assets::kSplashHeight);

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(config.width, config.height, "PixelArt98", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return;
    }
    glfwSetWindowPos(window, config.x, config.y);
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(load_glfw_gl_proc)) {
        std::fprintf(stderr, "GLAD OpenGL loader initialization failed for splash screen\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return;
    }
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    configure_splash_imgui();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    {
        SplashScreen splash;
        std::string splash_error;
        if (splash.load(assets::splash_png(), &splash_error)) {
            glfwShowWindow(window);
            const auto start = std::chrono::steady_clock::now();
            while (!glfwWindowShouldClose(window) &&
                   std::chrono::steady_clock::now() - start < std::chrono::seconds(kSplashDurationSeconds)) {
                render_splash_frame(window, splash);
            }
        } else {
            std::fprintf(stderr, "%s\n", splash_error.c_str());
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    gladLoaderUnloadGL();
    glfwDestroyWindow(window);
    glfwTerminate();
}

} // namespace px
