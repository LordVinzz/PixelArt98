void GraphEffectWidgetTests::levels_inspector_uses_custom_port_histograms_and_parameter_grid() {
        GraphEffectWidget widget;
        widget.resize(1400, 850);
        widget.show();

        const std::optional<GraphAnchors> anchors = graph_anchors(widget.graph());
        const GraphEffectNodeSpec* brightness_spec =
            find_graph_effect_node_spec("adjustment.brightness-contrast");
        const GraphEffectNodeSpec* levels_spec =
            find_graph_effect_node_spec("adjustment.levels");
        QVERIFY(anchors.has_value());
        QVERIFY(brightness_spec != nullptr);
        QVERIFY(levels_spec != nullptr);

        QString error;
        GraphEffectNodeId brightness_id = 0;
        GraphEffectNodeId levels_id = 0;
        QVERIFY2(widget.add_node(brightness_spec->type_id, QPointF(-80.0, 80.0),
                                 &brightness_id, &error), qPrintable(error));
        QVERIFY2(widget.add_node(levels_spec->type_id, QPointF(140.0, 80.0),
                                 &levels_id, &error), qPrintable(error));
        QVERIFY2(widget.set_node_parameter(brightness_id, "brightness",
                                           std::int64_t{35}, &error), qPrintable(error));

        // Connect into Levels last so its real inspector remains selected while
        // the complete Source -> Brightness -> Levels -> Output graph is evaluated.
        QVERIFY2(widget.connect_nodes(levels_id, levels_spec->outputs.front().id,
                                      anchors->output_id,
                                      anchors->output_spec->inputs.front().id, &error),
                 qPrintable(error));
        QVERIFY2(widget.connect_nodes(anchors->source_id,
                                      anchors->source_spec->outputs.front().id,
                                      brightness_id, brightness_spec->inputs.front().id, &error),
                 qPrintable(error));
        QVERIFY2(widget.connect_nodes(brightness_id, brightness_spec->outputs.front().id,
                                      levels_id, levels_spec->inputs.front().id, &error),
                 qPrintable(error));

        const Document source = source_document();
        widget.set_source_document(source);
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);

        const QString id = QString::number(static_cast<qulonglong>(levels_id));
        auto* editor = widget.findChild<QWidget*>(
            QStringLiteral("GraphEffectLevelsEditor.%1").arg(id));
        auto* input_histogram = dynamic_cast<LevelsHistogramWidget*>(widget.findChild<QWidget*>(
            QStringLiteral("GraphEffectLevelsInputHistogram.%1").arg(id)));
        auto* output_histogram = dynamic_cast<LevelsHistogramWidget*>(widget.findChild<QWidget*>(
            QStringLiteral("GraphEffectLevelsOutputHistogram.%1").arg(id)));
        auto* parameter_grid = widget.findChild<QGridLayout*>(
            QStringLiteral("GraphEffectLevelsParametersGrid.%1").arg(id));
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->property("customEditorType").toString(), QStringLiteral("levels"));
        QVERIFY(input_histogram != nullptr);
        QVERIFY(output_histogram != nullptr);
        const QList<QWidget*> level_children = editor->findChildren<QWidget*>();
        QCOMPARE(std::count_if(level_children.begin(), level_children.end(), [](QWidget* child) {
                     return dynamic_cast<LevelsHistogramWidget*>(child) != nullptr;
                 }),
                 qsizetype{2});
        QVERIFY(parameter_grid != nullptr);

        for (const GraphEffectParameterSpec& parameter : levels_spec->parameters) {
            const QString name = QStringLiteral("GraphEffectParameter.%1.%2")
                                     .arg(id, QString::fromStdString(parameter.id));
            const QList<QWidget*> controls = widget.findChildren<QWidget*>(name);
            QCOMPARE(controls.size(), qsizetype{1});
            QVERIFY2(editor->findChild<QWidget*>(name) == controls.front(),
                     qPrintable(QStringLiteral("Levels control is outside the custom grid: %1")
                                    .arg(name)));
            QVERIFY2(parameter_grid->indexOf(controls.front()) >= 0,
                     qPrintable(QStringLiteral("Levels control is not managed by the custom grid: %1")
                                    .arg(name)));
        }

        QTRY_VERIFY_WITH_TIMEOUT(editor->property("histogramAvailable").toBool(),
                                 kPreviewTimeoutMilliseconds);
        QVERIFY(input_histogram->property("histogramAvailable").toBool());
        QVERIFY(output_histogram->property("histogramAvailable").toBool());
        QCOMPARE(input_histogram->property("histogramBins").toInt(), 256);
        QCOMPARE(output_histogram->property("histogramBins").toInt(), 256);
        QCOMPARE(input_histogram->property("histogramSampleCount").toLongLong(),
                 static_cast<qlonglong>(source.active_cel().pixels.size()));

        Document after_brightness = source;
        apply_brightness_contrast(after_brightness, 35, 0);
        const qlonglong source_checksum = static_cast<qlonglong>(
            luminance_histogram_checksum(source.active_cel().pixels));
        const qlonglong input_checksum = static_cast<qlonglong>(
            luminance_histogram_checksum(after_brightness.active_cel().pixels));
        QVERIFY(input_checksum != source_checksum);
        QCOMPARE(input_histogram->property("histogramChecksum").toLongLong(), input_checksum);
        QCOMPARE(output_histogram->property("histogramChecksum").toLongLong(), input_checksum);

        const qlonglong output_checksum_before =
            output_histogram->property("histogramChecksum").toLongLong();
        const int output_revision_before =
            output_histogram->property("histogramRevision").toInt();
        const int preview_revision_before = widget.property("previewRevision").toInt();
        QVERIFY2(widget.set_node_parameter(levels_id, "input_black",
                                           std::int64_t{120}, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(widget.property("previewRevision").toInt() >
                                     preview_revision_before,
                                 kPreviewTimeoutMilliseconds);
        QTRY_VERIFY_WITH_TIMEOUT(output_histogram->property("histogramRevision").toInt() >
                                     output_revision_before,
                                 kPreviewTimeoutMilliseconds);
        QCOMPARE(input_histogram->property("histogramChecksum").toLongLong(), input_checksum);
        QVERIFY(output_histogram->property("histogramChecksum").toLongLong() !=
                output_checksum_before);
        QVERIFY(validate_graph_effect(widget.graph(), GraphEffectValidationMode::Evaluable).ok());
    }

void GraphEffectWidgetTests::curves_inspector_uses_custom_graph_and_updates_point_arrays_atomically() {
        GraphEffectWidget widget;
        widget.resize(1400, 850);
        widget.show();
        const std::optional<GraphAnchors> anchors = graph_anchors(widget.graph());
        const GraphEffectNodeSpec* curves_spec =
            find_graph_effect_node_spec("adjustment.curves");
        QVERIFY(anchors.has_value());
        QVERIFY(curves_spec != nullptr);

        QString error;
        GraphEffectNodeId curves_id = 0;
        QVERIFY2(widget.add_node(curves_spec->type_id, QPointF(20.0, 120.0),
                                 &curves_id, &error), qPrintable(error));
        QVERIFY2(widget.connect_nodes(curves_id, curves_spec->outputs.front().id,
                                      anchors->output_id,
                                      anchors->output_spec->inputs.front().id, &error),
                 qPrintable(error));
        QVERIFY2(widget.connect_nodes(anchors->source_id,
                                      anchors->source_spec->outputs.front().id,
                                      curves_id, curves_spec->inputs.front().id, &error),
                 qPrintable(error));

        const Document source = source_document();
        widget.set_source_document(source);
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);

        const QString id = QString::number(static_cast<qulonglong>(curves_id));
        auto* editor = widget.findChild<QWidget*>(
            QStringLiteral("GraphEffectCurvesEditor.%1").arg(id));
        auto* curve = dynamic_cast<CurveEditorWidget*>(widget.findChild<QWidget*>(
            QStringLiteral("GraphEffectCurvesGraph.%1").arg(id)));
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->property("customEditorType").toString(), QStringLiteral("curves"));
        QVERIFY(curve != nullptr);
        QVERIFY(widget.findChild<QObject*>(
                    QStringLiteral("GraphEffectParameter.%1.curve_x").arg(id)) == nullptr);
        QVERIFY(widget.findChild<QObject*>(
                    QStringLiteral("GraphEffectParameter.%1.curve_y").arg(id)) == nullptr);
        for (QLineEdit* line_edit : editor->findChildren<QLineEdit*>()) {
            QVERIFY(!line_edit->objectName().contains(QStringLiteral("curve_x")));
            QVERIFY(!line_edit->objectName().contains(QStringLiteral("curve_y")));
        }

        QTRY_VERIFY_WITH_TIMEOUT(curve->property("histogramAvailable").toBool(),
                                 kPreviewTimeoutMilliseconds);
        QCOMPARE(curve->property("histogramBins").toInt(), 256);
        QCOMPARE(curve->property("histogramSampleCount").toLongLong(),
                 static_cast<qlonglong>(source.active_cel().pixels.size()));
        QCOMPARE(curve->property("histogramChecksum").toLongLong(),
                 static_cast<qlonglong>(
                     luminance_histogram_checksum(source.active_cel().pixels)));

        const GraphEffectNode* node = find_graph_effect_node(widget.graph(), curves_id);
        QVERIFY(node != nullptr);
        const std::vector<double>* curve_x_before = curve_parameter(*node, "curve_x");
        const std::vector<double>* curve_y_before = curve_parameter(*node, "curve_y");
        QVERIFY(curve_x_before != nullptr);
        QVERIFY(curve_y_before != nullptr);
        QCOMPARE(curve_x_before->size(), std::size_t{2});
        QCOMPARE(curve_y_before->size(), std::size_t{2});

        int graph_change_count = 0;
        widget.graph_changed = [&graph_change_count] { ++graph_change_count; };
        const int preview_revision_before = widget.property("previewRevision").toInt();
        const QRect graph_rect = curve->rect().adjusted(14, 14, -14, -14);
        QVERIFY(graph_rect.width() > 20);
        QVERIFY(graph_rect.height() > 20);
        const QPoint added_point(
            graph_rect.left() + qRound(graph_rect.width() * 0.35),
            graph_rect.bottom() - qRound(graph_rect.height() * 0.75));
        QTest::mouseClick(curve, Qt::LeftButton, Qt::NoModifier, added_point);

        node = find_graph_effect_node(widget.graph(), curves_id);
        QVERIFY(node != nullptr);
        const std::vector<double>* curve_x_after = curve_parameter(*node, "curve_x");
        const std::vector<double>* curve_y_after = curve_parameter(*node, "curve_y");
        QVERIFY(curve_x_after != nullptr);
        QVERIFY(curve_y_after != nullptr);
        QCOMPARE(curve_x_after->size(), std::size_t{3});
        QCOMPARE(curve_y_after->size(), std::size_t{3});
        QCOMPARE(curve_x_after->size(), curve_y_after->size());
        QCOMPARE(curve->property("curvePointCount").toInt(), 3);
        QCOMPARE(graph_change_count, 1);
        QTRY_VERIFY_WITH_TIMEOUT(widget.property("previewRevision").toInt() >
                                     preview_revision_before,
                                 kPreviewTimeoutMilliseconds);
        QVERIFY(widget.preview_available());
        QVERIFY(validate_graph_effect(widget.graph(), GraphEffectValidationMode::Evaluable).ok());
    }

