// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "platform/NativeFileDialogProvider.hpp"
#include "ui/AppSettings.hpp"
#include "ui/EditorApp.hpp"
#include "ui/StartupSplash.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/gl.h>
#include <imgui.h>
#include <nfd_glfw3.h>

#include <cstdio>

namespace {

void* load_glfw_gl_proc(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

void error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void apply_style() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.80f, 0.80f, 0.78f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.70f, 0.70f, 0.68f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.38f, 0.65f, 0.45f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.74f, 0.74f, 0.72f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.84f, 0.84f, 0.82f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.58f, 0.84f, 1.0f);
}

} // namespace

int main(int, char** argv) {
    static_cast<void>(argv);
    const px::AppSettings settings = px::load_app_settings();
    if (settings.show_splash_screen) {
        px::show_startup_splash();
    }

    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1440, 900, "PixelArt98 GLFW", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(load_glfw_gl_proc)) {
        std::fprintf(stderr, "GLAD OpenGL loader initialization failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    apply_style();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool nfd_ready = NFD_Init() == NFD_OKAY;
    if (!nfd_ready) {
        const char* error = NFD_GetError();
        std::fprintf(stderr, "NFD_Init failed: %s\n", error != nullptr ? error : "unknown error");
    }
    px::NativeFileDialogProvider dialogs([window]() {
        nfdwindowhandle_t handle = {};
        NFD_GetNativeWindowFromGLFWWindow(window, &handle);
        return handle;
    });

    {
        px::EditorApp app(nfd_ready ? &dialogs : nullptr, settings);
        while (!glfwWindowShouldClose(window) && !app.wants_quit()) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            app.render();
            ImGui::Render();

            int display_w = 0;
            int display_h = 0;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.78f, 0.78f, 0.76f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    }

    if (nfd_ready) {
        NFD_Quit();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    gladLoaderUnloadGL();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
