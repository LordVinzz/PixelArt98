void QtMainWindow::build_adjustments_menu() {
    auto control = [](const char* id, const char* label, int minimum, int maximum, int initial) {
        return AdjustmentControl{QString::fromLatin1(id), QString::fromUtf8(label), minimum, maximum, initial};
    };
    std::vector<AdjustmentSpec> adjustments;
    adjustments.push_back({QStringLiteral("brightness-contrast"), tr("Brightness / Contrast"),
        {control("brightness", "Brightness", -255, 255, 0), control("contrast", "Contrast", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) { apply_brightness_contrast(document, value[0], value[1]); },
        [](const std::vector<int>& value) {
            const float contrast = static_cast<float>(value[1]);
            const float factor = (259.0f * (contrast + 255.0f)) / (255.0f * (259.0f - contrast));
            GpuEffectRequest request; request.mode = GpuEffectMode::BrightnessContrast;
            request.params = {static_cast<float>(value[0]) / 255.0f, (factor - 1.0f) * 0.5f, 0.0f, 0.0f}; return request;
        }});
    adjustments.push_back({QStringLiteral("hsv"), tr("HSV"),
        {control("hue", "Hue", -180, 180, 0), control("saturation", "Saturation", -100, 100, 0),
         control("value", "Value", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) {
            apply_hsv(document, static_cast<float>(value[0]), static_cast<float>(value[1]) / 100.0f,
                      static_cast<float>(value[2]) / 100.0f);
        },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::Hsv;
            request.params = {static_cast<float>(value[0]) / 360.0f, static_cast<float>(value[1]) / 100.0f,
                              static_cast<float>(value[2]) / 100.0f, 0.0f}; return request;
        }});
    adjustments.push_back({QStringLiteral("temperature"), tr("Temperature"),
        {control("temperature", "Temperature", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) { apply_temperature(document, value[0]); },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::Temperature;
            request.params[0] = static_cast<float>(value[0]) / 100.0f; return request;
        }});
    adjustments.push_back({QStringLiteral("levels"), tr("Levels"),
        {control("input-black", "Input black", 0, 254, 0), control("input-white", "Input white", 1, 255, 255),
         control("gamma", "Gamma × 100", 5, 300, 100), control("output-black", "Output black", 0, 255, 0),
         control("output-white", "Output white", 0, 255, 255)},
        [](Document& document, const std::vector<int>& value) {
            LevelsSettings settings; settings.in_black = value[0]; settings.in_white = value[1];
            settings.gamma = static_cast<float>(value[2]) / 100.0f; settings.out_black = value[3]; settings.out_white = value[4];
            apply_levels(document, settings);
        },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::Levels;
            request.params = {static_cast<float>(value[0]) / 255.0f, static_cast<float>(value[1]) / 255.0f,
                              static_cast<float>(value[2]) / 100.0f, static_cast<float>(value[3]) / 255.0f};
            request.params2[0] = static_cast<float>(value[4]) / 255.0f; return request;
        }});
    adjustments.push_back({QStringLiteral("tonal-range"), tr("Tonal Range"),
        {control("white-point", "White point", -100, 100, 0), control("highlights", "Highlights", -100, 100, 0),
         control("shadows", "Shadows", -100, 100, 0), control("black-point", "Black point", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) { apply_tonal_range(document, value[0], value[1], value[2], value[3]); },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::TonalRange;
            request.params = {static_cast<float>(value[0]) / 100.0f, static_cast<float>(value[1]) / 100.0f,
                              static_cast<float>(value[2]) / 100.0f, static_cast<float>(value[3]) / 100.0f}; return request;
        }});
    adjustments.push_back({QStringLiteral("curves"), tr("Curves"), {}, {}, {}});
    adjustments.push_back({QStringLiteral("auto-level"), tr("Auto-Level"), {},
        [](Document& document, const std::vector<int>&) { apply_auto_level(document); },
        [](const std::vector<int>&) { GpuEffectRequest request; request.mode = GpuEffectMode::AutoLevel; return request; }});
    adjustments.push_back({QStringLiteral("posterize"), tr("Posterize"), {control("levels", "Levels", 2, 32, 8)},
        [](Document& document, const std::vector<int>& value) { apply_posterize(document, value[0]); },
        [](const std::vector<int>& value) { GpuEffectRequest request; request.mode = GpuEffectMode::Posterize; request.params[0] = static_cast<float>(value[0]); return request; }});
    adjustments.push_back({QStringLiteral("quantize"), tr("Quantize"), {},
        [this](Document& document, const std::vector<int>&) { apply_palette_quantize(document, controller_.document().palette, false); },
        [this](const std::vector<int>&) { GpuEffectRequest request; request.mode = GpuEffectMode::PaletteQuantize; request.params[0] = static_cast<float>(std::max<std::size_t>(2, controller_.document().palette.colors.size())); return request; }});
    adjustments.push_back({QStringLiteral("dither"), tr("Dither"), {},
        [this](Document& document, const std::vector<int>&) { apply_palette_quantize(document, controller_.document().palette, true); },
        [this](const std::vector<int>&) { GpuEffectRequest request; request.mode = GpuEffectMode::PaletteDither; request.params[0] = static_cast<float>(std::max<std::size_t>(2, controller_.document().palette.colors.size())); return request; }});

    QMenu* menu = menuBar()->addMenu(tr("&Adjustments"));
    menu->setObjectName(QStringLiteral("AdjustmentsMenu"));
    for (const AdjustmentSpec& adjustment : adjustments) {
        QAction* action = menu->addAction(adjustment.name);
        action->setObjectName(QStringLiteral("adjustment.") + adjustment.id);
        connect(action, &QAction::triggered, this, [this, adjustment] { show_adjustment_dialog(adjustment); });
    }
    menu->addSeparator();
    QAction* depth = menu->addAction(tr("Generate Depth Map..."));
    depth->setObjectName(QStringLiteral("adjustment.generate-depth-map"));
    connect(depth, &QAction::triggered, this, [this] { generate_depth_map(); });
}

