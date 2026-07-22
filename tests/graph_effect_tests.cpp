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

void test_mix_alpha_and_disabled_blend_branch() {
    GraphEffectGraph graph;
    const GraphEffectNodeId source = add_node(graph, "source.active-cel");
    const GraphEffectNodeId solid = add_node(graph, "generator.solid");
    const GraphEffectNodeId mix = add_node(graph, "combine.mix");
    const GraphEffectNodeId output = add_node(graph, "output");
    GraphEffectNode* solid_node = find_graph_effect_node(graph, solid);
    GraphEffectNode* mix_node = find_graph_effect_node(graph, mix);
    assert(solid_node != nullptr);
    assert(mix_node != nullptr);
    solid_node->parameters["color"] = rgba(255, 0, 0, 128);
    connect(graph, source, "image", mix, "base");
    connect(graph, solid, "image", mix, "blend");
    connect(graph, mix, "image", output, "image");

    Document document = Document::create(1, 1);
    document.active_cel().pixels[0] = rgba(0, 0, 255, 128);
    GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels[0] == rgba(170, 0, 85, 192));

    mix_node->parameters["blend_mode"] = std::string("multiply");
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels[0] == rgba(85, 0, 85, 192));

    document.active_cel().pixels[0] = rgba(10, 20, 30, 0);
    solid_node->parameters["color"] = rgba(255, 0, 0, 255);
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels[0] == rgba(255, 0, 0, 255));

    GraphEffectGraph bypass;
    const GraphEffectNodeId bypass_source = add_node(bypass, "source.active-cel");
    const GraphEffectNodeId incomplete_branch = add_node(bypass, "effect.invert-colors");
    const GraphEffectNodeId bypass_mix = add_node(bypass, "combine.mix");
    const GraphEffectNodeId bypass_output = add_node(bypass, "output");
    find_graph_effect_node(bypass, bypass_mix)->enabled = false;
    connect(bypass, bypass_source, "image", bypass_mix, "base");
    connect(bypass, incomplete_branch, "image", bypass_mix, "blend");
    connect(bypass, bypass_mix, "image", bypass_output, "image");

    assert(validate_graph_effect(bypass, GraphEffectValidationMode::Evaluable).ok());
    document.active_cel().pixels[0] = rgba(5, 10, 15, 64);
    result = evaluate_graph_effect(bypass, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);
    assert(result.peak_cached_images <= 2U);
}

void test_channel_split_outputs_and_round_trip() {
    Document document = Document::create(4, 1);
    document.active_cel().pixels = {
        rgba(11, 22, 33, 0), rgba(44, 55, 66, 1),
        rgba(77, 88, 99, 127), rgba(123, 145, 167, 255)};

    const std::array<std::string_view, 4> ports = {
        "red", "green", "blue", "alpha"};
    for (std::size_t channel = 0; channel < ports.size(); ++channel) {
        GraphEffectGraph graph;
        const GraphEffectNodeId source = add_node(graph, "source.active-cel");
        const GraphEffectNodeId split = add_node(graph, "channel.split-rgba");
        const GraphEffectNodeId output = add_node(graph, "output");
        connect(graph, source, "image", split, "image");
        connect(graph, split, ports[channel], output, "image");

        std::vector<Pixel> expected;
        expected.reserve(document.active_cel().pixels.size());
        for (const Pixel pixel : document.active_cel().pixels) {
            const std::array<std::uint8_t, 4> values = {
                r(pixel), g(pixel), b(pixel), a(pixel)};
            const std::uint8_t value = values[channel];
            expected.push_back(rgba(value, value, value, 255));
        }
        const GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
        assert(result.success);
        assert(result.pixels == expected);
        // Only the requested split output is materialized. The other three
        // ports must not inflate the cache.
        assert(result.peak_cached_images <= 2U);
    }

    GraphEffectGraph graph;
    const GraphEffectNodeId source = add_node(graph, "source.active-cel");
    const GraphEffectNodeId split = add_node(graph, "channel.split-rgba");
    const GraphEffectNodeId merge = add_node(graph, "channel.merge-rgba");
    const GraphEffectNodeId output = add_node(graph, "output");
    connect(graph, source, "image", split, "image");
    for (const std::string_view port : ports) connect(graph, split, port, merge, port);
    connect(graph, merge, "image", output, "image");

    GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);
    assert(result.peak_cached_images <= 5U);

    // Split/Merge are structural operations. Even a forged persisted value
    // must not turn them into ambiguous bypass nodes.
    find_graph_effect_node(graph, split)->enabled = false;
    find_graph_effect_node(graph, merge)->enabled = false;
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);

    TemporaryDirectory temporary;
    const std::filesystem::path path = temporary.path / "rgba-ports.pxgraph";
    std::string error;
    assert(save_graph_effect(path, graph, &error));
    assert(error.empty());
    GraphEffectGraph loaded;
    assert(load_graph_effect(path, loaded, &error));
    assert(error.empty());
    assert_graphs_equal(loaded, graph);
    assert(std::any_of(loaded.links.begin(), loaded.links.end(), [split, merge](const GraphEffectLink& link) {
        return link.from_node == split && link.from_port == "alpha" &&
               link.to_node == merge && link.to_port == "alpha";
    }));
    result = evaluate_graph_effect(loaded, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);
}

