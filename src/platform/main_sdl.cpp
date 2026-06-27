// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#define SDL_MAIN_HANDLED

#include "platform/NativeFileDialogProvider.hpp"
#include "ui/AppSettings.hpp"
#include "ui/EditorApp.hpp"
#include "ui/StartupSplash.hpp"

#include <SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <glad/gl.h>
#include <imgui.h>
#include <nfd_sdl2.h>

#include <cstdio>
#include <string>

namespace {

void* load_sdl_gl_proc(const char* name) {
    return SDL_GL_GetProcAddress(name);
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

int main(int argc, char** argv) {
    std::string import_image_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] == nullptr ? std::string{} : std::string(argv[i]);
        if (arg == "--import-image" && i + 1 < argc && argv[i + 1] != nullptr) {
            import_image_path = argv[++i];
        }
    }

    SDL_SetMainReady();
    const px::AppSettings settings = px::load_app_settings();
    if (settings.show_splash_screen) {
        px::show_startup_splash();
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow("PixelArt98 SDL2",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1440, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    if (!gladLoadGL(load_sdl_gl_proc)) {
        std::fprintf(stderr, "GLAD OpenGL loader initialization failed\n");
        SDL_GL_DeleteContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    apply_style();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool nfd_ready = NFD_Init() == NFD_OKAY;
    if (!nfd_ready) {
        const char* error = NFD_GetError();
        std::fprintf(stderr, "NFD_Init failed: %s\n", error != nullptr ? error : "unknown error");
    }
    px::NativeFileDialogProvider dialogs([window]() {
        nfdwindowhandle_t handle = {};
        NFD_GetNativeWindowFromSDLWindow(window, &handle);
        return handle;
    });

    bool done = false;
    {
        px::EditorApp app(nfd_ready ? &dialogs : nullptr, settings);
        if (!import_image_path.empty()) {
            app.import_image_document(import_image_path);
        }
        while (!done && !app.wants_quit()) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) done = true;
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    event.window.windowID == SDL_GetWindowID(window)) done = true;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            app.render();
            ImGui::Render();

            int display_w = 0;
            int display_h = 0;
            SDL_GL_GetDrawableSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.78f, 0.78f, 0.76f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window);
        }
    }

    if (nfd_ready) {
        NFD_Quit();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    gladLoaderUnloadGL();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
