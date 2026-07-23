void QtUiTests::edits_are_atomically_autosaved_for_crash_recovery() {
        const std::filesystem::path recovery_path = default_recovery_session_path();
        std::filesystem::remove(recovery_path);
        {
            QtMainWindow window(cpu_settings());
            window.replace_document_for_testing(patterned_document());
            QAction* select_all = nullptr;
            for (QAction* action : window.findChildren<QAction*>()) {
                if (action->text().remove(QLatin1Char('&')) == QStringLiteral("Select All")) {
                    select_all = action;
                    break;
                }
            }
            QVERIFY(select_all != nullptr);
            select_all->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(std::filesystem::exists(recovery_path), 8'000);

            ProjectBundle recovered;
            std::string error;
            QVERIFY2(load_recovery_project(recovery_path, recovered, &error), error.c_str());
            QVERIFY(recovered.document.selection.active);
            QVERIFY(recovered.document.undo_history_for_recovery().size() == 1U);
        }
        QVERIFY(!std::filesystem::exists(recovery_path));
    }

void QtUiTests::animation_export_dialogs_expose_format_specific_settings() {
        QTemporaryDir executable_directory;
        QVERIFY(executable_directory.isValid());
#if defined(Q_OS_WIN)
        const QString fake_ffmpeg = executable_directory.filePath(QStringLiteral("ffmpeg.exe"));
#else
        const QString fake_ffmpeg = executable_directory.filePath(QStringLiteral("ffmpeg"));
#endif
        QFile executable(fake_ffmpeg);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        QCOMPARE(executable.write("fake"), 4);
        executable.close();
        QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                          QFileDevice::ExeGroup));

        const bool had_override = qEnvironmentVariableIsSet("PIXELART_FFMPEG_PATH");
        const QByteArray previous_override = qgetenv("PIXELART_FFMPEG_PATH");
        QVERIFY(qputenv("PIXELART_FFMPEG_PATH", fake_ffmpeg.toUtf8()));

        QtMainWindow window(cpu_settings());
        QVERIFY(window.property("ffmpegAvailable").toBool());
        QCOMPARE(window.property("ffmpegExecutable").toString(), QFileInfo(fake_ffmpeg).absoluteFilePath());

        const auto inspect_dialog = [&window](const QString& action_name, bool gif) {
            QAction* action = window.findChild<QAction*>(action_name);
            QVERIFY(action != nullptr);
            bool inspected = false;
            QTimer::singleShot(0, &window, [&inspected, gif] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog != nullptr);
                QCOMPARE(dialog->objectName(), QStringLiteral("AnimationExportDialog"));
                QVERIFY(dialog->findChild<QSpinBox*>(QStringLiteral("AnimationExportScale")) != nullptr);
                QVERIFY(dialog->findChild<QCheckBox*>(
                            QStringLiteral("AnimationExportPreserveTiming")) != nullptr);
                QVERIFY(dialog->findChild<QDoubleSpinBox*>(QStringLiteral("AnimationExportFps")) != nullptr);
                if (gif) {
                    QVERIFY(dialog->findChild<QSpinBox*>(
                                QStringLiteral("AnimationExportGifColors")) != nullptr);
                    QVERIFY(dialog->findChild<QComboBox*>(
                                QStringLiteral("AnimationExportGifDither")) != nullptr);
                    QVERIFY(dialog->findChild<QComboBox*>(
                                QStringLiteral("AnimationExportGifLoop")) != nullptr);
                    QVERIFY(dialog->findChild<QSpinBox*>(
                                QStringLiteral("AnimationExportBitrate")) == nullptr);
                } else {
                    QVERIFY(dialog->findChild<QSpinBox*>(
                                QStringLiteral("AnimationExportBitrate")) != nullptr);
                    QVERIFY(dialog->findChild<QComboBox*>(
                                QStringLiteral("AnimationExportPreset")) != nullptr);
                    QVERIFY(dialog->findChild<QComboBox*>(
                                QStringLiteral("AnimationExportBackground")) != nullptr);
                    QVERIFY(dialog->findChild<QSpinBox*>(
                                QStringLiteral("AnimationExportGifColors")) == nullptr);
                }
                inspected = true;
                dialog->reject();
            });
            action->trigger();
            QVERIFY(inspected);
        };
        inspect_dialog(QStringLiteral("ExportAnimationGif"), true);
        inspect_dialog(QStringLiteral("ExportAnimationMp4"), false);

        if (had_override) QVERIFY(qputenv("PIXELART_FFMPEG_PATH", previous_override));
        else QVERIFY(qunsetenv("PIXELART_FFMPEG_PATH"));
    }