void test_channel_branch_effects_and_required_inputs() {
    GraphEffectGraph graph;
    const GraphEffectNodeId source = add_node(graph, "source.active-cel");
    const GraphEffectNodeId split = add_node(graph, "channel.split-rgba");
    const GraphEffectNodeId invert_red = add_node(graph, "effect.invert-colors");
    const GraphEffectNodeId invert_alpha = add_node(graph, "effect.invert-colors");
    const GraphEffectNodeId merge = add_node(graph, "channel.merge-rgba");
    const GraphEffectNodeId output = add_node(graph, "output");
    connect(graph, source, "image", split, "image");
    connect(graph, split, "red", invert_red, "image");
    connect(graph, invert_red, "image", merge, "red");
    connect(graph, split, "green", merge, "green");
    connect(graph, split, "blue", merge, "blue");
    connect(graph, split, "alpha", merge, "alpha");
    connect(graph, merge, "image", output, "image");

    Document document = Document::create(3, 1);
    document.active_cel().pixels = {
        rgba(0, 20, 30, 0), rgba(63, 80, 90, 127), rgba(255, 110, 120, 255)};
    GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    std::vector<Pixel> expected;
    for (const Pixel pixel : document.active_cel().pixels) {
        expected.push_back(rgba(static_cast<std::uint8_t>(255U - r(pixel)),
                                g(pixel), b(pixel), a(pixel)));
    }
    assert(result.pixels == expected);

    // The alpha data is carried as grayscale RGB, so normal image effects can
    // be inserted on that branch exactly like on an RGB branch.
    connect(graph, split, "alpha", invert_alpha, "image");
    connect(graph, invert_alpha, "image", merge, "alpha");
    expected.clear();
    for (const Pixel pixel : document.active_cel().pixels) {
        expected.push_back(rgba(static_cast<std::uint8_t>(255U - r(pixel)),
                                g(pixel), b(pixel),
                                static_cast<std::uint8_t>(255U - a(pixel))));
    }
    result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels == expected);

    GraphEffectGraph incomplete;
    const GraphEffectNodeId incomplete_source = add_node(incomplete, "source.active-cel");
    const GraphEffectNodeId incomplete_split = add_node(incomplete, "channel.split-rgba");
    const GraphEffectNodeId incomplete_merge = add_node(incomplete, "channel.merge-rgba");
    const GraphEffectNodeId incomplete_output = add_node(incomplete, "output");
    connect(incomplete, incomplete_source, "image", incomplete_split, "image");
    connect(incomplete, incomplete_split, "red", incomplete_merge, "red");
    connect(incomplete, incomplete_split, "green", incomplete_merge, "green");
    connect(incomplete, incomplete_split, "blue", incomplete_merge, "blue");
    connect(incomplete, incomplete_merge, "image", incomplete_output, "image");
    assert(validate_graph_effect(incomplete, GraphEffectValidationMode::Structural).ok());
    const GraphEffectValidation validation =
        validate_graph_effect(incomplete, GraphEffectValidationMode::Evaluable);
    assert(!validation.ok());
    assert(has_diagnostic(validation, "missing-input"));
    assert(!evaluate_graph_effect(incomplete, document).success);

    const std::vector<GraphEffectLink> before_invalid_port = incomplete.links;
    std::string error;
    assert(!connect_graph_effect_nodes(incomplete, incomplete_split, "image",
                                       incomplete_merge, "alpha", &error));
    assert(!error.empty());
    assert(incomplete.links == before_invalid_port);
}