void QtMainWindow::build_animation_dock() {
    auto* dock = new QDockWidget(tr("Animation"), this);
    dock->setObjectName(QStringLiteral("AnimationDock"));
    auto* timeline = new FrameTimelineWidget;
    frames_list_ = timeline;
    frames_list_->setObjectName(QStringLiteral("AnimationFrames"));

    auto* play = icon_button(icon_resource(QStringLiteral("play_pause_animation.png")), tr("Play / Pause"));
    auto* stop = icon_button(icon_resource(QStringLiteral("stop_animation.png")), tr("Stop"));
    auto* add = icon_button(icon_resource(QStringLiteral("new_frame_animation.png")), tr("New Frame"));
    auto* duplicate = icon_button(icon_resource(QStringLiteral("duplicate_layer.png")), tr("Duplicate Frame"));
    auto* remove = icon_button(icon_resource(QStringLiteral("remove_layer.png")), tr("Delete Frame"));
    play->setObjectName(QStringLiteral("animation.playPause"));
    stop->setObjectName(QStringLiteral("animation.stop"));
    add->setObjectName(QStringLiteral("animation.newFrame"));
    duplicate->setObjectName(QStringLiteral("animation.duplicateFrame"));
    remove->setObjectName(QStringLiteral("animation.deleteFrame"));
    play->setCheckable(true);

    auto* onion = new QCheckBox(tr("Onion Skin"));
    onion->setObjectName(QStringLiteral("animation.onionSkin"));
    onion->setChecked(true);

    auto* actions = new QWidget;
    actions->setObjectName(QStringLiteral("AnimationActions"));
    auto* actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(4);
    actions_layout->addWidget(play);
    actions_layout->addWidget(stop);
    actions_layout->addWidget(add);
    actions_layout->addWidget(duplicate);
    actions_layout->addWidget(remove);
    actions_layout->addStretch(1);
    actions_layout->addWidget(onion);

    timeline->duration_committed = [this](int frame_index, int duration_ms) {
        if (!controller_.document().set_frame_duration(frame_index, duration_ms)) return;
        controller_.mark_changed("Change Frame Duration");
        if (playing_ && frame_index == controller_.document().active_frame) {
            playback_timer_->setInterval(duration_ms);
        }
        refresh_frames();
        setWindowModified(controller_.modified());
        setWindowTitle(QStringLiteral("PixelArt98[*]"));
        set_status(QString::fromStdString(controller_.status()));
    };
    connect(frames_list_, &QListWidget::currentRowChanged, this, [this](int row) { if (row >= 0) { controller_.document().active_frame = row; controller_.invalidate_display(); canvas_->update(); mark_graph_effect_source_changed(); } });
    connect(play, &QToolButton::toggled, this, [this](bool checked) { playing_ = checked; if (playing_) playback_timer_->start(std::max(20, controller_.document().frames[static_cast<std::size_t>(controller_.document().active_frame)].duration_ms)); else playback_timer_->stop(); });
    connect(stop, &QToolButton::clicked, this, [this, play] { playing_ = false; playback_timer_->stop(); play->setChecked(false); });
    connect(add, &QToolButton::clicked, this, [this] { controller_.document().add_frame(false); controller_.mark_changed("Add Frame"); refresh_all(); });
    connect(duplicate, &QToolButton::clicked, this, [this] { controller_.document().duplicate_frame(controller_.document().active_frame); controller_.mark_changed("Duplicate Frame"); refresh_all(); });
    connect(remove, &QToolButton::clicked, this, [this] { if (controller_.document().remove_frame(controller_.document().active_frame)) controller_.mark_changed("Delete Frame"); refresh_all(); });
    connect(onion, &QCheckBox::toggled, canvas_, &QtCanvasWidget::set_onion_visible);
    dock->setWidget(vertical_panel({frames_list_, actions}));
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    docks_.push_back(dock);
}