void QtUiTests::graph_workspace_hides_canvas_only_docks_and_restores_them() {
        QtMainWindow window(cpu_settings());
        window.show();
        QApplication::processEvents();

        auto* workspace = window.findChild<QTabWidget*>(QStringLiteral("MainWorkspaceTabs"));
        QVERIFY(workspace != nullptr);
        QCOMPARE(workspace->currentIndex(), 0);

        const std::array<const char*, 6> canvas_only_dock_names = {
            "ToolsDock", "ColorsDock", "ModelPreviewDock", "TilePreviewDock", "ModelDock",
            "TextDock"};
        std::array<QDockWidget*, 6> canvas_only_docks{};
        for (std::size_t index = 0; index < canvas_only_dock_names.size(); ++index) {
            canvas_only_docks[index] = window.findChild<QDockWidget*>(
                QString::fromLatin1(canvas_only_dock_names[index]));
            QVERIFY2(canvas_only_docks[index] != nullptr, canvas_only_dock_names[index]);
            canvas_only_docks[index]->show();
            QVERIFY(!canvas_only_docks[index]->isHidden());
        }
        auto* layers = window.findChild<QDockWidget*>(QStringLiteral("LayersDock"));
        QVERIFY(layers != nullptr);
        layers->show();

        workspace->setCurrentIndex(1);
        QApplication::processEvents();
        for (QDockWidget* dock : canvas_only_docks) QVERIFY(dock->isHidden());
        QVERIFY(!layers->isHidden());

        workspace->setCurrentIndex(0);
        QApplication::processEvents();
        for (QDockWidget* dock : canvas_only_docks) QVERIFY(!dock->isHidden());
        QVERIFY(!layers->isHidden());

        // A panel already hidden by the user stays hidden after a round trip.
        canvas_only_docks[3]->hide();
        workspace->setCurrentIndex(1);
        QApplication::processEvents();
        workspace->setCurrentIndex(0);
        QApplication::processEvents();
        QVERIFY(canvas_only_docks[3]->isHidden());
        for (std::size_t index = 0; index < canvas_only_docks.size(); ++index) {
            if (index != 3U) QVERIFY(!canvas_only_docks[index]->isHidden());
        }

        // Closing on GraphEffect must persist the canvas layout, not the
        // temporary all-hidden state.
        for (QDockWidget* dock : canvas_only_docks) dock->show();
        workspace->setCurrentIndex(1);
        QApplication::processEvents();
        QVERIFY(window.close());

        QtMainWindow restored(cpu_settings());
        restored.show();
        QApplication::processEvents();
        for (const char* name : canvas_only_dock_names) {
            QDockWidget* dock = restored.findChild<QDockWidget*>(QString::fromLatin1(name));
            QVERIFY2(dock != nullptr, name);
            QVERIFY2(!dock->isHidden(), name);
        }
    }

