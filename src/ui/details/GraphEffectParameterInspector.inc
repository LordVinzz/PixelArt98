    QWidget* parameter_editor(GraphEffectNodeId node_id,
                              const GraphEffectParameterSpec& parameter,
                              const GraphEffectParameter& value) {
        const QString object_name = QStringLiteral("GraphEffectParameter.%1.%2")
                                        .arg(static_cast<qulonglong>(node_id))
                                        .arg(QString::fromStdString(parameter.id));
        switch (parameter.kind) {
            case GraphEffectParameterKind::Integer: {
                auto* spin = new QSpinBox(inspector_content_);
                spin->setObjectName(object_name);
                const double raw_minimum = parameter.minimum.value_or(
                    static_cast<double>(std::numeric_limits<int>::lowest()));
                const double raw_maximum = parameter.maximum.value_or(
                    static_cast<double>(std::numeric_limits<int>::max()));
                const int minimum = static_cast<int>(std::clamp(
                    raw_minimum, static_cast<double>(std::numeric_limits<int>::lowest()),
                    static_cast<double>(std::numeric_limits<int>::max())));
                const int maximum = static_cast<int>(std::clamp(
                    raw_maximum, static_cast<double>(std::numeric_limits<int>::lowest()),
                    static_cast<double>(std::numeric_limits<int>::max())));
                spin->setRange(minimum, maximum);
                const std::int64_t current = std::get_if<std::int64_t>(&value) != nullptr
                    ? *std::get_if<std::int64_t>(&value)
                    : 0;
                spin->setValue(static_cast<int>(std::clamp<std::int64_t>(
                    current, static_cast<std::int64_t>(minimum), static_cast<std::int64_t>(maximum))));
                QObject::connect(spin, &QSpinBox::valueChanged, &owner_,
                                 [this, node_id, parameter_id = parameter.id](int next) {
                                     QString error;
                                     (void)owner_.set_node_parameter(
                                         node_id, parameter_id, static_cast<std::int64_t>(next), &error);
                                 });
                return spin;
            }
            case GraphEffectParameterKind::Number: {
                auto* spin = new QDoubleSpinBox(inspector_content_);
                spin->setObjectName(object_name);
                spin->setDecimals(4);
                spin->setRange(parameter.minimum.value_or(-1.0e6),
                               parameter.maximum.value_or(1.0e6));
                spin->setSingleStep((spin->maximum() - spin->minimum()) <= 2.0 ? 0.01 : 1.0);
                spin->setValue(std::get_if<double>(&value) != nullptr ? *std::get_if<double>(&value) : 0.0);
                QObject::connect(spin, &QDoubleSpinBox::valueChanged, &owner_,
                                 [this, node_id, parameter_id = parameter.id](double next) {
                                     QString error;
                                     (void)owner_.set_node_parameter(node_id, parameter_id, next, &error);
                                 });
                return spin;
            }
            case GraphEffectParameterKind::Boolean: {
                auto* check = new QCheckBox(inspector_content_);
                check->setObjectName(object_name);
                check->setChecked(std::get_if<bool>(&value) != nullptr && *std::get_if<bool>(&value));
                QObject::connect(check, &QCheckBox::toggled, &owner_,
                                 [this, node_id, parameter_id = parameter.id](bool checked) {
                                     QString error;
                                     (void)owner_.set_node_parameter(node_id, parameter_id, checked, &error);
                                 });
                return check;
            }
            case GraphEffectParameterKind::Color: {
                auto* button = new QPushButton(inspector_content_);
                button->setObjectName(object_name);
                const Pixel current = std::get_if<Pixel>(&value) != nullptr
                    ? *std::get_if<Pixel>(&value)
                    : Pixel{0};
                style_color_button(button, current);
                QObject::connect(button, &QPushButton::clicked, &owner_,
                                 [this, button, node_id, parameter_id = parameter.id, current] {
                                     const QColor chosen = QColorDialog::getColor(
                                         color_from_pixel(current), &owner_,
                                         GraphEffectWidget::tr("Choose color"),
                                         QColorDialog::ShowAlphaChannel);
                                     if (!chosen.isValid()) return;
                                     const Pixel next = pixel_from_color(chosen);
                                     QString error;
                                     if (owner_.set_node_parameter(node_id, parameter_id, next, &error)) {
                                         style_color_button(button, next);
                                     }
                                 });
                return button;
            }
            case GraphEffectParameterKind::Choice: {
                auto* combo = new QComboBox(inspector_content_);
                combo->setObjectName(object_name);
                for (const std::string& choice : parameter.choices) {
                    combo->addItem(QString::fromStdString(choice), QString::fromStdString(choice));
                }
                const std::string current = std::get_if<std::string>(&value) != nullptr
                    ? *std::get_if<std::string>(&value)
                    : std::string{};
                const int current_index = combo->findData(QString::fromStdString(current));
                if (current_index >= 0) combo->setCurrentIndex(current_index);
                QObject::connect(combo, &QComboBox::currentIndexChanged, &owner_,
                                 [this, combo, node_id, parameter_id = parameter.id](int) {
                                     QString error;
                                     (void)owner_.set_node_parameter(
                                         node_id, parameter_id,
                                         combo->currentData().toString().toStdString(), &error);
                                 });
                return combo;
            }
            case GraphEffectParameterKind::NumberArray: {
                auto* edit = new QLineEdit(inspector_content_);
                edit->setObjectName(object_name);
                const std::vector<double> current = std::get_if<std::vector<double>>(&value) != nullptr
                    ? *std::get_if<std::vector<double>>(&value)
                    : std::vector<double>{};
                edit->setText(parameter_text(current));
                edit->setToolTip(GraphEffectWidget::tr("Comma-separated numbers"));
                QObject::connect(edit, &QLineEdit::editingFinished, &owner_,
                                 [this, edit, node_id, parameter_id = parameter.id, current] {
                                     const auto parsed = parse_number_array(edit->text());
                                     if (!parsed.has_value()) {
                                         edit->setText(parameter_text(current));
                                         publish_status(GraphEffectWidget::tr(
                                             "A comma-separated list of numbers is required."), true);
                                         return;
                                     }
                                     QString error;
                                     (void)owner_.set_node_parameter(
                                         node_id, parameter_id, *parsed, &error);
                                 });
                return edit;
            }
        }
        return nullptr;
    }

    void update_specialized_inspector_histograms() {
        if (!inspected_node_.has_value()) return;
        const GraphEffectNodeId node_id = *inspected_node_;
        const QString id = QString::number(static_cast<qulonglong>(node_id));
        const bool captured = evaluation_.inspected_node_id == inspected_node_ &&
                              evaluation_.inspected_node_evaluated;
        const std::size_t expected = evaluation_.width > 0 && evaluation_.height > 0
            ? static_cast<std::size_t>(evaluation_.width) *
                  static_cast<std::size_t>(evaluation_.height)
            : 0U;
        const bool input_available = captured && expected != 0U &&
                                     evaluation_.inspected_input_pixels.size() == expected;
        const bool output_available = captured && expected != 0U &&
                                      evaluation_.inspected_output_pixels.size() == expected;
        const CurveEditorWidget::Histograms input_histograms = input_available
            ? histograms_from_pixels(evaluation_.inspected_input_pixels)
            : CurveEditorWidget::Histograms{};
        const CurveEditorWidget::Histograms output_histograms = output_available
            ? histograms_from_pixels(evaluation_.inspected_output_pixels)
            : CurveEditorWidget::Histograms{};

        if (auto* levels = inspector_content_->findChild<QWidget*>(
                QStringLiteral("GraphEffectLevelsEditor.%1").arg(id));
            levels != nullptr) {
            levels->setProperty("histogramAvailable", input_available && output_available);
            auto* input = find_widget<LevelsHistogramWidget>(levels,
                QStringLiteral("GraphEffectLevelsInputHistogram.%1").arg(id));
            auto* output = find_widget<LevelsHistogramWidget>(levels,
                QStringLiteral("GraphEffectLevelsOutputHistogram.%1").arg(id));
            if (input != nullptr) {
                input->setProperty("histogramAvailable", input_available);
                input->setProperty("histogramSampleCount",
                                   histogram_sample_count(input_histograms[0]));
                input->set_histogram(input_histograms[0]);
            }
            if (output != nullptr) {
                output->setProperty("histogramAvailable", output_available);
                output->setProperty("histogramSampleCount",
                                    histogram_sample_count(output_histograms[0]));
                output->set_histogram(output_histograms[0]);
            }
        }

        if (auto* curves = inspector_content_->findChild<QWidget*>(
                QStringLiteral("GraphEffectCurvesEditor.%1").arg(id));
            curves != nullptr) {
            curves->setProperty("histogramAvailable", input_available);
            auto* graph = find_widget<CurveEditorWidget>(curves,
                QStringLiteral("GraphEffectCurvesGraph.%1").arg(id));
            if (graph != nullptr) {
                graph->set_histograms(input_histograms);
                graph->setProperty("histogramAvailable", input_available);
                graph->setProperty("histogramChecksum",
                                   histogram_checksum(input_histograms[0]));
                graph->setProperty("histogramSampleCount",
                                   histogram_sample_count(input_histograms[0]));
                graph->setProperty("histogramRevision",
                                   graph->property("histogramRevision").toInt() + 1);
            }
        }
    }

    void sync_curves_target_editors(GraphEffectNodeId node_id) {
        if (!inspected_node_.has_value() || *inspected_node_ != node_id) return;
        const GraphEffectNode* node = find_graph_effect_node(graph_, node_id);
        if (node == nullptr || node->type_id != "adjustment.curves") return;

        const QString id = QString::number(static_cast<qulonglong>(node_id));
        const bool luminance = graph_boolean_parameter(*node, "luminance", true);
        const bool red_enabled = graph_boolean_parameter(*node, "red", false);
        const bool green_enabled = graph_boolean_parameter(*node, "green", false);
        const bool blue_enabled = graph_boolean_parameter(*node, "blue", false);
        auto* luminance_mode = inspector_content_->findChild<QRadioButton*>(
            QStringLiteral("GraphEffectCurvesModeLuminance.%1").arg(id));
        auto* rgb_mode = inspector_content_->findChild<QRadioButton*>(
            QStringLiteral("GraphEffectCurvesModeRgb.%1").arg(id));
        auto* red = inspector_content_->findChild<QCheckBox*>(
            QStringLiteral("GraphEffectParameter.%1.red").arg(id));
        auto* green = inspector_content_->findChild<QCheckBox*>(
            QStringLiteral("GraphEffectParameter.%1.green").arg(id));
        auto* blue = inspector_content_->findChild<QCheckBox*>(
            QStringLiteral("GraphEffectParameter.%1.blue").arg(id));
        if (luminance_mode != nullptr) {
            const QSignalBlocker blocker(luminance_mode);
            luminance_mode->setChecked(luminance);
        }
        if (rgb_mode != nullptr) {
            const QSignalBlocker blocker(rgb_mode);
            rgb_mode->setChecked(!luminance);
        }
        for (const auto& [editor, checked] :
             {std::pair<QCheckBox*, bool>{red, red_enabled},
              std::pair<QCheckBox*, bool>{green, green_enabled},
              std::pair<QCheckBox*, bool>{blue, blue_enabled}}) {
            if (editor == nullptr) continue;
            const QSignalBlocker blocker(editor);
            editor->setChecked(checked);
            editor->setEnabled(!luminance);
        }
        auto* curve = find_widget<CurveEditorWidget>(inspector_content_,
            QStringLiteral("GraphEffectCurvesGraph.%1").arg(id));
        if (curve != nullptr) {
            curve->set_targets(luminance, red_enabled, green_enabled, blue_enabled);
        }
    }

    void sync_curves_points_editor(GraphEffectNodeId node_id) {
        if (!inspected_node_.has_value() || *inspected_node_ != node_id) return;
        const GraphEffectNode* node = find_graph_effect_node(graph_, node_id);
        if (node == nullptr || node->type_id != "adjustment.curves") return;
        const QString id = QString::number(static_cast<qulonglong>(node_id));
        auto* curve = find_widget<CurveEditorWidget>(inspector_content_,
            QStringLiteral("GraphEffectCurvesGraph.%1").arg(id));
        if (curve == nullptr) return;
        const std::vector<double> x = graph_array_parameter(*node, "curve_x", {0.0, 1.0});
        const std::vector<double> y = graph_array_parameter(*node, "curve_y", {0.0, 1.0});
        (void)curve->set_curve_points(x, y);
    }

    void remove_selected_nodes() {
        const std::vector<GraphEffectNodeId> selected = scene_->selected_node_ids();
        if (selected.empty()) return;
        bool changed = false;
        for (GraphEffectNodeId node_id : selected) {
            changed = remove_graph_effect_node(graph_, node_id) || changed;
        }
        if (!changed) return;
        inspected_node_.reset();
        rebuild_scene(std::nullopt);
        rebuild_inspector(std::nullopt);
        mark_graph_changed(true);
        publish_status(GraphEffectWidget::tr("Selected nodes removed"), false);
    }

    void mark_graph_changed(bool evaluate) {
        if (rebuilding_scene_) return;
        dirty_ = true;
        owner_.setProperty("graphDirty", true);
        if (owner_.graph_changed) owner_.graph_changed();
        if (evaluate) owner_.schedule_preview();
    }

    void publish_status(const QString& message, bool error) {
        status_label_->setText(message);
        status_label_->setStyleSheet(error
            ? QStringLiteral("QLabel { color: #ff7777; background: #3b2428; }")
            : QStringLiteral("QLabel { color: #d9dce5; background: #292c33; }"));
        owner_.setProperty("lastError", error ? message : QString());
        if (owner_.status_changed) owner_.status_changed(message);
    }

    void evaluate_preview() {
        if (!source_document_.has_value()) {
            evaluation_ = {};
            update_specialized_inspector_histograms();
            preview_->clear();
            apply_action_->setEnabled(false);
            owner_.setProperty("previewAvailable", false);
            publish_status(GraphEffectWidget::tr("Waiting for a source image"), false);
            return;
        }

        std::optional<GraphEffectNodeId> histogram_node;
        if (inspected_node_.has_value()) {
            const GraphEffectNode* inspected = find_graph_effect_node(graph_, *inspected_node_);
            if (inspected != nullptr && (inspected->type_id == "adjustment.levels" ||
                                         inspected->type_id == "adjustment.curves")) {
                histogram_node = *inspected_node_;
            }
        }
        evaluation_ = evaluate_graph_effect(graph_, *source_document_, histogram_node);
        update_specialized_inspector_histograms();
        const std::size_t expected = evaluation_.width > 0 && evaluation_.height > 0
            ? static_cast<std::size_t>(evaluation_.width) *
                  static_cast<std::size_t>(evaluation_.height)
            : 0U;
        const bool valid = evaluation_.success && expected != 0U &&
                           evaluation_.pixels.size() == expected;
        apply_action_->setEnabled(valid);
        owner_.setProperty("previewAvailable", valid);
        const int revision = owner_.property("previewRevision").toInt() + 1;
        owner_.setProperty("previewRevision", revision);
        if (!valid) {
            preview_->clear();
            QString message = diagnostic_text(evaluation_.diagnostics);
            if (message.isEmpty()) message = GraphEffectWidget::tr("The graph could not be evaluated.");
            publish_status(message, true);
            return;
        }

        preview_->set_pixels(evaluation_.pixels, evaluation_.width, evaluation_.height);
        publish_status(GraphEffectWidget::tr("Live preview updated · %1 × %2")
                           .arg(evaluation_.width)
                           .arg(evaluation_.height),
                       false);
    }

    GraphEffectWidget& owner_;
    GraphEffectGraph graph_;
    std::optional<Document> source_document_;
    GraphEffectEvaluation evaluation_;
    QToolBar* toolbar_ = nullptr;
    QAction* new_action_ = nullptr;
    QAction* save_action_ = nullptr;
    QAction* load_action_ = nullptr;
    QAction* apply_action_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QTreeWidget* palette_ = nullptr;
    GraphScene* scene_ = nullptr;
    GraphView* view_ = nullptr;
    QScrollArea* inspector_scroll_ = nullptr;
    QWidget* inspector_content_ = nullptr;
    QFormLayout* inspector_form_ = nullptr;
    GraphPreview* preview_ = nullptr;
    QLabel* status_label_ = nullptr;
    QTimer* preview_timer_ = nullptr;
    std::optional<GraphEffectNodeId> inspected_node_;
    QString graph_path_;
    bool dirty_ = false;
    bool rebuilding_scene_ = false;
    int palette_add_offset_ = 0;
};