void GraphEffectWidgetTests::curves_inspector_keeps_luminance_and_rgb_targets_exclusive() {
        GraphEffectWidget widget;
        widget.resize(1400, 850);
        widget.show();
        widget.set_source_document(source_document());
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);

        QString error;
        GraphEffectNodeId curves_id = 0;
        QVERIFY2(widget.add_node("adjustment.curves", QPointF(20.0, 120.0),
                                 &curves_id, &error), qPrintable(error));
        const QString id = QString::number(static_cast<qulonglong>(curves_id));
        const QString prefix = QStringLiteral("GraphEffectParameter.%1.").arg(id);
        auto* luminance = widget.findChild<QRadioButton*>(
            QStringLiteral("GraphEffectCurvesModeLuminance.%1").arg(id));
        auto* rgb = widget.findChild<QRadioButton*>(
            QStringLiteral("GraphEffectCurvesModeRgb.%1").arg(id));
        auto* red = widget.findChild<QCheckBox*>(prefix + QStringLiteral("red"));
        auto* green = widget.findChild<QCheckBox*>(prefix + QStringLiteral("green"));
        auto* blue = widget.findChild<QCheckBox*>(prefix + QStringLiteral("blue"));
        QVERIFY(luminance != nullptr);
        QVERIFY(rgb != nullptr);
        QVERIFY(red != nullptr);
        QVERIFY(green != nullptr);
        QVERIFY(blue != nullptr);
        QVERIFY(luminance->isChecked());
        QVERIFY(!rgb->isChecked());
        QVERIFY(!red->isChecked());
        QVERIFY(!green->isChecked());
        QVERIFY(!blue->isChecked());

        rgb->click();
        QVERIFY(!luminance->isChecked());
        QVERIFY(rgb->isChecked());
        QVERIFY(red->isChecked());
        QVERIFY(green->isChecked());
        QVERIFY(blue->isChecked());
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);
        QTRY_VERIFY_WITH_TIMEOUT(widget.property("lastError").toString().isEmpty(),
                                 kPreviewTimeoutMilliseconds);
        QTest::qWait(100);
        const int rgb_revision = widget.property("previewRevision").toInt();

        green->click();
        blue->click();
        QTRY_VERIFY_WITH_TIMEOUT(widget.property("previewRevision").toInt() > rgb_revision,
                                 kPreviewTimeoutMilliseconds);
        const int single_channel_revision = widget.property("previewRevision").toInt();
        red->click();
        QVERIFY(red->isChecked());
        QVERIFY(!green->isChecked());
        QVERIFY(!blue->isChecked());
        QVERIFY(!luminance->isChecked());
        QTest::qWait(100);
        QCOMPARE(widget.property("previewRevision").toInt(), single_channel_revision);
        QVERIFY(widget.preview_available());
        QVERIFY(widget.property("lastError").toString().isEmpty());

        green->click();
        red->click();
        QVERIFY(!luminance->isChecked());
        QVERIFY(rgb->isChecked());
        QVERIFY(!red->isChecked());
        QVERIFY(green->isChecked());
        luminance->click();
        QVERIFY(luminance->isChecked());
        QVERIFY(!rgb->isChecked());
        QVERIFY(!red->isChecked());
        QVERIFY(!green->isChecked());
        QVERIFY(!blue->isChecked());

        QVERIFY2(widget.set_node_parameter(curves_id, "blue", true, &error), qPrintable(error));
        QVERIFY(!luminance->isChecked());
        QVERIFY(rgb->isChecked());
        QVERIFY(blue->isChecked());
        QVERIFY2(widget.set_node_parameter(curves_id, "blue", false, &error), qPrintable(error));
        QVERIFY(!luminance->isChecked());
        QVERIFY(rgb->isChecked());
        QVERIFY(blue->isChecked());
        QVERIFY(validate_graph_effect(widget.graph(), GraphEffectValidationMode::Evaluable).ok());
    }

