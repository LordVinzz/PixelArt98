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

constexpr std::size_t kMaximumGraphNodes = 4096;
constexpr std::size_t kMaximumGraphLinks = 16384;
constexpr std::size_t kMaximumGraphParameters = 256;
constexpr std::uintmax_t kMaximumGraphFileBytes = 16U * 1024U * 1024U;

using ImagePtr = std::shared_ptr<const std::vector<Pixel>>;

void set_error(std::string* error, std::string value) {
    if (error != nullptr) {
        *error = std::move(value);
    }
}

GraphEffectPortSpec image_port(std::string id, std::string label, bool required = true) {
    return {std::move(id), std::move(label), GraphEffectPortType::Image, required};
}

GraphEffectParameterSpec integer_parameter(std::string id,
                                           std::string label,
                                           std::int64_t initial,
                                           double minimum,
                                           double maximum) {
    return {std::move(id), std::move(label), GraphEffectParameterKind::Integer,
            GraphEffectParameter{initial}, minimum, maximum, {}};
}

GraphEffectParameterSpec number_parameter(std::string id,
                                          std::string label,
                                          double initial,
                                          double minimum,
                                          double maximum) {
    return {std::move(id), std::move(label), GraphEffectParameterKind::Number,
            GraphEffectParameter{initial}, minimum, maximum, {}};
}

GraphEffectParameterSpec boolean_parameter(std::string id, std::string label, bool initial) {
    return {std::move(id), std::move(label), GraphEffectParameterKind::Boolean,
            GraphEffectParameter{initial}, std::nullopt, std::nullopt, {}};
}

GraphEffectParameterSpec color_parameter(std::string id, std::string label, Pixel initial) {
    return {std::move(id), std::move(label), GraphEffectParameterKind::Color,
            GraphEffectParameter{initial}, std::nullopt, std::nullopt, {}};
}

GraphEffectParameterSpec choice_parameter(std::string id,
                                          std::string label,
                                          std::string initial,
                                          std::vector<std::string> choices) {
    return {std::move(id), std::move(label), GraphEffectParameterKind::Choice,
            GraphEffectParameter{std::move(initial)}, std::nullopt, std::nullopt,
            std::move(choices)};
}

GraphEffectParameterSpec array_parameter(std::string id,
                                         std::string label,
                                         std::vector<double> initial) {
    return {std::move(id), std::move(label), GraphEffectParameterKind::NumberArray,
            GraphEffectParameter{std::move(initial)}, std::nullopt, std::nullopt, {}};
}

GraphEffectNodeSpec unary_node(std::string type_id,
                               std::string label,
                               std::string category,
                               std::vector<GraphEffectParameterSpec> parameters = {}) {
    return {std::move(type_id), std::move(label), std::move(category),
            {image_port("image", "Image")}, {image_port("image", "Image")},
            std::move(parameters)};
}

GraphEffectNodeSpec generator_node(std::string type_id,
                                   std::string label,
                                   std::vector<GraphEffectParameterSpec> parameters) {
    return {std::move(type_id), std::move(label), "Generators", {},
            {image_port("image", "Image")}, std::move(parameters)};
}

std::vector<std::string> blend_mode_choices() {
    return {"normal", "multiply", "additive", "color-burn", "color-dodge",
            "reflect", "glow", "overlay", "difference", "negation",
            "lighten", "darken", "screen", "xor"};
}