void QtMainWindow::build_history_dock() {
    auto* dock = new QDockWidget(tr("History"), this);
    dock->setObjectName(QStringLiteral("HistoryDock"));
    history_list_ = new QListWidget;
    history_list_->setObjectName(QStringLiteral("HistoryList"));
    history_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    history_list_->setSortingEnabled(false);
    auto* undo = new QPushButton(tr("Undo"));
    undo->setObjectName(QStringLiteral("HistoryUndo"));
    auto* redo = new QPushButton(tr("Redo"));
    redo->setObjectName(QStringLiteral("HistoryRedo"));
    auto* controls = new QWidget;
    auto* controls_layout = new QHBoxLayout(controls);
    controls_layout->setContentsMargins(0, 0, 0, 0);
    controls_layout->addWidget(undo);
    controls_layout->addWidget(redo);
    connect(undo, &QPushButton::clicked, this, [this] { if (controller_.undo()) refresh_all(); });
    connect(redo, &QPushButton::clicked, this, [this] { if (controller_.redo()) refresh_all(); });
    connect(history_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        const int available = static_cast<int>(controller_.document().undo_history_for_recovery().size() +
                                               controller_.document().redo_history_for_recovery().size());
        const int target = std::clamp(row, 0, available);
        bool changed = false;
        while (static_cast<int>(controller_.document().undo_history_for_recovery().size()) > target) {
            if (!controller_.undo()) break;
            changed = true;
        }
        while (static_cast<int>(controller_.document().undo_history_for_recovery().size()) < target) {
            if (!controller_.redo()) break;
            changed = true;
        }
        if (changed) refresh_all();
    });
    auto* explanation = new QLabel(tr("Select any entry to restore that document state. "
                                      "Future redo states are shown in gray."));
    explanation->setWordWrap(true);
    dock->setWidget(vertical_panel({history_list_, controls, explanation}));
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    docks_.push_back(dock);
    refresh_history();
}

void QtMainWindow::build_model_dock() {
    auto* dock = new QDockWidget(tr("Cuboid / UV Editor"), this); dock->setObjectName(QStringLiteral("ModelDock")); model_list_ = new QListWidget; auto* add = new QPushButton(tr("Add Cuboid")); auto* remove = new QPushButton(tr("Remove")); auto* face = new QComboBox; for (int i = 0; i < 6; ++i) face->addItem(QString::fromUtf8(model_face_name(i)), i);
    connect(model_list_, &QListWidget::currentRowChanged, this, [this](int row) { if (row >= 0) controller_.model().selected_cuboid = row; });
    connect(add, &QPushButton::clicked, this, [this] { controller_.add_cuboid(); refresh_model(); });
    connect(remove, &QPushButton::clicked, this, [this] { if (controller_.remove_selected_cuboid()) refresh_model(); });
    connect(face, &QComboBox::currentIndexChanged, this, [this](int index) { controller_.model().selected_face = index; });
    dock->setWidget(vertical_panel({model_list_, add, remove, face})); addDockWidget(Qt::LeftDockWidgetArea, dock); docks_.push_back(dock);
}

