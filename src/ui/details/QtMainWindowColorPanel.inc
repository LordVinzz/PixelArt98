class QtColorPanel final : public QWidget {
public:
    explicit QtColorPanel(EditorController& controller, QWidget* parent = nullptr)
        : QWidget(parent), controller_(controller) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(8);
        setMinimumHeight(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        auto* chips = new QHBoxLayout;
        primary_chip_ = new ColorChip;
        secondary_chip_ = new ColorChip;
        auto* swap = new QPushButton(tr("Swap"));
        chips->addWidget(primary_chip_);
        chips->addWidget(secondary_chip_);
        chips->addWidget(swap);
        chips->addStretch(1);
        root->addLayout(chips);

        color_square_ = new ColorSquare;
        root->addWidget(color_square_);

        auto* rgba_row = new QHBoxLayout;
        rgba_row->setContentsMargins(0, 0, 0, 0);
        rgba_row->setSpacing(6);
        red_slider_ = component_slider(255);
        green_slider_ = component_slider(255);
        blue_slider_ = component_slider(255);
        alpha_component_slider_ = component_slider(255);
        add_component_slider(rgba_row, tr("R"), red_slider_);
        add_component_slider(rgba_row, tr("G"), green_slider_);
        add_component_slider(rgba_row, tr("B"), blue_slider_);
        add_component_slider(rgba_row, tr("A"), alpha_component_slider_);
        rgba_row->addStretch(1);
        root->addLayout(rgba_row);

        auto* hsv_row = new QHBoxLayout;
        hsv_row->setContentsMargins(0, 0, 0, 0);
        hsv_row->setSpacing(6);
        hue_component_slider_ = component_slider(359);
        saturation_slider_ = component_slider(255);
        value_slider_ = component_slider(255);
        add_component_slider(hsv_row, tr("H"), hue_component_slider_);
        add_component_slider(hsv_row, tr("S"), saturation_slider_);
        add_component_slider(hsv_row, tr("V"), value_slider_);
        hsv_row->addStretch(1);
        root->addLayout(hsv_row);

        hex_edit_ = new QLineEdit;
        hex_edit_->setMaxLength(9);
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->addRow(tr("Hex"), hex_edit_);
        root->addLayout(form);

        palette_ = new PaletteList;
        palette_->setViewMode(QListView::IconMode);
        palette_->setResizeMode(QListView::Adjust);
        palette_->setIconSize(QSize(12, 12));
        palette_->setGridSize(QSize(16, 16));
        palette_->setMinimumHeight(0);
        palette_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        root->addWidget(palette_);

        auto* palette_actions = new QHBoxLayout;
        palette_actions->setContentsMargins(0, 0, 0, 0);
        palette_actions->setSpacing(4);
        auto* add_active = new QPushButton(tr("+ Active"));
        auto* add_both = new QPushButton(tr("+ Both"));
        auto* build_ramp = new QPushButton(tr("Build Ramp"));
        auto* remove_selected = new QPushButton(tr("Remove"));
        palette_actions->addWidget(add_active);
        palette_actions->addWidget(add_both);
        palette_actions->addWidget(build_ramp);
        palette_actions->addWidget(remove_selected);
        root->addLayout(palette_actions);
        root->addStretch(1);

        primary_chip_->clicked = [this] { set_active(false); };
        secondary_chip_->clicked = [this] { set_active(true); };
        connect(swap, &QPushButton::clicked, this, [this] {
            std::swap(controller_.tool().primary, controller_.tool().secondary);
            refresh_from_tool();
            notify_changed();
        });
        color_square_->changed = [this](int saturation, int value) {
            QColor next = current_;
            next.setHsv(hue_, saturation, value, current_.alpha());
            set_current_color(next, true);
        };
        connect_rgb_slider(red_slider_);
        connect_rgb_slider(green_slider_);
        connect_rgb_slider(blue_slider_);
        connect_rgb_slider(alpha_component_slider_);
        connect_hsv_slider(hue_component_slider_);
        connect_hsv_slider(saturation_slider_);
        connect_hsv_slider(value_slider_);
        connect(hex_edit_, &QLineEdit::editingFinished, this, [this] {
            if (updating_) return;
            if (const std::optional<QColor> parsed = parse_rgba_hex(hex_edit_->text())) {
                set_current_color(*parsed, true);
            } else {
                sync_controls();
            }
        });
        palette_->color_chosen = [this](Pixel color, bool secondary) {
            if (secondary) set_active(true);
            set_current_color(qcolor(color), true);
        };
        connect(add_active, &QPushButton::clicked, this, [this] { add_palette_color(active_secondary_ ? controller_.tool().secondary : controller_.tool().primary); });
        connect(add_both, &QPushButton::clicked, this, [this] {
            const Palette before = controller_.document().palette;
            append_palette_color(controller_.tool().primary);
            append_palette_color(controller_.tool().secondary);
            commit_palette_change("Add Current Colors", before);
        });
        connect(build_ramp, &QPushButton::clicked, this, [this] { build_palette_from_current_colors(); });
        connect(remove_selected, &QPushButton::clicked, this, [this] { remove_selected_palette_colors(); });

        rebuild_palette();
        refresh_from_tool();
    }

    void refresh_from_tool() {
        rebuild_palette();
        primary_chip_->set_color(qcolor(controller_.tool().primary));
        secondary_chip_->set_color(qcolor(controller_.tool().secondary));
        primary_chip_->set_selected(!active_secondary_);
        secondary_chip_->set_selected(active_secondary_);
        set_current_color(qcolor(active_secondary_ ? controller_.tool().secondary : controller_.tool().primary), false);
    }

    std::function<void()> changed;
    std::function<void()> document_changed;

private:
    static QSlider* component_slider(int maximum) {
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, maximum);
        slider->setMinimumWidth(42);
        slider->setFixedHeight(22);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        slider->setSingleStep(1);
        slider->setPageStep(maximum == 359 ? 30 : 16);
        return slider;
    }

    static void add_component_slider(QHBoxLayout* row, const QString& label, QSlider* slider) {
        auto* control = new QWidget;
        auto* layout = new QHBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        auto* text = new QLabel(label);
        text->setFixedWidth(10);
        text->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(text);
        layout->addWidget(slider);
        row->addWidget(control, 1);
    }

    static void set_slider_gradient(QSlider* slider, const QString& stops) {
        slider->setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal {"
            "border: 1px solid #555; height: 9px;"
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, %1);"
            "}"
            "QSlider::handle:horizontal {"
            "background: #f2f2f2; border: 1px solid #222; width: 8px; margin: -5px 0;"
            "}").arg(stops));
    }

    static QString color_stop(double position, const QColor& color) {
        return QStringLiteral("stop:%1 %2").arg(position, 0, 'f', 3).arg(color.name());
    }

    void connect_rgb_slider(QSlider* slider) {
        connect(slider, &QSlider::valueChanged, this, [this] {
            if (updating_) return;
            set_current_color(QColor(red_slider_->value(), green_slider_->value(), blue_slider_->value(), alpha_component_slider_->value()), true);
        });
    }

    void connect_hsv_slider(QSlider* slider) {
        connect(slider, &QSlider::valueChanged, this, [this] {
            if (updating_) return;
            QColor next;
            next.setHsv(hue_component_slider_->value(), saturation_slider_->value(), value_slider_->value(), alpha_component_slider_->value());
            set_current_color(next, true);
        });
    }

    void set_active(bool secondary) {
        active_secondary_ = secondary;
        refresh_from_tool();
    }

    void set_current_color(QColor color, bool push_to_tool) {
        current_ = color.toRgb();
        const QColor hsv = current_.toHsv();
        if (hsv.hue() >= 0) hue_ = hsv.hue();
        saturation_ = hsv.saturation();
        value_ = hsv.value();
        if (push_to_tool) {
            (active_secondary_ ? controller_.tool().secondary : controller_.tool().primary) = pixel_color(current_);
            primary_chip_->set_color(qcolor(controller_.tool().primary));
            secondary_chip_->set_color(qcolor(controller_.tool().secondary));
            notify_changed();
        }
        sync_controls();
    }

    void sync_controls() {
        updating_ = true;
        red_slider_->setValue(current_.red());
        green_slider_->setValue(current_.green());
        blue_slider_->setValue(current_.blue());
        alpha_component_slider_->setValue(current_.alpha());
        hue_component_slider_->setValue(hue_);
        saturation_slider_->setValue(saturation_);
        value_slider_->setValue(value_);
        hex_edit_->setText(rgba_hex(current_));
        color_square_->set_hsv(hue_, saturation_, value_);
        set_slider_gradient(red_slider_, QStringLiteral("stop:0 #000000, stop:1 #FF0000"));
        set_slider_gradient(green_slider_, QStringLiteral("stop:0 #000000, stop:1 #00FF00"));
        set_slider_gradient(blue_slider_, QStringLiteral("stop:0 #000000, stop:1 #0000FF"));
        set_slider_gradient(alpha_component_slider_, QStringLiteral("stop:0 #000000, stop:1 #FFFFFF"));
        set_slider_gradient(hue_component_slider_, QStringLiteral("stop:0 #FF0000, stop:0.167 #FFFF00, stop:0.333 #00FF00, stop:0.500 #00FFFF, stop:0.667 #0000FF, stop:0.833 #FF00FF, stop:1 #FF0000"));
        set_slider_gradient(saturation_slider_,
                            color_stop(0.0, QColor::fromHsv(hue_, 0, value_)) + QStringLiteral(", ") +
                            color_stop(1.0, QColor::fromHsv(hue_, 255, value_)));
        set_slider_gradient(value_slider_,
                            color_stop(0.0, QColor::fromHsv(hue_, saturation_, 0)) + QStringLiteral(", ") +
                            color_stop(1.0, QColor::fromHsv(hue_, saturation_, 255)));
        primary_chip_->set_selected(!active_secondary_);
        secondary_chip_->set_selected(active_secondary_);
        updating_ = false;
    }

    void notify_changed() {
        if (changed) changed();
    }

    void append_palette_color(Pixel color) {
        auto& colors = controller_.document().palette.colors;
        if (std::find(colors.begin(), colors.end(), color) == colors.end()) {
            colors.push_back(color);
            controller_.document().palette.active = static_cast<int>(colors.size()) - 1;
        }
    }

    void commit_palette_change(const char* name, const Palette& before) {
        if (before.colors == controller_.document().palette.colors && before.active == controller_.document().palette.active) {
            return;
        }
        controller_.document().commit_palette_edit(name, before);
        controller_.mark_changed(name);
        rebuild_palette();
        if (document_changed) document_changed();
        notify_changed();
    }

    void add_palette_color(Pixel color) {
        const Palette before = controller_.document().palette;
        append_palette_color(color);
        commit_palette_change("Add Palette Color", before);
    }

    void build_palette_from_current_colors() {
        constexpr int steps = 8;
        const Palette before = controller_.document().palette;
        const Pixel first = controller_.tool().primary;
        const Pixel second = controller_.tool().secondary;
        auto& colors = controller_.document().palette.colors;
        colors.clear();
        colors.reserve(steps);
        for (int i = 0; i < steps; ++i) {
            const int denominator = steps - 1;
            const auto interpolate = [i](int a_value, int b_value) {
                return static_cast<std::uint8_t>((a_value * (denominator - i) + b_value * i + denominator / 2) / denominator);
            };
            colors.push_back(rgba(interpolate(r(first), r(second)),
                                  interpolate(g(first), g(second)),
                                  interpolate(b(first), b(second)),
                                  interpolate(a(first), a(second))));
        }
        controller_.document().palette.active = 0;
        commit_palette_change("Build Palette Ramp", before);
    }

    void remove_selected_palette_colors() {
        const QList<QListWidgetItem*> selected = palette_->selectedItems();
        if (selected.empty()) {
            return;
        }
        const Palette before = controller_.document().palette;
        auto& colors = controller_.document().palette.colors;
        for (QListWidgetItem* item : selected) {
            const Pixel color = static_cast<Pixel>(item->data(Qt::UserRole).toULongLong());
            const auto found = std::find(colors.begin(), colors.end(), color);
            if (found != colors.end()) {
                colors.erase(found);
            }
        }
        if (colors.empty()) {
            controller_.document().palette.active = 0;
        } else {
            controller_.document().palette.active = std::clamp(controller_.document().palette.active, 0, static_cast<int>(colors.size()) - 1);
        }
        commit_palette_change("Remove Palette Colors", before);
    }

    void rebuild_palette() {
        const QSignalBlocker blocker(palette_);
        palette_->clear();
        for (Pixel color : controller_.document().palette.colors) {
            QPixmap icon(12, 12);
            icon.fill(qcolor(color));
            auto* item = new QListWidgetItem(QIcon(icon), QString());
            item->setData(Qt::UserRole, static_cast<qulonglong>(color));
            palette_->addItem(item);
        }
    }

    EditorController& controller_;
    ColorChip* primary_chip_ = nullptr;
    ColorChip* secondary_chip_ = nullptr;
    ColorSquare* color_square_ = nullptr;
    PaletteList* palette_ = nullptr;
    QSlider* red_slider_ = nullptr;
    QSlider* green_slider_ = nullptr;
    QSlider* blue_slider_ = nullptr;
    QSlider* alpha_component_slider_ = nullptr;
    QSlider* hue_component_slider_ = nullptr;
    QSlider* saturation_slider_ = nullptr;
    QSlider* value_slider_ = nullptr;
    QLineEdit* hex_edit_ = nullptr;
    QColor current_ = Qt::black;
    int hue_ = 0;
    int saturation_ = 0;
    int value_ = 0;
    bool active_secondary_ = false;
    bool updating_ = false;
};