void QtUiTests::canvas_reports_pointer_and_live_rectangular_selection() {
        QtMainWindow window(cpu_settings());
        Document document = patterned_document();
        document.selection.select_rect(2, 3, 6, 8, SelectionCombineMode::Replace);
        window.replace_document_for_testing(std::move(document));
        window.show();
        QApplication::processEvents();

        auto* canvas = dynamic_cast<QtCanvasWidget*>(
            window.findChild<QWidget*>(QStringLiteral("CanvasWidget")));
        auto* pointer = window.findChild<QLabel*>(QStringLiteral("PointerCoordinatesLabel"));
        auto* selection = window.findChild<QLabel*>(QStringLiteral("SelectionGeometryLabel"));
        QVERIFY(canvas != nullptr);
        QVERIFY(pointer != nullptr);
        QVERIFY(selection != nullptr);
        QCOMPARE(selection->text(), QStringLiteral("Selection: (2, 3) → (6, 8) · 5 × 6"));
        QTest::mouseMove(canvas, canvas->rect().center());
        QTRY_VERIFY(!pointer->text().contains(QChar(0x2014)));

        auto* tools = window.findChild<QButtonGroup*>();
        QVERIFY(tools != nullptr);
        QAbstractButton* rectangle_select = tools->button(static_cast<int>(ToolType::RectSelect));
        QVERIFY(rectangle_select != nullptr);
        rectangle_select->click();
        const QPoint start = canvas->rect().center() - QPoint(30, 18);
        const QPoint end = start + QPoint(48, 36);
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end);
        QTRY_VERIFY(selection->text().endsWith(QStringLiteral("· 5 × 4")));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    }

void QtUiTests::history_selection_transform_and_brush_controls_are_real() {
        QtMainWindow window(cpu_settings());
        window.replace_document_for_testing(Document::create(16, 16));
        window.show();
        QApplication::processEvents();

        auto* canvas = dynamic_cast<QtCanvasWidget*>(
            window.findChild<QWidget*>(QStringLiteral("CanvasWidget")));
        auto* history = window.findChild<QListWidget*>(QStringLiteral("HistoryList"));
        auto* history_undo = window.findChild<QPushButton*>(QStringLiteral("HistoryUndo"));
        auto* history_redo = window.findChild<QPushButton*>(QStringLiteral("HistoryRedo"));
        QVERIFY(canvas != nullptr);
        QVERIFY(history != nullptr);
        QVERIFY(history_undo != nullptr);
        QVERIFY(history_redo != nullptr);
        QCOMPARE(history->count(), 1);
        QCOMPARE(history->item(0)->text(), QStringLiteral("History baseline"));

        canvas->setFocus();
        QTest::keySequence(canvas, QKeySequence(QKeySequence::SelectAll));
        QTRY_COMPARE(history->count(), 2);
        QVERIFY(history->item(1)->text().contains(QStringLiteral("Select All")));
        QVERIFY(window.document().selection.active);
        history->setCurrentRow(0);
        QTRY_VERIFY(!window.document().selection.active);
        QCOMPARE(history->currentRow(), 0);
        QVERIFY(history->item(1)->font().italic());
        history->setCurrentRow(1);
        QTRY_VERIFY(window.document().selection.active);
        QCOMPARE(history->currentRow(), 1);

        QVERIFY(window.findChild<QMenu*>(QStringLiteral("SelectionMenu")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("SelectionExpand")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("SelectionContract")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("SelectionBorder")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("SelectionSmooth")) != nullptr);

        auto* tools = window.findChild<QButtonGroup*>();
        QVERIFY(tools != nullptr);
        QAbstractButton* ellipse_select = tools->button(static_cast<int>(ToolType::EllipseSelect));
        QAbstractButton* move_pixels = tools->button(static_cast<int>(ToolType::MovePixels));
        QAbstractButton* brush = tools->button(static_cast<int>(ToolType::Brush));
        QVERIFY(ellipse_select != nullptr);
        QVERIFY(move_pixels != nullptr);
        QVERIFY(brush != nullptr);

        ellipse_select->click();
        QCOMPARE(ellipse_select->isChecked(), true);
        move_pixels->click();
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("SelectionTransformHelp")) == nullptr);
        QVERIFY(window.findChild<QDoubleSpinBox*>(QStringLiteral("SelectionScaleX")) != nullptr);
        QVERIFY(window.findChild<QDoubleSpinBox*>(QStringLiteral("SelectionScaleY")) != nullptr);
        QVERIFY(window.findChild<QDoubleSpinBox*>(QStringLiteral("SelectionRotation")) != nullptr);
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("SelectionTransformApply")) != nullptr);

        brush->click();
        QApplication::processEvents();
        auto* tools_dock =
            window.findChild<QDockWidget*>(QStringLiteral("ToolsDock"));
        auto* tool_options =
            window.findChild<QWidget*>(QStringLiteral("ToolOptionsPanel"));
        auto* pressure_size =
            window.findChild<QCheckBox*>(QStringLiteral("BrushPressureSize"));
        auto* pressure_opacity =
            window.findChild<QCheckBox*>(QStringLiteral("BrushPressureOpacity"));
        QVERIFY(tools_dock != nullptr);
        QVERIFY(tool_options != nullptr);
        QVERIFY(pressure_size != nullptr);
        QVERIFY(pressure_opacity != nullptr);
        QVERIFY(tools_dock->width() >= tools_dock->minimumWidth());
        QVERIFY(tool_options->width() >= tool_options->minimumWidth());
        QVERIFY(pressure_size->width() >= pressure_size->sizeHint().width());
        QVERIFY(pressure_opacity->width() >= pressure_opacity->sizeHint().width());
        QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("BrushOpacity")) != nullptr);
        QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("BrushHardness")) != nullptr);
        QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("BrushSpacing")) != nullptr);
        QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("BrushSmoothing")) != nullptr);
    }

