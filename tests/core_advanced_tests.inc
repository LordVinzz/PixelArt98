static void test_gpu_chunking_policy() {
    const GpuEffectMode chunk_safe[] = {
        GpuEffectMode::BrightnessContrast,
        GpuEffectMode::Hsv,
        GpuEffectMode::Temperature,
        GpuEffectMode::Levels,
        GpuEffectMode::TonalRange,
        GpuEffectMode::Curves,
        GpuEffectMode::PaletteQuantize,
        GpuEffectMode::PaletteDither,
        GpuEffectMode::AutoLevel,
        GpuEffectMode::Grayscale,
        GpuEffectMode::Sepia,
        GpuEffectMode::InvertColors,
        GpuEffectMode::InvertAlpha,
        GpuEffectMode::Posterize,
        GpuEffectMode::OilPainting,
        GpuEffectMode::InkSketch,
        GpuEffectMode::PencilSketch,
        GpuEffectMode::GaussianBlur,
        GpuEffectMode::MotionBlur,
        GpuEffectMode::MedianBlur,
        GpuEffectMode::SurfaceBlur,
        GpuEffectMode::ReduceNoise,
        GpuEffectMode::Glow,
        GpuEffectMode::RedEyeRemoval,
        GpuEffectMode::Sharpen,
        GpuEffectMode::SoftenPortrait,
        GpuEffectMode::DepthOfField,
        GpuEffectMode::EdgeDetect,
        GpuEffectMode::Emboss,
        GpuEffectMode::Outline,
        GpuEffectMode::Relief
    };
    const GpuEffectMode full_image_only[] = {
        GpuEffectMode::RadialBlur,
        GpuEffectMode::ZoomBlur,
        GpuEffectMode::Pixelate,
        GpuEffectMode::Crystalize,
        GpuEffectMode::FrostedGlass,
        GpuEffectMode::Bulge,
        GpuEffectMode::Twist,
        GpuEffectMode::TileReflection,
        GpuEffectMode::Dents,
        GpuEffectMode::PolarInversion,
        GpuEffectMode::AddNoise,
        GpuEffectMode::Vignette,
        GpuEffectMode::Clouds,
        GpuEffectMode::JuliaFractal,
        GpuEffectMode::MandelbrotFractal,
        GpuEffectMode::Turbulence,
        GpuEffectMode::AffineTransform
    };

    bool covered[48] = {};
    for (GpuEffectMode mode : chunk_safe) {
        GpuEffectRequest request;
        request.mode = mode;
        assert(GpuEffectRenderer::effect_supports_chunking(request));
        covered[static_cast<int>(mode)] = true;
    }
    for (GpuEffectMode mode : full_image_only) {
        GpuEffectRequest request;
        request.mode = mode;
        assert(!GpuEffectRenderer::effect_supports_chunking(request));
        covered[static_cast<int>(mode)] = true;
    }
    for (bool mode_covered : covered) {
        assert(mode_covered);
    }

    GpuEffectRequest blur;
    blur.mode = GpuEffectMode::GaussianBlur;
    blur.params = {12.0f, 0.0f, 0.0f, 0.0f};
    assert(GpuEffectRenderer::effect_chunk_halo(blur) == 12);

    GpuEffectRequest depth_of_field;
    depth_of_field.mode = GpuEffectMode::DepthOfField;
    depth_of_field.params = {128.0f / 255.0f, 1.0f, 32.0f, 9.0f};
    assert(GpuEffectRenderer::effect_chunk_halo(depth_of_field) == 9);

    GpuEffectRequest motion;
    motion.mode = GpuEffectMode::MotionBlur;
    motion.params = {64.0f, 0.0f, 0.0f, 0.0f};
    assert(GpuEffectRenderer::effect_chunk_halo(motion) == 24);

    GpuEffectRequest twist;
    twist.mode = GpuEffectMode::Twist;
    assert(GpuEffectRenderer::effect_chunk_halo(twist) == 0);

    GpuBackendCapabilities older_gpu;
    older_gpu.max_texture_size = 4096;
    older_gpu.working_texture_budget = 64ULL * 1024ULL * 1024ULL;
    older_gpu.supports_chunking = true;
    const int older_extent = GpuEffectRenderer::choose_chunk_extent(9921, 14031, 12, older_gpu);
    assert(older_extent > 0);
    assert(older_extent <= older_gpu.max_texture_size - 24);

    GpuBackendCapabilities modern_gpu;
    modern_gpu.max_texture_size = 16384;
    modern_gpu.working_texture_budget = 512ULL * 1024ULL * 1024ULL;
    modern_gpu.supports_chunking = true;
    const int modern_extent = GpuEffectRenderer::choose_chunk_extent(9921, 14031, 12, modern_gpu);
    assert(modern_extent >= older_extent);
    assert(modern_extent <= modern_gpu.max_texture_size - 24);
}

