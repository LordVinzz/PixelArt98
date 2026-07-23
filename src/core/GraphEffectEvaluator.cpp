// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/GraphEffect.hpp"

#include "core/Filters.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <streambuf>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace px {

namespace {
using ImagePtr = std::shared_ptr<const std::vector<Pixel>>;

const GraphEffectParameterSpec* find_parameter_spec(const GraphEffectNodeSpec& spec,
                                                     std::string_view id) {
    const auto iterator = std::find_if(spec.parameters.begin(), spec.parameters.end(),
                                       [id](const GraphEffectParameterSpec& parameter) {
        return parameter.id == id;
    });
    return iterator == spec.parameters.end() ? nullptr : &*iterator;
}

bool link_is_used_for_evaluation(const GraphEffectGraph& graph, const GraphEffectLink& link) {
    const auto target = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&link](const GraphEffectNode& node) {
        return node.id == link.to_node;
    });
    return target == graph.nodes.end() || target->type_id != "combine.mix" || target->enabled ||
           link.to_port != "blend";
}

const GraphEffectParameter& parameter_value(const GraphEffectNode& node,
                                            const GraphEffectNodeSpec& spec,
                                            std::string_view id) {
    const auto value_iterator = node.parameters.find(std::string(id));
    if (value_iterator != node.parameters.end()) {
        return value_iterator->second;
    }
    const GraphEffectParameterSpec* parameter = find_parameter_spec(spec, id);
    if (parameter != nullptr) {
        return parameter->default_value;
    }
    static const GraphEffectParameter fallback = 0.0;
    return fallback;
}

double number_value(const GraphEffectNode& node,
                    const GraphEffectNodeSpec& spec,
                    std::string_view id) {
    const GraphEffectParameter& value = parameter_value(node, spec, id);
    if (const auto* number = std::get_if<double>(&value); number != nullptr) {
        return *number;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value); integer != nullptr) {
        return static_cast<double>(*integer);
    }
    return 0.0;
}

int integer_value(const GraphEffectNode& node,
                  const GraphEffectNodeSpec& spec,
                  std::string_view id) {
    const double value = number_value(node, spec, id);
    const double low = static_cast<double>(std::numeric_limits<int>::min());
    const double high = static_cast<double>(std::numeric_limits<int>::max());
    return static_cast<int>(std::clamp(std::round(value), low, high));
}

bool boolean_value(const GraphEffectNode& node,
                   const GraphEffectNodeSpec& spec,
                   std::string_view id) {
    const GraphEffectParameter& value = parameter_value(node, spec, id);
    const auto* result = std::get_if<bool>(&value);
    return result != nullptr && *result;
}

Pixel color_value(const GraphEffectNode& node,
                  const GraphEffectNodeSpec& spec,
                  std::string_view id) {
    const GraphEffectParameter& value = parameter_value(node, spec, id);
    const auto* result = std::get_if<Pixel>(&value);
    return result == nullptr ? rgba(0, 0, 0, 255) : *result;
}

std::string string_value(const GraphEffectNode& node,
                         const GraphEffectNodeSpec& spec,
                         std::string_view id) {
    const GraphEffectParameter& value = parameter_value(node, spec, id);
    const auto* result = std::get_if<std::string>(&value);
    return result == nullptr ? std::string{} : *result;
}

