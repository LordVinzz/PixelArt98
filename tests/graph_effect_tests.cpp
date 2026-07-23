// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#include "core/GraphEffect.hpp"

#include "core/Filters.hpp"

// Keep assertions active in Release configurations.
#undef NDEBUG
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace px;

namespace {

Document patterned_document(int width = 8, int height = 6) {
    Document document = Document::create(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto red = static_cast<std::uint8_t>((x * 31 + y * 17 + 19) % 256);
            const auto green = static_cast<std::uint8_t>((x * 13 + y * 47 + 23) % 256);
            const auto blue = static_cast<std::uint8_t>((x * 59 + y * 7 + 29) % 256);
            document.active_cel().pixels[static_cast<std::size_t>(document.pixel_index(x, y))] =
                rgba(red, green, blue, 255);
        }
    }
    return document;
}

GraphEffectNode& node_of_type(GraphEffectGraph& graph, std::string_view type_id) {
    const auto iterator = std::find_if(graph.nodes.begin(), graph.nodes.end(), [type_id](const GraphEffectNode& node) {
        return node.type_id == type_id;
    });
    assert(iterator != graph.nodes.end());
    return *iterator;
}

GraphEffectNodeId add_node(GraphEffectGraph& graph, std::string_view type_id, double x = 0.0, double y = 0.0) {
    GraphEffectNodeId id = 0;
    std::string error;
    assert(add_graph_effect_node(graph, type_id, x, y, &id, &error));
    assert(error.empty());
    assert(id != 0);
    return id;
}

void connect(GraphEffectGraph& graph,
             GraphEffectNodeId from,
             std::string_view from_port,
             GraphEffectNodeId to,
             std::string_view to_port) {
    std::string error;
    assert(connect_graph_effect_nodes(graph, from, from_port, to, to_port, &error));
    assert(error.empty());
}

bool has_diagnostic(const GraphEffectValidation& validation, std::string_view code) {
    return std::any_of(validation.diagnostics.begin(), validation.diagnostics.end(),
                       [code](const GraphEffectDiagnostic& diagnostic) { return diagnostic.code == code; });
}

GraphEffectGraph graph_with_unary(std::string_view type_id, GraphEffectNodeId* unary_id = nullptr) {
    GraphEffectGraph graph = make_default_graph_effect();
    const GraphEffectNodeId source = node_of_type(graph, "source.active-cel").id;
    const GraphEffectNodeId output = node_of_type(graph, "output").id;
    const GraphEffectNodeId effect = add_node(graph, type_id, 200.0, 80.0);
    connect(graph, source, "image", effect, "image");
    connect(graph, effect, "image", output, "image");
    if (unary_id != nullptr) *unary_id = effect;
    return graph;
}

GraphEffectGraph graph_with_generator(std::string_view type_id, GraphEffectNodeId* generator_id = nullptr) {
    GraphEffectGraph graph;
    const GraphEffectNodeId generator = add_node(graph, type_id, 40.0, 80.0);
    const GraphEffectNodeId output = add_node(graph, "output", 300.0, 80.0);
    connect(graph, generator, "image", output, "image");
    if (generator_id != nullptr) *generator_id = generator;
    return graph;
}