static void test_depth_map_chunking_and_normalization() {
    const std::vector<DepthTile> tiles = build_depth_tiles(2500, 1300, 1024, 128);
    assert(tiles.size() == 6);
    std::vector<bool> covered(static_cast<std::size_t>(2500 * 1300), false);
    for (const DepthTile& tile : tiles) {
        assert(tile.width > 0);
        assert(tile.height > 0);
        assert(tile.expanded_x <= tile.x);
        assert(tile.expanded_y <= tile.y);
        assert(tile.expanded_x + tile.expanded_width >= tile.x + tile.width);
        assert(tile.expanded_y + tile.expanded_height >= tile.y + tile.height);
        for (int y = tile.y; y < tile.y + tile.height; ++y) {
            for (int x = tile.x; x < tile.x + tile.width; ++x) {
                covered[static_cast<std::size_t>(y * 2500 + x)] = true;
            }
        }
    }
    assert(std::all_of(covered.begin(), covered.end(), [](bool value) { return value; }));

    std::vector<float> depth = {0.0f, 0.25f, 0.5f, 1.0f};
    std::vector<Pixel> source = {
        rgba(10, 10, 10, 255),
        rgba(20, 20, 20, 255),
        rgba(30, 30, 30, 255),
        rgba(40, 40, 40, 0)
    };
    std::vector<Pixel> pixels = normalize_depth_to_pixels(depth, source, 2, 2);
    assert(pixels.size() == 4);
    assert(r(pixels[0]) == 0);
    assert(r(pixels[1]) > r(pixels[0]));
    assert(r(pixels[2]) > r(pixels[1]));
    assert(r(pixels[2]) == 255);
    assert(r(pixels[3]) == 0);
    assert(a(pixels[3]) == 255);
    assert(depth_pixels_have_full_range(pixels));

    std::vector<float> flat_depth = {0.5f, 0.5f, 0.5f, 0.5f};
    std::vector<Pixel> flat_pixels = normalize_depth_to_pixels(flat_depth, source, 2, 2);
    assert(!depth_pixels_have_full_range(flat_pixels));
}