void test_channel_merge_external_images_and_repeated_lifetimes() {
    GraphEffectGraph merge_graph;
    const GraphEffectNodeId red = add_node(merge_graph, "generator.solid");
    const GraphEffectNodeId green = add_node(merge_graph, "generator.solid");
    const GraphEffectNodeId blue = add_node(merge_graph, "generator.solid");
    const GraphEffectNodeId alpha = add_node(merge_graph, "generator.solid");
    const GraphEffectNodeId merge = add_node(merge_graph, "channel.merge-rgba");
    const GraphEffectNodeId output = add_node(merge_graph, "output");
    find_graph_effect_node(merge_graph, red)->parameters["color"] = rgba(255, 0, 0, 0);
    find_graph_effect_node(merge_graph, green)->parameters["color"] = rgba(0, 255, 0, 17);
    find_graph_effect_node(merge_graph, blue)->parameters["color"] = rgba(0, 0, 255, 128);
    find_graph_effect_node(merge_graph, alpha)->parameters["color"] = rgba(100, 150, 200, 0);
    connect(merge_graph, red, "image", merge, "red");
    connect(merge_graph, green, "image", merge, "green");
    connect(merge_graph, blue, "image", merge, "blue");
    connect(merge_graph, alpha, "image", merge, "alpha");
    connect(merge_graph, merge, "image", output, "image");

    const Document document = patterned_document(2, 2);
    const GraphEffectEvaluation merged = evaluate_graph_effect(merge_graph, document);
    assert(merged.success);
    // Merge samples rounded luminance and deliberately ignores the carrier
    // image's alpha byte. In particular, fully transparent inputs remain
    // useful channel sources.
    assert(std::all_of(merged.pixels.begin(), merged.pixels.end(), [](Pixel pixel) {
        return pixel == rgba(54, 182, 18, 143);
    }));

    GraphEffectGraph repeated;
    GraphEffectNodeId previous = add_node(repeated, "source.active-cel");
    for (int index = 0; index < 12; ++index) {
        const GraphEffectNodeId split = add_node(repeated, "channel.split-rgba");
        const GraphEffectNodeId merge_node = add_node(repeated, "channel.merge-rgba");
        connect(repeated, previous, "image", split, "image");
        for (const std::string_view port : {"red", "green", "blue", "alpha"}) {
            connect(repeated, split, port, merge_node, port);
        }
        previous = merge_node;
    }
    const GraphEffectNodeId repeated_output = add_node(repeated, "output");
    connect(repeated, previous, "image", repeated_output, "image");
    const GraphEffectEvaluation repeated_result = evaluate_graph_effect(repeated, document);
    assert(repeated_result.success);
    assert(repeated_result.pixels == document.active_cel().pixels);
    assert(repeated_result.peak_cached_images <= 5U);
}

void test_channel_port_consumer_lifetimes() {
    GraphEffectGraph graph;
    const GraphEffectNodeId source = add_node(graph, "source.active-cel");
    const GraphEffectNodeId split = add_node(graph, "channel.split-rgba");
    const GraphEffectNodeId invert = add_node(graph, "effect.invert-colors");
    const GraphEffectNodeId grayscale = add_node(graph, "effect.grayscale");
    const GraphEffectNodeId mix = add_node(graph, "combine.mix");
    const GraphEffectNodeId output = add_node(graph, "output");
    connect(graph, source, "image", split, "image");
    // Fan out one port twice; the three other split outputs are intentionally
    // unused and must never be retained in the cache.
    connect(graph, split, "red", invert, "image");
    connect(graph, split, "red", grayscale, "image");
    connect(graph, invert, "image", mix, "base");
    connect(graph, grayscale, "image", mix, "blend");
    connect(graph, mix, "image", output, "image");

    const Document document = patterned_document(5, 4);
    const GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(result.pixels.size() == document.active_cel().pixels.size());
    assert(result.peak_cached_images <= 3U);
}

