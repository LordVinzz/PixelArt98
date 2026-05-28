#define GLFW_INCLUDE_NONE

#include "core/Document.hpp"
#include "core/Model.hpp"
#include "render/GLCanvasTexture.hpp"
#include "render/Renderer3D.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace px;

static void* load_glfw_gl_proc(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

int main() {
#if defined(__APPLE__)
    if (std::getenv("PIXELART_RUN_GL_SMOKE") == nullptr) {
        std::cout << "OpenGL smoke skipped: set PIXELART_RUN_GL_SMOKE=1 for interactive macOS GL validation\n";
        return 0;
    }
#endif
    if (!glfwInit()) {
        std::cout << "OpenGL smoke skipped: GLFW init failed\n";
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(64, 64, "pixelart smoke", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cout << "OpenGL smoke skipped: hidden context unavailable\n";
        return 0;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(load_glfw_gl_proc)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << "OpenGL smoke skipped: GLAD load failed\n";
        return 0;
    }

    bool ok = false;
    {
        Document doc = Document::create(16, 16);
        std::fill(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(), rgba(255, 0, 0, 255));
        GLCanvasTexture canvas;
        canvas.update(doc.width, doc.height, doc.composite_active());

        ModelDocument model = ModelDocument::create_default();
        model.texture_width = doc.width;
        model.texture_height = doc.height;
        clamp_model_uvs(model);
        Renderer3D renderer;
        ModelViewportState viewport;
        auto composite = doc.composite_active();
        ok = renderer.render_model_to_texture(model, canvas.id(), doc.width, doc.height, viewport, 96, 96, composite);
        if (ok) {
            std::vector<Pixel> pixels(96 * 96, 0);
            glBindTexture(GL_TEXTURE_2D, renderer.texture_id());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            ok = std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
                return a(p) != 0 && (r(p) != 0 || g(p) != 0 || b(p) != 0);
            });
        }
        std::fill(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(), 0);
        canvas.update(doc.width, doc.height, doc.composite_active());
        composite = doc.composite_active();
        ok = ok && renderer.render_model_to_texture(model, canvas.id(), doc.width, doc.height, viewport, 96, 96, composite);
        if (ok) {
            std::vector<Pixel> pixels(96 * 96, 0);
            glBindTexture(GL_TEXTURE_2D, renderer.texture_id());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            ok = std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
                return a(p) != 0 && (r(p) != 0 || g(p) != 0 || b(p) != 0);
            });
        }
    }

    gladLoaderUnloadGL();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (!ok) {
        std::cerr << "OpenGL smoke failed: renderer output was blank\n";
        return 1;
    }
    std::cout << "OpenGL smoke passed\n";
    return 0;
}