static void test_depth_map_stitching_aligns_overlapped_tiles() {
    constexpr int width = 8;
    constexpr int height = 4;
    auto expected = [](int x, int y) {
        return static_cast<float>(x) + static_cast<float>(y) * 0.25f;
    };

    DepthTile left;
    left.x = 0;
    left.y = 0;
    left.width = 4;
    left.height = height;
    left.expanded_x = 0;
    left.expanded_y = 0;
    left.expanded_width = 5;
    left.expanded_height = height;

    DepthTile right;
    right.x = 4;
    right.y = 0;
    right.width = 4;
    right.height = height;
    right.expanded_x = 3;
    right.expanded_y = 0;
    right.expanded_width = 5;
    right.expanded_height = height;

    DepthTilePrediction left_prediction;
    left_prediction.tile = left;
    left_prediction.depth.resize(static_cast<std::size_t>(left.expanded_width * left.expanded_height));
    for (int y = 0; y < left.expanded_height; ++y) {
        for (int x = 0; x < left.expanded_width; ++x) {
            left_prediction.depth[static_cast<std::size_t>(y * left.expanded_width + x)] =
                expected(left.expanded_x + x, left.expanded_y + y);
        }
    }

    DepthTilePrediction right_prediction;
    right_prediction.tile = right;
    right_prediction.depth.resize(static_cast<std::size_t>(right.expanded_width * right.expanded_height));
    for (int y = 0; y < right.expanded_height; ++y) {
        for (int x = 0; x < right.expanded_width; ++x) {
            const float true_depth = expected(right.expanded_x + x, right.expanded_y + y);
            right_prediction.depth[static_cast<std::size_t>(y * right.expanded_width + x)] = true_depth * 2.0f + 7.0f;
        }
    }

    const std::vector<float> stitched = stitch_depth_tiles({left_prediction, right_prediction}, width, height);
    assert(stitched.size() == static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float actual = stitched[static_cast<std::size_t>(y * width + x)];
            assert(std::abs(actual - expected(x, y)) < 0.05f);
        }
    }
}

static void test_depth_map_layer_insert_is_undoable() {
    auto doc = Document::create(3, 2);
    doc.layers[0].name = "Photo";
    std::vector<Pixel> depth(static_cast<std::size_t>(doc.width * doc.height), rgba(128, 128, 128, 255));
    doc.insert_layer(1, "Photo Depth Map", depth, "Generate Depth Map");
    assert(doc.layers.size() == 2);
    assert(doc.active_layer == 1);
    assert(doc.layers[1].name == "Photo Depth Map");
    assert(r(doc.active_cel().pixels[0]) == 128);
    assert(doc.undo());
    assert(doc.layers.size() == 1);
    assert(doc.layers[0].name == "Photo");
    assert(doc.redo());
    assert(doc.layers.size() == 2);
    assert(r(doc.active_cel().pixels[0]) == 128);
}

static void test_depth_of_field_filter() {
    auto doc = Document::create(5, 1);
    for (int x = 0; x < doc.width; ++x) {
        doc.active_cel().pixels[static_cast<std::size_t>(x)] =
            rgba(static_cast<std::uint8_t>(x * 50), 0, 0, static_cast<std::uint8_t>(220 - x * 10));
    }
    const std::vector<Pixel> before = doc.active_cel().pixels;
    std::vector<Pixel> depth = {
        rgba(0, 0, 0, 255),
        rgba(64, 64, 64, 255),
        rgba(128, 128, 128, 255),
        rgba(192, 192, 192, 255),
        rgba(255, 255, 255, 255)
    };
    DepthOfFieldSettings settings;
    settings.focus_depth = 128;
    settings.aperture = 100;
    settings.falloff = 32;
    settings.max_radius = 2;
    apply_depth_of_field(doc, depth, settings);
    const auto& pixels = doc.active_cel().pixels;
    assert(pixels[2] == before[2]);
    assert(pixels[0] != before[0]);
    assert(a(pixels[0]) < a(before[0]));
    assert(a(pixels[0]) > 0);

    auto selected = Document::create(3, 1);
    selected.active_cel().pixels[0] = rgba(0, 0, 0, 255);
    selected.active_cel().pixels[1] = rgba(128, 0, 0, 255);
    selected.active_cel().pixels[2] = rgba(255, 0, 0, 255);
    std::vector<Pixel> selected_depth = {
        rgba(0, 0, 0, 255),
        rgba(0, 0, 0, 255),
        rgba(0, 0, 0, 255)
    };
    selected.selection.resize(3, 1);
    selected.selection.active = true;
    selected.selection.mask = {0, 255, 0};
    apply_depth_of_field(selected, selected_depth, settings);
    assert(r(selected.active_cel().pixels[0]) == 0);
    assert(r(selected.active_cel().pixels[1]) != 128);
    assert(r(selected.active_cel().pixels[2]) == 255);

    auto mismatch = Document::create(2, 1);
    const auto mismatch_before = mismatch.active_cel().pixels;
    apply_depth_of_field(mismatch, {}, settings);
    assert(mismatch.active_cel().pixels == mismatch_before);
}