void QtUiTests::selection_transform_frame_does_not_cover_floating_content() {
        const QColor content_color(190, 35, 55, 255);
        QImage rendered(120, 120, QImage::Format_RGBA8888);
        rendered.fill(content_color);
        {
            QPainter painter(&rendered);
            QtCanvasWidget::paint_selection_transform_frame(
                painter, QRectF(20.0, 30.0, 80.0, 70.0));
        }
        QCOMPARE(rendered.pixelColor(QPoint(60, 65)), content_color);
    }

void QtUiTests::layer_visibility_is_synchronized_in_both_directions() {
        QtMainWindow window(cpu_settings());
        Document document = patterned_document();
        document.add_layer("Second");
        document.active_layer = 0;
        window.replace_document_for_testing(std::move(document));
        auto* layers = window.findChild<QListWidget*>(QStringLiteral("LayersList"));
        auto* visible = window.findChild<QCheckBox*>(QStringLiteral("LayerVisible"));
        QVERIFY(layers != nullptr);
        QVERIFY(visible != nullptr);
        layers->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(!window.document().layers[0].visible);
        QVERIFY(!visible->isChecked());
        visible->setChecked(true);
        QVERIFY(window.document().layers[0].visible);
        QCOMPARE(layers->item(0)->checkState(), Qt::Checked);
        layers->setCurrentRow(1);
        layers->item(1)->setCheckState(Qt::Unchecked);
        QVERIFY(!window.document().layers[1].visible);
        QCOMPARE(layers->currentRow(), 1);
    }

