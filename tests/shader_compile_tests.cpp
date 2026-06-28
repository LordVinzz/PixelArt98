// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#define GLFW_INCLUDE_NONE

#include "render/GpuEffectRenderer.hpp"
#include "render/MeshUvOverlayRenderer.hpp"
#include "render/Renderer3D.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <array>
#include <iostream>
#include <string>

#if defined(__APPLE__)
bool compile_metal_shaders(bool& skipped, std::string& message);
#endif

namespace {

void* load_glfw_gl_proc(const char* name) {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

const char* shader_type_name(GLenum type) {
    if (type == GL_VERTEX_SHADER) {
        return "vertex";
    }
    if (type == GL_FRAGMENT_SHADER) {
        return "fragment";
    }
    return "unknown";
}

GLuint compile_gl_shader(GLenum type, const char* program_name, const char* source, std::string& error) {
    if (source == nullptr || source[0] == '\0') {
        error = std::string(program_name) + " " + shader_type_name(type) + " shader source is empty";
        return 0;
    }

    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) {
        return shader;
    }

    std::array<char, 4096> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    error = std::string(program_name) + " " + shader_type_name(type) + " shader compile failed: " + log.data();
    glDeleteShader(shader);
    return 0;
}

bool compile_gl_program(const char* name, const char* vertex_source, const char* fragment_source) {
    std::string error;
    const GLuint vertex_shader = compile_gl_shader(GL_VERTEX_SHADER, name, vertex_source, error);
    if (vertex_shader == 0) {
        std::cerr << "[FAIL] " << error << "\n";
        return false;
    }

    const GLuint fragment_shader = compile_gl_shader(GL_FRAGMENT_SHADER, name, fragment_source, error);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        std::cerr << "[FAIL] " << error << "\n";
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 4096> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::cerr << "[FAIL] " << name << " shader link failed: " << log.data() << "\n";
        glDeleteProgram(program);
        return false;
    }

    glDeleteProgram(program);
    std::cout << "[PASS] GLSL program compiles: " << name << "\n";
    return true;
}

bool compile_glsl_shaders() {
    if (!glfwInit()) {
        std::cout << "[SKIP] GLSL shader compile: GLFW init failed\n";
        return true;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(64, 64, "pixelart shader compile", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cout << "[SKIP] GLSL shader compile: hidden OpenGL context unavailable\n";
        return true;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGL(load_glfw_gl_proc)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cout << "[SKIP] GLSL shader compile: GLAD load failed\n";
        return true;
    }

    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (version != nullptr) {
        std::cout << "OpenGL version: " << version << "\n";
    }

    bool ok = true;
    ok = compile_gl_program("GpuEffectRenderer",
                            px::GpuEffectRenderer::vertex_shader_source(),
                            px::GpuEffectRenderer::fragment_shader_source()) && ok;
    ok = compile_gl_program("Renderer3D",
                            px::Renderer3D::vertex_shader_source(),
                            px::Renderer3D::fragment_shader_source()) && ok;
    ok = compile_gl_program("MeshUvOverlayRenderer",
                            px::MeshUvOverlayRenderer::vertex_shader_source(),
                            px::MeshUvOverlayRenderer::fragment_shader_source()) && ok;

    glfwDestroyWindow(window);
    glfwTerminate();
    return ok;
}

} // namespace

int main() {
    bool ok = compile_glsl_shaders();

#if defined(__APPLE__)
    bool metal_skipped = false;
    std::string metal_message;
    const bool metal_ok = compile_metal_shaders(metal_skipped, metal_message);
    if (metal_skipped) {
        std::cout << "[SKIP] Metal shader compile: " << metal_message << "\n";
    } else if (metal_ok) {
        std::cout << "[PASS] Metal shader library and pipeline compile\n";
    } else {
        std::cerr << "[FAIL] Metal shader compile: " << metal_message << "\n";
    }
    ok = metal_ok && ok;
#endif

    return ok ? 0 : 1;
}