std::vector<double> array_value(const GraphEffectNode& node,
                                const GraphEffectNodeSpec& spec,
                                std::string_view id) {
    const GraphEffectParameter& value = parameter_value(node, spec, id);
    const auto* result = std::get_if<std::vector<double>>(&value);
    return result == nullptr ? std::vector<double>{} : *result;
}
LayerBlendMode blend_mode_from_id(std::string_view id, bool& valid) {
    valid = true;
    if (id == "normal") return LayerBlendMode::Normal;
    if (id == "multiply") return LayerBlendMode::Multiply;
    if (id == "additive") return LayerBlendMode::Additive;
    if (id == "color-burn") return LayerBlendMode::ColorBurn;
    if (id == "color-dodge") return LayerBlendMode::ColorDodge;
    if (id == "reflect") return LayerBlendMode::Reflect;
    if (id == "glow") return LayerBlendMode::Glow;
    if (id == "overlay") return LayerBlendMode::Overlay;
    if (id == "difference") return LayerBlendMode::Difference;
    if (id == "negation") return LayerBlendMode::Negation;
    if (id == "lighten") return LayerBlendMode::Lighten;
    if (id == "darken") return LayerBlendMode::Darken;
    if (id == "screen") return LayerBlendMode::Screen;
    if (id == "xor") return LayerBlendMode::Xor;
    valid = false;
    return LayerBlendMode::Normal;
}

