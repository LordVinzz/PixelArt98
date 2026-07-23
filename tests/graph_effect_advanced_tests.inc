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

    const std::filesystem::path optional_defaults = temporary.path / "optional-defaults.pxgraph";
    {
        std::ofstream out(optional_defaults);
        out << "{\"format\":\"pixelart98-graph-effect\",\"version\":1,"
               "\"nodes\":[{\"id\":1,\"type_id\":\"source.active-cel\","
               "\"position\":{\"x\":0,\"y\":0}},{\"id\":2,\"type_id\":\"output\","
               "\"position\":{\"x\":100,\"y\":0}}],\"links\":[{\"from\":{\"node\":1,"
               "\"port\":\"image\"},\"to\":{\"node\":2,\"port\":\"image\"}}]}";
    }
    assert(load_graph_effect(optional_defaults, loaded, &error));
    assert(error.empty());
    assert(loaded.name == "Untitled Graph");
    assert(loaded.nodes.size() == 2U);
    assert(std::all_of(loaded.nodes.begin(), loaded.nodes.end(), [](const GraphEffectNode& node) {
        return node.enabled && node.parameters.empty();
    }));

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