static Pixel composite_blend_sample(LayerBlendMode mode) {
    auto doc = Document::create(1, 1);
    doc.active_cel().pixels[0] = rgba(80, 120, 160, 255);
    doc.add_layer("Blend");
    doc.frames[0].cels[1].pixels[0] = rgba(40, 200, 90, 255);
    doc.layers[1].blend_mode = mode;
    return doc.composite_active()[0];
}

static void test_layer_blend_modes() {
    assert(std::string(layer_blend_mode_name(LayerBlendMode::Additive)) == "Additive");
    assert(std::string(layer_blend_mode_name(LayerBlendMode::ColorBurn)) == "Color Burn");
    assert(std::string(layer_blend_mode_name(LayerBlendMode::ColorDodge)) == "Color Dodge");
    assert(std::string(layer_blend_mode_name(LayerBlendMode::Xor)) == "Xor");

    Pixel normal = composite_blend_sample(LayerBlendMode::Normal);
    assert(r(normal) == 40);
    assert(g(normal) == 200);
    assert(b(normal) == 90);

    Pixel multiply = composite_blend_sample(LayerBlendMode::Multiply);
    assert(r(multiply) == (80 * 40) / 255);
    assert(g(multiply) == (120 * 200) / 255);
    assert(b(multiply) == (160 * 90) / 255);

    Pixel additive = composite_blend_sample(LayerBlendMode::Additive);
    assert(r(additive) == 120);
    assert(g(additive) == 255);
    assert(b(additive) == 250);

    Pixel difference = composite_blend_sample(LayerBlendMode::Difference);
    assert(r(difference) == 40);
    assert(g(difference) == 80);
    assert(b(difference) == 70);

    Pixel negation = composite_blend_sample(LayerBlendMode::Negation);
    assert(r(negation) == 120);
    assert(g(negation) == 190);
    assert(b(negation) == 250);

    Pixel lighten = composite_blend_sample(LayerBlendMode::Lighten);
    assert(r(lighten) == 80);
    assert(g(lighten) == 200);
    assert(b(lighten) == 160);

    Pixel darken = composite_blend_sample(LayerBlendMode::Darken);
    assert(r(darken) == 40);
    assert(g(darken) == 120);
    assert(b(darken) == 90);

    Pixel screen = composite_blend_sample(LayerBlendMode::Screen);
    assert(r(screen) == 108);
    assert(g(screen) == 226);
    assert(b(screen) == 194);

    Pixel xored = composite_blend_sample(LayerBlendMode::Xor);
    assert(r(xored) == (80 ^ 40));
    assert(g(xored) == (120 ^ 200));
    assert(b(xored) == (160 ^ 90));

    const LayerBlendMode modes[] = {LayerBlendMode::ColorBurn, LayerBlendMode::ColorDodge, LayerBlendMode::Reflect,
                                    LayerBlendMode::Glow, LayerBlendMode::Overlay};
    for (LayerBlendMode mode : modes) {
        assert(a(composite_blend_sample(mode)) == 255);
    }
}