std::uint8_t clamp_blend_channel(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

Pixel apply_graph_blend_mode(Pixel base, Pixel blend, LayerBlendMode mode) {
    const auto apply = [base, blend](auto operation) {
        return rgba(clamp_blend_channel(operation(r(base), r(blend))),
                    clamp_blend_channel(operation(g(base), g(blend))),
                    clamp_blend_channel(operation(b(base), b(blend))), a(blend));
    };
    switch (mode) {
        case LayerBlendMode::Normal:
            return blend;
        case LayerBlendMode::Multiply:
            return apply([](int first, int second) { return (first * second) / 255; });
        case LayerBlendMode::Additive:
            return apply([](int first, int second) { return first + second; });
        case LayerBlendMode::ColorBurn:
            return apply([](int first, int second) {
                return second == 0 ? 0 : std::max(0, 255 - ((255 - first) * 255) / second);
            });
        case LayerBlendMode::ColorDodge:
            return apply([](int first, int second) {
                return second == 255 ? 255 : std::min(255, (first * 255) / (255 - second));
            });
        case LayerBlendMode::Reflect:
            return apply([](int first, int second) {
                if (second == 0) return first;
                if (second == 255) return 255;
                return std::min(255, (first * first) / (255 - second));
            });
        case LayerBlendMode::Glow:
            return apply([](int first, int second) {
                if (second == 0) return first;
                if (first == 255) return 255;
                return std::min(255, (second * second) / (255 - first));
            });
        case LayerBlendMode::Overlay:
            return apply([](int first, int second) {
                return second < 128 ? (2 * first * second) / 255
                                    : 255 - (2 * (255 - first) * (255 - second)) / 255;
            });
        case LayerBlendMode::Difference:
            return apply([](int first, int second) { return std::abs(first - second); });
        case LayerBlendMode::Negation:
            return apply([](int first, int second) { return 255 - std::abs(255 - first - second); });
        case LayerBlendMode::Lighten:
            return apply([](int first, int second) { return std::max(first, second); });
        case LayerBlendMode::Darken:
            return apply([](int first, int second) { return std::min(first, second); });
        case LayerBlendMode::Screen:
            return apply([](int first, int second) {
                return 255 - ((255 - first) * (255 - second)) / 255;
            });
        case LayerBlendMode::Xor:
            return apply([](int first, int second) { return first ^ second; });
    }
    return blend;
}

Pixel composite_graph_blend(Pixel base, Pixel blend, LayerBlendMode mode, float opacity) {
    const float clamped_opacity = std::clamp(opacity, 0.0f, 1.0f);
    const float base_alpha = static_cast<float>(a(base)) / 255.0f;
    const float blend_alpha = (static_cast<float>(a(blend)) / 255.0f) * clamped_opacity;
    if (blend_alpha <= 0.0f) return base;
    if (base_alpha <= 0.0f && clamped_opacity >= 1.0f) return blend;

    const float output_alpha = blend_alpha + base_alpha * (1.0f - blend_alpha);
    if (output_alpha <= 0.000001f) return 0;
    const Pixel mode_color = apply_graph_blend_mode(base, blend, mode);
    const auto channel = [&](std::uint8_t base_channel,
                             std::uint8_t blend_channel,
                             std::uint8_t mode_channel) {
        const float base_value = static_cast<float>(base_channel) / 255.0f;
        const float blend_value = static_cast<float>(blend_channel) / 255.0f;
        const float mode_value = static_cast<float>(mode_channel) / 255.0f;
        const float premultiplied =
            (1.0f - blend_alpha) * base_alpha * base_value +
            (1.0f - base_alpha) * blend_alpha * blend_value +
            base_alpha * blend_alpha * mode_value;
        return static_cast<std::uint8_t>(std::clamp(
            std::round((premultiplied / output_alpha) * 255.0f), 0.0f, 255.0f));
    };
    return rgba(channel(r(base), r(blend), r(mode_color)),
                channel(g(base), g(blend), g(mode_color)),
                channel(b(base), b(blend), b(mode_color)),
                static_cast<std::uint8_t>(std::clamp(std::round(output_alpha * 255.0f), 0.0f, 255.0f)));
}

void copy_selection_to_scratch(const Document& source, Document& scratch) {
    const std::size_t expected = static_cast<std::size_t>(scratch.width) *
                                 static_cast<std::size_t>(scratch.height);
    if (source.selection.width == scratch.width && source.selection.height == scratch.height &&
        source.selection.mask.size() == expected) {
        scratch.selection = source.selection;
    }
}

Document make_scratch(const Document& source, const std::vector<Pixel>& pixels) {
    Document scratch = Document::create(source.width, source.height);
    scratch.active_cel().pixels = pixels;
    scratch.palette = source.palette;
    copy_selection_to_scratch(source, scratch);
    return scratch;
}

std::uint8_t interpolate_channel(std::uint8_t first, std::uint8_t second, double amount) {
    const double value = static_cast<double>(first) * (1.0 - amount) +
                         static_cast<double>(second) * amount;
    return static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
}

Pixel interpolate_pixel(Pixel first, Pixel second, double amount) {
    const double clamped = std::clamp(amount, 0.0, 1.0);
    return rgba(interpolate_channel(r(first), r(second), clamped),
                interpolate_channel(g(first), g(second), clamped),
                interpolate_channel(b(first), b(second), clamped),
                interpolate_channel(a(first), a(second), clamped));
}

std::vector<Pixel> evaluate_generator(const GraphEffectNode& node,
                                      const GraphEffectNodeSpec& spec,
                                      const Document& source) {
    Document scratch = make_scratch(source,
                                    std::vector<Pixel>(static_cast<std::size_t>(source.width) *
                                                       static_cast<std::size_t>(source.height), 0));
    if (!node.enabled) {
        return std::move(scratch.active_cel().pixels);
    }
    if (node.type_id == "generator.solid") {
        const Pixel color = color_value(node, spec, "color");
        for (int y = 0; y < source.height; ++y) {
            for (int x = 0; x < source.width; ++x) {
                if (scratch.selection.contains(x, y)) {
                    scratch.active_cel().pixels[static_cast<std::size_t>(scratch.pixel_index(x, y))] = color;
                }
            }
        }
    } else if (node.type_id == "generator.gradient") {
        const Pixel first = color_value(node, spec, "color_a");
        const Pixel second = color_value(node, spec, "color_b");
        const double x0 = number_value(node, spec, "x0");
        const double y0 = number_value(node, spec, "y0");
        const double x1 = number_value(node, spec, "x1");
        const double y1 = number_value(node, spec, "y1");
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        const double denominator = std::max(0.0000001, dx * dx + dy * dy);
        for (int y = 0; y < source.height; ++y) {
            const double normalized_y = source.height <= 1 ? 0.0 :
                static_cast<double>(y) / static_cast<double>(source.height - 1);
            for (int x = 0; x < source.width; ++x) {
                if (!scratch.selection.contains(x, y)) continue;
                const double normalized_x = source.width <= 1 ? 0.0 :
                    static_cast<double>(x) / static_cast<double>(source.width - 1);
                const double amount = ((normalized_x - x0) * dx + (normalized_y - y0) * dy) /
                                      denominator;
                scratch.active_cel().pixels[static_cast<std::size_t>(scratch.pixel_index(x, y))] =
                    interpolate_pixel(first, second, amount);
            }
        }
    } else if (node.type_id == "generator.clouds") {
        apply_clouds(scratch, integer_value(node, spec, "scale"),
                     integer_value(node, spec, "roughness"),
                     color_value(node, spec, "color_a"), color_value(node, spec, "color_b"));
    } else if (node.type_id == "generator.turbulence") {
        apply_turbulence(scratch, integer_value(node, spec, "scale"),
                         integer_value(node, spec, "octaves"));
    } else if (node.type_id == "generator.julia") {
        apply_julia_fractal(scratch, static_cast<float>(number_value(node, spec, "zoom")),
                            static_cast<float>(number_value(node, spec, "angle")));
    } else if (node.type_id == "generator.mandelbrot") {
        apply_mandelbrot_fractal(scratch, static_cast<float>(number_value(node, spec, "zoom")),
                                 static_cast<float>(number_value(node, spec, "angle")),
                                 boolean_value(node, spec, "invert"));
    }
    scratch.clear_history();
    return std::move(scratch.active_cel().pixels);
}

void apply_unary_effect(const GraphEffectNode& node,
                        const GraphEffectNodeSpec& spec,
                        Document& scratch) {
    const std::string& type = node.type_id;
    if (type == "adjustment.brightness-contrast") {
        apply_brightness_contrast(scratch, integer_value(node, spec, "brightness"),
                                  integer_value(node, spec, "contrast"));
    } else if (type == "adjustment.hsv") {
        apply_hsv(scratch, static_cast<float>(number_value(node, spec, "hue")),
                  static_cast<float>(number_value(node, spec, "saturation")),
                  static_cast<float>(number_value(node, spec, "value")));
    } else if (type == "adjustment.temperature") {
        apply_temperature(scratch, integer_value(node, spec, "temperature"));
    } else if (type == "adjustment.levels") {
        LevelsSettings settings;
        settings.in_black = integer_value(node, spec, "input_black");
        settings.in_white = integer_value(node, spec, "input_white");
        settings.gamma = static_cast<float>(number_value(node, spec, "gamma"));
        settings.out_black = integer_value(node, spec, "output_black");
        settings.out_white = integer_value(node, spec, "output_white");
        settings.red = boolean_value(node, spec, "red");
        settings.green = boolean_value(node, spec, "green");
        settings.blue = boolean_value(node, spec, "blue");
        apply_levels(scratch, settings);
    } else if (type == "adjustment.tonal-range") {
        apply_tonal_range(scratch, integer_value(node, spec, "white_point"),
                          integer_value(node, spec, "highlights"),
                          integer_value(node, spec, "shadows"),
                          integer_value(node, spec, "black_point"));
    } else if (type == "adjustment.curves") {
        CurvesSettings settings;
        const std::vector<double> curve_x = array_value(node, spec, "curve_x");
        const std::vector<double> curve_y = array_value(node, spec, "curve_y");
        const std::size_t count = std::min({curve_x.size(), curve_y.size(),
                                            static_cast<std::size_t>(kMaxCurvePoints)});
        settings.point_count = static_cast<int>(std::max<std::size_t>(2, count));
        for (std::size_t index = 0; index < count; ++index) {
            settings.x[index] = static_cast<float>(curve_x[index]);
            settings.y[index] = static_cast<float>(curve_y[index]);
        }
        settings.luma = boolean_value(node, spec, "luminance");
        settings.red = boolean_value(node, spec, "red");
        settings.green = boolean_value(node, spec, "green");
        settings.blue = boolean_value(node, spec, "blue");
        apply_curves(scratch, settings);
    } else if (type == "adjustment.auto-level") {
        apply_auto_level(scratch);
    } else if (type == "adjustment.posterize") {
        apply_posterize(scratch, integer_value(node, spec, "levels"));
    } else if (type == "adjustment.quantize") {
        apply_palette_quantize(scratch, scratch.palette, false);
    } else if (type == "adjustment.dither") {
        apply_palette_quantize(scratch, scratch.palette, true);
    } else if (type == "effect.grayscale") {
        apply_grayscale(scratch);
    } else if (type == "effect.sepia") {
        apply_sepia(scratch);
    } else if (type == "effect.invert-colors") {
        apply_invert(scratch, false);
    } else if (type == "effect.invert-alpha") {
        apply_invert(scratch, true);
    } else if (type == "effect.gaussian-blur") {
        apply_gaussian_blur(scratch, integer_value(node, spec, "radius"));
    } else if (type == "effect.motion-blur") {
        apply_motion_blur(scratch, integer_value(node, spec, "distance"),
                          static_cast<float>(number_value(node, spec, "angle")));
    } else if (type == "effect.radial-blur") {
        apply_radial_blur(scratch, integer_value(node, spec, "amount"),
                          integer_value(node, spec, "center_x"), integer_value(node, spec, "center_y"));
    } else if (type == "effect.zoom-blur") {
        apply_zoom_blur(scratch, integer_value(node, spec, "amount"),
                        integer_value(node, spec, "center_x"), integer_value(node, spec, "center_y"));
    } else if (type == "effect.median-blur") {
        apply_median_blur(scratch, integer_value(node, spec, "radius"));
    } else if (type == "effect.surface-blur") {
        apply_surface_blur(scratch, integer_value(node, spec, "radius"),
                           integer_value(node, spec, "threshold"));
    } else if (type == "effect.pixelate") {
        apply_pixelate(scratch, integer_value(node, spec, "cell_size"));
    } else if (type == "effect.crystalize") {
        apply_crystalize(scratch, integer_value(node, spec, "cell_size"));
    } else if (type == "effect.frosted-glass") {
        apply_frosted_glass(scratch, integer_value(node, spec, "radius"));
    } else if (type == "effect.bulge") {
        apply_bulge(scratch, static_cast<float>(number_value(node, spec, "strength")),
                    integer_value(node, spec, "center_x"), integer_value(node, spec, "center_y"));
    } else if (type == "effect.twist") {
        apply_twist(scratch, static_cast<float>(number_value(node, spec, "turns")),
                    integer_value(node, spec, "center_x"), integer_value(node, spec, "center_y"),
                    integer_value(node, spec, "effect_size"));
    } else if (type == "effect.tile-reflection") {
        apply_tile_reflection(scratch, integer_value(node, spec, "tile_size"));
    } else if (type == "effect.dents") {
        apply_dents(scratch, integer_value(node, spec, "scale"), integer_value(node, spec, "amount"));
    } else if (type == "effect.polar-inversion") {
        apply_polar_inversion(scratch, static_cast<float>(number_value(node, spec, "scale")));
    } else if (type == "effect.add-noise") {
        apply_add_noise(scratch, integer_value(node, spec, "intensity"),
                        integer_value(node, spec, "coverage"),
                        integer_value(node, spec, "color_saturation"));
    } else if (type == "effect.reduce-noise") {
        apply_reduce_noise(scratch, integer_value(node, spec, "radius"));
    } else if (type == "effect.glow") {
        apply_glow(scratch, integer_value(node, spec, "radius"),
                   integer_value(node, spec, "brightness"), integer_value(node, spec, "contrast"));
    } else if (type == "effect.red-eye-removal") {
        apply_red_eye_removal(scratch, integer_value(node, spec, "strength"));
    } else if (type == "effect.sharpen") {
        apply_sharpen(scratch, integer_value(node, spec, "amount"));
    } else if (type == "effect.soften-portrait") {
        apply_soften_portrait(scratch, integer_value(node, spec, "softness"),
                              integer_value(node, spec, "lighting"), integer_value(node, spec, "warmth"));
    } else if (type == "effect.vignette") {
        apply_vignette(scratch, integer_value(node, spec, "radius"),
                       integer_value(node, spec, "strength"), integer_value(node, spec, "center_x"),
                       integer_value(node, spec, "center_y"));
    } else if (type == "effect.edge-detect") {
        apply_edge_detect(scratch, integer_value(node, spec, "strength"));
    } else if (type == "effect.emboss") {
        apply_emboss(scratch, static_cast<float>(number_value(node, spec, "angle")));
    } else if (type == "effect.outline") {
        apply_outline(scratch, integer_value(node, spec, "thickness"),
                      integer_value(node, spec, "intensity"));
    } else if (type == "effect.relief") {
        apply_relief(scratch, static_cast<float>(number_value(node, spec, "angle")));
    } else if (type == "effect.oil-painting") {
        apply_oil_painting(scratch, integer_value(node, spec, "brush_size"),
                           integer_value(node, spec, "coarseness"));
    } else if (type == "effect.ink-sketch") {
        apply_ink_sketch(scratch, integer_value(node, spec, "outline"),
                         integer_value(node, spec, "coloring"));
    } else if (type == "effect.pencil-sketch") {
        apply_pencil_sketch(scratch, integer_value(node, spec, "tip_size"),
                            integer_value(node, spec, "range"));
    }
}
} // namespace
GraphEffectEvaluation evaluate_graph_effect(const GraphEffectGraph& graph,
                                             const Document& document,
                                             std::optional<GraphEffectNodeId> inspected_node) {
    GraphEffectEvaluation evaluation;
    evaluation.width = document.width;
    evaluation.height = document.height;
    evaluation.inspected_node_id = inspected_node;
    GraphEffectValidation validation = validate_graph_effect(graph, GraphEffectValidationMode::Evaluable);
    evaluation.diagnostics = validation.diagnostics;
    const std::size_t expected = document.width > 0 && document.height > 0
                                     ? static_cast<std::size_t>(document.width) *
                                           static_cast<std::size_t>(document.height)
                                     : 0U;
    if (!document.valid() || !document.has_active_cel() || document.active_cel().pixels.size() != expected) {
        evaluation.diagnostics.push_back({GraphEffectDiagnosticSeverity::Error, "invalid-document",
                                          "Cannot evaluate GraphEffect on an invalid document", std::nullopt,
                                          std::nullopt});
    }
    if (!validation.ok() || expected == 0U || !document.has_active_cel()) return evaluation;

    using OutputKey = std::pair<GraphEffectNodeId, std::string>;
    std::map<OutputKey, ImagePtr> outputs;
    const auto input_for = [&graph, &outputs](GraphEffectNodeId node_id, std::string_view port) -> ImagePtr {
        const auto link = std::find_if(graph.links.begin(), graph.links.end(),
                                       [node_id, port](const GraphEffectLink& candidate) {
            return candidate.to_node == node_id && candidate.to_port == port;
        });
        if (link == graph.links.end()) return {};
        const auto output = outputs.find({link->from_node, link->from_port});
        return output == outputs.end() ? ImagePtr{} : output->second;
    };

    GraphEffectNodeId output_node_id = 0;
    for (const GraphEffectNode& node : graph.nodes) {
        if (node.type_id == "output") output_node_id = node.id;
    }
    std::unordered_set<GraphEffectNodeId> reachable;
    std::vector<GraphEffectNodeId> pending{output_node_id};
    while (!pending.empty()) {
        const GraphEffectNodeId current = pending.back();
        pending.pop_back();
        if (!reachable.insert(current).second) continue;
        for (const GraphEffectLink& link : graph.links) {
            if (link.to_node == current && link_is_used_for_evaluation(graph, link)) {
                pending.push_back(link.from_node);
            }
        }
    }
    std::map<OutputKey, std::size_t> remaining_consumers;
    for (const GraphEffectLink& link : graph.links) {
        if (reachable.contains(link.from_node) && reachable.contains(link.to_node) &&
            link_is_used_for_evaluation(graph, link)) {
            ++remaining_consumers[{link.from_node, link.from_port}];
        }
    }
    const auto output_is_used = [&remaining_consumers](GraphEffectNodeId node_id,
                                                        std::string_view port) {
        const auto consumer = remaining_consumers.find({node_id, std::string(port)});
        return consumer != remaining_consumers.end() && consumer->second > 0U;
    };
    try {
        for (GraphEffectNodeId id : validation.topological_order) {
            if (!reachable.contains(id)) continue;
            const GraphEffectNode* node = find_graph_effect_node(graph, id);
            if (node == nullptr) continue;
            const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node->type_id);
            if (spec == nullptr) continue;
            ImagePtr image_input;
            ImagePtr output;
            std::map<std::string, ImagePtr> produced_outputs;
            if (node->type_id == "source.active-cel") {
                output = std::make_shared<const std::vector<Pixel>>(document.active_cel().pixels);
            } else if (node->type_id == "output") {
                image_input = input_for(id, "image");
                output = image_input;
            } else if (node->type_id.starts_with("generator.")) {
                output = std::make_shared<const std::vector<Pixel>>(evaluate_generator(*node, *spec, document));
            } else if (node->type_id == "channel.split-rgba") {
                image_input = input_for(id, "image");
                if (!image_input || image_input->size() != expected) {
                    throw std::runtime_error("Split Channels node received invalid input");
                }
                constexpr std::array<std::string_view, 4> channel_ports = {
                    "red", "green", "blue", "alpha"};
                for (std::size_t channel = 0; channel < channel_ports.size(); ++channel) {
                    const std::string_view port = channel_ports[channel];
                    if (!output_is_used(id, port)) continue;
                    auto channel_pixels = std::make_shared<std::vector<Pixel>>(expected, 0);
                    for (std::size_t index = 0; index < expected; ++index) {
                        const Pixel source = (*image_input)[index];
                        std::uint8_t value = 0;
                        switch (channel) {
                            case 0U: value = r(source); break;
                            case 1U: value = g(source); break;
                            case 2U: value = b(source); break;
                            default: value = a(source); break;
                        }
                        (*channel_pixels)[index] = rgba(value, value, value, 255);
                    }
                    produced_outputs.emplace(std::string(port), std::move(channel_pixels));
                }
            } else if (node->type_id == "channel.merge-rgba") {
                const std::array<ImagePtr, 4> channels = {
                    input_for(id, "red"), input_for(id, "green"),
                    input_for(id, "blue"), input_for(id, "alpha")};
                if (std::any_of(channels.begin(), channels.end(), [expected](const ImagePtr& image) {
                        return !image || image->size() != expected;
                    })) {
                    throw std::runtime_error("Merge Channels node received invalid input");
                }
                const auto channel_value = [](Pixel pixel) {
                    return static_cast<std::uint8_t>(std::clamp(
                        static_cast<int>(luminance(pixel) + 0.5F), 0, 255));
                };
                auto merged = std::make_shared<std::vector<Pixel>>(expected, 0);
                for (std::size_t index = 0; index < expected; ++index) {
                    (*merged)[index] = rgba(channel_value((*channels[0])[index]),
                                            channel_value((*channels[1])[index]),
                                            channel_value((*channels[2])[index]),
                                            channel_value((*channels[3])[index]));
                }
                output = std::move(merged);
            } else if (node->type_id == "combine.mix") {
                const ImagePtr base = input_for(id, "base");
                if (!node->enabled) {
                    output = base;
                } else {
                    const ImagePtr blend = input_for(id, "blend");
                    bool valid_mode = false;
                    const LayerBlendMode mode = blend_mode_from_id(string_value(*node, *spec, "blend_mode"), valid_mode);
                    if (!valid_mode || !base || !blend || base->size() != expected || blend->size() != expected) {
                        throw std::runtime_error("Mix node received invalid input");
                    }
                    const float opacity = static_cast<float>(number_value(*node, *spec, "opacity"));
                    auto mixed = std::make_shared<std::vector<Pixel>>(expected, 0);
                    for (std::size_t index = 0; index < expected; ++index) {
                        (*mixed)[index] = composite_graph_blend((*base)[index], (*blend)[index], mode, opacity);
                    }
                    output = std::move(mixed);
                }
            } else {
                image_input = input_for(id, "image");
                if (!image_input || image_input->size() != expected) {
                    throw std::runtime_error("Effect node received invalid input");
                }
                if (!node->enabled) {
                    output = image_input;
                } else {
                    Document scratch = make_scratch(document, *image_input);
                    apply_unary_effect(*node, *spec, scratch);
                    scratch.clear_history();
                    output = std::make_shared<const std::vector<Pixel>>(std::move(scratch.active_cel().pixels));
                }
            }
            if (output) produced_outputs.emplace("image", output);
            if (produced_outputs.empty() ||
                std::any_of(produced_outputs.begin(), produced_outputs.end(),
                            [expected](const auto& entry) {
                                return !entry.second || entry.second->size() != expected;
                            })) {
                throw std::runtime_error("GraphEffect node produced an invalid image");
            }
            if (inspected_node && id == *inspected_node) {
                evaluation.inspected_node_evaluated = true;
                if (image_input) evaluation.inspected_input_pixels = *image_input;
                const auto image_output = produced_outputs.find("image");
                if (image_output != produced_outputs.end()) {
                    evaluation.inspected_output_pixels = *image_output->second;
                }
            }
            // Do not keep an extra reference alive while the normal consumer
            // accounting releases the source image below.
            image_input.reset();
            output.reset();
            for (auto& [port, image] : produced_outputs) {
                if (node->type_id != "output" && !output_is_used(id, port)) continue;
                outputs.insert_or_assign({id, port}, std::move(image));
            }
            evaluation.peak_cached_images = std::max(evaluation.peak_cached_images, outputs.size());
            for (const GraphEffectLink& link : graph.links) {
                if (link.to_node != id || !reachable.contains(link.from_node) ||
                    !link_is_used_for_evaluation(graph, link)) {
                    continue;
                }
                const OutputKey source_output{link.from_node, link.from_port};
                auto remaining = remaining_consumers.find(source_output);
                if (remaining == remaining_consumers.end() || remaining->second == 0U) continue;
                --remaining->second;
                if (remaining->second == 0U) outputs.erase(source_output);
            }
        }
    } catch (const std::exception& exception) {
        evaluation.diagnostics.push_back({GraphEffectDiagnosticSeverity::Error, "evaluation-failed",
                                          exception.what(), std::nullopt, std::nullopt});
        return evaluation;
    }

    const auto output = outputs.find({output_node_id, "image"});
    if (output == outputs.end() || !output->second) {
        evaluation.diagnostics.push_back({GraphEffectDiagnosticSeverity::Error, "missing-output",
                                          "GraphEffect did not produce an output image", output_node_id,
                                          std::nullopt});
        return evaluation;
    }
    evaluation.pixels = *output->second;
    if (document.selection.active && document.selection.width == document.width &&
        document.selection.height == document.height && document.selection.mask.size() == expected) {
        for (std::size_t index = 0; index < expected; ++index) {
            if (document.selection.mask[index] == 0U) {
                evaluation.pixels[index] = document.active_cel().pixels[index];
            }
        }
    }
    evaluation.success = true;
    return evaluation;
}
} // namespace px
