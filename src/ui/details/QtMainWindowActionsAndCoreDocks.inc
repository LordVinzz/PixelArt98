void QtMainWindow::build_actions() {
    auto* toolbar = addToolBar(tr("Canvas"));
    toolbar->setObjectName(QStringLiteral("CanvasToolbar"));
    const auto add = [this, toolbar](const QString& text, const QKeySequence& shortcut, auto callback) {
        QAction* action = toolbar->addAction(text);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    add(tr("Undo"), QKeySequence::Undo, [this] { if (controller_.undo()) refresh_all(); });
    add(tr("Redo"), QKeySequence::Redo, [this] { if (controller_.redo()) refresh_all(); });
    toolbar->addSeparator();
    add(tr("-"), {}, [this] { zoom_active_workspace(false); });
    zoom_label_ = new QLabel(toolbar);
    zoom_label_->setMinimumWidth(82);
    zoom_label_->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(zoom_label_);
    add(tr("+"), {}, [this] { zoom_active_workspace(true); });
    add(tr("1:1"), {}, [this] { actual_size_active_workspace(); });
    add(tr("Fit"), {}, [this] { fit_active_workspace(); });
}

void QtMainWindow::build_menus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    auto add_action = [this](QMenu* menu, const QString& label, const QKeySequence& shortcut, auto callback) {
        QAction* action = menu->addAction(label);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    add_action(file, tr("New Document..."), QKeySequence::New, [this] { new_document(); });
    add_action(file, tr("Save Project..."), QKeySequence::Save, [this] { save_project_as(); });
    add_action(file, tr("Load Project..."), QKeySequence::Open, [this] { load_project_from(); });
    file->addSeparator();
    add_action(file, tr("Import Image as Document..."), {}, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Image as Document"), remembered_directory(QStringLiteral("import")),
            image_filter());
        if (!path.isEmpty()) import_image_document(path);
    });
    add_action(file, tr("Import Image as Layer..."), {}, [this] { import_layer(); });
    add_action(file, tr("Export Current PNG..."), {}, [this] { export_current_png(); });
    add_action(file, tr("Export Spritesheet..."), {}, [this] { export_spritesheet_file(); });
    QAction* export_gif_action = add_action(file, tr("Export Animation as GIF..."), {}, [this] {
        export_animation(AnimationExportFormat::Gif);
    });
    export_gif_action->setObjectName(QStringLiteral("ExportAnimationGif"));
    QAction* export_mp4_action = add_action(file, tr("Export Animation as MP4..."), {}, [this] {
        export_animation(AnimationExportFormat::Mp4);
    });
    export_mp4_action->setObjectName(QStringLiteral("ExportAnimationMp4"));
    add_action(file, tr("Export APNG..."), {}, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export APNG"), QDir(remembered_directory(QStringLiteral("export"))).filePath(QStringLiteral("animation.png")),
            tr("PNG (*.png)"));
        std::string error; if (!path.isEmpty() && !export_apng(path.toStdString(), controller_.document(), &error)) report_error(tr("Export APNG"), error); else if (!path.isEmpty()) remember_file_directory(QStringLiteral("export"), path);
    });
    add_action(file, tr("Import Aseprite..."), {}, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import Aseprite"), remembered_directory(QStringLiteral("import")), tr("Aseprite (*.ase *.aseprite)"));
        Document document; std::string error; if (!path.isEmpty() && import_aseprite(path.toStdString(), document, &error)) { controller_.replace_document(std::move(document)); project_path_.clear(); has_recoverable_session_ = true; remember_file_directory(QStringLiteral("import"), path); refresh_all(); } else if (!path.isEmpty()) report_error(tr("Import Aseprite"), error);
    });
    add_action(file, tr("Export Aseprite..."), {}, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Aseprite"), QDir(remembered_directory(QStringLiteral("export"))).filePath(QStringLiteral("sprite.aseprite")), tr("Aseprite (*.aseprite)"));
        std::string error; if (!path.isEmpty() && !export_aseprite(path.toStdString(), controller_.document(), &error)) report_error(tr("Export Aseprite"), error); else if (!path.isEmpty()) remember_file_directory(QStringLiteral("export"), path);
    });
    file->addSeparator();
    for (const QString& kind : {QStringLiteral("Model JSON"), QStringLiteral("STL Model"), QStringLiteral("Minecraft Model")}) add_action(file, tr("Import %1...").arg(kind), {}, [this, kind] { import_model(kind); });
    for (const QString& kind : {QStringLiteral("Model JSON"), QStringLiteral("glTF Model"), QStringLiteral("ThreeJSPack"), QStringLiteral("STL Model"), QStringLiteral("Minecraft Model")}) add_action(file, tr("Export %1...").arg(kind), {}, [this, kind] { export_model(kind); });
    file->addSeparator();
    add_action(file, tr("Quit"), QKeySequence::Quit, [this] { close(); });

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    add_action(edit, tr("Undo"), QKeySequence::Undo, [this] { if (controller_.undo()) refresh_all(); });
    add_action(edit, tr("Redo"), QKeySequence::Redo, [this] { if (controller_.redo()) refresh_all(); });
    edit->addSeparator();
    QAction* cut = add_action(edit, tr("Cut"), QKeySequence::Cut, [this] { cut_selection_to_clipboard(); });
    QAction* copy = add_action(edit, tr("Copy"), QKeySequence::Copy, [this] { (void)copy_selection_to_clipboard(); });
    QAction* paste = add_action(edit, tr("Paste"), QKeySequence::Paste, [this] { paste_from_clipboard(); });
    cut->setObjectName(QStringLiteral("EditCut"));
    copy->setObjectName(QStringLiteral("EditCopy"));
    paste->setObjectName(QStringLiteral("EditPaste"));
    edit->addSeparator();
    add_action(edit, tr("Select All"), QKeySequence::SelectAll, [this] { controller_.select_all(); refresh_all(); });
    QAction* deselect = add_action(edit, tr("Deselect"), QKeySequence(QStringLiteral("Ctrl+D")), [this] { controller_.clear_selection(); refresh_all(); });
    deselect->setObjectName(QStringLiteral("EditDeselect"));
    add_action(edit, tr("Invert Selection"), QKeySequence(QStringLiteral("Ctrl+I")), [this] { controller_.invert_selection(); refresh_all(); });
    add_action(edit, tr("Delete Selection"), QKeySequence::Delete, [this] { controller_.delete_selection(); refresh_all(); });

    QMenu* selection = menuBar()->addMenu(tr("&Selection"));
    selection->setObjectName(QStringLiteral("SelectionMenu"));
    const auto add_refinement = [this, selection](const QString& label, const QString& object_name,
                                                   const std::function<bool(int)>& operation) {
        QAction* action = selection->addAction(label);
        action->setObjectName(object_name);
        connect(action, &QAction::triggered, this, [this, label, operation] {
            bool accepted = false;
            const int radius = QInputDialog::getInt(this, label, tr("Radius (pixels)"), 1, 1, 256, 1,
                                                    &accepted);
            if (accepted && operation(radius)) refresh_all();
        });
    };
    add_refinement(tr("Expand..."), QStringLiteral("SelectionExpand"),
                   [this](int radius) { return controller_.expand_selection(radius); });
    add_refinement(tr("Contract..."), QStringLiteral("SelectionContract"),
                   [this](int radius) { return controller_.contract_selection(radius); });
    add_refinement(tr("Border..."), QStringLiteral("SelectionBorder"),
                   [this](int radius) { return controller_.border_selection(radius); });
    add_refinement(tr("Smooth..."), QStringLiteral("SelectionSmooth"),
                   [this](int radius) { return controller_.smooth_selection(radius); });

    QMenu* image = menuBar()->addMenu(tr("&Image"));
    QAction* image_resize = add_action(image, tr("Resize Image..."), {}, [this] { show_image_resize_dialog(); });
    QAction* canvas_resize = add_action(image, tr("Canvas Size..."), {}, [this] { show_canvas_resize_dialog(); });
    QAction* crop = add_action(image, tr("Crop..."), {}, [this] { show_crop_dialog(); });
    image_resize->setObjectName(QStringLiteral("ImageResize"));
    canvas_resize->setObjectName(QStringLiteral("CanvasResize"));
    crop->setObjectName(QStringLiteral("ImageCrop"));
    image->addSeparator();
    QAction* flip_horizontal = add_action(image, tr("Flip Horizontal"), {}, [this] { if (flip_document_horizontal(controller_.document(), &controller_.model())) { controller_.mark_changed("Flip Image Horizontally"); refresh_all(); } });
    QAction* flip_vertical = add_action(image, tr("Flip Vertical"), {}, [this] { if (flip_document_vertical(controller_.document(), &controller_.model())) { controller_.mark_changed("Flip Image Vertically"); refresh_all(); } });
    QAction* rotate_clockwise = add_action(image, tr("Rotate 90 Clockwise"), {}, [this] { if (rotate_document_90_clockwise(controller_.document(), &controller_.model())) { controller_.mark_changed("Rotate Image 90 Clockwise"); refresh_all(); canvas_->fit_to_canvas(); } });
    QAction* rotate_counter_clockwise = add_action(image, tr("Rotate 90 Counter-Clockwise"), {}, [this] { if (rotate_document_90_counter_clockwise(controller_.document(), &controller_.model())) { controller_.mark_changed("Rotate Image 90 Counter-Clockwise"); refresh_all(); canvas_->fit_to_canvas(); } });
    QAction* rotate_180 = add_action(image, tr("Rotate 180"), {}, [this] { if (rotate_document_180(controller_.document(), &controller_.model())) { controller_.mark_changed("Rotate Image 180"); refresh_all(); } });
    flip_horizontal->setObjectName(QStringLiteral("ImageFlipHorizontal"));
    flip_vertical->setObjectName(QStringLiteral("ImageFlipVertical"));
    rotate_clockwise->setObjectName(QStringLiteral("ImageRotate90Clockwise"));
    rotate_counter_clockwise->setObjectName(QStringLiteral("ImageRotate90CounterClockwise"));
    rotate_180->setObjectName(QStringLiteral("ImageRotate180"));
    add_action(image, tr("Straighten..."), {}, [this] { show_effect_preview(tr("Straighten"), [](Document& doc, int amount) { apply_straighten(doc, static_cast<float>(amount - 50) * 0.4f, ResamplingMode::Bicubic); }); });
    add_action(image, tr("Rotate / Zoom..."), {}, [this] { show_effect_preview(tr("Rotate / Zoom"), [](Document& doc, int amount) { apply_rotate_zoom(doc, static_cast<float>(amount - 50) * 3.6f, 1.0f, 0, 0, ResamplingMode::Bicubic); }); });

    build_adjustments_menu();

    QMenu* effects = menuBar()->addMenu(tr("E&ffects"));
    const auto effect_control = [](const char* id, const char* label, int minimum, int maximum, int initial) {
        return AdjustmentControl{QString::fromLatin1(id), QString::fromUtf8(label), minimum, maximum, initial};
    };
    const auto add_configurable_effect = [this](QMenu* menu, const char* id, const QString& name,
                                                 std::vector<AdjustmentControl> controls,
                                                 std::function<void(Document&, const std::vector<int>&)> operation,
                                                 std::function<GpuEffectRequest(const std::vector<int>&)> gpu_request) {
        add_effect_action(menu, AdjustmentSpec{QString::fromLatin1(id), name, std::move(controls),
                                               std::move(operation), std::move(gpu_request)});
    };
    const auto request_for = [](GpuEffectMode mode) {
        return [mode](const std::vector<int>&) { GpuEffectRequest request; request.mode = mode; return request; };
    };
    QMenu* artistic = effects->addMenu(tr("Artistic"));
    add_configurable_effect(artistic, "ink-sketch", tr("Ink Sketch"), {effect_control("outline", "Outline", 0, 100, 50), effect_control("coloring", "Coloring", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_ink_sketch(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::InkSketch; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(artistic, "oil-painting", tr("Oil Painting"), {effect_control("brush-size", "Brush size", 1, 16, 6), effect_control("coarseness", "Coarseness", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_oil_painting(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::OilPainting; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(artistic, "pencil-sketch", tr("Pencil Sketch"), {effect_control("tip-size", "Pencil tip size", 1, 10, 5), effect_control("range", "Color range", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_pencil_sketch(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::PencilSketch; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* blurs = effects->addMenu(tr("Blurs"));
    add_configurable_effect(blurs, "gaussian-blur", tr("Gaussian Blur"), {effect_control("radius", "Radius", 1, 32, 4)},
        [](Document& d, const std::vector<int>& v) { apply_gaussian_blur(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::GaussianBlur; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(blurs, "motion-blur", tr("Motion Blur"), {effect_control("distance", "Distance", 1, 24, 8), effect_control("angle", "Angle", -180, 180, 0)},
        [](Document& d, const std::vector<int>& v) { apply_motion_blur(d, v[0], static_cast<float>(v[1])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MotionBlur; r.params = {static_cast<float>(v[0]), 0, static_cast<float>(v[1]) * 3.14159265f / 180.0f, 0}; return r; });
    add_configurable_effect(blurs, "radial-blur", tr("Radial Blur"), {effect_control("amount", "Amount", 2, 64, 20), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_radial_blur(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::RadialBlur; r.params[1] = static_cast<float>(v[0]); r.params2 = {percentage(v[1]), percentage(v[2]), 0, 0}; return r; });
    add_configurable_effect(blurs, "zoom-blur", tr("Zoom Blur"), {effect_control("amount", "Amount", 2, 64, 20), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_zoom_blur(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::ZoomBlur; r.params[1] = static_cast<float>(v[0]); r.params2 = {percentage(v[1]), percentage(v[2]), 0, 0}; return r; });
    add_configurable_effect(blurs, "median-blur", tr("Median Blur"), {effect_control("radius", "Radius", 1, 8, 2)},
        [](Document& d, const std::vector<int>& v) { apply_median_blur(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MedianBlur; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(blurs, "surface-blur", tr("Surface Blur"), {effect_control("radius", "Radius", 1, 8, 3), effect_control("threshold", "Threshold", 0, 255, 32)},
        [](Document& d, const std::vector<int>& v) { apply_surface_blur(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::SurfaceBlur; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* color = effects->addMenu(tr("Color"));
    add_configurable_effect(color, "auto-level-effect", tr("Auto-Level"), {}, [](Document& d, const std::vector<int>&) { apply_auto_level(d); }, request_for(GpuEffectMode::AutoLevel));
    add_configurable_effect(color, "black-white", tr("Black and White"), {}, [](Document& d, const std::vector<int>&) { apply_grayscale(d); }, request_for(GpuEffectMode::Grayscale));
    add_configurable_effect(color, "sepia", tr("Sepia"), {}, [](Document& d, const std::vector<int>&) { apply_sepia(d); }, request_for(GpuEffectMode::Sepia));
    add_configurable_effect(color, "invert-colors", tr("Invert Colors"), {}, [](Document& d, const std::vector<int>&) { apply_invert(d, false); }, request_for(GpuEffectMode::InvertColors));
    add_configurable_effect(color, "invert-alpha", tr("Invert Alpha"), {}, [](Document& d, const std::vector<int>&) { apply_invert(d, true); }, request_for(GpuEffectMode::InvertAlpha));
    add_configurable_effect(color, "posterize-effect", tr("Posterize"), {effect_control("levels", "Levels", 2, 32, 8)}, [](Document& d, const std::vector<int>& v) { apply_posterize(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Posterize; r.params[0] = static_cast<float>(v[0]); return r; });
    QMenu* distort = effects->addMenu(tr("Distort"));
    add_configurable_effect(distort, "bulge", tr("Bulge"), {effect_control("strength", "Strength", -200, 200, 50), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_bulge(d, percentage(v[0]), v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Bulge; r.params[0] = percentage(v[0]); r.params2 = {percentage(v[1]), percentage(v[2]), 0, 0}; return r; });
    add_configurable_effect(distort, "crystalize", tr("Crystalize"), {effect_control("cell-size", "Cell size", 2, 64, 12)},
        [](Document& d, const std::vector<int>& v) { apply_crystalize(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Crystalize; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "dents", tr("Dents"), {effect_control("scale", "Scale", 2, 256, 64), effect_control("amount", "Amount", 1, 64, 20)},
        [](Document& d, const std::vector<int>& v) { apply_dents(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Dents; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(distort, "frosted-glass", tr("Frosted Glass"), {effect_control("scatter-radius", "Scatter radius", 1, 32, 5)},
        [](Document& d, const std::vector<int>& v) { apply_frosted_glass(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::FrostedGlass; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "pixelate", tr("Pixelate"), {effect_control("cell-size", "Cell size", 2, 64, 8)},
        [](Document& d, const std::vector<int>& v) { apply_pixelate(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Pixelate; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "polar-inversion", tr("Polar Inversion"), {effect_control("scale", "Scale (%)", 10, 200, 100)},
        [](Document& d, const std::vector<int>& v) { apply_polar_inversion(d, percentage(v[0])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::PolarInversion; r.params[0] = percentage(v[0]); return r; });
    add_configurable_effect(distort, "tile-reflection", tr("Tile Reflection"), {effect_control("tile-size", "Tile size", 2, 256, 32)},
        [](Document& d, const std::vector<int>& v) { apply_tile_reflection(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::TileReflection; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "twist", tr("Twist"), {effect_control("turns", "Turns × 100", -400, 400, 100), effect_control("size", "Effect size (%)", 10, 200, 100), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_twist(d, percentage(v[0]), v[2], v[3], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Twist; r.params = {percentage(v[0]), percentage(v[1]), 0, 0}; r.params2 = {percentage(v[2]), percentage(v[3]), 0, 0}; return r; });
    QMenu* noise = effects->addMenu(tr("Noise"));
    add_configurable_effect(noise, "add-noise", tr("Add Noise"), {effect_control("intensity", "Intensity", 0, 255, 64), effect_control("coverage", "Coverage", 0, 100, 100), effect_control("color-saturation", "Color saturation", 0, 100, 100)},
        [](Document& d, const std::vector<int>& v) { apply_add_noise(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::AddNoise; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), 0}; return r; });
    add_configurable_effect(noise, "median", tr("Median"), {effect_control("radius", "Radius", 1, 8, 2)},
        [](Document& d, const std::vector<int>& v) { apply_median_blur(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MedianBlur; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(noise, "reduce-noise", tr("Reduce Noise"), {effect_control("radius", "Radius", 1, 8, 2)},
        [](Document& d, const std::vector<int>& v) { apply_reduce_noise(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::ReduceNoise; r.params[0] = static_cast<float>(v[0]); return r; });
    QMenu* object = effects->addMenu(tr("Object"));
    add_configurable_effect(object, "feather", tr("Feather"), {effect_control("radius", "Radius", 1, 16, 3)},
        [](Document& d, const std::vector<int>& v) { apply_morphology(d, v[0], true); }, {});
    add_configurable_effect(object, "outline-object", tr("Outline"), {effect_control("thickness", "Thickness", 1, 16, 3), effect_control("intensity", "Intensity", 0, 255, 255)},
        [](Document& d, const std::vector<int>& v) { apply_outline(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Outline; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* photo = effects->addMenu(tr("Photo"));
    add_configurable_effect(photo, "glow", tr("Glow"), {effect_control("radius", "Radius", 1, 16, 5), effect_control("brightness", "Brightness", -100, 100, 30), effect_control("contrast", "Contrast", -100, 100, 0)},
        [](Document& d, const std::vector<int>& v) { apply_glow(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Glow; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), 0}; return r; });
    add_configurable_effect(photo, "red-eye-removal", tr("Red Eye Removal"), {effect_control("strength", "Strength", 0, 100, 70)},
        [](Document& d, const std::vector<int>& v) { apply_red_eye_removal(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::RedEyeRemoval; r.params[1] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(photo, "sharpen", tr("Sharpen"), {effect_control("amount", "Amount", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_sharpen(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Sharpen; r.params[1] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(photo, "soften-portrait", tr("Soften Portrait"), {effect_control("softness", "Softness", 0, 100, 50), effect_control("lighting", "Lighting", -100, 100, 0), effect_control("warmth", "Warmth", -100, 100, 0)},
        [](Document& d, const std::vector<int>& v) { apply_soften_portrait(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::SoftenPortrait; r.params = {static_cast<float>(std::max(1, v[0] / 12)), static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])}; return r; });
    add_configurable_effect(photo, "vignette", tr("Vignette"), {effect_control("radius", "Radius (%)", 10, 200, 70), effect_control("strength", "Strength", 0, 100, 60), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_vignette(d, v[0], v[1], v[2], v[3]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Vignette; r.params = {percentage(v[0]), static_cast<float>(v[1]), 0, 0}; r.params2 = {percentage(v[2]), percentage(v[3]), 0, 0}; return r; });
    QMenu* render = effects->addMenu(tr("Render"));
    add_configurable_effect(render, "clouds", tr("Clouds"), {effect_control("scale", "Scale", 2, 512, 96), effect_control("roughness", "Roughness / octaves", 1, 8, 4)},
        [this](Document& d, const std::vector<int>& v) { apply_clouds(d, v[0], v[1], controller_.tool().primary, controller_.tool().secondary); }, [this](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Clouds; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; r.primary = controller_.tool().primary; r.secondary = controller_.tool().secondary; return r; });
    add_configurable_effect(render, "julia-fractal", tr("Julia Fractal"), {effect_control("zoom", "Zoom × 100", 10, 1000, 100), effect_control("angle", "Angle", -180, 180, 0)},
        [](Document& d, const std::vector<int>& v) { apply_julia_fractal(d, percentage(v[0]), static_cast<float>(v[1])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::JuliaFractal; r.params = {percentage(v[0]), 0, static_cast<float>(v[1]) * 3.14159265f / 180.0f, 0}; return r; });
    add_configurable_effect(render, "mandelbrot-fractal", tr("Mandelbrot Fractal"), {effect_control("zoom", "Zoom × 100", 10, 1000, 100), effect_control("angle", "Angle", -180, 180, 0), effect_control("invert", "Invert (0 / 1)", 0, 1, 0)},
        [](Document& d, const std::vector<int>& v) { apply_mandelbrot_fractal(d, percentage(v[0]), static_cast<float>(v[1]), v[2] != 0); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MandelbrotFractal; r.params = {percentage(v[0]), static_cast<float>(v[2]), static_cast<float>(v[1]) * 3.14159265f / 180.0f, 0}; return r; });
    add_configurable_effect(render, "turbulence", tr("Turbulence"), {effect_control("scale", "Scale", 2, 512, 96), effect_control("octaves", "Octaves", 1, 8, 4)},
        [](Document& d, const std::vector<int>& v) { apply_turbulence(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Turbulence; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* stylize = effects->addMenu(tr("Stylize"));
    add_configurable_effect(stylize, "edge-detect", tr("Edge Detect"), {effect_control("strength", "Strength", 1, 200, 100)},
        [](Document& d, const std::vector<int>& v) { apply_edge_detect(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::EdgeDetect; r.params[1] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(stylize, "emboss", tr("Emboss"), {effect_control("angle", "Angle", -180, 180, 45)},
        [](Document& d, const std::vector<int>& v) { apply_emboss(d, static_cast<float>(v[0])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Emboss; r.params[0] = static_cast<float>(v[0]) * 3.14159265f / 180.0f; return r; });
    add_configurable_effect(stylize, "outline-stylize", tr("Outline"), {effect_control("thickness", "Thickness", 1, 16, 3), effect_control("intensity", "Intensity", 0, 255, 255)},
        [](Document& d, const std::vector<int>& v) { apply_outline(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Outline; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(stylize, "relief", tr("Relief"), {effect_control("angle", "Angle", -180, 180, 45)},
        [](Document& d, const std::vector<int>& v) { apply_relief(d, static_cast<float>(v[0])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Relief; r.params[0] = static_cast<float>(v[0]) * 3.14159265f / 180.0f; return r; });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QAction* grid = add_action(view, tr("Grid"), {}, [this](bool checked) { canvas_->set_grid_visible(checked); }); grid->setCheckable(true); grid->setChecked(true);
    QAction* checker = add_action(view, tr("Checkerboard"), {}, [this](bool checked) { canvas_->set_checker_visible(checked); }); checker->setCheckable(true); checker->setChecked(true);
    view->addSeparator();
    QAction* zoom_in = add_action(view, tr("Zoom In"), {}, [this] { zoom_active_workspace(true); });
    zoom_in->setShortcuts(QKeySequence::keyBindings(QKeySequence::ZoomIn));
    QAction* zoom_out = add_action(view, tr("Zoom Out"), {}, [this] { zoom_active_workspace(false); });
    zoom_out->setShortcuts(QKeySequence::keyBindings(QKeySequence::ZoomOut));
    add_action(view, tr("Actual Size"), QKeySequence(QStringLiteral("Ctrl+1")),
               [this] { actual_size_active_workspace(); });
    add_action(view, tr("Fit to View"), QKeySequence(QStringLiteral("Ctrl+0")),
               [this] { fit_active_workspace(); });

    QMenu* options = menuBar()->addMenu(tr("&Options"));
    auto option = [this, options](const QString& label, bool& setting) {
        QAction* action = options->addAction(label); action->setCheckable(true); action->setChecked(setting);
        connect(action, &QAction::toggled, this, [this, &setting](bool checked) {
            setting = checked;
            persist_app_settings();
        });
        return action;
    };
    option(tr("Show Splash Screen"), settings_.show_splash_screen)
        ->setObjectName(QStringLiteral("OptionsShowSplash"));
    option(tr("Auto-open Error Console"), settings_.auto_open_error_console)
        ->setObjectName(QStringLiteral("OptionsAutoOpenErrorConsole"));
    option(tr("Heavy GPU Optimization"), settings_.heavy_gpu_optimization)
        ->setObjectName(QStringLiteral("OptionsHeavyGpu"));
#if defined(__APPLE__)
    option(tr("MPS Backend"), settings_.mps_backend)
        ->setObjectName(QStringLiteral("OptionsMpsBackend"));
#endif
    options->addSeparator();
    QAction* ffmpeg_path = add_action(options, tr("FFmpeg Executable..."), {},
                                      [this] { select_ffmpeg_executable(); });
    ffmpeg_path->setObjectName(QStringLiteral("OptionsFfmpegExecutable"));
    QAction* ffmpeg_auto = add_action(options, tr("Use Auto-detected FFmpeg"), {}, [this] {
        settings_.ffmpeg_path.clear();
        ffmpeg_executable_ = detect_ffmpeg_executable();
        setProperty("ffmpegAvailable", !ffmpeg_executable_.isEmpty());
        setProperty("ffmpegExecutable", ffmpeg_executable_);
        persist_app_settings();
        set_status(ffmpeg_executable_.isEmpty()
                       ? tr("FFmpeg was not detected")
                       : tr("Auto-detected FFmpeg: %1").arg(ffmpeg_executable_));
    });
    ffmpeg_auto->setObjectName(QStringLiteral("OptionsFfmpegAutoDetect"));

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    add_action(help, tr("Check for Updates..."), {}, [this] { check_for_updates(); });
    help->addSeparator();
    add_action(help, tr("About PixelArt98"), {}, [this] { show_about_dialog(); });
}

void QtMainWindow::build_docks() {
    build_tools_dock(); build_colors_dock(); build_layers_dock();
    build_animation_dock(); build_history_dock(); build_model_dock(); build_preview_docks();
    build_text_dock(); build_console_dock();
    constexpr std::array<const char*, 6> canvas_only_dock_names = {
        "ToolsDock", "ColorsDock", "ModelPreviewDock", "TilePreviewDock", "ModelDock", "TextDock"};
    canvas_only_docks_.clear();
    canvas_only_docks_.reserve(canvas_only_dock_names.size());
    for (const char* name : canvas_only_dock_names) {
        if (QDockWidget* dock = findChild<QDockWidget*>(QString::fromLatin1(name)); dock != nullptr) {
            canvas_only_docks_.push_back(dock);
        }
    }
    auto* window = new QMenu(tr("&Window"), this);
    menuBar()->insertMenu(menuBar()->actions().back(), window);
    for (QDockWidget* dock : docks_) window->addAction(dock->toggleViewAction());
}

void QtMainWindow::build_tools_dock() {
    auto* dock = new QDockWidget(tr("Tools"), this);
    dock->setObjectName(QStringLiteral("ToolsDock"));

    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    auto* tools_panel = new QWidget;
    auto* tools_grid = new QGridLayout(tools_panel);
    tools_grid->setContentsMargins(0, 0, 0, 0);
    tools_grid->setHorizontalSpacing(4);
    tools_grid->setVerticalSpacing(4);
    tools_grid->setSizeConstraint(QLayout::SetFixedSize);
    constexpr int tool_button_size = 40;
    constexpr int tool_grid_spacing = 4;
    constexpr int tool_grid_columns = 2;
    constexpr int tool_grid_width = tool_grid_columns * tool_button_size + (tool_grid_columns - 1) * tool_grid_spacing;
    constexpr int tool_options_minimum_width = 220;
    layout->addStrut(tool_options_minimum_width);
    tools_panel->setFixedWidth(tool_grid_width);
    tool_buttons_ = new QButtonGroup(this);
    tool_buttons_->setExclusive(true);

    constexpr std::array<ToolType, 16> tools = {
        ToolType::Pencil, ToolType::Brush, ToolType::Eraser, ToolType::Line, ToolType::Rectangle,
        ToolType::Ellipse, ToolType::Bucket, ToolType::Gradient, ToolType::Eyedropper, ToolType::CloneStamp,
        ToolType::RectSelect, ToolType::EllipseSelect, ToolType::LassoSelect, ToolType::MagicWand, ToolType::MovePixels, ToolType::Text
    };
    for (int index = 0; index < static_cast<int>(tools.size()); ++index) {
        const ToolType tool = tools[static_cast<std::size_t>(index)];
        auto* button = new QToolButton;
        button->setCheckable(true);
        button->setIcon(tool_icon(tool));
        button->setIconSize(QSize(28, 28));
        button->setFixedSize(QSize(tool_button_size, tool_button_size));
        button->setToolTip(QString::fromUtf8(tool_name(tool)));
        button->setStatusTip(QString::fromUtf8(tool_name(tool)));
        tool_buttons_->addButton(button, static_cast<int>(tool));
        tool_button_widgets_.push_back(button);
        tools_grid->addWidget(button, index / 2, index % 2);
    }

    connect(tool_buttons_, &QButtonGroup::idClicked, this, [this](int tool_id) {
        if (static_cast<ToolType>(tool_id) != ToolType::Text) {
            cancel_raster_text_edit(true);
        }
        controller_.tool().tool = static_cast<ToolType>(tool_id);
        rebuild_tool_options();
        set_status(QString::fromUtf8(tool_name(controller_.tool().tool)));
    });

    auto* tool_options_panel = new QWidget;
    tool_options_panel->setObjectName(QStringLiteral("ToolOptionsPanel"));
    tool_options_layout_ = new QVBoxLayout(tool_options_panel);
    tool_options_layout_->setContentsMargins(0, 0, 0, 0);
    tool_options_layout_->setSpacing(6);

    layout->addWidget(tools_panel, 0, Qt::AlignHCenter);
    layout->addWidget(tool_options_panel);
    layout->addStretch(1);

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    docks_.push_back(dock);

    if (QAbstractButton* pencil_button = tool_buttons_->button(static_cast<int>(ToolType::Pencil)); pencil_button != nullptr) {
        pencil_button->setChecked(true);
    }
    rebuild_tool_options();
}

void QtMainWindow::build_colors_dock() {
    auto* dock = new QDockWidget(tr("Colors"), this);
    dock->setObjectName(QStringLiteral("ColorsDock"));
    color_panel_ = new QtColorPanel(controller_);
    color_panel_->changed = [this] {
        update_raster_text_preview();
        canvas_->update();
        set_status(tr("Primary %1  Secondary %2")
                       .arg(rgba_hex(qcolor(controller_.tool().primary)), rgba_hex(qcolor(controller_.tool().secondary))));
    };
    color_panel_->document_changed = [this] { mark_graph_effect_source_changed(); };
    dock->setWidget(color_panel_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    docks_.push_back(dock);
}

void QtMainWindow::build_layers_dock() {
    auto* dock = new QDockWidget(tr("Layers"), this); dock->setObjectName(QStringLiteral("LayersDock")); layers_list_ = new QListWidget;
    layers_list_->setObjectName(QStringLiteral("LayersList"));
    auto* add = icon_button(icon_resource(QStringLiteral("new_layer.png")), tr("New Layer"));
    auto* duplicate = icon_button(icon_resource(QStringLiteral("duplicate_layer.png")), tr("Duplicate Layer"));
    auto* remove = icon_button(icon_resource(QStringLiteral("remove_layer.png")), tr("Remove Layer"));
    auto* up = icon_button(icon_resource(QStringLiteral("up.png")), tr("Move Layer Up"));
    auto* down = icon_button(rotated_icon_resource(QStringLiteral("up.png"), 180.0), tr("Move Layer Down"));
    auto* layer_actions = new QWidget;
    auto* layer_actions_layout = new QHBoxLayout(layer_actions);
    layer_actions_layout->setContentsMargins(0, 0, 0, 0);
    layer_actions_layout->setSpacing(4);
    layer_actions_layout->addWidget(add);
    layer_actions_layout->addWidget(duplicate);
    layer_actions_layout->addWidget(remove);
    layer_actions_layout->addWidget(up);
    layer_actions_layout->addWidget(down);
    layer_actions_layout->addStretch(1);
    blend_mode_ = new QComboBox; for (int i = 0; i <= static_cast<int>(LayerBlendMode::Xor); ++i) blend_mode_->addItem(QString::fromUtf8(layer_blend_mode_name(static_cast<LayerBlendMode>(i))), i);
    auto* opacity = new QSlider(Qt::Horizontal); opacity->setRange(0, 100); opacity->setValue(100);
    auto* visible = new QCheckBox(tr("Visible")); visible->setObjectName(QStringLiteral("LayerVisible")); visible->setChecked(true); auto* clip = new QCheckBox(tr("Clip to below")); auto* mask = new QCheckBox(tr("Layer mask"));
    auto* reveal_mask = icon_button(icon_resource(QStringLiteral("reveal_mask.png")), tr("Reveal Mask"));
    auto* hide_mask = icon_button(icon_resource(QStringLiteral("hide_mask.png")), tr("Hide Mask"));
    auto* selection_mask = icon_button(icon_resource(QStringLiteral("mask_from_selection.png")), tr("Mask from Selection"));
    auto* clear_mask = icon_button(icon_resource(QStringLiteral("clear_from_selection.png")), tr("Clear Mask"));
    auto* mask_actions = new QWidget;
    auto* mask_actions_layout = new QHBoxLayout(mask_actions);
    mask_actions_layout->setContentsMargins(0, 0, 0, 0);
    mask_actions_layout->setSpacing(4);
    mask_actions_layout->addWidget(reveal_mask);
    mask_actions_layout->addWidget(hide_mask);
    mask_actions_layout->addWidget(selection_mask);
    mask_actions_layout->addWidget(clear_mask);
    mask_actions_layout->addStretch(1);
    connect(layers_list_, &QListWidget::currentRowChanged, this, [this, opacity, visible, clip, mask](int row) { if (row >= 0 && row < static_cast<int>(controller_.document().layers.size())) { controller_.document().active_layer = row; const Layer& layer = controller_.document().layers[static_cast<std::size_t>(row)]; const QSignalBlocker block_blend_mode(blend_mode_); const QSignalBlocker block_opacity(opacity); const QSignalBlocker block_visible(visible); const QSignalBlocker block_clip(clip); const QSignalBlocker block_mask(mask); blend_mode_->setCurrentIndex(static_cast<int>(layer.blend_mode)); opacity->setValue(static_cast<int>(layer.opacity * 100.0f)); visible->setChecked(layer.visible); clip->setChecked(layer.clip_to_below); mask->setChecked(layer.mask_enabled); controller_.invalidate_display(); canvas_->update(); mark_graph_effect_source_changed(); } });
    connect(layers_list_, &QListWidget::itemChanged, this, [this, visible](QListWidgetItem* item) {
        const int row = layers_list_->row(item);
        if (row < 0 || row >= static_cast<int>(controller_.document().layers.size())) return;
        const bool checked = item->checkState() == Qt::Checked;
        if (!controller_.document().set_layer_visible(row, checked)) return;
        if (row == controller_.document().active_layer) {
            const QSignalBlocker blocker(visible);
            visible->setChecked(checked);
        }
        controller_.mark_changed("Layer Visibility");
        controller_.invalidate_display();
        canvas_->update();
        if (model_preview_ != nullptr) model_preview_->update();
        setWindowModified(controller_.modified());
    });
    connect(layers_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { bool ok = false; const QString name = QInputDialog::getText(this, tr("Rename Layer"), tr("Name"), QLineEdit::Normal, item->text(), &ok); if (ok && controller_.document().set_layer_name(layers_list_->row(item), name.toStdString())) { controller_.mark_changed("Rename Layer"); refresh_all(); } });
    connect(add, &QToolButton::clicked, this, [this] { controller_.document().add_layer("Layer"); controller_.mark_changed("Add Layer"); refresh_all(); });
    connect(duplicate, &QToolButton::clicked, this, [this] { controller_.document().duplicate_layer(controller_.document().active_layer); controller_.mark_changed("Duplicate Layer"); refresh_all(); });
    connect(remove, &QToolButton::clicked, this, [this] { if (controller_.document().remove_layer(controller_.document().active_layer)) controller_.mark_changed("Remove Layer"); refresh_all(); });
    connect(up, &QToolButton::clicked, this, [this] { const int from = controller_.document().active_layer; controller_.document().move_layer(from, std::max(0, from - 1)); controller_.mark_changed("Move Layer"); refresh_all(); });
    connect(down, &QToolButton::clicked, this, [this] { const int from = controller_.document().active_layer; controller_.document().move_layer(from, std::min(static_cast<int>(controller_.document().layers.size()) - 1, from + 1)); controller_.mark_changed("Move Layer"); refresh_all(); });
    connect(blend_mode_, &QComboBox::currentIndexChanged, this, [this](int index) { if (controller_.document().set_layer_blend_mode(controller_.document().active_layer, static_cast<LayerBlendMode>(index))) { controller_.mark_changed("Layer Blend Mode"); refresh_all(); } });
    connect(opacity, &QSlider::valueChanged, this, [this](int value) { if (controller_.document().set_layer_opacity(controller_.document().active_layer, static_cast<float>(value) / 100.0f)) { controller_.mark_changed("Layer Opacity"); refresh_all(); } });
    connect(visible, &QCheckBox::toggled, this, [this](bool checked) { if (controller_.document().set_layer_visible(controller_.document().active_layer, checked)) { controller_.mark_changed("Layer Visibility"); refresh_all(); } });
    connect(clip, &QCheckBox::toggled, this, [this](bool checked) { if (controller_.document().set_layer_clipped(controller_.document().active_layer, checked)) { controller_.mark_changed("Layer Clipping"); refresh_all(); } });
    connect(mask, &QCheckBox::toggled, this, [this](bool checked) { const int index = controller_.document().active_layer; if (index < 0 || index >= static_cast<int>(controller_.document().layers.size())) return; std::vector<std::uint8_t> values = controller_.document().layers[static_cast<std::size_t>(index)].mask; if (checked && values.size() != static_cast<std::size_t>(controller_.document().width * controller_.document().height)) values.assign(static_cast<std::size_t>(controller_.document().width * controller_.document().height), 255); if (controller_.document().set_layer_mask(index, std::move(values), checked, "Toggle Layer Mask")) { controller_.mark_changed("Toggle Layer Mask"); refresh_all(); } });
    connect(reveal_mask, &QToolButton::clicked, this, [this] { std::vector<std::uint8_t> values(static_cast<std::size_t>(controller_.document().width * controller_.document().height), 255); if (controller_.document().set_layer_mask(controller_.document().active_layer, std::move(values), true, "Reveal Layer Mask")) { controller_.mark_changed("Reveal Layer Mask"); refresh_all(); } });
    connect(hide_mask, &QToolButton::clicked, this, [this] { std::vector<std::uint8_t> values(static_cast<std::size_t>(controller_.document().width * controller_.document().height), 0); if (controller_.document().set_layer_mask(controller_.document().active_layer, std::move(values), true, "Hide Layer Mask")) { controller_.mark_changed("Hide Layer Mask"); refresh_all(); } });
    connect(selection_mask, &QToolButton::clicked, this, [this] { std::vector<std::uint8_t> values(controller_.document().selection.mask.size(), 0); for (std::size_t i = 0; i < values.size(); ++i) values[i] = controller_.document().selection.active && controller_.document().selection.mask[i] != 0 ? 255 : 0; if (controller_.document().set_layer_mask(controller_.document().active_layer, std::move(values), true, "Mask from Selection")) { controller_.mark_changed("Mask from Selection"); refresh_all(); } });
    connect(clear_mask, &QToolButton::clicked, this, [this] { if (controller_.document().set_layer_mask(controller_.document().active_layer, {}, false, "Clear Layer Mask")) { controller_.mark_changed("Clear Layer Mask"); refresh_all(); } });
    dock->setWidget(vertical_panel({layers_list_, layer_actions, visible, clip, blend_mode_, opacity, mask, mask_actions})); addDockWidget(Qt::RightDockWidgetArea, dock); docks_.push_back(dock);
}