void test_intermediate_image_lifetime() {
    GraphEffectGraph chain;
    const GraphEffectNodeId source = add_node(chain, "source.active-cel");
    GraphEffectNodeId previous = source;
    for (int index = 0; index < 40; ++index) {
        const GraphEffectNodeId effect = add_node(chain, "effect.invert-colors");
        connect(chain, previous, "image", effect, "image");
        previous = effect;
    }
    const GraphEffectNodeId output = add_node(chain, "output");
    connect(chain, previous, "image", output, "image");

    const Document document = patterned_document(3, 2);
    GraphEffectEvaluation result = evaluate_graph_effect(chain, document);
    assert(result.success);
    assert(result.pixels == document.active_cel().pixels);
    assert(result.peak_cached_images <= 2U);

    GraphEffectGraph fan_out;
    const GraphEffectNodeId fan_source = add_node(fan_out, "source.active-cel");
    const GraphEffectNodeId invert = add_node(fan_out, "effect.invert-colors");
    const GraphEffectNodeId grayscale = add_node(fan_out, "effect.grayscale");
    const GraphEffectNodeId mix = add_node(fan_out, "combine.mix");
    const GraphEffectNodeId fan_output = add_node(fan_out, "output");
    connect(fan_out, fan_source, "image", invert, "image");
    connect(fan_out, fan_source, "image", grayscale, "image");
    connect(fan_out, invert, "image", mix, "base");
    connect(fan_out, grayscale, "image", mix, "blend");
    connect(fan_out, mix, "image", fan_output, "image");

    result = evaluate_graph_effect(fan_out, document);
    assert(result.success);
    assert(result.pixels.size() == document.active_cel().pixels.size());
    assert(result.peak_cached_images <= 3U);
}

void test_generators_and_determinism() {
    Document document = patterned_document(9, 7);
    GraphEffectNodeId generator = 0;
    GraphEffectGraph solid = graph_with_generator("generator.solid", &generator);
    find_graph_effect_node(solid, generator)->parameters["color"] = rgba(12, 34, 56, 78);
    GraphEffectEvaluation result = evaluate_graph_effect(solid, document);
    assert(result.success);
    assert(std::all_of(result.pixels.begin(), result.pixels.end(), [](Pixel pixel) {
        return pixel == rgba(12, 34, 56, 78);
    }));

    GraphEffectGraph gradient = graph_with_generator("generator.gradient", &generator);
    GraphEffectNode* gradient_node = find_graph_effect_node(gradient, generator);
    assert(gradient_node != nullptr);
    gradient_node->parameters["color_a"] = rgba(0, 0, 0, 255);
    gradient_node->parameters["color_b"] = rgba(255, 255, 255, 255);
    gradient_node->parameters["x0"] = 0.0;
    gradient_node->parameters["y0"] = 0.0;
    gradient_node->parameters["x1"] = 1.0;
    gradient_node->parameters["y1"] = 0.0;
    result = evaluate_graph_effect(gradient, document);
    assert(result.success);
    assert(result.pixels.front() == rgba(0, 0, 0, 255));
    assert(result.pixels[8] == rgba(255, 255, 255, 255));

    for (const std::string_view type : {"generator.clouds", "generator.turbulence",
                                        "generator.julia", "generator.mandelbrot"}) {
        GraphEffectGraph graph = graph_with_generator(type);
        const GraphEffectEvaluation first = evaluate_graph_effect(graph, document);
        const GraphEffectEvaluation second = evaluate_graph_effect(graph, document);
        assert(first.success);
        assert(second.success);
        assert(first.pixels.size() == document.active_cel().pixels.size());
        assert(first.pixels == second.pixels);
    }

    document.selection.select_rect(2, 2, 3, 3, true);
    result = evaluate_graph_effect(solid, document);
    assert(result.success);
    assert(result.pixels[static_cast<std::size_t>(document.pixel_index(2, 2))] == rgba(12, 34, 56, 78));
    assert(result.pixels[0] == document.active_cel().pixels[0]);
}