void QtMainWindow::build_preview_docks() {
    auto* tile = new QDockWidget(tr("Tile Preview"), this); tile->setObjectName(QStringLiteral("TilePreviewDock")); tile_preview_label_ = new QLabel(tr("Tile preview follows the active canvas.")); tile_preview_label_->setAlignment(Qt::AlignCenter); tile->setWidget(tile_preview_label_); addDockWidget(Qt::BottomDockWidgetArea, tile); docks_.push_back(tile); tile->hide();
    auto* model = new QDockWidget(tr("3D Preview"), this); model->setObjectName(QStringLiteral("ModelPreviewDock")); model_preview_ = new QtModelPreviewWidget(controller_); model_preview_->model_changed = [this] { refresh_model(); }; model->setWidget(model_preview_); addDockWidget(Qt::RightDockWidgetArea, model); docks_.push_back(model); model->hide();
}

void QtMainWindow::build_text_dock() {
    text_dock_ = new QDockWidget(tr("Text"), this);
    text_dock_->setObjectName(QStringLiteral("TextDock"));
    text_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    text_dock_->setMinimumWidth(300);

    auto* panel = new QWidget(text_dock_);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    raster_text_input_ = new QPlainTextEdit(panel);
    raster_text_input_->setObjectName(QStringLiteral("RasterTextInput"));
    raster_text_input_->setPlaceholderText(tr("Enter text…"));
    raster_text_input_->setMaximumHeight(110);
    layout->addWidget(raster_text_input_);

    auto* options_panel = new QWidget(panel);
    auto* form = new QFormLayout(options_panel);
    form->setContentsMargins(0, 0, 0, 0);
    QSettings preferences;

    raster_text_font_ = new QFontComboBox(options_panel);
    raster_text_font_->setObjectName(QStringLiteral("RasterTextFont"));
    raster_text_font_->setCurrentFont(QFont(preferences.value(
        QStringLiteral("text/fontFamily"),
        raster_text_font_->currentFont().family()).toString()));

    raster_text_size_ = new QSpinBox(options_panel);
    raster_text_size_->setObjectName(QStringLiteral("RasterTextSize"));
    raster_text_size_->setRange(1, 512);
    raster_text_size_->setValue(std::clamp(
        preferences.value(QStringLiteral("text/pixelSize"), 16).toInt(), 1, 512));
    raster_text_size_->setSuffix(tr(" px"));

    raster_text_box_width_ = std::max(
        1, preferences.value(QStringLiteral("text/boxWidth"), 128).toInt());
    raster_text_box_height_ = std::max(
        1, preferences.value(QStringLiteral("text/boxHeight"), 64).toInt());

    raster_text_alignment_ = new QComboBox(options_panel);
    raster_text_alignment_->setObjectName(QStringLiteral("RasterTextAlignment"));
    raster_text_alignment_->addItem(tr("Left"), static_cast<int>(RasterTextAlignment::Left));
    raster_text_alignment_->addItem(tr("Center"), static_cast<int>(RasterTextAlignment::Center));
    raster_text_alignment_->addItem(tr("Right"), static_cast<int>(RasterTextAlignment::Right));
    raster_text_alignment_->setCurrentIndex(std::clamp(
        preferences.value(QStringLiteral("text/alignment"), 0).toInt(), 0, 2));

    raster_text_bold_ = new QCheckBox(tr("Bold"), options_panel);
    raster_text_bold_->setObjectName(QStringLiteral("RasterTextBold"));
    raster_text_bold_->setChecked(
        preferences.value(QStringLiteral("text/bold"), false).toBool());
    raster_text_italic_ = new QCheckBox(tr("Italic"), options_panel);
    raster_text_italic_->setObjectName(QStringLiteral("RasterTextItalic"));
    raster_text_italic_->setChecked(
        preferences.value(QStringLiteral("text/italic"), false).toBool());
    raster_text_antialias_ = new QCheckBox(tr("Antialias"), options_panel);
    raster_text_antialias_->setObjectName(QStringLiteral("RasterTextAntialias"));
    raster_text_antialias_->setChecked(
        preferences.value(QStringLiteral("text/antialias"), false).toBool());

    auto* styles = new QWidget(options_panel);
    auto* styles_layout = new QGridLayout(styles);
    styles_layout->setContentsMargins(0, 0, 0, 0);
    styles_layout->setHorizontalSpacing(12);
    styles_layout->setVerticalSpacing(4);
    styles_layout->addWidget(raster_text_bold_, 0, 0);
    styles_layout->addWidget(raster_text_italic_, 0, 1);
    styles_layout->addWidget(raster_text_antialias_, 1, 0, 1, 2);
    styles_layout->setColumnStretch(1, 1);

    form->addRow(tr("Font"), raster_text_font_);
    form->addRow(tr("Pixel size"), raster_text_size_);
    form->addRow(tr("Alignment"), raster_text_alignment_);
    form->addRow(tr("Style"), styles);
    layout->addWidget(options_panel);

    auto* buttons = new QWidget(panel);
    auto* buttons_layout = new QHBoxLayout(buttons);
    buttons_layout->setContentsMargins(0, 0, 0, 0);
    buttons_layout->addStretch(1);
    raster_text_cancel_ = new QPushButton(tr("Cancel"), buttons);
    raster_text_cancel_->setObjectName(QStringLiteral("RasterTextCancel"));
    raster_text_apply_ = new QPushButton(tr("Apply"), buttons);
    raster_text_apply_->setObjectName(QStringLiteral("RasterTextApply"));
    raster_text_apply_->setDefault(true);
    raster_text_apply_->setEnabled(false);
    raster_text_cancel_->setEnabled(false);
    buttons_layout->addWidget(raster_text_cancel_);
    buttons_layout->addWidget(raster_text_apply_);
    layout->addWidget(buttons);
    layout->addStretch(1);

    connect(raster_text_input_, &QPlainTextEdit::textChanged, this,
            &QtMainWindow::update_raster_text_preview);
    const auto parameter_changed = [this] {
        const RasterTextOptions options = current_raster_text_options();
        persist_raster_text_options(options);
        update_raster_text_preview();
    };
    connect(raster_text_font_, &QFontComboBox::currentFontChanged, this,
            [parameter_changed](const QFont&) { parameter_changed(); });
    connect(raster_text_size_, &QSpinBox::valueChanged, this,
            [parameter_changed](int) { parameter_changed(); });
    connect(raster_text_alignment_, &QComboBox::currentIndexChanged, this,
            [parameter_changed](int) { parameter_changed(); });
    connect(raster_text_bold_, &QCheckBox::toggled, this,
            [parameter_changed](bool) { parameter_changed(); });
    connect(raster_text_italic_, &QCheckBox::toggled, this,
            [parameter_changed](bool) { parameter_changed(); });
    connect(raster_text_antialias_, &QCheckBox::toggled, this,
            [parameter_changed](bool) { parameter_changed(); });
    connect(raster_text_apply_, &QPushButton::clicked, this,
            &QtMainWindow::apply_raster_text_edit);
    connect(raster_text_cancel_, &QPushButton::clicked, this,
            [this] { cancel_raster_text_edit(false); });
    connect(text_dock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible && raster_text_edit_active_) cancel_raster_text_edit(false);
    });

    text_dock_->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, text_dock_);
    docks_.push_back(text_dock_);
    text_dock_->hide();
}