void assert_graphs_equal(const GraphEffectGraph& first, const GraphEffectGraph& second) {
    assert(first.name == second.name);
    assert(first.links == second.links);
    assert(first.nodes.size() == second.nodes.size());
    for (std::size_t index = 0; index < first.nodes.size(); ++index) {
        const GraphEffectNode& a_node = first.nodes[index];
        const GraphEffectNode& b_node = second.nodes[index];
        assert(a_node.id == b_node.id);
        assert(a_node.type_id == b_node.type_id);
        assert(a_node.x == b_node.x);
        assert(a_node.y == b_node.y);
        assert(a_node.enabled == b_node.enabled);
        assert(a_node.parameters == b_node.parameters);
    }
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("pixelart_graph_effect_tests_" + std::to_string(stamp));
        std::error_code error;
        assert(std::filesystem::create_directory(path, error));
        assert(!error);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void test_catalog_and_default_graph() {
    const auto& catalog = graph_effect_catalog();
    assert(catalog.size() >= 40U);
    assert(find_graph_effect_node_spec("source.active-cel") != nullptr);
    assert(find_graph_effect_node_spec("combine.mix") != nullptr);
    assert(find_graph_effect_node_spec("generator.clouds") != nullptr);
    assert(find_graph_effect_node_spec("adjustment.curves") != nullptr);
    const GraphEffectNodeSpec* split_spec = find_graph_effect_node_spec("channel.split-rgba");
    const GraphEffectNodeSpec* merge_spec = find_graph_effect_node_spec("channel.merge-rgba");
    assert(split_spec != nullptr);
    assert(merge_spec != nullptr);
    assert(split_spec->label == "Split Channels");
    assert(merge_spec->label == "Merge Channels");
    assert(split_spec->category == "Channels");
    assert(merge_spec->category == "Channels");
    assert(!split_spec->bypassable);
    assert(!merge_spec->bypassable);
    assert(split_spec->inputs.size() == 1U);
    assert(split_spec->inputs.front().id == "image");
    assert(split_spec->inputs.front().type == GraphEffectPortType::Image);
    assert(split_spec->inputs.front().required);
    assert(split_spec->outputs.size() == 4U);
    assert(merge_spec->inputs.size() == 4U);
    assert(merge_spec->outputs.size() == 1U);
    assert(merge_spec->outputs.front().id == "image");
    const std::array<std::string_view, 4> channel_ports = {
        "red", "green", "blue", "alpha"};
    for (std::size_t index = 0; index < channel_ports.size(); ++index) {
        assert(split_spec->outputs[index].id == channel_ports[index]);
        assert(split_spec->outputs[index].type == GraphEffectPortType::Image);
        assert(merge_spec->inputs[index].id == channel_ports[index]);
        assert(merge_spec->inputs[index].type == GraphEffectPortType::Image);
        assert(merge_spec->inputs[index].required);
    }
    assert(find_graph_effect_node_spec("does-not-exist") == nullptr);

    const GraphEffectNodeSpec* output_spec = find_graph_effect_node_spec("output");
    const GraphEffectNodeSpec* source_spec = find_graph_effect_node_spec("source.active-cel");
    assert(output_spec != nullptr);
    assert(source_spec != nullptr);
    assert(!source_spec->bypassable);
    assert(!output_spec->bypassable);
    assert(output_spec->inputs.size() == 1U);
    assert(output_spec->outputs.empty());

    GraphEffectGraph graph = make_default_graph_effect();
    assert(graph.nodes.size() == 2U);
    assert(graph.links.size() == 1U);
    assert(validate_graph_effect(graph, GraphEffectValidationMode::Structural).ok());
    assert(validate_graph_effect(graph, GraphEffectValidationMode::Evaluable).ok());
    assert(graph_effect_next_node_id(graph) == 3U);

    Document document = patterned_document();
    const std::vector<Pixel> original = document.active_cel().pixels;
    GraphEffectEvaluation evaluation = evaluate_graph_effect(graph, document);
    assert(evaluation.success);
    assert(evaluation.width == document.width);
    assert(evaluation.height == document.height);
    assert(evaluation.pixels == original);
    assert(document.active_cel().pixels == original);
    assert(!document.undo());
}

void test_graph_editing_and_cycle_rejection() {
    GraphEffectGraph graph = make_default_graph_effect();
    const GraphEffectNodeId source = node_of_type(graph, "source.active-cel").id;
    const GraphEffectNodeId output = node_of_type(graph, "output").id;
    const GraphEffectNodeId first = add_node(graph, "effect.grayscale");
    const GraphEffectNodeId second = add_node(graph, "effect.sepia");

    connect(graph, source, "image", first, "image");
    connect(graph, first, "image", second, "image");
    connect(graph, second, "image", output, "image");
    assert(graph.links.size() == 3U);
    assert(validate_graph_effect(graph).ok());

    const std::vector<GraphEffectLink> before_cycle = graph.links;
    std::string error;
    assert(!connect_graph_effect_nodes(graph, second, "image", first, "image", &error));
    assert(error.find("cycle") != std::string::npos);
    assert(graph.links == before_cycle);

    error.clear();
    assert(!connect_graph_effect_nodes(graph, output, "image", first, "image", &error));
    assert(error.find("port") != std::string::npos);
    assert(graph.links == before_cycle);

    GraphEffectGraph duplicate = graph;
    duplicate.links.push_back({source, "image", output, "image"});
    const GraphEffectValidation duplicate_validation =
        validate_graph_effect(duplicate, GraphEffectValidationMode::Structural);
    assert(!duplicate_validation.ok());
    assert(has_diagnostic(duplicate_validation, "duplicate-input"));

    assert(remove_graph_effect_node(graph, first));
    assert(find_graph_effect_node(graph, first) == nullptr);
    assert(std::none_of(graph.links.begin(), graph.links.end(), [first](const GraphEffectLink& link) {
        return link.from_node == first || link.to_node == first;
    }));
    assert(!remove_graph_effect_node(graph, first));
}

void test_drafts_and_structural_validation() {
    GraphEffectGraph graph = make_default_graph_effect();
    const GraphEffectNodeId draft = add_node(graph, "effect.gaussian-blur");
    assert(validate_graph_effect(graph, GraphEffectValidationMode::Structural).ok());
    assert(validate_graph_effect(graph, GraphEffectValidationMode::Evaluable).ok());
    assert(evaluate_graph_effect(graph, patterned_document()).success);

    GraphEffectNode* draft_node = find_graph_effect_node(graph, draft);
    assert(draft_node != nullptr);
    draft_node->parameters["radius"] = std::string("wrong");
    GraphEffectValidation invalid_parameter =
        validate_graph_effect(graph, GraphEffectValidationMode::Structural);
    assert(!invalid_parameter.ok());
    assert(has_diagnostic(invalid_parameter, "parameter-type"));

    draft_node->parameters["radius"] = std::int64_t{1000};
    invalid_parameter = validate_graph_effect(graph, GraphEffectValidationMode::Structural);
    assert(!invalid_parameter.ok());
    assert(has_diagnostic(invalid_parameter, "parameter-range"));

    draft_node->parameters["radius"] = std::int64_t{4};
    draft_node->parameters["future"] = 1.0;
    invalid_parameter = validate_graph_effect(graph, GraphEffectValidationMode::Structural);
    assert(!invalid_parameter.ok());
    assert(has_diagnostic(invalid_parameter, "unknown-parameter"));

    draft_node->parameters.erase("future");
    graph.nodes.push_back(graph.nodes.front());
    GraphEffectValidation duplicate = validate_graph_effect(graph, GraphEffectValidationMode::Structural);
    assert(!duplicate.ok());
    assert(has_diagnostic(duplicate, "duplicate-node-id"));
}

void test_unary_chain_selection_and_bypass() {
    GraphEffectGraph graph = make_default_graph_effect();
    const GraphEffectNodeId source = node_of_type(graph, "source.active-cel").id;
    const GraphEffectNodeId output = node_of_type(graph, "output").id;
    const GraphEffectNodeId brightness = add_node(graph, "adjustment.brightness-contrast");
    const GraphEffectNodeId invert = add_node(graph, "effect.invert-colors");
    find_graph_effect_node(graph, brightness)->parameters["brightness"] = std::int64_t{25};
    connect(graph, source, "image", brightness, "image");
    connect(graph, brightness, "image", invert, "image");
    connect(graph, invert, "image", output, "image");

    Document document = patterned_document(4, 3);
    document.selection.select_rect(1, 1, 2, 1, true);
    const std::vector<Pixel> original = document.active_cel().pixels;
    Document reference = document;
    apply_brightness_contrast(reference, 25, 0);
    apply_invert(reference, false);

    GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == reference.active_cel().pixels);
    assert(document.active_cel().pixels == original);
    assert(document.selection.selected_count() == 2);

    GraphEffectNode* brightness_node = find_graph_effect_node(graph, brightness);
    assert(brightness_node != nullptr);
    brightness_node->enabled = false;
    reference = document;
    apply_invert(reference, false);
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == reference.active_cel().pixels);
}