void test_curves_and_malformed_graphs() {
    GraphEffectNodeId curves_id = 0;
    GraphEffectGraph graph = graph_with_unary("adjustment.curves", &curves_id);
    GraphEffectNode* curves = find_graph_effect_node(graph, curves_id);
    assert(curves != nullptr);
    curves->parameters["curve_x"] = std::vector<double>{0.0, 0.5, 1.0};
    curves->parameters["curve_y"] = std::vector<double>{0.0, 0.8, 1.0};
    curves->parameters["luminance"] = false;
    curves->parameters["red"] = true;
    assert(validate_graph_effect(graph).ok());

    Document document = Document::create(1, 1);
    document.active_cel().pixels[0] = rgba(64, 96, 128, 255);
    const GraphEffectEvaluation result = evaluate_graph_effect(graph, document);
    assert(result.success);
    assert(r(result.pixels[0]) > 64);
    assert(g(result.pixels[0]) == 96);
    assert(b(result.pixels[0]) == 128);

    curves->parameters["curve_x"] = std::vector<double>{0.0, 0.7, 0.6, 1.0};
    GraphEffectValidation invalid = validate_graph_effect(graph);
    assert(!invalid.ok());
    assert(has_diagnostic(invalid, "invalid-curve"));

    GraphEffectGraph missing_output;
    add_node(missing_output, "source.active-cel");
    assert(validate_graph_effect(missing_output, GraphEffectValidationMode::Structural).ok());
    invalid = validate_graph_effect(missing_output, GraphEffectValidationMode::Evaluable);
    assert(!invalid.ok());
    assert(has_diagnostic(invalid, "output-count"));

    GraphEffectGraph dangling = make_default_graph_effect();
    dangling.links[0].from_node = 999999U;
    invalid = validate_graph_effect(dangling, GraphEffectValidationMode::Structural);
    assert(!invalid.ok());
    assert(has_diagnostic(invalid, "missing-link-endpoint"));

    GraphEffectGraph unknown = make_default_graph_effect();
    unknown.nodes[0].type_id = "future.unknown";
    invalid = validate_graph_effect(unknown, GraphEffectValidationMode::Structural);
    assert(!invalid.ok());
    assert(has_diagnostic(invalid, "unknown-node-type"));
}

void test_save_load_round_trip_and_corruptions() {
    TemporaryDirectory temporary;
    const std::filesystem::path graph_path = temporary.path / "portrait.pxgraph";
    GraphEffectGraph graph = make_default_graph_effect();
    graph.name = "Portrait pipeline";
    const GraphEffectNodeId solid = add_node(graph, "generator.solid", 12.5, -8.25);
    const GraphEffectNodeId curves = add_node(graph, "adjustment.curves", 90.0, 100.0);
    const GraphEffectNodeId mandelbrot = add_node(graph, "generator.mandelbrot", 140.0, 120.0);
    find_graph_effect_node(graph, solid)->parameters["color"] = rgba(1, 2, 3, 4);
    GraphEffectNode* curves_node = find_graph_effect_node(graph, curves);
    assert(curves_node != nullptr);
    curves_node->parameters["curve_x"] = std::vector<double>{0.0, 0.4, 1.0};
    curves_node->parameters["curve_y"] = std::vector<double>{0.0, 0.7, 1.0};
    GraphEffectNode* mandelbrot_node = find_graph_effect_node(graph, mandelbrot);
    assert(mandelbrot_node != nullptr);
    mandelbrot_node->parameters["invert"] = true;
    mandelbrot_node->enabled = false;

    std::string error;
    assert(save_graph_effect(graph_path, graph, &error));
    assert(error.empty());
    GraphEffectGraph loaded;
    assert(load_graph_effect(graph_path, loaded, &error));
    assert(error.empty());
    assert_graphs_equal(graph, loaded);

    graph.name = "Overwritten graph";
    assert(save_graph_effect(graph_path.string(), graph, &error));
    assert(load_graph_effect(graph_path.string(), loaded, &error));
    assert(loaded.name == "Overwritten graph");

    GraphEffectGraph draft;
    add_node(draft, "effect.gaussian-blur");
    const std::filesystem::path draft_path = temporary.path / "draft.pxgraph";
    assert(save_graph_effect(draft_path, draft, &error));
    assert(load_graph_effect(draft_path, loaded, &error));
    assert(validate_graph_effect(loaded, GraphEffectValidationMode::Structural).ok());
    assert(!evaluate_graph_effect(loaded, patterned_document()).success);

    const std::filesystem::path invalid_json = temporary.path / "invalid.pxgraph";
    {
        std::ofstream out(invalid_json);
        out << "{ definitely not json";
    }
    GraphEffectGraph unchanged = make_default_graph_effect();
    loaded = unchanged;
    assert(!load_graph_effect(invalid_json, loaded, &error));
    assert(!error.empty());
    assert_graphs_equal(loaded, unchanged);

    const std::filesystem::path wrong_version = temporary.path / "future.pxgraph";
    {
        std::ofstream out(wrong_version);
        out << "{\"format\":\"pixelart98-graph-effect\",\"version\":999,"
               "\"name\":\"future\",\"nodes\":[],\"links\":[]}";
    }
    assert(!load_graph_effect(wrong_version, loaded, &error));
    assert(error.find("version") != std::string::npos);

    assert(!load_graph_effect(temporary.path / "missing.pxgraph", loaded, &error));
    assert(!error.empty());
}