std::vector<GraphEffectNodeSpec> build_catalog() {
    std::vector<GraphEffectNodeSpec> specs;
    specs.reserve(52);
    specs.push_back({"source.active-cel", "Active Cel", "Input", {},
                     {image_port("image", "Image")}, {}, false});
    specs.push_back({"output", "Output", "Output", {image_port("image", "Image")}, {}, {}, false});
    specs.push_back({"combine.mix", "Mix", "Combine",
                     {image_port("base", "Base"), image_port("blend", "Blend")},
                     {image_port("image", "Image")},
                     {number_parameter("opacity", "Opacity", 1.0, 0.0, 1.0),
                      choice_parameter("blend_mode", "Blend mode", "normal", blend_mode_choices())}});
    specs.push_back({"channel.split-rgba", "Split Channels", "Channels",
                     {image_port("image", "Image")},
                     {image_port("red", "Red"), image_port("green", "Green"),
                      image_port("blue", "Blue"), image_port("alpha", "Alpha")},
                     {}, false});
    specs.push_back({"channel.merge-rgba", "Merge Channels", "Channels",
                     {image_port("red", "Red"), image_port("green", "Green"),
                      image_port("blue", "Blue"), image_port("alpha", "Alpha")},
                     {image_port("image", "Image")}, {}, false});

    specs.push_back(generator_node("generator.solid", "Solid Color",
                                   {color_parameter("color", "Color", rgba(255, 255, 255, 255))}));
    specs.push_back(generator_node("generator.gradient", "Gradient",
                                   {color_parameter("color_a", "Start color", rgba(0, 0, 0, 255)),
                                    color_parameter("color_b", "End color", rgba(255, 255, 255, 255)),
                                    number_parameter("x0", "Start X", 0.0, 0.0, 1.0),
                                    number_parameter("y0", "Start Y", 0.0, 0.0, 1.0),
                                    number_parameter("x1", "End X", 1.0, 0.0, 1.0),
                                    number_parameter("y1", "End Y", 1.0, 0.0, 1.0)}));
    specs.push_back(generator_node("generator.clouds", "Clouds",
                                   {integer_parameter("scale", "Scale", 96, 2.0, 512.0),
                                    integer_parameter("roughness", "Roughness", 4, 1.0, 8.0),
                                    color_parameter("color_a", "Color A", rgba(0, 0, 0, 255)),
                                    color_parameter("color_b", "Color B", rgba(255, 255, 255, 255))}));
    specs.push_back(generator_node("generator.turbulence", "Turbulence",
                                   {integer_parameter("scale", "Scale", 96, 2.0, 512.0),
                                    integer_parameter("octaves", "Octaves", 4, 1.0, 8.0)}));
    specs.push_back(generator_node("generator.julia", "Julia Fractal",
                                   {number_parameter("zoom", "Zoom", 1.0, 0.1, 10.0),
                                    number_parameter("angle", "Angle", 0.0, -180.0, 180.0)}));
    specs.push_back(generator_node("generator.mandelbrot", "Mandelbrot Fractal",
                                   {number_parameter("zoom", "Zoom", 1.0, 0.1, 10.0),
                                    number_parameter("angle", "Angle", 0.0, -180.0, 180.0),
                                    boolean_parameter("invert", "Invert", false)}));

    specs.push_back(unary_node("adjustment.brightness-contrast", "Brightness / Contrast", "Adjustments",
                               {integer_parameter("brightness", "Brightness", 0, -255.0, 255.0),
                                integer_parameter("contrast", "Contrast", 0, -100.0, 100.0)}));
    specs.push_back(unary_node("adjustment.hsv", "HSV", "Adjustments",
                               {number_parameter("hue", "Hue", 0.0, -180.0, 180.0),
                                number_parameter("saturation", "Saturation", 0.0, -1.0, 1.0),
                                number_parameter("value", "Value", 0.0, -1.0, 1.0)}));
    specs.push_back(unary_node("adjustment.temperature", "Temperature", "Adjustments",
                               {integer_parameter("temperature", "Temperature", 0, -100.0, 100.0)}));
    specs.push_back(unary_node("adjustment.levels", "Levels", "Adjustments",
                               {integer_parameter("input_black", "Input black", 0, 0.0, 254.0),
                                integer_parameter("input_white", "Input white", 255, 1.0, 255.0),
                                number_parameter("gamma", "Gamma", 1.0, 0.05, 3.0),
                                integer_parameter("output_black", "Output black", 0, 0.0, 255.0),
                                integer_parameter("output_white", "Output white", 255, 0.0, 255.0),
                                boolean_parameter("red", "Red", true),
                                boolean_parameter("green", "Green", true),
                                boolean_parameter("blue", "Blue", true)}));
    specs.push_back(unary_node("adjustment.tonal-range", "Tonal Range", "Adjustments",
                               {integer_parameter("white_point", "White point", 0, -100.0, 100.0),
                                integer_parameter("highlights", "Highlights", 0, -100.0, 100.0),
                                integer_parameter("shadows", "Shadows", 0, -100.0, 100.0),
                                integer_parameter("black_point", "Black point", 0, -100.0, 100.0)}));
    specs.push_back(unary_node("adjustment.curves", "Curves", "Adjustments",
                               {array_parameter("curve_x", "Curve X", {0.0, 1.0}),
                                array_parameter("curve_y", "Curve Y", {0.0, 1.0}),
                                boolean_parameter("luminance", "Luminance", true),
                                boolean_parameter("red", "Red", false),
                                boolean_parameter("green", "Green", false),
                                boolean_parameter("blue", "Blue", false)}));
    specs.push_back(unary_node("adjustment.auto-level", "Auto-Level", "Adjustments"));
    specs.push_back(unary_node("adjustment.posterize", "Posterize", "Adjustments",
                               {integer_parameter("levels", "Levels", 8, 2.0, 64.0)}));
    specs.push_back(unary_node("adjustment.quantize", "Quantize", "Adjustments"));
    specs.push_back(unary_node("adjustment.dither", "Dither", "Adjustments"));

    specs.push_back(unary_node("effect.grayscale", "Grayscale", "Color"));
    specs.push_back(unary_node("effect.sepia", "Sepia", "Color"));
    specs.push_back(unary_node("effect.invert-colors", "Invert Colors", "Color"));
    specs.push_back(unary_node("effect.invert-alpha", "Invert Alpha", "Color"));
    specs.push_back(unary_node("effect.gaussian-blur", "Gaussian Blur", "Blurs",
                               {integer_parameter("radius", "Radius", 4, 1.0, 32.0)}));
    specs.push_back(unary_node("effect.motion-blur", "Motion Blur", "Blurs",
                               {integer_parameter("distance", "Distance", 8, 1.0, 24.0),
                                number_parameter("angle", "Angle", 0.0, -180.0, 180.0)}));
    specs.push_back(unary_node("effect.radial-blur", "Radial Blur", "Blurs",
                               {integer_parameter("amount", "Amount", 20, 2.0, 64.0),
                                integer_parameter("center_x", "Center X", 50, 0.0, 100.0),
                                integer_parameter("center_y", "Center Y", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.zoom-blur", "Zoom Blur", "Blurs",
                               {integer_parameter("amount", "Amount", 20, 2.0, 64.0),
                                integer_parameter("center_x", "Center X", 50, 0.0, 100.0),
                                integer_parameter("center_y", "Center Y", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.median-blur", "Median Blur", "Blurs",
                               {integer_parameter("radius", "Radius", 2, 1.0, 8.0)}));
    specs.push_back(unary_node("effect.surface-blur", "Surface Blur", "Blurs",
                               {integer_parameter("radius", "Radius", 3, 1.0, 8.0),
                                integer_parameter("threshold", "Threshold", 32, 0.0, 255.0)}));
    specs.push_back(unary_node("effect.pixelate", "Pixelate", "Distort",
                               {integer_parameter("cell_size", "Cell size", 8, 2.0, 64.0)}));
    specs.push_back(unary_node("effect.crystalize", "Crystalize", "Distort",
                               {integer_parameter("cell_size", "Cell size", 12, 2.0, 64.0)}));
    specs.push_back(unary_node("effect.frosted-glass", "Frosted Glass", "Distort",
                               {integer_parameter("radius", "Radius", 5, 1.0, 32.0)}));
    specs.push_back(unary_node("effect.bulge", "Bulge", "Distort",
                               {number_parameter("strength", "Strength", 0.5, -2.0, 2.0),
                                integer_parameter("center_x", "Center X", 50, 0.0, 100.0),
                                integer_parameter("center_y", "Center Y", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.twist", "Twist", "Distort",
                               {number_parameter("turns", "Turns", 1.0, -4.0, 4.0),
                                integer_parameter("center_x", "Center X", 50, 0.0, 100.0),
                                integer_parameter("center_y", "Center Y", 50, 0.0, 100.0),
                                integer_parameter("effect_size", "Effect size", 100, 10.0, 200.0)}));
    specs.push_back(unary_node("effect.tile-reflection", "Tile Reflection", "Distort",
                               {integer_parameter("tile_size", "Tile size", 32, 2.0, 256.0)}));
    specs.push_back(unary_node("effect.dents", "Dents", "Distort",
                               {integer_parameter("scale", "Scale", 64, 2.0, 256.0),
                                integer_parameter("amount", "Amount", 20, 1.0, 64.0)}));
    specs.push_back(unary_node("effect.polar-inversion", "Polar Inversion", "Distort",
                               {number_parameter("scale", "Scale", 1.0, 0.1, 2.0)}));
    specs.push_back(unary_node("effect.add-noise", "Add Noise", "Noise",
                               {integer_parameter("intensity", "Intensity", 64, 0.0, 255.0),
                                integer_parameter("coverage", "Coverage", 100, 0.0, 100.0),
                                integer_parameter("color_saturation", "Color saturation", 100, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.reduce-noise", "Reduce Noise", "Noise",
                               {integer_parameter("radius", "Radius", 2, 1.0, 8.0)}));
    specs.push_back(unary_node("effect.glow", "Glow", "Photo",
                               {integer_parameter("radius", "Radius", 5, 1.0, 16.0),
                                integer_parameter("brightness", "Brightness", 30, -100.0, 100.0),
                                integer_parameter("contrast", "Contrast", 0, -100.0, 100.0)}));
    specs.push_back(unary_node("effect.red-eye-removal", "Red Eye Removal", "Photo",
                               {integer_parameter("strength", "Strength", 70, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.sharpen", "Sharpen", "Photo",
                               {integer_parameter("amount", "Amount", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.soften-portrait", "Soften Portrait", "Photo",
                               {integer_parameter("softness", "Softness", 50, 0.0, 100.0),
                                integer_parameter("lighting", "Lighting", 0, -100.0, 100.0),
                                integer_parameter("warmth", "Warmth", 0, -100.0, 100.0)}));
    specs.push_back(unary_node("effect.vignette", "Vignette", "Photo",
                               {integer_parameter("radius", "Radius", 70, 10.0, 200.0),
                                integer_parameter("strength", "Strength", 60, 0.0, 100.0),
                                integer_parameter("center_x", "Center X", 50, 0.0, 100.0),
                                integer_parameter("center_y", "Center Y", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.edge-detect", "Edge Detect", "Stylize",
                               {integer_parameter("strength", "Strength", 100, 1.0, 200.0)}));
    specs.push_back(unary_node("effect.emboss", "Emboss", "Stylize",
                               {number_parameter("angle", "Angle", 45.0, -180.0, 180.0)}));
    specs.push_back(unary_node("effect.outline", "Outline", "Stylize",
                               {integer_parameter("thickness", "Thickness", 3, 1.0, 16.0),
                                integer_parameter("intensity", "Intensity", 255, 0.0, 255.0)}));
    specs.push_back(unary_node("effect.relief", "Relief", "Stylize",
                               {number_parameter("angle", "Angle", 45.0, -180.0, 180.0)}));
    specs.push_back(unary_node("effect.oil-painting", "Oil Painting", "Artistic",
                               {integer_parameter("brush_size", "Brush size", 6, 1.0, 16.0),
                                integer_parameter("coarseness", "Coarseness", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.ink-sketch", "Ink Sketch", "Artistic",
                               {integer_parameter("outline", "Outline", 50, 0.0, 100.0),
                                integer_parameter("coloring", "Coloring", 50, 0.0, 100.0)}));
    specs.push_back(unary_node("effect.pencil-sketch", "Pencil Sketch", "Artistic",
                               {integer_parameter("tip_size", "Tip size", 5, 1.0, 10.0),
                                integer_parameter("range", "Range", 50, 0.0, 100.0)}));
    return specs;
}

const GraphEffectPortSpec* find_port(const std::vector<GraphEffectPortSpec>& ports,
                                     std::string_view id) {
    const auto iterator = std::find_if(ports.begin(), ports.end(), [id](const GraphEffectPortSpec& port) {
        return port.id == id;
    });
    return iterator == ports.end() ? nullptr : &*iterator;
}

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

bool parameter_matches_kind(const GraphEffectParameter& value, GraphEffectParameterKind kind) {
    switch (kind) {
        case GraphEffectParameterKind::Integer:
            return std::holds_alternative<std::int64_t>(value);
        case GraphEffectParameterKind::Number:
            return std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value);
        case GraphEffectParameterKind::Boolean:
            return std::holds_alternative<bool>(value);
        case GraphEffectParameterKind::Color:
            return std::holds_alternative<Pixel>(value);
        case GraphEffectParameterKind::Choice:
            return std::holds_alternative<std::string>(value);
        case GraphEffectParameterKind::NumberArray:
            return std::holds_alternative<std::vector<double>>(value);
    }
    return false;
}

void add_diagnostic(GraphEffectValidation& validation,
                    GraphEffectDiagnosticSeverity severity,
                    std::string code,
                    std::string message,
                    std::optional<GraphEffectNodeId> node_id = std::nullopt,
                    std::optional<std::size_t> link_index = std::nullopt) {
    validation.diagnostics.push_back({severity, std::move(code), std::move(message), node_id, link_index});
}

std::string first_validation_error(const GraphEffectValidation& validation) {
    const auto iterator = std::find_if(validation.diagnostics.begin(), validation.diagnostics.end(),
                                       [](const GraphEffectDiagnostic& diagnostic) {
        return diagnostic.severity == GraphEffectDiagnosticSeverity::Error;
    });
    return iterator == validation.diagnostics.end() ? "Graph validation failed" : iterator->message;
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

nlohmann::json parameter_to_json(const GraphEffectParameter& parameter) {
    return std::visit([](const auto& value) -> nlohmann::json {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, bool>) {
            return {{"type", "bool"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, std::int64_t>) {
            return {{"type", "integer"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, double>) {
            return {{"type", "number"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, std::string>) {
            return {{"type", "string"}, {"value", value}};
        } else if constexpr (std::is_same_v<Value, Pixel>) {
            return {{"type", "color"}, {"value", {r(value), g(value), b(value), a(value)}}};
        } else {
            return {{"type", "number-array"}, {"value", value}};
        }
    }, parameter);
}

GraphEffectParameter parameter_from_json(const nlohmann::json& json) {
    const std::string type = json.at("type").get<std::string>();
    const nlohmann::json& value = json.at("value");
    if (type == "bool") return value.get<bool>();
    if (type == "integer") return value.get<std::int64_t>();
    if (type == "number") {
        const double number = value.get<double>();
        if (!std::isfinite(number)) throw std::runtime_error("Graph parameter number is not finite");
        return number;
    }
    if (type == "string") return value.get<std::string>();
    if (type == "color") {
        if (!value.is_array() || value.size() != 4U) throw std::runtime_error("Graph color must have four channels");
        const auto channel = [&value](std::size_t index) {
            const int channel_value = value.at(index).get<int>();
            if (channel_value < 0 || channel_value > 255) {
                throw std::runtime_error("Graph color channel is outside the 0..255 range");
            }
            return static_cast<std::uint8_t>(channel_value);
        };
        return rgba(channel(0), channel(1), channel(2), channel(3));
    }
    if (type == "number-array") {
        std::vector<double> numbers = value.get<std::vector<double>>();
        if (numbers.size() > kMaximumGraphParameters ||
            std::any_of(numbers.begin(), numbers.end(), [](double number) { return !std::isfinite(number); })) {
            throw std::runtime_error("Graph number array is invalid");
        }
        return numbers;
    }
    throw std::runtime_error("Unknown graph parameter type: " + type);
}

nlohmann::json graph_to_json(const GraphEffectGraph& graph) {
    nlohmann::json root;
    root["format"] = kGraphEffectFormat;
    root["version"] = kGraphEffectFormatVersion;
    root["name"] = graph.name;
    root["nodes"] = nlohmann::json::array();
    for (const GraphEffectNode& node : graph.nodes) {
        nlohmann::json parameters = nlohmann::json::object();
        for (const auto& [id, value] : node.parameters) {
            parameters[id] = parameter_to_json(value);
        }
        root["nodes"].push_back({{"id", node.id}, {"type_id", node.type_id},
                                  {"position", {{"x", node.x}, {"y", node.y}}},
                                  {"enabled", node.enabled}, {"parameters", std::move(parameters)}});
    }
    root["links"] = nlohmann::json::array();
    for (const GraphEffectLink& link : graph.links) {
        root["links"].push_back({{"from", {{"node", link.from_node}, {"port", link.from_port}}},
                                  {"to", {{"node", link.to_node}, {"port", link.to_port}}}});
    }
    return root;
}

GraphEffectGraph graph_from_json(const nlohmann::json& root) {
    if (root.at("format").get<std::string>() != kGraphEffectFormat) {
        throw std::runtime_error("Not a PixelArt98 GraphEffect file");
    }
    const int version = root.at("version").get<int>();
    if (version != kGraphEffectFormatVersion) {
        throw std::runtime_error("Unsupported GraphEffect version: " + std::to_string(version));
    }
    GraphEffectGraph graph;
    graph.name = root.value("name", "Untitled Graph");
    const nlohmann::json& nodes = root.at("nodes");
    const nlohmann::json& links = root.at("links");
    if (!nodes.is_array() || nodes.size() > kMaximumGraphNodes ||
        !links.is_array() || links.size() > kMaximumGraphLinks) {
        throw std::runtime_error("GraphEffect file exceeds structural limits");
    }
    for (const nlohmann::json& node_json : nodes) {
        GraphEffectNode node;
        node.id = node_json.at("id").get<GraphEffectNodeId>();
        node.type_id = node_json.at("type_id").get<std::string>();
        node.x = node_json.at("position").at("x").get<double>();
        node.y = node_json.at("position").at("y").get<double>();
        node.enabled = node_json.value("enabled", true);
        const nlohmann::json& parameters = node_json.value("parameters", nlohmann::json::object());
        if (!parameters.is_object() || parameters.size() > kMaximumGraphParameters) {
            throw std::runtime_error("Graph node has too many parameters");
        }
        for (const auto& [id, parameter_json] : parameters.items()) {
            node.parameters.emplace(id, parameter_from_json(parameter_json));
        }
        graph.nodes.push_back(std::move(node));
    }
    for (const nlohmann::json& link_json : links) {
        GraphEffectLink link;
        link.from_node = link_json.at("from").at("node").get<GraphEffectNodeId>();
        link.from_port = link_json.at("from").at("port").get<std::string>();
        link.to_node = link_json.at("to").at("node").get<GraphEffectNodeId>();
        link.to_port = link_json.at("to").at("port").get<std::string>();
        graph.links.push_back(std::move(link));
    }
    return graph;
}

class BoundedStringBuffer final : public std::streambuf {
public:
    explicit BoundedStringBuffer(std::size_t limit) : limit_(limit) {
        data_.reserve(std::min<std::size_t>(limit, 64U * 1024U));
    }

    [[nodiscard]] bool exceeded() const noexcept { return exceeded_; }
    [[nodiscard]] std::string take() { return std::move(data_); }

protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) return traits_type::not_eof(character);
        if (data_.size() >= limit_) {
            exceeded_ = true;
            return traits_type::eof();
        }
        data_.push_back(traits_type::to_char_type(character));
        return character;
    }

    std::streamsize xsputn(const char* source, std::streamsize count) override {
        if (count <= 0) return 0;
        const std::size_t requested = static_cast<std::size_t>(count);
        const std::size_t remaining = limit_ - data_.size();
        const std::size_t written = std::min(requested, remaining);
        data_.append(source, written);
        if (written != requested) exceeded_ = true;
        return static_cast<std::streamsize>(written);
    }

private:
    std::size_t limit_ = 0;
    std::string data_;
    bool exceeded_ = false;
};

bool serialize_graph_bounded(const GraphEffectGraph& graph, std::string& serialized, std::string* error) {
    BoundedStringBuffer buffer(static_cast<std::size_t>(kMaximumGraphFileBytes));
    std::ostream stream(&buffer);
    stream << std::setw(2) << graph_to_json(graph) << '\n';
    stream.flush();
    if (buffer.exceeded() || !stream) {
        set_error(error, "GraphEffect serialization exceeds the maximum file size");
        return false;
    }
    serialized = buffer.take();
    return true;
}

void remove_temporary_file(const std::filesystem::path& path) noexcept {
    if (path.empty()) return;
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
}

bool install_graph_file(const std::filesystem::path& temporary,
                        const std::filesystem::path& destination,
                        std::string* error) {
#if defined(_WIN32)
    std::error_code exists_error;
    const bool destination_exists = std::filesystem::exists(destination, exists_error);
    if (exists_error) {
        set_error(error, "Could not inspect GraphEffect destination: " + exists_error.message());
        return false;
    }
    BOOL installed = FALSE;
    if (destination_exists) {
        installed = ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                                 REPLACEFILE_WRITE_THROUGH, nullptr, nullptr);
    } else {
        installed = MoveFileExW(temporary.c_str(), destination.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (installed == FALSE) {
        const std::error_code windows_error(static_cast<int>(GetLastError()), std::system_category());
        set_error(error, "Could not install GraphEffect file: " + windows_error.message());
        return false;
    }
    return true;
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, destination, rename_error);
    if (rename_error) {
        set_error(error, "Could not install GraphEffect file: " + rename_error.message());
        return false;
    }
    return true;
#endif
}

} // namespace

bool GraphEffectValidation::ok() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const GraphEffectDiagnostic& diagnostic) {
        return diagnostic.severity == GraphEffectDiagnosticSeverity::Error;
    });
}

const std::vector<GraphEffectNodeSpec>& graph_effect_catalog() {
    static const std::vector<GraphEffectNodeSpec> catalog = build_catalog();
    return catalog;
}

const GraphEffectNodeSpec* find_graph_effect_node_spec(std::string_view type_id) {
    const auto& catalog = graph_effect_catalog();
    const auto iterator = std::find_if(catalog.begin(), catalog.end(), [type_id](const GraphEffectNodeSpec& spec) {
        return spec.type_id == type_id;
    });
    return iterator == catalog.end() ? nullptr : &*iterator;
}

GraphEffectGraph make_default_graph_effect() {
    GraphEffectGraph graph;
    graph.name = "New GraphEffect";
    GraphEffectNodeId source_id = 0;
    GraphEffectNodeId output_id = 0;
    static_cast<void>(add_graph_effect_node(graph, "source.active-cel", 40.0, 80.0, &source_id));
    static_cast<void>(add_graph_effect_node(graph, "output", 360.0, 80.0, &output_id));
    static_cast<void>(connect_graph_effect_nodes(graph, source_id, "image", output_id, "image"));
    return graph;
}

GraphEffectNodeId graph_effect_next_node_id(const GraphEffectGraph& graph) {
    GraphEffectNodeId largest = 0;
    for (const GraphEffectNode& node : graph.nodes) largest = std::max(largest, node.id);
    return largest == std::numeric_limits<GraphEffectNodeId>::max() ? 0 : largest + 1U;
}

GraphEffectNode* find_graph_effect_node(GraphEffectGraph& graph, GraphEffectNodeId id) {
    const auto iterator = std::find_if(graph.nodes.begin(), graph.nodes.end(), [id](const GraphEffectNode& node) {
        return node.id == id;
    });
    return iterator == graph.nodes.end() ? nullptr : &*iterator;
}

const GraphEffectNode* find_graph_effect_node(const GraphEffectGraph& graph, GraphEffectNodeId id) {
    const auto iterator = std::find_if(graph.nodes.begin(), graph.nodes.end(), [id](const GraphEffectNode& node) {
        return node.id == id;
    });
    return iterator == graph.nodes.end() ? nullptr : &*iterator;
}

bool add_graph_effect_node(GraphEffectGraph& graph,
                           std::string_view type_id,
                           double x,
                           double y,
                           GraphEffectNodeId* out_id,
                           std::string* error) {
    const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(type_id);
    if (spec == nullptr) {
        set_error(error, "Unknown GraphEffect node type: " + std::string(type_id));
        return false;
    }
    if (graph.nodes.size() >= kMaximumGraphNodes || !std::isfinite(x) || !std::isfinite(y)) {
        set_error(error, "Cannot add GraphEffect node: graph limit or invalid position");
        return false;
    }
    const GraphEffectNodeId id = graph_effect_next_node_id(graph);
    if (id == 0) {
        set_error(error, "Cannot allocate a GraphEffect node ID");
        return false;
    }
    GraphEffectNode node;
    node.id = id;
    node.type_id = spec->type_id;
    node.x = x;
    node.y = y;
    for (const GraphEffectParameterSpec& parameter : spec->parameters) {
        node.parameters.emplace(parameter.id, parameter.default_value);
    }
    graph.nodes.push_back(std::move(node));
    if (out_id != nullptr) *out_id = id;
    return true;
}

bool remove_graph_effect_node(GraphEffectGraph& graph, GraphEffectNodeId id) {
    const auto iterator = std::find_if(graph.nodes.begin(), graph.nodes.end(), [id](const GraphEffectNode& node) {
        return node.id == id;
    });
    if (iterator == graph.nodes.end()) return false;
    graph.nodes.erase(iterator);
    std::erase_if(graph.links, [id](const GraphEffectLink& link) {
        return link.from_node == id || link.to_node == id;
    });
    return true;
}

bool connect_graph_effect_nodes(GraphEffectGraph& graph,
                                GraphEffectNodeId from_node,
                                std::string_view from_port,
                                GraphEffectNodeId to_node,
                                std::string_view to_port,
                                std::string* error) {
    const GraphEffectNode* source = find_graph_effect_node(graph, from_node);
    const GraphEffectNode* target = find_graph_effect_node(graph, to_node);
    if (source == nullptr || target == nullptr) {
        set_error(error, "Cannot connect GraphEffect nodes: endpoint does not exist");
        return false;
    }
    const GraphEffectNodeSpec* source_spec = find_graph_effect_node_spec(source->type_id);
    const GraphEffectNodeSpec* target_spec = find_graph_effect_node_spec(target->type_id);
    if (source_spec == nullptr || target_spec == nullptr) {
        set_error(error, "Cannot connect an unknown GraphEffect node type");
        return false;
    }
    const GraphEffectPortSpec* source_port = find_port(source_spec->outputs, from_port);
    const GraphEffectPortSpec* target_port = find_port(target_spec->inputs, to_port);
    if (source_port == nullptr || target_port == nullptr || source_port->type != target_port->type) {
        set_error(error, "Cannot connect GraphEffect nodes: invalid or incompatible port");
        return false;
    }
    const std::vector<GraphEffectLink> original_links = graph.links;
    std::erase_if(graph.links, [to_node, to_port](const GraphEffectLink& link) {
        return link.to_node == to_node && link.to_port == to_port;
    });
    graph.links.push_back({from_node, std::string(from_port), to_node, std::string(to_port)});
    const GraphEffectValidation validation = validate_graph_effect(graph, GraphEffectValidationMode::Structural);
    if (!validation.ok()) {
        graph.links = original_links;
        set_error(error, first_validation_error(validation));
        return false;
    }
    return true;
}

bool disconnect_graph_effect_input(GraphEffectGraph& graph,
                                   GraphEffectNodeId to_node,
                                   std::string_view to_port) {
    const std::size_t before = graph.links.size();
    std::erase_if(graph.links, [to_node, to_port](const GraphEffectLink& link) {
        return link.to_node == to_node && link.to_port == to_port;
    });
    return graph.links.size() != before;
}

GraphEffectValidation validate_graph_effect(const GraphEffectGraph& graph,
                                            GraphEffectValidationMode mode) {
    GraphEffectValidation validation;
    if (graph.nodes.size() > kMaximumGraphNodes) {
        add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "too-many-nodes",
                       "GraphEffect has too many nodes");
    }
    if (graph.links.size() > kMaximumGraphLinks) {
        add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "too-many-links",
                       "GraphEffect has too many links");
    }

    std::unordered_map<GraphEffectNodeId, const GraphEffectNode*> nodes;
    std::unordered_map<GraphEffectNodeId, std::size_t> indegree;
    nodes.reserve(graph.nodes.size());
    indegree.reserve(graph.nodes.size());
    for (const GraphEffectNode& node : graph.nodes) {
        if (node.id == 0) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "zero-node-id",
                           "GraphEffect node IDs must be non-zero", node.id);
        }
        if (!nodes.emplace(node.id, &node).second) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "duplicate-node-id",
                           "Duplicate GraphEffect node ID " + std::to_string(node.id), node.id);
        }
        indegree.try_emplace(node.id, 0U);
        if (node.type_id.empty()) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "empty-node-type",
                           "GraphEffect node type cannot be empty", node.id);
        }
        if (!std::isfinite(node.x) || !std::isfinite(node.y)) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "invalid-position",
                           "GraphEffect node position is not finite", node.id);
        }
        if (node.parameters.size() > kMaximumGraphParameters) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "too-many-parameters",
                           "GraphEffect node has too many parameters", node.id);
        }
    }

    std::set<std::tuple<GraphEffectNodeId, std::string, GraphEffectNodeId, std::string>> unique_links;
    std::set<std::pair<GraphEffectNodeId, std::string>> occupied_inputs;
    std::unordered_map<GraphEffectNodeId, std::vector<GraphEffectNodeId>> adjacency;
    adjacency.reserve(graph.nodes.size());
    for (std::size_t index = 0; index < graph.links.size(); ++index) {
        const GraphEffectLink& link = graph.links[index];
        const bool source_exists = nodes.contains(link.from_node);
        const bool target_exists = nodes.contains(link.to_node);
        if (!source_exists || !target_exists) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "missing-link-endpoint",
                           "GraphEffect link references a missing node", std::nullopt, index);
            continue;
        }
        if (link.from_port.empty() || link.to_port.empty()) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "empty-port-id",
                           "GraphEffect link port IDs cannot be empty", std::nullopt, index);
        }
        if (!unique_links.emplace(link.from_node, link.from_port, link.to_node, link.to_port).second) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "duplicate-link",
                           "Duplicate GraphEffect link", std::nullopt, index);
        }
        if (!occupied_inputs.emplace(link.to_node, link.to_port).second) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "duplicate-input",
                           "A GraphEffect input cannot have multiple links", link.to_node, index);
        }
        adjacency[link.from_node].push_back(link.to_node);
        ++indegree[link.to_node];
    }

    std::queue<GraphEffectNodeId> ready;
    for (const GraphEffectNode& node : graph.nodes) {
        if (indegree[node.id] == 0U) ready.push(node.id);
    }
    while (!ready.empty()) {
        const GraphEffectNodeId id = ready.front();
        ready.pop();
        validation.topological_order.push_back(id);
        const auto adjacent = adjacency.find(id);
        if (adjacent == adjacency.end()) continue;
        for (GraphEffectNodeId next : adjacent->second) {
            auto degree = indegree.find(next);
            if (degree != indegree.end() && degree->second > 0U) {
                --degree->second;
                if (degree->second == 0U) ready.push(next);
            }
        }
    }
    if (validation.topological_order.size() != nodes.size()) {
        add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "cycle",
                       "GraphEffect contains a cycle");
        validation.topological_order.clear();
    }

    std::vector<GraphEffectNodeId> output_ids;
    for (const GraphEffectNode& node : graph.nodes) {
        if (node.type_id == "output") output_ids.push_back(node.id);
    }
    std::unordered_set<GraphEffectNodeId> reachable;
    if (output_ids.size() == 1U) {
        std::vector<GraphEffectNodeId> pending = output_ids;
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
    }

    for (const GraphEffectNode& node : graph.nodes) {
        const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node.type_id);
        if (spec == nullptr) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "unknown-node-type",
                           "Unknown GraphEffect node type: " + node.type_id, node.id);
            continue;
        }
        for (const auto& [parameter_id, parameter] : node.parameters) {
            const GraphEffectParameterSpec* parameter_spec = find_parameter_spec(*spec, parameter_id);
            if (parameter_spec == nullptr) {
                add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "unknown-parameter",
                               "Unknown parameter '" + parameter_id + "'", node.id);
                continue;
            }
            if (!parameter_matches_kind(parameter, parameter_spec->kind)) {
                add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "parameter-type",
                               "Parameter '" + parameter_id + "' has the wrong type", node.id);
                continue;
            }
            const double numeric = parameter_spec->kind == GraphEffectParameterKind::Integer ||
                                           parameter_spec->kind == GraphEffectParameterKind::Number
                                       ? number_value(node, *spec, parameter_id)
                                       : 0.0;
            if ((parameter_spec->kind == GraphEffectParameterKind::Integer ||
                 parameter_spec->kind == GraphEffectParameterKind::Number) &&
                (!std::isfinite(numeric) ||
                 (parameter_spec->minimum && numeric < *parameter_spec->minimum) ||
                 (parameter_spec->maximum && numeric > *parameter_spec->maximum))) {
                add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "parameter-range",
                               "Parameter '" + parameter_id + "' is outside its allowed range", node.id);
            }
            if (parameter_spec->kind == GraphEffectParameterKind::Choice) {
                const std::string choice = string_value(node, *spec, parameter_id);
                if (std::find(parameter_spec->choices.begin(), parameter_spec->choices.end(), choice) ==
                    parameter_spec->choices.end()) {
                    add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "parameter-choice",
                                   "Parameter '" + parameter_id + "' has an unknown choice", node.id);
                }
            }
        }
        if (mode != GraphEffectValidationMode::Evaluable || !reachable.contains(node.id)) continue;
        for (const GraphEffectPortSpec& input : spec->inputs) {
            if (!input.required) continue;
            if (node.type_id == "combine.mix" && !node.enabled && input.id == "blend") continue;
            const bool connected = std::any_of(graph.links.begin(), graph.links.end(),
                                               [&node, &input](const GraphEffectLink& link) {
                return link.to_node == node.id && link.to_port == input.id;
            });
            if (!connected) {
                add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "missing-input",
                               "Node '" + spec->label + "' is missing input '" + input.label + "'", node.id);
            }
        }
    }
    if (mode == GraphEffectValidationMode::Evaluable && output_ids.size() != 1U) {
        add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "output-count",
                       "GraphEffect must contain exactly one Output node");
    }

    for (std::size_t index = 0; index < graph.links.size(); ++index) {
        const GraphEffectLink& link = graph.links[index];
        const GraphEffectNode* source = find_graph_effect_node(graph, link.from_node);
        const GraphEffectNode* target = find_graph_effect_node(graph, link.to_node);
        if (source == nullptr || target == nullptr) continue;
        const GraphEffectNodeSpec* source_spec = find_graph_effect_node_spec(source->type_id);
        const GraphEffectNodeSpec* target_spec = find_graph_effect_node_spec(target->type_id);
        if (source_spec == nullptr || target_spec == nullptr) continue;
        const GraphEffectPortSpec* source_port = find_port(source_spec->outputs, link.from_port);
        const GraphEffectPortSpec* target_port = find_port(target_spec->inputs, link.to_port);
        if (source_port == nullptr || target_port == nullptr) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "unknown-port",
                           "GraphEffect link references an unknown port", std::nullopt, index);
        } else if (source_port->type != target_port->type) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "port-type",
                           "GraphEffect link joins incompatible port types", std::nullopt, index);
        }
    }

    for (const GraphEffectNode& node : graph.nodes) {
        if (node.type_id != "adjustment.curves") continue;
        const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node.type_id);
        if (spec == nullptr) continue;
        const std::vector<double> curve_x = array_value(node, *spec, "curve_x");
        const std::vector<double> curve_y = array_value(node, *spec, "curve_y");
        bool valid_curve = curve_x.size() == curve_y.size() && curve_x.size() >= 2U &&
                           curve_x.size() <= static_cast<std::size_t>(kMaxCurvePoints);
        for (std::size_t index = 0; valid_curve && index < curve_x.size(); ++index) {
            valid_curve = std::isfinite(curve_x[index]) && std::isfinite(curve_y[index]) &&
                          curve_x[index] >= 0.0 && curve_x[index] <= 1.0 &&
                          curve_y[index] >= 0.0 && curve_y[index] <= 1.0 &&
                          (index == 0U || curve_x[index] > curve_x[index - 1U]);
        }
        if (!valid_curve) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "invalid-curve",
                           "Curves points must contain 2 to 8 ordered points in the [0, 1] range", node.id);
        }
        const bool luma = boolean_value(node, *spec, "luminance");
        const bool any_rgb = boolean_value(node, *spec, "red") || boolean_value(node, *spec, "green") ||
                             boolean_value(node, *spec, "blue");
        if ((!luma && !any_rgb) || (luma && any_rgb)) {
            add_diagnostic(validation, GraphEffectDiagnosticSeverity::Error, "invalid-curve-target",
                           "Curves must target either luminance or at least one RGB channel", node.id);
        }
    }
    return validation;
}

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

