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

bool boolean_value(const GraphEffectNode& node,
                   const GraphEffectNodeSpec& spec,
                   std::string_view id) {
    const GraphEffectParameter& value = parameter_value(node, spec, id);
    const auto* result = std::get_if<bool>(&value);
    return result != nullptr && *result;
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
} // namespace px