void QtUiTests::clipboard_keeps_pixels_floating_until_deselect() {
        QtMainWindow window(cpu_settings());
        Document source = patterned_document();
        source.selection.select_rect(3, 4, 5, 6, SelectionCombineMode::Replace);
        const std::vector<Pixel> expected = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));
        auto* copy = window.findChild<QAction*>(QStringLiteral("EditCopy"));
        auto* paste = window.findChild<QAction*>(QStringLiteral("EditPaste"));
        auto* deselect = window.findChild<QAction*>(QStringLiteral("EditDeselect"));
        QVERIFY(copy != nullptr);
        QVERIFY(paste != nullptr);
        QVERIFY(deselect != nullptr);
        QCOMPARE(copy->shortcut(), QKeySequence(QKeySequence::Copy));
        copy->trigger();
        window.replace_document_for_testing(Document::create(16, 16));
        paste->trigger();
        QVERIFY(window.document().floating_selection.active);
        QCOMPARE(window.document().floating_selection.source_x, 3);
        QCOMPARE(window.document().floating_selection.source_y, 4);
        QCOMPARE(window.document().active_cel().pixels[static_cast<std::size_t>(window.document().pixel_index(3, 4))], Pixel{0});
        deselect->trigger();
        QVERIFY(!window.document().floating_selection.active);
        for (int y = 4; y <= 6; ++y) {
            for (int x = 3; x <= 5; ++x) {
                const std::size_t index = static_cast<std::size_t>(window.document().pixel_index(x, y));
                QCOMPARE(window.document().active_cel().pixels[index], expected[index]);
            }
        }
    }

void QtUiTests::levels_dialog_has_live_histograms_and_parameter_grid() {
        QtMainWindow window(cpu_settings());
        window.replace_document_for_testing(patterned_document());
        auto* action = window.findChild<QAction*>(QStringLiteral("adjustment.levels"));
        QVERIFY(action != nullptr);
        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.levels"));
            QVERIFY(dialog != nullptr);
            auto* input = dialog->findChild<QWidget*>(QStringLiteral("LevelsInputHistogram"));
            auto* output = dialog->findChild<QWidget*>(QStringLiteral("LevelsOutputHistogram"));
            auto* grid = dialog->findChild<QGridLayout*>(QStringLiteral("LevelsParametersGrid"));
            QVERIFY(input != nullptr);
            QVERIFY(output != nullptr);
            QVERIFY(grid != nullptr);
            QCOMPARE(grid->rowCount(), 5);
            QCOMPARE(grid->columnCount(), 3);
            const int revision = output->property("histogramRevision").toInt();
            auto* black = dialog->findChild<QSlider*>(QStringLiteral("AdjustmentControl.input-black"));
            QVERIFY(black != nullptr);
            black->setValue(80);
            QVERIFY(output->property("histogramRevision").toInt() > revision);
            dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"))->click();
        });
        action->trigger();
    }

void QtUiTests::curves_dialog_starts_as_a_true_identity_and_can_be_edited() {
        QtMainWindow window(cpu_settings());
        window.replace_document_for_testing(patterned_document());
        auto* action = window.findChild<QAction*>(QStringLiteral("adjustment.curves"));
        QVERIFY(action != nullptr);
        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.curves"));
            auto* graph = dialog == nullptr ? nullptr : dialog->findChild<QWidget*>(QStringLiteral("CurvesGraph"));
            QVERIFY(dialog != nullptr);
            QVERIFY(graph != nullptr);
            QCOMPARE(graph->property("curvePointCount").toInt(), 3);
            QCOMPARE(graph->property("curveQuarterOutput").toInt(), 250);
            auto* luminance = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesMode.luminance"));
            auto* rgb = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesMode.rgb"));
            auto* red = dialog->findChild<QCheckBox*>(QStringLiteral("CurvesChannel.red"));
            auto* green = dialog->findChild<QCheckBox*>(QStringLiteral("CurvesChannel.green"));
            auto* blue = dialog->findChild<QCheckBox*>(QStringLiteral("CurvesChannel.blue"));
            QVERIFY(luminance != nullptr);
            QVERIFY(rgb != nullptr);
            QVERIFY(red != nullptr);
            QVERIFY(green != nullptr);
            QVERIFY(blue != nullptr);
            QVERIFY(luminance->isChecked());
            QVERIFY(!rgb->isChecked());
            QVERIFY(!red->isEnabled());
            QVERIFY(!green->isEnabled());
            QVERIFY(!blue->isEnabled());
            rgb->click();
            QVERIFY(!luminance->isChecked());
            QVERIFY(rgb->isChecked());
            QVERIFY(red->isEnabled());
            QVERIFY(green->isEnabled());
            QVERIFY(blue->isEnabled());
            green->click();
            blue->click();
            QCOMPARE(graph->property("channel").toInt(), 1);
            QVERIFY(graph->property("redEnabled").toBool());
            QVERIFY(!graph->property("greenEnabled").toBool());
            QVERIFY(!graph->property("blueEnabled").toBool());
            red->click();
            QVERIFY(red->isChecked());
            QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier,
                              QPoint(graph->width() * 3 / 4, graph->height() / 3));
            QCOMPARE(graph->property("curvePointCount").toInt(), 4);
            dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"))->click();
        });
        action->trigger();
    }

