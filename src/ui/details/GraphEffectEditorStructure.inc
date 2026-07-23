class GraphEffectWidget::Impl final {
public:
    explicit Impl(GraphEffectWidget& owner) : owner_(owner) {
        graph_ = make_default_graph_effect();

        auto* root_layout = new QVBoxLayout(&owner_);
        root_layout->setContentsMargins(0, 0, 0, 0);
        root_layout->setSpacing(0);

        toolbar_ = new QToolBar(GraphEffectWidget::tr("GraphEffect"), &owner_);
        toolbar_->setObjectName(QStringLiteral("GraphEffectToolbar"));
        toolbar_->setMovable(false);
        new_action_ = toolbar_->addAction(GraphEffectWidget::tr("New"));
        save_action_ = toolbar_->addAction(GraphEffectWidget::tr("Save"));
        load_action_ = toolbar_->addAction(GraphEffectWidget::tr("Load"));
        toolbar_->addSeparator();
        apply_action_ = toolbar_->addAction(GraphEffectWidget::tr("Apply to image"));
        new_action_->setObjectName(QStringLiteral("GraphEffectNew"));
        save_action_->setObjectName(QStringLiteral("GraphEffectSave"));
        load_action_->setObjectName(QStringLiteral("GraphEffectLoad"));
        apply_action_->setObjectName(QStringLiteral("GraphEffectApply"));
        apply_action_->setEnabled(false);
        root_layout->addWidget(toolbar_);

        splitter_ = new QSplitter(Qt::Horizontal, &owner_);
        splitter_->setObjectName(QStringLiteral("GraphEffectSplitter"));
        splitter_->setChildrenCollapsible(false);
        root_layout->addWidget(splitter_, 1);

        auto* palette_panel = new QWidget(splitter_);
        palette_panel->setObjectName(QStringLiteral("GraphEffectPalettePanel"));
        auto* palette_layout = new QVBoxLayout(palette_panel);
        palette_layout->setContentsMargins(6, 6, 6, 6);
        palette_layout->addWidget(new QLabel(GraphEffectWidget::tr("Nodes"), palette_panel));
        palette_ = new QTreeWidget(palette_panel);
        palette_->setObjectName(QStringLiteral("GraphEffectPalette"));
        palette_->setHeaderHidden(true);
        palette_->setRootIsDecorated(true);
        palette_->setUniformRowHeights(true);
        palette_->setMinimumWidth(190);
        palette_layout->addWidget(palette_, 1);
        populate_palette();

        scene_ = new GraphScene(&owner_);
        view_ = new GraphView(splitter_);
        view_->setScene(scene_);
        view_->setMinimumWidth(420);

        auto* right_panel = new QWidget(splitter_);
        right_panel->setObjectName(QStringLiteral("GraphEffectPropertiesPanel"));
        auto* right_layout = new QVBoxLayout(right_panel);
        right_layout->setContentsMargins(6, 6, 6, 6);
        right_layout->setSpacing(6);
        right_layout->addWidget(new QLabel(GraphEffectWidget::tr("Inspector"), right_panel));
        inspector_scroll_ = new QScrollArea(right_panel);
        inspector_scroll_->setObjectName(QStringLiteral("GraphEffectInspector"));
        inspector_scroll_->setWidgetResizable(true);
        inspector_scroll_->setFrameShape(QFrame::StyledPanel);
        inspector_content_ = new QWidget;
        inspector_form_ = new QFormLayout(inspector_content_);
        inspector_form_->setContentsMargins(6, 6, 6, 6);
        inspector_form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        inspector_scroll_->setWidget(inspector_content_);
        right_layout->addWidget(inspector_scroll_, 1);
        right_layout->addWidget(new QLabel(GraphEffectWidget::tr("Live preview"), right_panel));
        preview_ = new GraphPreview(right_panel);
        right_layout->addWidget(preview_, 1);

        splitter_->addWidget(palette_panel);
        splitter_->addWidget(view_);
        splitter_->addWidget(right_panel);
        splitter_->setStretchFactor(0, 0);
        splitter_->setStretchFactor(1, 1);
        splitter_->setStretchFactor(2, 0);
        splitter_->setSizes({210, 760, 310});

        status_label_ = new QLabel(GraphEffectWidget::tr("Waiting for a source image"), &owner_);
        status_label_->setObjectName(QStringLiteral("GraphEffectStatus"));
        status_label_->setWordWrap(true);
        status_label_->setMinimumHeight(28);
        status_label_->setContentsMargins(8, 4, 8, 4);
        root_layout->addWidget(status_label_);

        preview_timer_ = new QTimer(&owner_);
        preview_timer_->setObjectName(QStringLiteral("GraphEffectPreviewTimer"));
        preview_timer_->setSingleShot(true);
        preview_timer_->setInterval(kPreviewDebounceMilliseconds);

        QObject::connect(preview_timer_, &QTimer::timeout, &owner_, [this] { evaluate_preview(); });
        QObject::connect(new_action_, &QAction::triggered, &owner_, [this] { owner_.reset_graph(); });
        QObject::connect(save_action_, &QAction::triggered, &owner_, [this] { save_from_toolbar(); });
        QObject::connect(load_action_, &QAction::triggered, &owner_, [this] { load_from_toolbar(); });
        QObject::connect(apply_action_, &QAction::triggered, &owner_, [this] {
            (void)owner_.apply_preview();
        });
        QObject::connect(palette_, &QTreeWidget::itemDoubleClicked, &owner_,
                         [this](QTreeWidgetItem* item, int) { add_palette_item(item); });

        scene_->node_moved = [this](GraphEffectNodeId node_id, const QPointF& position) {
            GraphEffectNode* node = find_graph_effect_node(graph_, node_id);
            if (node == nullptr || (node->x == position.x() && node->y == position.y())) return;
            node->x = position.x();
            node->y = position.y();
            mark_graph_changed(false);
        };
        scene_->node_selected = [this](GraphEffectNodeId node_id) { rebuild_inspector(node_id); };
        scene_->connection_requested =
            [this](GraphEffectNodeId from_node,
                   const std::string& from_port,
                   GraphEffectNodeId to_node,
                   const std::string& to_port) {
                QString error;
                (void)owner_.connect_nodes(from_node, from_port, to_node, to_port, &error);
            };
        view_->delete_requested = [this] { remove_selected_nodes(); };

        rebuild_scene(std::nullopt);
        rebuild_inspector(std::nullopt);
        owner_.setProperty("graphDirty", false);
        owner_.setProperty("previewAvailable", false);
        owner_.setProperty("previewRevision", 0);
        QTimer::singleShot(0, &owner_, [this] { frame_graph(); });
    }

    ~Impl() {
        if (preview_timer_ != nullptr) preview_timer_->stop();
    }

    void populate_palette() {
        palette_->clear();
        std::map<std::string, QTreeWidgetItem*> categories;
        for (const GraphEffectNodeSpec& spec : graph_effect_catalog()) {
            QTreeWidgetItem* category_item = nullptr;
            const auto category_found = categories.find(spec.category);
            if (category_found == categories.end()) {
                category_item = new QTreeWidgetItem(palette_);
                category_item->setText(0, QString::fromStdString(spec.category));
                QFont font = category_item->font(0);
                font.setBold(true);
                category_item->setFont(0, font);
                category_item->setForeground(0, category_color(spec.category));
                category_item->setFlags(category_item->flags() & ~Qt::ItemIsSelectable);
                categories[spec.category] = category_item;
            } else {
                category_item = category_found->second;
            }
            auto* node_item = new QTreeWidgetItem(category_item);
            node_item->setText(0, QString::fromStdString(spec.label));
            node_item->setData(0, Qt::UserRole, QString::fromStdString(spec.type_id));
            node_item->setToolTip(0, QString::fromStdString(spec.type_id));
        }
        palette_->expandAll();
    }

    void add_palette_item(QTreeWidgetItem* item) {
        if (item == nullptr) return;
        const QString type_id = item->data(0, Qt::UserRole).toString();
        if (type_id.isEmpty()) return;
        const QPoint viewport_center = view_->viewport()->rect().center();
        QPointF position = view_->mapToScene(viewport_center);
        position += QPointF(18.0 * static_cast<qreal>(palette_add_offset_),
                            18.0 * static_cast<qreal>(palette_add_offset_));
        palette_add_offset_ = (palette_add_offset_ + 1) % 8;
        GraphEffectNodeId new_id = 0;
        QString error;
        if (owner_.add_node(type_id.toStdString(), position, &new_id, &error)) {
            if (GraphNodeItem* item_to_select = scene_->node_item(new_id); item_to_select != nullptr) {
                item_to_select->setSelected(true);
                view_->centerOn(item_to_select);
            }
        }
    }

    void save_from_toolbar() {
        QString destination = graph_path_;
        if (destination.isEmpty()) {
            destination = QFileDialog::getSaveFileName(
                &owner_, GraphEffectWidget::tr("Save GraphEffect"),
                QStringLiteral("graph.pxgraph"), graph_file_filter());
        }
        if (destination.isEmpty()) return;
        QString error;
        (void)owner_.save_graph(destination, &error);
    }

    void load_from_toolbar() {
        const QString source = QFileDialog::getOpenFileName(
            &owner_, GraphEffectWidget::tr("Load GraphEffect"), QString(), graph_file_filter());
        if (source.isEmpty()) return;
        QString error;
        (void)owner_.load_graph(source, &error);
    }

    void frame_graph() {
        const QRectF contents = scene_->itemsBoundingRect().adjusted(-80.0, -80.0, 80.0, 80.0);
        if (contents.isValid() && !contents.isEmpty()) {
            view_->fitInView(contents, Qt::KeepAspectRatio);
            const qreal fitted_scale = view_->transform().m11();
            if (fitted_scale > 1.25) {
                view_->resetTransform();
                view_->scale(1.25, 1.25);
                view_->centerOn(contents.center());
            }
        }
    }

    void rebuild_scene(std::optional<GraphEffectNodeId> selected_node) {
        rebuilding_scene_ = true;
        scene_->rebuild(graph_);
        rebuilding_scene_ = false;
        owner_.setProperty("nodeCount", static_cast<int>(graph_.nodes.size()));
        owner_.setProperty("linkCount", static_cast<int>(graph_.links.size()));
        if (selected_node.has_value()) {
            if (GraphNodeItem* item = scene_->node_item(*selected_node); item != nullptr) {
                item->setSelected(true);
            }
        }
    }

    void clear_inspector() {
        while (inspector_form_->count() > 0) {
            QLayoutItem* item = inspector_form_->takeAt(0);
            if (item == nullptr) break;
            if (QWidget* widget = item->widget(); widget != nullptr) delete widget;
            if (QLayout* child_layout = item->layout(); child_layout != nullptr) delete child_layout;
            delete item;
        }
    }

    void rebuild_inspector(std::optional<GraphEffectNodeId> node_id) {
        inspected_node_ = node_id;
        clear_inspector();
        if (!node_id.has_value()) {
            auto* hint = new QLabel(GraphEffectWidget::tr("Select a node to edit its parameters."),
                                    inspector_content_);
            hint->setWordWrap(true);
            inspector_form_->addRow(hint);
            return;
        }

        GraphEffectNode* node = find_graph_effect_node(graph_, *node_id);
        if (node == nullptr) {
            rebuild_inspector(std::nullopt);
            return;
        }
        const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node->type_id);
        if (spec == nullptr) {
            auto* unknown = new QLabel(GraphEffectWidget::tr("Unknown node type: %1")
                                           .arg(QString::fromStdString(node->type_id)),
                                       inspector_content_);
            unknown->setWordWrap(true);
            inspector_form_->addRow(unknown);
            return;
        }

        auto* title = new QLabel(QStringLiteral("<b>%1</b><br><small>%2</small>")
                                     .arg(QString::fromStdString(spec->label).toHtmlEscaped(),
                                          QString::fromStdString(spec->type_id).toHtmlEscaped()),
                                 inspector_content_);
        title->setObjectName(node_object_name(*node_id));
        title->setTextFormat(Qt::RichText);
        inspector_form_->addRow(title);

        if (spec->bypassable) {
            auto* enabled = new QCheckBox(GraphEffectWidget::tr("Enabled"), inspector_content_);
            enabled->setObjectName(QStringLiteral("GraphEffectEnabled.%1")
                                       .arg(static_cast<qulonglong>(*node_id)));
            enabled->setChecked(node->enabled);
            inspector_form_->addRow(enabled);
            QObject::connect(enabled, &QCheckBox::toggled, &owner_,
                             [this, selected_id = *node_id](bool checked) {
                                 QString error;
                                 (void)owner_.set_node_enabled(selected_id, checked, &error);
                             });
        }

        if (node->type_id == "adjustment.levels") {
            inspector_form_->addRow(levels_editor(*node, *spec));
            update_specialized_inspector_histograms();
            owner_.schedule_preview();
            return;
        }
        if (node->type_id == "adjustment.curves") {
            inspector_form_->addRow(curves_editor(*node));
            update_specialized_inspector_histograms();
            owner_.schedule_preview();
            return;
        }

        for (const GraphEffectParameterSpec& parameter : spec->parameters) {
            const auto found = node->parameters.find(parameter.id);
            const GraphEffectParameter value = found == node->parameters.end()
                ? parameter.default_value
                : found->second;
            QWidget* editor = parameter_editor(*node_id, parameter, value);
            if (editor != nullptr) {
                inspector_form_->addRow(QString::fromStdString(parameter.label), editor);
            }
        }
    }

    QWidget* levels_editor(const GraphEffectNode& node, const GraphEffectNodeSpec& spec) {
        const GraphEffectNodeId node_id = node.id;
        const QString id = QString::number(static_cast<qulonglong>(node_id));
        auto* editor = new QWidget(inspector_content_);
        editor->setObjectName(QStringLiteral("GraphEffectLevelsEditor.%1").arg(id));
        editor->setProperty("customEditorType", QStringLiteral("levels"));
        editor->setProperty("histogramAvailable", false);
        auto* layout = new QVBoxLayout(editor);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* histograms = new QWidget(editor);
        auto* histogram_grid = new QGridLayout(histograms);
        histogram_grid->setContentsMargins(0, 0, 0, 0);
        histogram_grid->setHorizontalSpacing(4);
        histogram_grid->setColumnStretch(0, 1);
        histogram_grid->setColumnStretch(1, 1);
        auto* input_title = new QLabel(GraphEffectWidget::tr("Input histogram"), histograms);
        auto* output_title = new QLabel(GraphEffectWidget::tr("Output histogram"), histograms);
        input_title->setObjectName(QStringLiteral("GraphEffectLevelsInputTitle.%1").arg(id));
        output_title->setObjectName(QStringLiteral("GraphEffectLevelsOutputTitle.%1").arg(id));
        input_title->setAlignment(Qt::AlignCenter);
        output_title->setAlignment(Qt::AlignCenter);
        auto* input_histogram = new LevelsHistogramWidget(histograms);
        auto* output_histogram = new LevelsHistogramWidget(histograms);
        input_histogram->setObjectName(
            QStringLiteral("GraphEffectLevelsInputHistogram.%1").arg(id));
        output_histogram->setObjectName(
            QStringLiteral("GraphEffectLevelsOutputHistogram.%1").arg(id));
        for (LevelsHistogramWidget* histogram : {input_histogram, output_histogram}) {
            histogram->setMinimumSize(120, 100);
            histogram->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
            histogram->setProperty("histogramAvailable", false);
            histogram->set_histogram({});
        }
        histogram_grid->addWidget(input_title, 0, 0);
        histogram_grid->addWidget(output_title, 0, 1);
        histogram_grid->addWidget(input_histogram, 1, 0);
        histogram_grid->addWidget(output_histogram, 1, 1);
        layout->addWidget(histograms);

        auto* parameters = new QWidget(editor);
        auto* parameter_grid = new QGridLayout(parameters);
        parameter_grid->setObjectName(
            QStringLiteral("GraphEffectLevelsParametersGrid.%1").arg(id));
        parameter_grid->setContentsMargins(0, 4, 0, 0);
        parameter_grid->setHorizontalSpacing(8);
        parameter_grid->setVerticalSpacing(4);
        parameter_grid->setColumnStretch(1, 1);
        int row = 0;
        for (const GraphEffectParameterSpec& parameter : spec.parameters) {
            const auto found = node.parameters.find(parameter.id);
            const GraphEffectParameter value = found == node.parameters.end()
                ? parameter.default_value
                : found->second;
            QWidget* control = parameter_editor(node_id, parameter, value);
            if (control == nullptr) continue;
            auto* label = new QLabel(QString::fromStdString(parameter.label), parameters);
            label->setBuddy(control);
            parameter_grid->addWidget(label, row, 0);
            parameter_grid->addWidget(control, row, 1);
            ++row;
        }
        layout->addWidget(parameters);
        return editor;
    }

    QWidget* curves_editor(const GraphEffectNode& node) {
        const GraphEffectNodeId node_id = node.id;
        const QString id = QString::number(static_cast<qulonglong>(node_id));
        auto* container = new QWidget(inspector_content_);
        container->setObjectName(QStringLiteral("GraphEffectCurvesEditor.%1").arg(id));
        container->setProperty("customEditorType", QStringLiteral("curves"));
        container->setProperty("histogramAvailable", false);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* targets = new QWidget(container);
        auto* targets_layout = new QHBoxLayout(targets);
        targets_layout->setContentsMargins(0, 0, 0, 0);
        targets_layout->setSpacing(5);
        targets_layout->addWidget(new QLabel(GraphEffectWidget::tr("Mode:"), targets));
        auto* luminance_mode = new QRadioButton(GraphEffectWidget::tr("Luminance"), targets);
        auto* rgb_mode = new QRadioButton(GraphEffectWidget::tr("RGB"), targets);
        luminance_mode->setObjectName(
            QStringLiteral("GraphEffectCurvesModeLuminance.%1").arg(id));
        rgb_mode->setObjectName(QStringLiteral("GraphEffectCurvesModeRgb.%1").arg(id));
        auto* modes = new QButtonGroup(container);
        modes->setExclusive(true);
        modes->addButton(luminance_mode, 0);
        modes->addButton(rgb_mode, 1);
        targets_layout->addWidget(luminance_mode);
        targets_layout->addWidget(rgb_mode);

        auto* red = new QCheckBox(GraphEffectWidget::tr("R"), targets);
        auto* green = new QCheckBox(GraphEffectWidget::tr("G"), targets);
        auto* blue = new QCheckBox(GraphEffectWidget::tr("B"), targets);
        red->setObjectName(QStringLiteral("GraphEffectParameter.%1.red").arg(id));
        green->setObjectName(QStringLiteral("GraphEffectParameter.%1.green").arg(id));
        blue->setObjectName(QStringLiteral("GraphEffectParameter.%1.blue").arg(id));

        bool luminance = graph_boolean_parameter(node, "luminance", true);
        bool use_red = graph_boolean_parameter(node, "red", false);
        bool use_green = graph_boolean_parameter(node, "green", false);
        bool use_blue = graph_boolean_parameter(node, "blue", false);
        if (luminance || (!use_red && !use_green && !use_blue)) {
            luminance = true;
            use_red = use_green = use_blue = false;
        }
        luminance_mode->setChecked(luminance);
        rgb_mode->setChecked(!luminance);
        red->setChecked(use_red);
        green->setChecked(use_green);
        blue->setChecked(use_blue);
        for (QCheckBox* channel : {red, green, blue}) {
            channel->setEnabled(!luminance);
            targets_layout->addWidget(channel);
        }
        targets_layout->addStretch(1);
        layout->addWidget(targets);

        auto* curve = new CurveEditorWidget({}, container);
        curve->setObjectName(QStringLiteral("GraphEffectCurvesGraph.%1").arg(id));
        curve->setMinimumSize(260, 180);
        curve->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        curve->setProperty("histogramAvailable", false);
        curve->setProperty("histogramRevision", 0);
        curve->setProperty("histogramChecksum", qlonglong{0});
        curve->setProperty("histogramSampleCount", qlonglong{0});
        const std::vector<double> curve_x =
            graph_array_parameter(node, "curve_x", {0.0, 1.0});
        const std::vector<double> curve_y =
            graph_array_parameter(node, "curve_y", {0.0, 1.0});
        (void)curve->set_curve_points(curve_x, curve_y);
        curve->set_targets(luminance, use_red, use_green, use_blue);
        layout->addWidget(curve);

        auto* footer = new QWidget(container);
        auto* footer_layout = new QHBoxLayout(footer);
        footer_layout->setContentsMargins(0, 0, 0, 0);
        auto* help = new QLabel(
            GraphEffectWidget::tr("Drag points to shape the curve; right-click to remove."), footer);
        help->setWordWrap(true);
        auto* reset = new QPushButton(GraphEffectWidget::tr("Reset"), footer);
        reset->setObjectName(QStringLiteral("GraphEffectCurvesReset.%1").arg(id));
        footer_layout->addWidget(help, 1);
        footer_layout->addWidget(reset);
        layout->addWidget(footer);

        curve->changed = [this, node_id, curve] {
            GraphEffectNode* edited = find_graph_effect_node(graph_, node_id);
            if (edited == nullptr) return;
            const CurvesSettings settings = curve->settings();
            const int point_count = std::clamp(settings.point_count, 0, kMaxCurvePoints);
            std::vector<double> x;
            std::vector<double> y;
            x.reserve(static_cast<std::size_t>(point_count));
            y.reserve(static_cast<std::size_t>(point_count));
            for (int index = 0; index < point_count; ++index) {
                x.push_back(static_cast<double>(settings.x[static_cast<std::size_t>(index)]));
                y.push_back(static_cast<double>(settings.y[static_cast<std::size_t>(index)]));
            }
            const auto current_x = edited->parameters.find("curve_x");
            const auto current_y = edited->parameters.find("curve_y");
            if (current_x != edited->parameters.end() && current_y != edited->parameters.end() &&
                current_x->second == GraphEffectParameter{x} &&
                current_y->second == GraphEffectParameter{y}) {
                return;
            }
            edited->parameters["curve_x"] = std::move(x);
            edited->parameters["curve_y"] = std::move(y);
            mark_graph_changed(true);
        };

        const auto apply_targets = [this, node_id, curve, luminance_mode, rgb_mode,
                                    red, green, blue] {
            GraphEffectNode* edited = find_graph_effect_node(graph_, node_id);
            if (edited == nullptr) return;
            const bool use_luminance = luminance_mode->isChecked();
            if (use_luminance) {
                const QSignalBlocker red_blocker(red);
                const QSignalBlocker green_blocker(green);
                const QSignalBlocker blue_blocker(blue);
                red->setChecked(false);
                green->setChecked(false);
                blue->setChecked(false);
            } else if (rgb_mode->isChecked() && !red->isChecked() &&
                !green->isChecked() && !blue->isChecked()) {
                const QSignalBlocker red_blocker(red);
                const QSignalBlocker green_blocker(green);
                const QSignalBlocker blue_blocker(blue);
                red->setChecked(true);
                green->setChecked(true);
                blue->setChecked(true);
            }
            for (QCheckBox* channel : {red, green, blue}) channel->setEnabled(!use_luminance);
            const bool next_red = !use_luminance && red->isChecked();
            const bool next_green = !use_luminance && green->isChecked();
            const bool next_blue = !use_luminance && blue->isChecked();
            curve->set_targets(use_luminance, next_red, next_green, next_blue);
            const std::array<bool, 4> before = {
                graph_boolean_parameter(*edited, "luminance", true),
                graph_boolean_parameter(*edited, "red", false),
                graph_boolean_parameter(*edited, "green", false),
                graph_boolean_parameter(*edited, "blue", false)};
            const std::array<bool, 4> after = {
                use_luminance, next_red, next_green, next_blue};
            if (before == after) return;
            edited->parameters["luminance"] = after[0];
            edited->parameters["red"] = after[1];
            edited->parameters["green"] = after[2];
            edited->parameters["blue"] = after[3];
            mark_graph_changed(true);
        };
        QObject::connect(modes, &QButtonGroup::idClicked, &owner_,
                         [apply_targets](int) { apply_targets(); });
        for (QCheckBox* channel : {red, green, blue}) {
            QObject::connect(channel, &QCheckBox::toggled, &owner_,
                             [channel, red, green, blue, apply_targets](bool) {
                if (!red->isChecked() && !green->isChecked() && !blue->isChecked()) {
                    const QSignalBlocker blocker(channel);
                    channel->setChecked(true);
                }
                apply_targets();
            });
        }
        QObject::connect(reset, &QPushButton::clicked, curve, &CurveEditorWidget::reset_curve);
        return container;
    }