bool save_graph_effect(const std::filesystem::path& path,
                       const GraphEffectGraph& graph,
                       std::string* error) {
    if (error != nullptr) error->clear();
    std::filesystem::path temporary;
    try {
        const GraphEffectValidation validation =
            validate_graph_effect(graph, GraphEffectValidationMode::Structural);
        if (!validation.ok()) {
            set_error(error, first_validation_error(validation));
            return false;
        }
        if (path.empty()) {
            set_error(error, "GraphEffect path is empty");
            return false;
        }

        std::string serialized;
        if (!serialize_graph_bounded(graph, serialized, error)) return false;

        static std::atomic<std::uint64_t> sequence{0};
        temporary = path;
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary += ".tmp-" + std::to_string(timestamp) + "-" +
                     std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));

        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            set_error(error, "Could not create temporary GraphEffect file");
            return false;
        }
        file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        file.flush();
        const bool write_succeeded = static_cast<bool>(file);
        file.close();
        const bool close_succeeded = static_cast<bool>(file);
        if (!write_succeeded || !close_succeeded) {
            remove_temporary_file(temporary);
            set_error(error, "Could not write GraphEffect file");
            return false;
        }

        if (!install_graph_file(temporary, path, error)) {
            remove_temporary_file(temporary);
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        remove_temporary_file(temporary);
        set_error(error, std::string("Could not save GraphEffect: ") + exception.what());
        return false;
    } catch (...) {
        remove_temporary_file(temporary);
        set_error(error, "Could not save GraphEffect: unknown error");
        return false;
    }
}