void QtUiTests::graph_effect_tab_previews_round_trips_and_applies() {
        QtMainWindow window(cpu_settings());
        Document document = patterned_document();
        const std::vector<Pixel> before = document.active_cel().pixels;
        window.replace_document_for_testing(std::move(document));
        auto* workspace = window.findChild<QTabWidget*>(QStringLiteral("MainWorkspaceTabs"));
        auto* graph_widget = dynamic_cast<GraphEffectWidget*>(
            window.findChild<QWidget*>(QStringLiteral("GraphEffectWidget")));
        QVERIFY(workspace != nullptr);
        QVERIFY(graph_widget != nullptr);
        workspace->setCurrentWidget(graph_widget);
        window.show();
        QApplication::processEvents();
        QTRY_VERIFY(graph_widget->preview_available());
        QVERIFY(graph_widget->preview_pixels() == before);

        const GraphEffectNodeSpec* brightness_spec = nullptr;
        for (const GraphEffectNodeSpec& spec : graph_effect_catalog()) {
            const bool has_brightness = std::any_of(spec.parameters.begin(), spec.parameters.end(),
                [](const GraphEffectParameterSpec& parameter) { return parameter.id == "brightness"; });
            if (has_brightness && spec.inputs.size() == 1U && spec.outputs.size() == 1U) {
                brightness_spec = &spec;
                break;
            }
        }
        QVERIFY(brightness_spec != nullptr);
        GraphEffectNodeId source_id = 0;
        GraphEffectNodeId output_id = 0;
        for (const GraphEffectNode& node : graph_widget->graph().nodes) {
            const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node.type_id);
            if (spec != nullptr && spec->category == "Input") source_id = node.id;
            if (spec != nullptr && spec->category == "Output") output_id = node.id;
        }
        QVERIFY(source_id != 0);
        QVERIFY(output_id != 0);

        GraphEffectNodeId effect_id = 0;
        QString error;
        QVERIFY(graph_widget->add_node(brightness_spec->type_id, QPointF(0.0, 120.0), &effect_id, &error));
        const GraphEffectNodeSpec* source_spec = find_graph_effect_node_spec(
            find_graph_effect_node(graph_widget->graph(), source_id)->type_id);
        const GraphEffectNodeSpec* output_spec = find_graph_effect_node_spec(
            find_graph_effect_node(graph_widget->graph(), output_id)->type_id);
        QVERIFY(source_spec != nullptr);
        QVERIFY(output_spec != nullptr);
        QVERIFY(graph_widget->connect_nodes(source_id, source_spec->outputs.front().id,
                                            effect_id, brightness_spec->inputs.front().id, &error));
        QVERIFY(graph_widget->connect_nodes(effect_id, brightness_spec->outputs.front().id,
                                            output_id, output_spec->inputs.front().id, &error));
        const int edited_revision = graph_widget->property("previewRevision").toInt();
        QVERIFY(graph_widget->set_node_parameter(effect_id, "brightness", std::int64_t{60}, &error));
        QTRY_VERIFY(graph_widget->property("previewRevision").toInt() > edited_revision);
        QVERIFY(graph_widget->preview_available());
        QVERIFY(graph_widget->preview_pixels() != before);

        QTemporaryDir graph_directory;
        QVERIFY(graph_directory.isValid());
        const QString graph_path = graph_directory.filePath(QStringLiteral("live-preview.pxgraph"));
        const std::size_t saved_node_count = graph_widget->graph().nodes.size();
        QVERIFY(graph_widget->save_graph(graph_path, &error));
        graph_widget->reset_graph();
        QCOMPARE(graph_widget->graph().nodes.size(), std::size_t{2});
        const int loaded_revision = graph_widget->property("previewRevision").toInt();
        QVERIFY(graph_widget->load_graph(graph_path, &error));
        QCOMPARE(graph_widget->graph().nodes.size(), saved_node_count);
        QTRY_VERIFY(graph_widget->property("previewRevision").toInt() > loaded_revision);
        QVERIFY(graph_widget->preview_available());
        const std::vector<Pixel> expected = graph_widget->preview_pixels();
        QVERIFY(graph_widget->apply_preview());
        QVERIFY(window.document().active_cel().pixels == expected);
        QCOMPARE(workspace->currentWidget(), static_cast<QWidget*>(
            window.findChild<QWidget*>(QStringLiteral("CanvasWidget"))));
    }