void GraphEffectWidgetTests::generator_can_feed_second_mix_input() {
        GraphEffectWidget widget;
        widget.show();
        const Document source = source_document();
        widget.set_source_document(source);
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);
        const std::vector<Pixel> identity_preview = widget.preview_pixels();

        const std::optional<GraphAnchors> anchors = graph_anchors(widget.graph());
        const GraphEffectNodeSpec* generator = generator_spec();
        const GraphEffectNodeSpec* mix = mix_spec();
        QVERIFY(anchors.has_value());
        QVERIFY2(generator != nullptr, "The public catalog must expose an image generator");
        QVERIFY2(mix != nullptr, "The public catalog must expose a two-input mix node");
        QVERIFY(!generator->outputs.empty());
        QVERIFY(mix->inputs.size() >= 2U);
        QVERIFY(!mix->outputs.empty());

        QString error;
        GraphEffectNodeId generator_id = 0;
        GraphEffectNodeId mix_id = 0;
        QVERIFY2(widget.add_node(generator->type_id, QPointF(-180.0, 220.0),
                                 &generator_id, &error), qPrintable(error));
        QVERIFY2(widget.add_node(mix->type_id, QPointF(120.0, 100.0), &mix_id, &error),
                 qPrintable(error));
        QVERIFY2(widget.connect_nodes(anchors->source_id, anchors->source_spec->outputs.front().id,
                                      mix_id, mix->inputs[0].id, &error), qPrintable(error));
        QVERIFY2(widget.connect_nodes(generator_id, generator->outputs.front().id,
                                      mix_id, mix->inputs[1].id, &error), qPrintable(error));
        QVERIFY2(widget.connect_nodes(mix_id, mix->outputs.front().id, anchors->output_id,
                                      anchors->output_spec->inputs.front().id, &error),
                 qPrintable(error));

        QVERIFY(validate_graph_effect(widget.graph(), GraphEffectValidationMode::Evaluable).ok());
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available() &&
                                     widget.preview_pixels() != identity_preview,
                                 kPreviewTimeoutMilliseconds);
    }