QtMainWindow::QtMainWindow(AppSettings settings, QWidget* parent)
    : QMainWindow(parent), settings_(settings) {
    setObjectName(QStringLiteral("PixelArt98MainWindow"));
    setWindowTitle(QStringLiteral("PixelArt98"));
    setDockNestingEnabled(true);
    QMainWindow::DockOptions dock_options =
        QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks;
#if !defined(__APPLE__)
    dock_options |= QMainWindow::GroupedDragging;
#endif
    setDockOptions(dock_options);
    resize(1440, 900);

    ffmpeg_executable_ = detect_ffmpeg_executable(QString::fromStdString(settings_.ffmpeg_path));
    setProperty("ffmpegAvailable", !ffmpeg_executable_.isEmpty());
    setProperty("ffmpegExecutable", ffmpeg_executable_);

    network_manager_ = new QNetworkAccessManager(this);
    workspace_tabs_ = new QTabWidget(this);
    workspace_tabs_->setObjectName(QStringLiteral("MainWorkspaceTabs"));
    workspace_tabs_->setTabPosition(QTabWidget::North);
    workspace_tabs_->setDocumentMode(true);

    canvas_ = new QtCanvasWidget(controller_, workspace_tabs_);
    canvas_->setObjectName(QStringLiteral("CanvasWidget"));
    canvas_->editor_changed = [this] { refresh_all(); };
    canvas_->pointer_coordinates_changed = [this](std::optional<QPoint> coordinates) {
        update_pointer_status(coordinates);
    };
    canvas_->selection_geometry_changed = [this] { refresh_selection_status(); };
    canvas_->selection_preview_changed = [this](std::optional<QRect> preview) {
        if (preview.has_value()) update_selection_status(*preview);
        else refresh_selection_status();
    };
    canvas_->text_requested = [this](int x, int y, bool secondary) {
        begin_raster_text_edit(x, y, secondary);
    };
    canvas_->raster_text_box_changed = [this](int x, int y, int width, int height) {
        if (!raster_text_edit_active_) return;
        raster_text_x_ = x;
        raster_text_y_ = y;
        raster_text_box_width_ = width;
        raster_text_box_height_ = height;
        persist_raster_text_options(current_raster_text_options());
        update_raster_text_preview();
    };
    canvas_->raster_text_box_resize_finished = [this] {
        if (raster_text_edit_active_ && raster_text_input_ != nullptr) {
            raster_text_input_->setFocus();
        }
    };
    graph_effect_widget_ = new GraphEffectWidget(workspace_tabs_);
    graph_effect_widget_->setObjectName(QStringLiteral("GraphEffectWidget"));
    graph_effect_widget_->apply_requested = [this](const std::vector<Pixel>& pixels, int width, int height) {
        Document& document = controller_.document();
        if (!document.has_active_cel() || width != document.width || height != document.height ||
            pixels.size() != document.active_cel().pixels.size()) {
            set_status(tr("GraphEffect: preview dimensions no longer match the active document"));
            return;
        }
        if (pixels == document.active_cel().pixels) {
            set_status(tr("GraphEffect: the graph does not change the active layer"));
            return;
        }
        document.replace_active_pixels(pixels, "Apply GraphEffect");
        controller_.mark_changed("Apply GraphEffect");
        workspace_tabs_->setCurrentWidget(canvas_);
        refresh_all();
    };
    graph_effect_widget_->status_changed = [this](const QString& status) { set_status(status); };
    workspace_tabs_->addTab(canvas_, tr("Canvas"));
    workspace_tabs_->addTab(graph_effect_widget_, tr("GraphEffect"));
    connect(workspace_tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        update_workspace_dock_visibility();
        if (workspace_tabs_->widget(index) == graph_effect_widget_) sync_graph_effect_source();
        refresh_zoom_label();
    });
    setCentralWidget(workspace_tabs_);

    pointer_coordinates_label_ = new QLabel(this);
    pointer_coordinates_label_->setObjectName(QStringLiteral("PointerCoordinatesLabel"));
    pointer_coordinates_label_->setMinimumWidth(145);
    pointer_coordinates_label_->setToolTip(tr("Zero-based canvas coordinates"));
    statusBar()->addPermanentWidget(pointer_coordinates_label_);

    selection_geometry_label_ = new QLabel(this);
    selection_geometry_label_->setObjectName(QStringLiteral("SelectionGeometryLabel"));
    selection_geometry_label_->setMinimumWidth(300);
    selection_geometry_label_->setToolTip(tr("Selection bounds and size in pixels"));
    statusBar()->addPermanentWidget(selection_geometry_label_);
    update_pointer_status(std::nullopt);
    refresh_selection_status();

    build_actions();
    build_menus();
    build_docks();
    restore_ui_state();

    playback_timer_ = new QTimer(this);
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, [this] { update_playback(); });
    recovery_timer_ = new QTimer(this);
    recovery_timer_->setSingleShot(true);
    recovery_timer_->setInterval(1'500);
    connect(recovery_timer_, &QTimer::timeout, this, [this] { save_recovery_session_now(); });
    initialize_crash_recovery();
    refresh_all(false);
}

QtMainWindow::~QtMainWindow() {
    recovery_shutting_down_ = true;
    if (recovery_timer_ != nullptr) recovery_timer_->stop();
    finish_recovery_worker();
    if (!shutdown_state_saved_) {
        save_ui_state();
        mark_clean_shutdown();
    }
}