void QtUiTests::graph_effect_source_refresh_is_deferred_until_the_tab_is_visible() {
        QtMainWindow window(cpu_settings());
        auto* workspace = window.findChild<QTabWidget*>(QStringLiteral("MainWorkspaceTabs"));
        auto* canvas = window.findChild<QWidget*>(QStringLiteral("CanvasWidget"));
        auto* graph_widget = dynamic_cast<GraphEffectWidget*>(
            window.findChild<QWidget*>(QStringLiteral("GraphEffectWidget")));
        QVERIFY(workspace != nullptr);
        QVERIFY(canvas != nullptr);
        QVERIFY(graph_widget != nullptr);

        Document initial = patterned_document();
        const Pixel initial_pixel = initial.active_cel().pixels.front();
        window.replace_document_for_testing(std::move(initial));
        workspace->setCurrentWidget(graph_widget);
        window.show();
        QTRY_VERIFY(graph_widget->preview_available());
        QTRY_COMPARE(graph_widget->preview_pixels().front(), initial_pixel);
        QTest::qWait(100);
        const int initial_revision = graph_widget->property("previewRevision").toInt();

        workspace->setCurrentWidget(canvas);
        Document replacement = patterned_document();
        const Pixel replacement_pixel = rgba(7, 31, 197, 255);
        replacement.active_cel().pixels.front() = replacement_pixel;
        window.replace_document_for_testing(std::move(replacement));
        QTest::qWait(100);
        QCOMPARE(graph_widget->property("previewRevision").toInt(), initial_revision);
        QCOMPARE(graph_widget->preview_pixels().front(), initial_pixel);

        workspace->setCurrentWidget(graph_widget);
        QTRY_VERIFY(graph_widget->property("previewRevision").toInt() > initial_revision);
        QTRY_COMPARE(graph_widget->preview_pixels().front(), replacement_pixel);
        workspace->setCurrentWidget(canvas);
    }