void test_bounded_atomic_save_and_color_validation() {
    TemporaryDirectory temporary;
    const std::filesystem::path graph_path = temporary.path / "bounded.pxgraph";
    GraphEffectGraph original = make_default_graph_effect();
    original.name = "Graph kept on failed replacement";
    std::string error;
    assert(save_graph_effect(graph_path, original, &error));
    assert(error.empty());

    GraphEffectGraph oversized = original;
    oversized.name.assign(17U * 1024U * 1024U, 'x');
    assert(!save_graph_effect(graph_path, oversized, &error));
    assert(error.find("maximum file size") != std::string::npos);

    GraphEffectGraph loaded;
    assert(load_graph_effect(graph_path, loaded, &error));
    assert(error.empty());
    assert_graphs_equal(loaded, original);
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(temporary.path)) {
        assert(!entry.path().filename().string().starts_with("bounded.pxgraph.tmp-"));
    }

    const std::filesystem::path directory_destination = temporary.path / "existing-directory";
    assert(std::filesystem::create_directory(directory_destination));
    assert(!save_graph_effect(directory_destination, original, &error));
    assert(!error.empty());
    assert(std::filesystem::is_directory(directory_destination));
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(temporary.path)) {
        assert(!entry.path().filename().string().starts_with("existing-directory.tmp-"));
    }

    const auto write_graph_with_color = [&temporary](int channel) {
        const std::filesystem::path path =
            temporary.path / ("invalid-color-" + std::to_string(channel) + ".pxgraph");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        assert(out);
        out << "{\"format\":\"pixelart98-graph-effect\",\"version\":1,"
               "\"name\":\"invalid color\",\"nodes\":["
               "{\"id\":1,\"type_id\":\"generator.solid\","
               "\"position\":{\"x\":0,\"y\":0},\"enabled\":true,"
               "\"parameters\":{\"color\":{\"type\":\"color\",\"value\":["
            << channel
            << ",0,0,255]}}},{\"id\":2,\"type_id\":\"output\","
               "\"position\":{\"x\":100,\"y\":0},\"enabled\":true,"
               "\"parameters\":{}}],\"links\":[{\"from\":{\"node\":1,"
               "\"port\":\"image\"},\"to\":{\"node\":2,\"port\":\"image\"}}]}";
        out.close();
        assert(out);
        return path;
    };

    for (const int invalid_channel : {-1, 256}) {
        loaded = original;
        assert(!load_graph_effect(write_graph_with_color(invalid_channel), loaded, &error));
        assert(error.find("0..255") != std::string::npos);
        assert_graphs_equal(loaded, original);
    }
}

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