void test_node_inspection_probe() {
    GraphEffectGraph graph = make_default_graph_effect();
    const GraphEffectNodeId source = node_of_type(graph, "source.active-cel").id;
    const GraphEffectNodeId output = node_of_type(graph, "output").id;
    const GraphEffectNodeId brightness = add_node(graph, "adjustment.brightness-contrast");
    const GraphEffectNodeId levels = add_node(graph, "adjustment.levels");
    GraphEffectNode* brightness_node = find_graph_effect_node(graph, brightness);
    GraphEffectNode* levels_node = find_graph_effect_node(graph, levels);
    assert(brightness_node != nullptr);
    assert(levels_node != nullptr);
    brightness_node->parameters["brightness"] = std::int64_t{35};
    levels_node->parameters["input_black"] = std::int64_t{18};
    levels_node->parameters["input_white"] = std::int64_t{224};
    levels_node->parameters["gamma"] = 0.8;
    levels_node->parameters["output_black"] = std::int64_t{7};
    levels_node->parameters["output_white"] = std::int64_t{242};
    connect(graph, source, "image", brightness, "image");
    connect(graph, brightness, "image", levels, "image");
    connect(graph, levels, "image", output, "image");

    const Document document = patterned_document(5, 4);
    Document after_brightness = document;
    apply_brightness_contrast(after_brightness, 35, 0);
    Document after_levels = after_brightness;
    LevelsSettings settings;
    settings.in_black = 18;
    settings.in_white = 224;
    settings.gamma = 0.8F;
    settings.out_black = 7;
    settings.out_white = 242;
    apply_levels(after_levels, settings);

    GraphEffectEvaluation result = evaluate_graph_effect(graph, document, levels);
    assert(result.success);
    assert(result.inspected_node_id == levels);
    assert(result.inspected_node_evaluated);
    assert(result.inspected_input_pixels == after_brightness.active_cel().pixels);
    assert(result.inspected_output_pixels == after_levels.active_cel().pixels);
    assert(result.pixels == after_levels.active_cel().pixels);

    levels_node->enabled = false;
    result = evaluate_graph_effect(graph, document, levels);
    assert(result.success);
    assert(result.inspected_node_id == levels);
    assert(result.inspected_node_evaluated);
    assert(result.inspected_input_pixels == after_brightness.active_cel().pixels);
    assert(result.inspected_output_pixels == after_brightness.active_cel().pixels);
    assert(result.pixels == after_brightness.active_cel().pixels);

    const GraphEffectNodeId disconnected = add_node(graph, "adjustment.curves");
    result = evaluate_graph_effect(graph, document, disconnected);
    assert(result.success);
    assert(result.inspected_node_id == disconnected);
    assert(!result.inspected_node_evaluated);
    assert(result.inspected_input_pixels.empty());
    assert(result.inspected_output_pixels.empty());
}