void GraphEffectWidgetTests::cycle_connection_is_refused_without_mutating_graph() {
        GraphEffectWidget widget;
        const GraphEffectNodeSpec* effect = adjustment_spec();
        QVERIFY(effect != nullptr);
        QVERIFY(!effect->inputs.empty());
        QVERIFY(!effect->outputs.empty());

        QString error;
        GraphEffectNodeId first = 0;
        GraphEffectNodeId second = 0;
        QVERIFY2(widget.add_node(effect->type_id, QPointF(-80.0, 260.0), &first, &error),
                 qPrintable(error));
        QVERIFY2(widget.add_node(effect->type_id, QPointF(180.0, 260.0), &second, &error),
                 qPrintable(error));
        QVERIFY2(widget.connect_nodes(first, effect->outputs.front().id,
                                      second, effect->inputs.front().id, &error),
                 qPrintable(error));
        const GraphEffectGraph before = widget.graph();

        error.clear();
        QVERIFY(!widget.connect_nodes(second, effect->outputs.front().id,
                                      first, effect->inputs.front().id, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(graphs_equal(widget.graph(), before));
    }

void GraphEffectWidgetTests::graph_roundtrips_through_pxgraph_file() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("round-trip.pxgraph"));

        GraphEffectWidget source_widget;
        QString error;
        GraphEffectNodeId effect_id = 0;
        QVERIFY2(insert_adjustment(source_widget, &effect_id, &error), qPrintable(error));
        QVERIFY2(source_widget.set_node_enabled(effect_id, false, &error), qPrintable(error));

        const GraphEffectNodeSpec* color_spec =
            spec_with_parameter_kind(GraphEffectParameterKind::Color, "generator");
        QVERIFY2(color_spec != nullptr, "The catalog must expose a color parameter");
        const auto color_parameter = std::find_if(
            color_spec->parameters.begin(), color_spec->parameters.end(),
            [](const GraphEffectParameterSpec& parameter) {
                return parameter.kind == GraphEffectParameterKind::Color;
            });
        QVERIFY(color_parameter != color_spec->parameters.end());
        GraphEffectNodeId color_node_id = 0;
        QVERIFY2(source_widget.add_node(color_spec->type_id, QPointF(-120.0, 300.0),
                                        &color_node_id, &error), qPrintable(error));
        QVERIFY2(source_widget.set_node_parameter(color_node_id, color_parameter->id,
                                                  rgba(12, 34, 56, 78), &error),
                 qPrintable(error));

        const GraphEffectNodeSpec* array_spec =
            spec_with_parameter_kind(GraphEffectParameterKind::NumberArray, "curves");
        QVERIFY2(array_spec != nullptr, "The catalog must expose an array parameter");
        GraphEffectNodeId array_node_id = 0;
        QVERIFY2(source_widget.add_node(array_spec->type_id, QPointF(180.0, 300.0),
                                        &array_node_id, &error), qPrintable(error));
        for (const GraphEffectParameterSpec& parameter : array_spec->parameters) {
            if (parameter.kind != GraphEffectParameterKind::NumberArray) continue;
            const bool is_output_curve = contains_any_fragment(parameter.id, {"curve_y", "output"});
            const std::vector<double> points = is_output_curve
                ? std::vector<double>{0.0, 0.18, 0.86, 1.0}
                : std::vector<double>{0.0, 0.3, 0.72, 1.0};
            QVERIFY2(source_widget.set_node_parameter(array_node_id, parameter.id, points, &error),
                     qPrintable(error));
        }
        const GraphEffectGraph expected = source_widget.graph();

        QVERIFY2(source_widget.save_graph(path, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(path));

        GraphEffectWidget loaded_widget;
        int graph_change_count = 0;
        loaded_widget.graph_changed = [&graph_change_count] { ++graph_change_count; };
        QVERIFY2(loaded_widget.load_graph(path, &error), qPrintable(error));
        QCOMPARE(graph_change_count, 1);
        QVERIFY(graphs_equal(loaded_widget.graph(), expected));
    }

void GraphEffectWidgetTests::invalid_load_is_transactional() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("corrupt.pxgraph"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray invalid_format = QByteArrayLiteral(
            R"json({"format":"not-pixelart98-graph-effect","version":1,"name":"Corrupt","nodes":[],"links":[]})json");
        QCOMPARE(file.write(invalid_format), static_cast<qint64>(invalid_format.size()));
        file.close();

        GraphEffectWidget widget;
        QString error;
        GraphEffectNodeId effect_id = 0;
        QVERIFY2(insert_adjustment(widget, &effect_id, &error), qPrintable(error));
        const GraphEffectGraph before = widget.graph();
        int graph_change_count = 0;
        widget.graph_changed = [&graph_change_count] { ++graph_change_count; };

        error.clear();
        QVERIFY(!widget.load_graph(path, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(graph_change_count, 0);
        QVERIFY(graphs_equal(widget.graph(), before));
    }

void GraphEffectWidgetTests::removing_node_removes_all_incident_links() {
        GraphEffectWidget widget;
        QString error;
        GraphEffectNodeId effect_id = 0;
        QVERIFY2(insert_adjustment(widget, &effect_id, &error), qPrintable(error));
        const std::size_t node_count = widget.graph().nodes.size();
        QVERIFY(std::any_of(widget.graph().links.begin(), widget.graph().links.end(),
                            [effect_id](const GraphEffectLink& link) {
                                return link.from_node == effect_id || link.to_node == effect_id;
                            }));

        QVERIFY2(widget.remove_node(effect_id, &error), qPrintable(error));
        QCOMPARE(widget.graph().nodes.size(), node_count - 1U);
        QVERIFY(find_graph_effect_node(widget.graph(), effect_id) == nullptr);
        QVERIFY(std::none_of(widget.graph().links.begin(), widget.graph().links.end(),
                            [effect_id](const GraphEffectLink& link) {
                                return link.from_node == effect_id || link.to_node == effect_id;
                            }));
    }

void GraphEffectWidgetTests::apply_preview_invokes_owner_callback_with_current_pixels() {
        GraphEffectWidget widget;
        widget.show();
        const Document source = source_document();
        widget.set_source_document(source);
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);

        QString error;
        GraphEffectNodeId effect_id = 0;
        QVERIFY2(insert_adjustment(widget, &effect_id, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available() &&
                                     widget.preview_pixels() != source.active_cel().pixels,
                                 kPreviewTimeoutMilliseconds);
        const std::vector<Pixel> expected = widget.preview_pixels();

        int callback_count = 0;
        int callback_width = 0;
        int callback_height = 0;
        std::vector<Pixel> callback_pixels;
        widget.apply_requested = [&](const std::vector<Pixel>& pixels, int width, int height) {
            ++callback_count;
            callback_width = width;
            callback_height = height;
            callback_pixels = pixels;
        };

        QVERIFY(widget.apply_preview());
        QCOMPARE(callback_count, 1);
        QCOMPARE(callback_width, source.width);
        QCOMPARE(callback_height, source.height);
        QVERIFY(callback_pixels == expected);
    }