static void test_model_json() {
    auto model = ModelDocument::create_default();
    model.add_cuboid();
    model.cuboids[0].uv[0] = {-10, 2, 999, 3};
    clamp_model_uvs(model);
    assert(uv_rect_valid(model.cuboids[0].uv[0], model.texture_width, model.texture_height));
    std::string json = model_to_json(model);
    ModelDocument decoded;
    std::string error;
    assert(model_from_json(json, decoded, &error));
    assert(decoded.cuboids.size() == model.cuboids.size());
    assert(decoded.texture_width == model.texture_width);

    Renderer3D renderer;
    FaceHit hit = renderer.pick_face(decoded, ModelViewportState{}, 128, 128, 64.0f, 64.0f);
    assert(hit.hit);
    float projected_x = 0.0f;
    float projected_y = 0.0f;
    assert(renderer.project_first_cuboid_corner(decoded, ModelViewportState{}, 128, 128, projected_x, projected_y));
    assert(renderer.project_world_point(decoded, ModelViewportState{}, 128, 128, 0.0f, 0.0f, 0.0f, projected_x, projected_y));

    assert(model.remove_selected());
    assert(model.remove_selected());
    assert(model.cuboids.empty());
    clamp_model_uvs(model);
    assert(model.cuboids.empty());
    assert(model.selected_cuboid == -1);
    std::string empty_json = model_to_json(model);
    ModelDocument empty_decoded;
    assert(model_from_json(empty_json, empty_decoded, &error));
    assert(empty_decoded.cuboids.empty());
    FaceHit empty_hit = renderer.pick_face(empty_decoded, ModelViewportState{}, 128, 128, 64.0f, 64.0f);
    assert(!empty_hit.hit);
}

static void test_model_transform_helpers() {
    Cuboid cuboid;
    cuboid.from = {1.0f, 2.0f, 3.0f};
    cuboid.to = {5.0f, 8.0f, 13.0f};
    cuboid.rotation_origin = {3.0f, 5.0f, 8.0f};

    assert(std::abs(cuboid_axis_size(cuboid, 0) - 4.0f) < 0.001f);
    translate_cuboid(cuboid, 0, 5.9f, true);
    assert(std::abs(cuboid.from[0] - 5.0f) < 0.001f);
    assert(std::abs(cuboid.to[0] - 9.0f) < 0.001f);
    assert(std::abs(cuboid.rotation_origin[0] - 7.0f) < 0.001f);

    scale_cuboid(cuboid, 1, 1.25f, true);
    assert(std::abs(cuboid_axis_size(cuboid, 1) - 12.0f) < 0.001f);
    assert(std::abs(cuboid.rotation_origin[1] - 5.0f) < 0.001f);

    rotate_cuboid(cuboid, 2, 67.0f, true);
    assert(cuboid.rotation_axis == 2);
    assert(std::abs(cuboid.rotation_angle - 60.0f) < 0.001f);
}