void test_mix_and_branching() {
    GraphEffectGraph graph;
    const GraphEffectNodeId source = add_node(graph, "source.active-cel");
    const GraphEffectNodeId solid = add_node(graph, "generator.solid");
    const GraphEffectNodeId mix = add_node(graph, "combine.mix");
    const GraphEffectNodeId output = add_node(graph, "output");
    find_graph_effect_node(graph, solid)->parameters["color"] = rgba(255, 0, 0, 255);
    connect(graph, source, "image", mix, "base");
    connect(graph, solid, "image", mix, "blend");
    connect(graph, mix, "image", output, "image");

    Document document = Document::create(2, 1);
    document.active_cel().pixels = {rgba(0, 0, 255, 255), rgba(0, 255, 0, 255)};
    GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == std::vector<Pixel>({rgba(255, 0, 0, 255), rgba(255, 0, 0, 255)}));

    GraphEffectNode* mix_node = find_graph_effect_node(graph, mix);
    assert(mix_node != nullptr);
    mix_node->parameters["opacity"] = 0.0;
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);

    mix_node->parameters["opacity"] = 1.0;
    mix_node->parameters["blend_mode"] = std::string("multiply");
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels[0] == rgba(0, 0, 0, 255));
    assert(result.pixels[1] == rgba(0, 0, 0, 255));

    mix_node->enabled = false;
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);
}

#include "graph_effect_advanced_tests.inc"
} // namespace

int main() {
    test_catalog_and_default_graph();
    test_graph_editing_and_cycle_rejection();
    test_drafts_and_structural_validation();
    test_unary_chain_selection_and_bypass();
    test_node_inspection_probe();
    test_mix_and_branching();
    test_mix_alpha_and_disabled_blend_branch();
    test_channel_split_outputs_and_round_trip();
    test_channel_branch_effects_and_required_inputs();
    test_channel_merge_external_images_and_repeated_lifetimes();
    test_channel_port_consumer_lifetimes();
    test_intermediate_image_lifetime();
    test_generators_and_determinism();
    test_curves_and_malformed_graphs();
    test_save_load_round_trip_and_corruptions();
    test_bounded_atomic_save_and_color_validation();
    std::cout << "GraphEffect tests passed\n";
    return 0;
}
