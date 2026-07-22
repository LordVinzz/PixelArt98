// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Document.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace px {

inline constexpr int kGraphEffectFormatVersion = 1;
inline constexpr std::string_view kGraphEffectFormat = "pixelart98-graph-effect";

using GraphEffectNodeId = std::uint64_t;
using GraphEffectParameter = std::variant<bool,
                                          std::int64_t,
                                          double,
                                          std::string,
                                          Pixel,
                                          std::vector<double>>;

struct GraphEffectNode {
    GraphEffectNodeId id = 0;
    std::string type_id;
    double x = 0.0;
    double y = 0.0;
    bool enabled = true;
    std::map<std::string, GraphEffectParameter> parameters;
};

struct GraphEffectLink {
    GraphEffectNodeId from_node = 0;
    std::string from_port;
    GraphEffectNodeId to_node = 0;
    std::string to_port;

    bool operator==(const GraphEffectLink&) const = default;
};

struct GraphEffectGraph {
    std::string name = "Untitled Graph";
    std::vector<GraphEffectNode> nodes;
    std::vector<GraphEffectLink> links;
};

enum class GraphEffectPortType {
    Image
};

struct GraphEffectPortSpec {
    std::string id;
    std::string label;
    GraphEffectPortType type = GraphEffectPortType::Image;
    bool required = true;
};

enum class GraphEffectParameterKind {
    Integer,
    Number,
    Boolean,
    Color,
    Choice,
    NumberArray
};

struct GraphEffectParameterSpec {
    std::string id;
    std::string label;
    GraphEffectParameterKind kind = GraphEffectParameterKind::Number;
    GraphEffectParameter default_value = 0.0;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::vector<std::string> choices;
};

struct GraphEffectNodeSpec {
    std::string type_id;
    std::string label;
    std::string category;
    std::vector<GraphEffectPortSpec> inputs;
    std::vector<GraphEffectPortSpec> outputs;
    std::vector<GraphEffectParameterSpec> parameters;
    // Structural routing nodes cannot be bypassed because they do not have a
    // single unambiguous input/output pair.
    bool bypassable = true;
};

enum class GraphEffectDiagnosticSeverity {
    Warning,
    Error
};

struct GraphEffectDiagnostic {
    GraphEffectDiagnosticSeverity severity = GraphEffectDiagnosticSeverity::Error;
    std::string code;
    std::string message;
    std::optional<GraphEffectNodeId> node_id;
    std::optional<std::size_t> link_index;
};

enum class GraphEffectValidationMode {
    Structural,
    Evaluable
};

struct GraphEffectValidation {
    std::vector<GraphEffectDiagnostic> diagnostics;
    std::vector<GraphEffectNodeId> topological_order;

    [[nodiscard]] bool ok() const;
};

struct GraphEffectEvaluation {
    bool success = false;
    int width = 0;
    int height = 0;
    std::vector<Pixel> pixels;
    std::vector<GraphEffectDiagnostic> diagnostics;
    // The requested node ID remains set even when the node is disconnected.
    // In that case it is not evaluated and both captured images stay empty.
    std::optional<GraphEffectNodeId> inspected_node_id;
    bool inspected_node_evaluated = false;
    std::vector<Pixel> inspected_input_pixels;
    std::vector<Pixel> inspected_output_pixels;
    // Highest number of output-port images retained by the evaluator at once.
    // This is useful for tests and diagnostics of large, deeply chained graphs.
    std::size_t peak_cached_images = 0;
};

[[nodiscard]] const std::vector<GraphEffectNodeSpec>& graph_effect_catalog();
[[nodiscard]] const GraphEffectNodeSpec* find_graph_effect_node_spec(std::string_view type_id);

[[nodiscard]] GraphEffectGraph make_default_graph_effect();
[[nodiscard]] GraphEffectNodeId graph_effect_next_node_id(const GraphEffectGraph& graph);
[[nodiscard]] GraphEffectNode* find_graph_effect_node(GraphEffectGraph& graph, GraphEffectNodeId id);
[[nodiscard]] const GraphEffectNode* find_graph_effect_node(const GraphEffectGraph& graph, GraphEffectNodeId id);

bool add_graph_effect_node(GraphEffectGraph& graph,
                           std::string_view type_id,
                           double x,
                           double y,
                           GraphEffectNodeId* out_id = nullptr,
                           std::string* error = nullptr);
bool remove_graph_effect_node(GraphEffectGraph& graph, GraphEffectNodeId id);
bool connect_graph_effect_nodes(GraphEffectGraph& graph,
                                GraphEffectNodeId from_node,
                                std::string_view from_port,
                                GraphEffectNodeId to_node,
                                std::string_view to_port,
                                std::string* error = nullptr);
bool disconnect_graph_effect_input(GraphEffectGraph& graph,
                                   GraphEffectNodeId to_node,
                                   std::string_view to_port);

[[nodiscard]] GraphEffectValidation validate_graph_effect(
    const GraphEffectGraph& graph,
    GraphEffectValidationMode mode = GraphEffectValidationMode::Evaluable);
[[nodiscard]] GraphEffectEvaluation evaluate_graph_effect(const GraphEffectGraph& graph,
                                                           const Document& document,
                                                           std::optional<GraphEffectNodeId> inspected_node =
                                                               std::nullopt);

bool save_graph_effect(const std::filesystem::path& path,
                       const GraphEffectGraph& graph,
                       std::string* error = nullptr);
bool load_graph_effect(const std::filesystem::path& path,
                       GraphEffectGraph& graph,
                       std::string* error = nullptr);
bool save_graph_effect(const std::string& path,
                       const GraphEffectGraph& graph,
                       std::string* error = nullptr);
bool load_graph_effect(const std::string& path,
                       GraphEffectGraph& graph,
                       std::string* error = nullptr);

} // namespace px