bool load_graph_effect(const std::filesystem::path& path,
                       GraphEffectGraph& graph,
                       std::string* error) {
    if (error != nullptr) error->clear();
    try {
        std::error_code size_error;
        const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
        if (size_error || file_size > kMaximumGraphFileBytes) {
            set_error(error, size_error ? "Could not inspect GraphEffect file: " + size_error.message()
                                        : "GraphEffect file is too large");
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            set_error(error, "Could not open GraphEffect file");
            return false;
        }
        nlohmann::json root;
        file >> root;
        GraphEffectGraph loaded = graph_from_json(root);
        const GraphEffectValidation validation = validate_graph_effect(loaded, GraphEffectValidationMode::Structural);
        if (!validation.ok()) {
            set_error(error, first_validation_error(validation));
            return false;
        }
        graph = std::move(loaded);
        return true;
    } catch (const std::exception& exception) {
        set_error(error, std::string("Could not load GraphEffect: ") + exception.what());
        return false;
    } catch (...) {
        set_error(error, "Could not load GraphEffect: unknown error");
        return false;
    }
}

bool save_graph_effect(const std::string& path,
                       const GraphEffectGraph& graph,
                       std::string* error) {
    return save_graph_effect(std::filesystem::path(path), graph, error);
}

bool load_graph_effect(const std::string& path,
                       GraphEffectGraph& graph,
                       std::string* error) {
    return load_graph_effect(std::filesystem::path(path), graph, error);
}

} // namespace px
