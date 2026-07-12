// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/Document.hpp"
#include "core/Filters.hpp"
#include "core/Model.hpp"
#include "render/GpuEffectRenderer.hpp"
#include "render/GLCanvasTexture.hpp"
#include "render/MpsEffectRenderer.hpp"
#include "render/Renderer3D.hpp"
#include "qt_offscreen_gl.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace px;

static bool report_step(const char* name, bool passed) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
    return passed;
}

#if defined(__APPLE__)
static void report_skip(const char* name, const std::string& reason) {
    std::cout << "[SKIP] " << name << ": " << reason << "\n";
}
#endif

static bool pixels_close(Pixel lhs, Pixel rhs) {
    const auto close = [](std::uint8_t a, std::uint8_t b) {
        return std::abs(static_cast<int>(a) - static_cast<int>(b)) <= 1;
    };
    return close(r(lhs), r(rhs)) && close(g(lhs), g(rhs)) && close(b(lhs), b(rhs)) && close(a(lhs), a(rhs));
}

static bool outputs_match(const std::vector<Pixel>& lhs, const std::vector<Pixel>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!pixels_close(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

static bool seam_pixels_match(const std::vector<Pixel>& lhs,
                              const std::vector<Pixel>& rhs,
                              int width,
                              int height,
                              int chunk_extent) {
    for (int y = 0; y < height; ++y) {
        for (int seam = chunk_extent; seam < width; seam += chunk_extent) {
            for (int x : {seam - 1, seam}) {
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                if (!pixels_close(lhs[index], rhs[index])) {
                    return false;
                }
            }
        }
    }
    for (int x = 0; x < width; ++x) {
        for (int seam = chunk_extent; seam < height; seam += chunk_extent) {
            for (int y : {seam - 1, seam}) {
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                if (!pixels_close(lhs[index], rhs[index])) {
                    return false;
                }
            }
        }
    }
    return true;
}

int main() {
#if defined(__APPLE__)
    if (std::getenv("PIXELART_RUN_GL_SMOKE") == nullptr) {
        std::cout << "OpenGL smoke skipped: set PIXELART_RUN_GL_SMOKE=1 for interactive macOS GL validation\n";
        return 0;
    }
#endif
    std::cout << "OpenGL smoke starting\n";
    QtOffscreenGlContext context;
    if (!context.ready()) {
        std::cout << "OpenGL smoke skipped: Qt offscreen context unavailable\n";
        return 0;
    }
    std::cout << "[PASS] Qt offscreen OpenGL context and GLAD loaded\n";
    std::cout << "OpenGL vendor: " << reinterpret_cast<const char*>(glGetString(GL_VENDOR)) << "\n";
    std::cout << "OpenGL renderer: " << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << "\n";
    std::cout << "OpenGL version: " << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << "\n";
    std::cout << "GLSL version: " << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)) << "\n";
    if (!qt_offscreen_gl_supports_glsl_330()) {
        std::cout << "OpenGL smoke skipped: GLSL 3.30 unavailable in CI offscreen context\n";
        return 0;
    }
    int gl_max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max_texture_size);
    std::cout << "OpenGL max texture size: " << gl_max_texture_size << "\n";

    bool ok = true;
    {
        Document doc = Document::create(16, 16);
        std::fill(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(), rgba(255, 0, 0, 255));
        GpuEffectRenderer effect_renderer;
        GpuEffectRequest effect_request;
        effect_request.mode = GpuEffectMode::Grayscale;
        bool grayscale_ok = effect_renderer.render_active_cel(doc, effect_request);
        if (!grayscale_ok && !effect_renderer.last_error().empty()) {
            std::cout << "       " << effect_renderer.last_error() << "\n";
        }
        if (grayscale_ok) {
            std::vector<Pixel> effect_pixels;
            grayscale_ok = effect_renderer.read_output_pixels(effect_pixels) &&
                           std::any_of(effect_pixels.begin(), effect_pixels.end(), [](Pixel p) {
                               return a(p) == 255 && r(p) > 0 && r(p) == g(p) && g(p) == b(p);
                           });
        }
        ok = report_step("OpenGL grayscale effect and readback", grayscale_ok) && ok;
        {
            Document curve_doc = Document::create(8, 8);
            for (int y = 0; y < curve_doc.height; ++y) {
                for (int x = 0; x < curve_doc.width; ++x) {
                    curve_doc.active_cel().pixels[static_cast<std::size_t>(y * curve_doc.width + x)] =
                        rgba(static_cast<std::uint8_t>(x * 31),
                             static_cast<std::uint8_t>(y * 29),
                             static_cast<std::uint8_t>((x + y) * 15),
                             255);
                }
            }
            CurvesSettings curve_settings;
            curve_settings.point_count = 3;
            curve_settings.x = {0.0f, 0.35f, 1.0f};
            curve_settings.y = {0.0f, 0.72f, 1.0f};
            curve_settings.luma = false;
            curve_settings.red = true;
            curve_settings.green = false;
            curve_settings.blue = true;
            Document cpu_curve_doc = curve_doc;
            apply_curves(cpu_curve_doc, curve_settings);
            GpuEffectRenderer curve_renderer;
            GpuEffectRequest curve_request;
            curve_request.mode = GpuEffectMode::Curves;
            curve_request.params2 = {0.0f, 1.0f, 0.0f, 1.0f};
            curve_request.curve_x = curve_settings.x;
            curve_request.curve_y = curve_settings.y;
            curve_request.curve_point_count = curve_settings.point_count;
            std::vector<Pixel> curve_pixels;
            bool curve_ok = curve_renderer.render_active_cel(curve_doc, curve_request) &&
                            curve_renderer.read_output_pixels(curve_pixels) &&
                            outputs_match(curve_pixels, cpu_curve_doc.active_cel().pixels);
            if (!curve_ok && !curve_renderer.last_error().empty()) {
                std::cout << "       " << curve_renderer.last_error() << "\n";
            }
            ok = report_step("OpenGL curves effect matches CPU reference", curve_ok) && ok;
        }
        {
            Document transform_doc = Document::create(8, 8);
            for (int y = 0; y < transform_doc.height; ++y) {
                for (int x = 0; x < transform_doc.width; ++x) {
                    transform_doc.active_cel().pixels[static_cast<std::size_t>(y * transform_doc.width + x)] =
                        rgba(static_cast<std::uint8_t>(x * 29),
                             static_cast<std::uint8_t>(y * 31),
                             static_cast<std::uint8_t>((x + y) * 17),
                             255);
                }
            }
            Document cpu_transform_doc = transform_doc;
            apply_rotate_zoom(cpu_transform_doc, 90.0f, 1.0f, 0, 0, ResamplingMode::Nearest);
            GpuEffectRenderer transform_renderer;
            GpuEffectRequest transform_request;
            transform_request.mode = GpuEffectMode::AffineTransform;
            transform_request.params = {3.14159265358979323846f * 0.5f, 1.0f, 0.0f, 0.0f};
            std::vector<Pixel> transform_pixels;
            bool transform_ok = transform_renderer.render_active_cel(transform_doc, transform_request) &&
                                transform_renderer.read_output_pixels(transform_pixels) &&
                                outputs_match(transform_pixels, cpu_transform_doc.active_cel().pixels);
            if (!transform_ok && !transform_renderer.last_error().empty()) {
                std::cout << "       " << transform_renderer.last_error() << "\n";
            }
            ok = report_step("OpenGL affine transform matches CPU nearest reference", transform_ok) && ok;
        }
        {
            Document dof_doc = Document::create(16, 12);
            std::vector<Pixel> depth(static_cast<std::size_t>(dof_doc.width * dof_doc.height), rgba(0, 0, 0, 255));
            for (int y = 0; y < dof_doc.height; ++y) {
                for (int x = 0; x < dof_doc.width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y * dof_doc.width + x);
                    dof_doc.active_cel().pixels[index] =
                        rgba(static_cast<std::uint8_t>((x * 17 + y * 9) & 0xff),
                             static_cast<std::uint8_t>((x * 7 + y * 21) & 0xff),
                             static_cast<std::uint8_t>((x * 3 + y * 13) & 0xff),
                             255);
                    const std::uint8_t depth_value = static_cast<std::uint8_t>((x * 255) / std::max(1, dof_doc.width - 1));
                    depth[index] = rgba(depth_value, depth_value, depth_value, 255);
                }
            }
            Document cpu_dof_doc = dof_doc;
            DepthOfFieldSettings dof_settings;
            dof_settings.focus_depth = 128;
            dof_settings.aperture = 100;
            dof_settings.falloff = 32;
            dof_settings.max_radius = 4;
            apply_depth_of_field(cpu_dof_doc, depth, dof_settings);

            GpuEffectRequest dof_request;
            dof_request.mode = GpuEffectMode::DepthOfField;
            dof_request.params = {128.0f / 255.0f, 1.0f, 32.0f, 4.0f};
            dof_request.depth_pixels = depth;

            GpuEffectRenderer dof_renderer;
            std::vector<Pixel> dof_pixels;
            bool dof_ok = dof_renderer.render_active_cel(dof_doc, dof_request) &&
                          dof_renderer.read_output_pixels(dof_pixels) &&
                          outputs_match(dof_pixels, cpu_dof_doc.active_cel().pixels);
            if (!dof_ok && !dof_renderer.last_error().empty()) {
                std::cout << "       " << dof_renderer.last_error() << "\n";
            }
            ok = report_step("OpenGL depth of field matches CPU reference", dof_ok) && ok;

            GpuBackendCapabilities dof_caps;
            dof_caps.max_texture_size = 10;
            dof_caps.working_texture_budget = 10ULL * 10ULL * sizeof(Pixel) * 4ULL;
            dof_caps.supports_chunking = true;
            GpuEffectRenderer chunked_dof_renderer;
            std::vector<Pixel> chunked_dof_pixels;
            bool chunked_dof_ok = chunked_dof_renderer.render_active_cel(dof_doc, dof_request, &dof_caps) &&
                                  chunked_dof_renderer.read_output_pixels(chunked_dof_pixels) &&
                                  chunked_dof_renderer.used_chunking() &&
                                  outputs_match(dof_pixels, chunked_dof_pixels);
            if (!chunked_dof_ok && !chunked_dof_renderer.last_error().empty()) {
                std::cout << "       " << chunked_dof_renderer.last_error() << "\n";
            }
            ok = report_step("OpenGL chunked depth of field matches full output", chunked_dof_ok) && ok;
        }