static void test_mesh_uv_unwrap() {
    ModelDocument model;
    model.cuboids.clear();
    MeshObject mesh;
    mesh.name = "Unwrap Test";
    auto add_triangle = [&](std::array<float, 3> a,
                            std::array<float, 3> b,
                            std::array<float, 3> c) {
        const int base = static_cast<int>(mesh.vertices.size());
        MeshVertex va;
        va.position = a;
        MeshVertex vb;
        vb.position = b;
        MeshVertex vc;
        vc.position = c;
        mesh.vertices.push_back(va);
        mesh.vertices.push_back(vb);
        mesh.vertices.push_back(vc);
        MeshTriangle triangle;
        triangle.indices = {base, base + 1, base + 2};
        mesh.triangles.push_back(triangle);
    };
    add_triangle({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    add_triangle({1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    add_triangle({4.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, {4.0f, 1.0f, 0.0f});
    model.meshes.push_back(std::move(mesh));

    const MeshUvUnwrapResult result = unwrap_model_mesh_uvs(model, 16, 16);
    assert(result.changed);
    assert(result.mesh_count == 1);
    assert(result.triangle_count == 3);
    assert(result.island_count == 2);
    assert(result.recommended_width >= 16);
    assert(result.recommended_height >= 16);
    assert(model.meshes.size() == 1U);
    assert(model.meshes[0].vertices.size() == 9U);
    for (const MeshVertex& vertex : model.meshes[0].vertices) {
        assert(vertex.uv[0] >= 0.0f && vertex.uv[0] <= 1.0f);
        assert(vertex.uv[1] >= 0.0f && vertex.uv[1] <= 1.0f);
    }
}

static void test_app_settings_roundtrip() {
    const auto settings_path = std::filesystem::temp_directory_path() / "pixelart98_settings_test.json";
    std::string error;
    AppSettings settings;
    settings.show_splash_screen = false;
    settings.auto_open_error_console = false;
    settings.heavy_gpu_optimization = false;
    settings.mps_backend = true;
    settings.depth_allow_cpu_fallback = false;
    settings.depth_tile_size = 768;
    settings.depth_tile_overlap = 96;
    settings.show_tools_panel = false;
    settings.show_colors_panel = false;
    settings.show_layers_panel = false;
    settings.show_adjustments_panel = false;
    settings.show_animation_panel = true;
    settings.show_history_panel = true;
    settings.show_model_uv_panel = true;
    settings.show_3d_preview = true;
    settings.show_canvas_cuboid_uv_overlay = true;
    settings.ffmpeg_path = "/custom/tools/ffmpeg";
    assert(save_app_settings(settings, settings_path, &error));

    const AppSettings loaded = load_app_settings(settings_path, &error);
    assert(!loaded.show_splash_screen);
    assert(!loaded.auto_open_error_console);
    assert(!loaded.heavy_gpu_optimization);
    assert(loaded.mps_backend);
    assert(!loaded.depth_allow_cpu_fallback);
    assert(loaded.depth_tile_size == 768);
    assert(loaded.depth_tile_overlap == 96);
    assert(!loaded.show_tools_panel);
    assert(!loaded.show_colors_panel);
    assert(!loaded.show_layers_panel);
    assert(!loaded.show_adjustments_panel);
    assert(loaded.show_animation_panel);
    assert(loaded.show_history_panel);
    assert(loaded.show_model_uv_panel);
    assert(loaded.show_3d_preview);
    assert(loaded.show_canvas_cuboid_uv_overlay);
    assert(loaded.ffmpeg_path == "/custom/tools/ffmpeg");
    const AppSettings defaults = load_app_settings(settings_path.parent_path() / "pixelart98_missing_settings_test.json", &error);
    assert(defaults.heavy_gpu_optimization);
    assert(!defaults.mps_backend);
    assert(defaults.depth_allow_cpu_fallback);
    assert(defaults.depth_tile_size == 1024);
    assert(defaults.depth_tile_overlap == 128);
    assert(defaults.show_tools_panel);
    assert(defaults.show_colors_panel);
    assert(defaults.show_layers_panel);
    assert(defaults.show_adjustments_panel);
    assert(!defaults.show_animation_panel);
    assert(!defaults.show_history_panel);
    assert(!defaults.show_model_uv_panel);
    assert(!defaults.show_3d_preview);
    assert(!defaults.show_canvas_cuboid_uv_overlay);
    settings.heavy_gpu_optimization = true;
    settings.mps_backend = true;
    assert(save_app_settings(settings, settings_path, &error));
    const AppSettings loaded_mps = load_app_settings(settings_path, &error);
    assert(loaded_mps.heavy_gpu_optimization);
    assert(loaded_mps.mps_backend);
    std::filesystem::remove(settings_path);
}

static void test_project_io() {
    auto doc = Document::create(8, 8);
    fill_bucket(doc, 0, 0, rgba(10, 20, 30), 255, false);
    doc.layers[0].blend_mode = LayerBlendMode::Add;
    doc.add_frame(true);
    doc.frames[1].duration_ms = 250;
    doc.add_tag("Blink", 0, 1);
    doc.playback_mode = PlaybackMode::PingPong;
    auto model = ModelDocument::create_default();

    auto base = std::filesystem::temp_directory_path() / "pixelart98_test.pixart";
    std::string error;
    assert(save_project(base.string(), doc, model, &error));
    ProjectBundle bundle;
    assert(load_project(base.string(), bundle, &error));
    assert(bundle.document.width == 8);
    assert(bundle.document.frames.size() == 2);
    assert(r(bundle.document.frames[0].cels[0].pixels[0]) == 10);
    assert(bundle.document.layers[0].blend_mode == LayerBlendMode::Add);
    assert(bundle.document.tags.size() == 1);
    assert(bundle.document.playback_mode == PlaybackMode::PingPong);

    auto png = std::filesystem::temp_directory_path() / "pixelart98_test.png";
    auto sheet = std::filesystem::temp_directory_path() / "pixelart98_sheet.png";
    auto sheet_json = std::filesystem::temp_directory_path() / "pixelart98_sheet.json";
    auto apng = std::filesystem::temp_directory_path() / "pixelart98_anim.png";
    auto ase = std::filesystem::temp_directory_path() / "pixelart98_sprite.aseprite";
    auto mc = std::filesystem::temp_directory_path() / "pixelart98_minecraft.json";
    auto mc_rotated = std::filesystem::temp_directory_path() / "pixelart98_rotated_minecraft.json";
    assert(export_png(png.string(), bundle.document, 0, &error));
    assert(export_spritesheet(sheet.string(), sheet_json.string(), bundle.document, &error));
    assert(export_apng(apng.string(), bundle.document, &error));
    Document imported_png;
    assert(import_image(png.string(), imported_png, &error));
    assert(imported_png.width == 8);
    assert(r(imported_png.active_cel().pixels[0]) == 10);
    assert(import_image_as_layer(png.string(), imported_png, "Imported", &error));
    assert(imported_png.layers.size() == 2);
    assert(export_aseprite(ase.string(), bundle.document, &error));
    Document imported_ase;
    assert(import_aseprite(ase.string(), imported_ase, &error));
    assert(imported_ase.frames.size() == 2);
    assert(imported_ase.layers.size() == 1);
    assert(r(imported_ase.frames[0].cels[0].pixels[0]) == 10);
    assert(export_minecraft_model(mc.string(), model, "texture.png", &error));
    ModelDocument imported_model;
    assert(import_minecraft_model(mc.string(), imported_model, &error));
    assert(imported_model.cuboids.size() == model.cuboids.size());
    {
        std::ofstream file(mc_rotated);
        file << R"JSON({
          "texture_size": [32, 32],
          "textures": {"0": "panel"},
          "elements": [{
            "name": "panel",
            "from": [1, 9, 0],
            "to": [17, 10, 16],
            "rotation": {"angle": 22.5, "axis": "z", "origin": [1, 9, 0]},
            "faces": {
              "north": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "south": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "east": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "west": {"uv": [0, 0, 8, 0.5], "texture": "#0"},
              "up": {"uv": [0, 0, 16, 16], "texture": "#0"},
              "down": {"uv": [0, 0, 16, 16], "texture": "#0"}
            }
          }]
        })JSON";
    }
    ModelDocument rotated_model;
    assert(import_minecraft_model(mc_rotated.string(), rotated_model, &error));
    assert(rotated_model.texture_width == 32);
    assert(rotated_model.texture_height == 32);
    assert(rotated_model.cuboids.size() == 1);
    assert(std::abs(rotated_model.cuboids[0].rotation_angle - 22.5f) < 0.001f);
    assert(rotated_model.cuboids[0].rotation_axis == 2);
    assert(rotated_model.cuboids[0].rotation_origin[0] == 1.0f);
    assert(rotated_model.cuboids[0].rotation_origin[1] == 9.0f);
    assert(rotated_model.cuboids[0].rotation_origin[2] == 0.0f);
    std::filesystem::remove(base);
    std::filesystem::remove(png);
    std::filesystem::remove(sheet);
    std::filesystem::remove(sheet_json);
    std::filesystem::remove(apng);
    std::filesystem::remove(ase);
    std::filesystem::remove(mc);
    std::filesystem::remove(mc_rotated);
}