void QtMainWindow::build_console_dock() { auto* dock = new QDockWidget(tr("Error Console"), this); dock->setObjectName(QStringLiteral("ErrorConsoleDock")); console_list_ = new QListWidget; dock->setWidget(console_list_); addDockWidget(Qt::BottomDockWidgetArea, dock); docks_.push_back(dock); dock->hide(); }

void QtMainWindow::rebuild_tool_options() {
    if (tool_options_layout_ == nullptr) return;
    clear_layout(tool_options_layout_);

    tool_size_spin_ = nullptr;
    tolerance_spin_ = nullptr;
    clone_source_label_ = nullptr;

    const ToolType tool = controller_.tool().tool;

    auto add_size_control = [this](const QString& label) {
        auto* control = new QWidget;
        auto* row = new QHBoxLayout(control);
        row->setContentsMargins(0, 0, 0, 0);
        auto* text = new QLabel(label);
        tool_size_spin_ = new QSpinBox;
        tool_size_spin_->setRange(1, 256);
        tool_size_spin_->setValue(controller_.tool().brush_size);
        connect(tool_size_spin_, &QSpinBox::valueChanged, this, [this](int value) { controller_.tool().brush_size = value; });
        row->addWidget(text);
        row->addStretch(1);
        row->addWidget(tool_size_spin_);
        tool_options_layout_->addWidget(control);
    };

    auto add_brush_engine_controls = [this]() {
        const auto add_percentage = [this](const QString& label, const QString& object_name,
                                           int value, const std::function<void(int)>& changed,
                                           int minimum = 0, int maximum = 100) {
            auto* control = new QWidget;
            auto* row = new QHBoxLayout(control);
            row->setContentsMargins(0, 0, 0, 0);
            auto* text = new QLabel(label);
            auto* spin = new QSpinBox;
            spin->setObjectName(object_name);
            spin->setRange(minimum, maximum);
            spin->setValue(value);
            spin->setSuffix(tr(" %"));
            connect(spin, &QSpinBox::valueChanged, this, changed);
            row->addWidget(text);
            row->addStretch(1);
            row->addWidget(spin);
            tool_options_layout_->addWidget(control);
        };
        add_percentage(tr("Opacity"), QStringLiteral("BrushOpacity"),
                       static_cast<int>(std::lround(controller_.tool().brush_opacity * 100.0f)),
                       [this](int value) { controller_.tool().brush_opacity = static_cast<float>(value) / 100.0f; }, 1);
        add_percentage(tr("Hardness"), QStringLiteral("BrushHardness"),
                       static_cast<int>(std::lround(controller_.tool().brush_hardness * 100.0f)),
                       [this](int value) { controller_.tool().brush_hardness = static_cast<float>(value) / 100.0f; });
        add_percentage(tr("Spacing"), QStringLiteral("BrushSpacing"),
                       static_cast<int>(std::lround(controller_.tool().brush_spacing * 100.0f)),
                       [this](int value) { controller_.tool().brush_spacing = static_cast<float>(value) / 100.0f; }, 1, 200);
        add_percentage(tr("Smoothing"), QStringLiteral("BrushSmoothing"),
                       static_cast<int>(std::lround(controller_.tool().brush_smoothing * 100.0f)),
                       [this](int value) { controller_.tool().brush_smoothing = static_cast<float>(value) / 100.0f; });
        auto* pressure_size = new QCheckBox(tr("Pressure controls size"));
        pressure_size->setObjectName(QStringLiteral("BrushPressureSize"));
        pressure_size->setChecked(controller_.tool().pressure_controls_size);
        connect(pressure_size, &QCheckBox::toggled, this,
                [this](bool checked) { controller_.tool().pressure_controls_size = checked; });
        tool_options_layout_->addWidget(pressure_size);
        auto* pressure_opacity = new QCheckBox(tr("Pressure controls opacity"));
        pressure_opacity->setObjectName(QStringLiteral("BrushPressureOpacity"));
        pressure_opacity->setChecked(controller_.tool().pressure_controls_opacity);
        connect(pressure_opacity, &QCheckBox::toggled, this,
                [this](bool checked) { controller_.tool().pressure_controls_opacity = checked; });
        tool_options_layout_->addWidget(pressure_opacity);
    };

    auto add_tolerance_control = [this]() {
        auto* control = new QWidget;
        auto* row = new QHBoxLayout(control);
        row->setContentsMargins(0, 0, 0, 0);
        auto* text = new QLabel(tr("Tolerance"));
        tolerance_spin_ = new QSpinBox;
        tolerance_spin_->setRange(0, 442);
        tolerance_spin_->setValue(controller_.tool().tolerance);
        connect(tolerance_spin_, &QSpinBox::valueChanged, this, [this](int value) { controller_.tool().tolerance = value; });
        row->addWidget(text);
        row->addStretch(1);
        row->addWidget(tolerance_spin_);
        tool_options_layout_->addWidget(control);

        auto* contiguous = new QCheckBox(tr("Contiguous"));
        contiguous->setChecked(controller_.tool().contiguous);
        connect(contiguous, &QCheckBox::toggled, this, [this](bool value) { controller_.tool().contiguous = value; });
        tool_options_layout_->addWidget(contiguous);
    };

    switch (tool) {
        case ToolType::Pencil:
            break;
        case ToolType::Brush:
            add_size_control(tr("Brush size"));
            add_brush_engine_controls();
            break;
        case ToolType::Eraser:
            add_size_control(tr("Brush size"));
            add_brush_engine_controls();
            break;
        case ToolType::Line:
            add_size_control(tr("Stroke width"));
            break;
        case ToolType::Rectangle:
            add_size_control(tr("Stroke width"));
            break;
        case ToolType::Ellipse:
            add_size_control(tr("Stroke width"));
            break;
        case ToolType::Bucket:
            add_tolerance_control();
            break;
        case ToolType::Gradient:
            break;
        case ToolType::Eyedropper:
            break;
        case ToolType::CloneStamp:
            add_size_control(tr("Brush size"));
            clone_source_label_ = new QLabel;
            clone_source_label_->setWordWrap(true);
            tool_options_layout_->addWidget(clone_source_label_);
            break;
        case ToolType::RectSelect:
        case ToolType::EllipseSelect:
            break;
        case ToolType::LassoSelect:
            break;
        case ToolType::MagicWand:
            add_tolerance_control();
            break;
        case ToolType::MovePixels:
            {
                auto* controls = new QWidget;
                auto* form = new QFormLayout(controls);
                form->setContentsMargins(0, 0, 0, 0);
                auto* scale_x = new QDoubleSpinBox;
                scale_x->setObjectName(QStringLiteral("SelectionScaleX"));
                scale_x->setRange(1.0, 1000.0);
                scale_x->setValue(100.0);
                scale_x->setSuffix(tr(" %"));
                auto* scale_y = new QDoubleSpinBox;
                scale_y->setObjectName(QStringLiteral("SelectionScaleY"));
                scale_y->setRange(1.0, 1000.0);
                scale_y->setValue(100.0);
                scale_y->setSuffix(tr(" %"));
                auto* rotation = new QDoubleSpinBox;
                rotation->setObjectName(QStringLiteral("SelectionRotation"));
                rotation->setRange(-180.0, 180.0);
                rotation->setDecimals(1);
                rotation->setSuffix(tr("°"));
                auto* lock_aspect = new QCheckBox(tr("Lock scale ratio"));
                lock_aspect->setObjectName(QStringLiteral("SelectionScaleLock"));
                lock_aspect->setChecked(true);
                connect(scale_x, &QDoubleSpinBox::valueChanged, controls,
                        [scale_y, lock_aspect](double value) {
                            if (!lock_aspect->isChecked()) return;
                            const QSignalBlocker blocker(scale_y);
                            scale_y->setValue(value);
                        });
                connect(scale_y, &QDoubleSpinBox::valueChanged, controls,
                        [scale_x, lock_aspect](double value) {
                            if (!lock_aspect->isChecked()) return;
                            const QSignalBlocker blocker(scale_x);
                            scale_x->setValue(value);
                        });
                form->addRow(tr("Scale X"), scale_x);
                form->addRow(tr("Scale Y"), scale_y);
                form->addRow(tr("Rotation"), rotation);
                form->addRow(lock_aspect);
                auto* apply = new QPushButton(tr("Apply transform"));
                apply->setObjectName(QStringLiteral("SelectionTransformApply"));
                connect(apply, &QPushButton::clicked, this, [this, scale_x, scale_y, rotation] {
                    if (controller_.apply_selection_transform(
                            static_cast<float>(scale_x->value() / 100.0),
                            static_cast<float>(scale_y->value() / 100.0),
                            static_cast<float>(rotation->value()))) refresh_all();
                });
                form->addRow(apply);
                tool_options_layout_->addWidget(controls);
            }
            break;
        case ToolType::Text:
            break;
    }

    if (clone_source_label_ != nullptr) {
        if (controller_.tool().clone_source_x >= 0 && controller_.tool().clone_source_y >= 0) {
            clone_source_label_->setText(tr("Source: (%1, %2)").arg(controller_.tool().clone_source_x).arg(controller_.tool().clone_source_y));
        } else {
            clone_source_label_->setText(tr("Source: not set"));
        }
    }
    tool_options_layout_->addStretch(1);
}