void QtUiTests::zoom_shortcuts_only_affect_the_active_workspace_tab() {
        QtMainWindow window(cpu_settings());
        auto* workspace = window.findChild<QTabWidget*>(QStringLiteral("MainWorkspaceTabs"));
        auto* canvas = dynamic_cast<QtCanvasWidget*>(
            window.findChild<QWidget*>(QStringLiteral("CanvasWidget")));
        auto* graph_widget = dynamic_cast<GraphEffectWidget*>(
            window.findChild<QWidget*>(QStringLiteral("GraphEffectWidget")));
        auto* graph_view = window.findChild<QGraphicsView*>(QStringLiteral("GraphEffectView"));
        QVERIFY(workspace != nullptr);
        QVERIFY(canvas != nullptr);
        QVERIFY(graph_widget != nullptr);
        QVERIFY(graph_view != nullptr);

        window.show();
        QApplication::processEvents();

        workspace->setCurrentWidget(canvas);
        canvas->setFocus();
        const double initial_canvas_zoom = canvas->zoom();
        const QTransform initial_graph_transform = graph_view->transform();

        QTest::keySequence(canvas, QKeySequence(QKeySequence::ZoomIn));
        QTRY_VERIFY(canvas->zoom() > initial_canvas_zoom);
        QCOMPARE(graph_view->transform(), initial_graph_transform);

        const double zoomed_canvas = canvas->zoom();
        QTest::keySequence(canvas, QKeySequence(QKeySequence::ZoomOut));
        QTRY_VERIFY(canvas->zoom() < zoomed_canvas);
        QCOMPARE(graph_view->transform(), initial_graph_transform);

        workspace->setCurrentWidget(graph_widget);
        graph_view->setFocus();
        graph_widget->actual_size();
        const double canvas_zoom_before_graph_shortcuts = canvas->zoom();
        const qreal initial_graph_scale = graph_view->transform().m11();

        QTest::keySequence(graph_view, QKeySequence(QKeySequence::ZoomIn));
        QTRY_VERIFY(graph_view->transform().m11() > initial_graph_scale);
        QCOMPARE(canvas->zoom(), canvas_zoom_before_graph_shortcuts);

        const qreal zoomed_graph_scale = graph_view->transform().m11();
        QTest::keySequence(graph_view, QKeySequence(QKeySequence::ZoomOut));
        QTRY_VERIFY(graph_view->transform().m11() < zoomed_graph_scale);
        QCOMPARE(canvas->zoom(), canvas_zoom_before_graph_shortcuts);
    }

void QtUiTests::configurable_adjustments_preview_on_cpu_and_cancel_cleanly() {
        struct Case { const char* id; const char* control; };
        const std::array<Case, 5> cases = {{{"brightness-contrast", "brightness"}, {"hsv", "hue"},
                                            {"temperature", "temperature"}, {"tonal-range", "shadows"},
                                            {"posterize", "levels"}}};
        QtMainWindow window(cpu_settings());
        for (const Case& test : cases) {
            window.replace_document_for_testing(patterned_document());
            auto* action = window.findChild<QAction*>(QStringLiteral("adjustment.") + QString::fromLatin1(test.id));
            QVERIFY(action != nullptr);
            QTimer::singleShot(0, &window, [&window, test] {
                auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.") + QString::fromLatin1(test.id));
                QVERIFY(dialog != nullptr);
                auto* slider = dialog->findChild<QSlider*>(QStringLiteral("AdjustmentControl.") + QString::fromLatin1(test.control));
                QVERIFY(slider != nullptr);
                slider->setValue(slider->value() == slider->maximum() ? slider->minimum() : slider->value() + 1);
                QCOMPARE(window.last_effect_backend(), std::string("cpu"));
                dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"))->click();
            });
            action->trigger();
        }
    }

void QtUiTests::animation_buttons_keep_frame_operations_working() {
        QtMainWindow window(cpu_settings());
        auto* add = window.findChild<QToolButton*>(QStringLiteral("animation.newFrame"));
        auto* duplicate = window.findChild<QToolButton*>(QStringLiteral("animation.duplicateFrame"));
        auto* remove = window.findChild<QToolButton*>(QStringLiteral("animation.deleteFrame"));
        QVERIFY(add != nullptr);
        QVERIFY(duplicate != nullptr);
        QVERIFY(remove != nullptr);
        QCOMPARE(window.document().frames.size(), std::size_t{1});
        add->click();
        duplicate->click();
        QCOMPARE(window.document().frames.size(), std::size_t{3});
        remove->click();
        QCOMPARE(window.document().frames.size(), std::size_t{2});
    }