#if defined(__APPLE__)
        {
            MpsEffectRenderer mps_renderer;
            const GpuBackendCapabilities mps_caps = mps_renderer.capabilities();
            std::cout << "MPS capabilities: max_texture_size=" << mps_caps.max_texture_size
                      << ", working_texture_budget=" << mps_caps.working_texture_budget
                      << ", supports_chunking=" << (mps_caps.supports_chunking ? "true" : "false") << "\n";
            GpuEffectRequest mps_request;
            mps_request.mode = GpuEffectMode::GaussianBlur;
            mps_request.params = {2.0f, 0.0f, 0.0f, 0.0f};
            if (mps_renderer.render_active_cel(doc, mps_request)) {
                std::vector<Pixel> mps_pixels;
                const bool mps_ok = mps_renderer.read_output_pixels(mps_pixels) &&
                                    std::any_of(mps_pixels.begin(), mps_pixels.end(), [](Pixel p) {
                                        return a(p) == 255 && r(p) > 0;
                                    });
                ok = report_step("Optional MPS blur effect and readback", mps_ok) && ok;
            } else {
                report_skip("Optional MPS blur effect", mps_renderer.last_error());
            }
        }
#endif
        {
            Document seam_doc = Document::create(32, 24);
            for (int y = 0; y < seam_doc.height; ++y) {
                for (int x = 0; x < seam_doc.width; ++x) {
                    seam_doc.active_cel().pixels[static_cast<std::size_t>(y * seam_doc.width + x)] =
                        rgba(static_cast<std::uint8_t>((x * 13 + y * 7) & 0xff),
                             static_cast<std::uint8_t>((x * 5 + y * 17) & 0xff),
                             static_cast<std::uint8_t>((x * 23 + y * 3) & 0xff),
                             255);
                }
            }
            GpuEffectRequest blur_request;
            blur_request.mode = GpuEffectMode::GaussianBlur;
            blur_request.params = {2.0f, 0.0f, 0.0f, 0.0f};

            GpuEffectRenderer full_renderer;
            std::vector<Pixel> full_pixels;
            bool full_render_ok = full_renderer.render_active_cel(seam_doc, blur_request) &&
                                  full_renderer.read_output_pixels(full_pixels);
            if (!full_render_ok && !full_renderer.last_error().empty()) {
                std::cout << "       " << full_renderer.last_error() << "\n";
            }
            ok = report_step("OpenGL seam baseline full-image blur", full_render_ok) && ok;

            GpuBackendCapabilities forced_caps;
            forced_caps.max_texture_size = 12;
            forced_caps.working_texture_budget = 12ULL * 12ULL * sizeof(Pixel) * 3ULL;
            forced_caps.supports_chunking = true;
            const int forced_extent = GpuEffectRenderer::choose_chunk_extent(
                seam_doc.width, seam_doc.height, GpuEffectRenderer::effect_chunk_halo(blur_request), forced_caps);

            GpuEffectRenderer chunked_renderer;
            std::vector<Pixel> chunked_pixels;
            bool chunk_render_ok = chunked_renderer.render_active_cel(seam_doc, blur_request, &forced_caps);
            if (!chunk_render_ok && !chunked_renderer.last_error().empty()) {
                std::cout << "       " << chunked_renderer.last_error() << "\n";
            }
            const bool chunk_read_ok = chunk_render_ok && chunked_renderer.read_output_pixels(chunked_pixels);
            const bool chunking_used = chunk_render_ok && chunked_renderer.used_chunking();
            const bool all_pixels_match = full_render_ok && chunk_read_ok && outputs_match(full_pixels, chunked_pixels);
            const bool seam_match = full_render_ok && chunk_read_ok &&
                                    seam_pixels_match(full_pixels, chunked_pixels, seam_doc.width, seam_doc.height, forced_extent);
            std::cout << "Forced chunk extent: " << forced_extent << "\n";
            ok = report_step("OpenGL forced chunk render", chunk_render_ok) && ok;
            ok = report_step("OpenGL forced chunk readback", chunk_read_ok) && ok;
            ok = report_step("OpenGL forced chunk path was used", chunking_used) && ok;
            ok = report_step("OpenGL chunked output matches full output", all_pixels_match) && ok;
            ok = report_step("OpenGL pixels on chunk borders match full output", seam_match) && ok;
        }

        GLCanvasTexture canvas;
        canvas.update(doc.width, doc.height, doc.composite_active());

        ModelDocument model = ModelDocument::create_default();
        model.texture_width = doc.width;
        model.texture_height = doc.height;
        clamp_model_uvs(model);
        Renderer3D renderer;
        ModelViewportState viewport;
        auto composite = doc.composite_active();
        bool model_render_ok = renderer.render_model_to_texture(model, canvas.id(), doc.width, doc.height, viewport, 96, 96, composite);
        if (model_render_ok) {
            std::vector<Pixel> pixels(96 * 96, 0);
            glBindTexture(GL_TEXTURE_2D, renderer.texture_id());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            model_render_ok = std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
                return a(p) != 0 && (r(p) != 0 || g(p) != 0 || b(p) != 0);
            });
        }
        ok = report_step("OpenGL 3D model render with populated texture", model_render_ok) && ok;
        std::fill(doc.active_cel().pixels.begin(), doc.active_cel().pixels.end(), 0);
        canvas.update(doc.width, doc.height, doc.composite_active());
        composite = doc.composite_active();
        bool transparent_model_ok = renderer.render_model_to_texture(model, canvas.id(), doc.width, doc.height, viewport, 96, 96, composite);
        if (transparent_model_ok) {
            std::vector<Pixel> pixels(96 * 96, 0);
            glBindTexture(GL_TEXTURE_2D, renderer.texture_id());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            transparent_model_ok = std::any_of(pixels.begin(), pixels.end(), [](Pixel p) {
                return a(p) != 0 && (r(p) != 0 || g(p) != 0 || b(p) != 0);
            });
        }
        ok = report_step("OpenGL 3D model render with transparent texture", transparent_model_ok) && ok;
    }

    if (!ok) {
        std::cerr << "OpenGL smoke failed: one or more required OpenGL checks failed\n";
        return 1;
    }
    std::cout << "OpenGL smoke passed\n";
    return 0;
}
