// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "core/Document.hpp"
#include "io/ProjectIO.hpp"
#include "ui/GraphEffectWidget.hpp"
#include "ui/QtCanvasWidget.hpp"
#include "ui/QtMainWindow.hpp"
#include "ui/TextRasterizer.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsView>
#include <QGridLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

using namespace px;

namespace {

AppSettings cpu_settings() {
    AppSettings settings;
    settings.show_splash_screen = false;
    settings.auto_open_error_console = false;
    settings.heavy_gpu_optimization = false;
    settings.mps_backend = false;
    return settings;
}

Document patterned_document() {
    Document document = Document::create(16, 16);
    for (int y = 0; y < document.height; ++y) {
        for (int x = 0; x < document.width; ++x) {
            document.active_cel().pixels[static_cast<std::size_t>(document.pixel_index(x, y))] =
                rgba(static_cast<std::uint8_t>(x * 13 + y),
                     static_cast<std::uint8_t>(x * 3 + y * 11),
                     static_cast<std::uint8_t>(255 - x * 7 - y * 5),
                     static_cast<std::uint8_t>(80 + ((x * 9 + y * 7) % 176)));
        }
    }
    return document;
}

} // namespace

class QtUiTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settings_dir_.isValid());
        QVERIFY(qputenv("PIXELART98_HOME",
                        settings_dir_.filePath(QStringLiteral("pixelart98-home")).toUtf8()));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
    }

    void main_window_builds_all_platform_neutral_controls() {
        QtMainWindow window(cpu_settings());
        auto* workspace = window.findChild<QTabWidget*>(QStringLiteral("MainWorkspaceTabs"));
        QVERIFY(workspace != nullptr);
        QCOMPARE(workspace->count(), 2);
        QCOMPARE(workspace->tabText(0), QStringLiteral("Canvas"));
        QCOMPARE(workspace->tabText(1), QStringLiteral("GraphEffect"));
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("GraphEffectWidget")) != nullptr);
        QVERIFY(window.findChild<QMenu*>(QStringLiteral("AdjustmentsMenu")) != nullptr);
        for (const char* name : {"ToolsDock", "ColorsDock", "LayersDock", "AnimationDock",
                                 "HistoryDock", "ModelDock", "TextDock", "ErrorConsoleDock"}) {
            QVERIFY2(window.findChild<QDockWidget*>(QString::fromLatin1(name)) != nullptr, name);
        }
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("PointerCoordinatesLabel")) != nullptr);
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("SelectionGeometryLabel")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("EditCut")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("EditCopy")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("EditPaste")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("ExportAnimationGif")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("ExportAnimationMp4")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("ImageResize")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("CanvasResize")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("ImageCrop")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("ImageFlipHorizontal")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("ImageRotate90Clockwise")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("OptionsFfmpegExecutable")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("OptionsFfmpegAutoDetect")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("OptionsHeavyGpu")) != nullptr);
#if defined(Q_OS_MACOS)
        QVERIFY(window.findChild<QAction*>(QStringLiteral("OptionsMpsBackend")) != nullptr);
#endif
        QVERIFY(window.property("ffmpegAvailable").isValid());
        QVERIFY(window.property("ffmpegExecutable").isValid());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("adjustment.levels")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("effect.gaussian-blur")) != nullptr);
        QVERIFY(!QIcon(QStringLiteral(":/icons/ellipse_select.png")).isNull());
        window.show();
        QApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void first_launch_uses_the_bundled_window_layout() {
        QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
        state.remove(QStringLiteral("windowGeometry"));
        state.remove(QStringLiteral("windowState"));
        state.remove(QStringLiteral("dockWindows"));
        state.sync();

        {
            QtMainWindow window(cpu_settings());
            window.show();
            QApplication::processEvents();

            const auto dock = [&window](const char* name) {
                return window.findChild<QDockWidget*>(QString::fromLatin1(name));
            };
            QDockWidget* tools = dock("ToolsDock");
            QDockWidget* colors = dock("ColorsDock");
            QDockWidget* layers = dock("LayersDock");
            QDockWidget* history = dock("HistoryDock");
            QDockWidget* animation = dock("AnimationDock");
            QDockWidget* model = dock("ModelDock");
            QDockWidget* model_preview = dock("ModelPreviewDock");
            QDockWidget* tile_preview = dock("TilePreviewDock");
            QDockWidget* text = dock("TextDock");
            QDockWidget* console = dock("ErrorConsoleDock");
            for (QDockWidget* widget : {tools, colors, layers, history, animation, model,
                                        model_preview, tile_preview, text, console}) {
                QVERIFY(widget != nullptr);
            }

            QVERIFY(tools->isFloating());
            QVERIFY(colors->isFloating());
            QVERIFY(layers->isFloating());
            QVERIFY(history->isFloating());
            QVERIFY(!tools->isHidden());
            QVERIFY(!colors->isHidden());
            QVERIFY(!layers->isHidden());
            QVERIFY(!history->isHidden());
            QVERIFY(animation->isHidden());
            QVERIFY(model->isHidden());
            QVERIFY(model->isFloating());
            QVERIFY(model_preview->isHidden());
            QVERIFY(tile_preview->isHidden());
            QVERIFY(text->isHidden());
            QVERIFY(console->isHidden());
            QVERIFY(tools->minimumWidth() >= 220);

            // A subsequently saved user layout must take precedence over the
            // bundled first-launch default.
            window.addDockWidget(Qt::BottomDockWidgetArea, colors);
            history->hide();
            state.setValue(QStringLiteral("windowState"), window.saveState(1));
            state.sync();
        }

        state.sync();
        {
            QtMainWindow restored(cpu_settings());
            auto* colors =
                restored.findChild<QDockWidget*>(QStringLiteral("ColorsDock"));
            auto* history =
                restored.findChild<QDockWidget*>(QStringLiteral("HistoryDock"));
            QVERIFY(colors != nullptr);
            QVERIFY(history != nullptr);
            QCOMPARE(restored.dockWidgetArea(colors), Qt::BottomDockWidgetArea);
            QVERIFY(history->isHidden());
        }
    }

    void detached_dock_keeps_visibility_and_geometry_after_restart() {
        QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
        state.remove(QStringLiteral("windowGeometry"));
        state.remove(QStringLiteral("windowState"));
        state.remove(QStringLiteral("dockWindows"));
        state.sync();

        QRect detached_geometry;
        {
            QtMainWindow window(cpu_settings());
            window.show();
            QApplication::processEvents();

            auto* tools =
                window.findChild<QDockWidget*>(QStringLiteral("ToolsDock"));
            auto* workspace =
                window.findChild<QTabWidget*>(QStringLiteral("MainWorkspaceTabs"));
            QVERIFY(tools != nullptr);
            QVERIFY(workspace != nullptr);
            tools->setFloating(true);
            tools->resize(280, 220);
            tools->move(120, 140);
            tools->show();
            QApplication::processEvents();

            QVERIFY(tools->isFloating());
            QVERIFY(!tools->isHidden());
            detached_geometry = tools->geometry();
            workspace->setCurrentIndex(1);
            QApplication::processEvents();
            QVERIFY(tools->isHidden());
            QVERIFY(window.close());
        }

        state.sync();
        QVERIFY(state.value(QStringLiteral("dockWindows/ToolsDock/floating")).toBool());
        QVERIFY(state.value(QStringLiteral("dockWindows/ToolsDock/visible")).toBool());
        QVERIFY(!state.value(QStringLiteral("dockWindows/ToolsDock/geometry"))
                     .toByteArray()
                     .isEmpty());

        // Simulate the first launch after upgrading from a version that only
        // persisted QMainWindow::saveState().
        state.remove(QStringLiteral("dockWindows"));
        state.sync();
        {
            QtMainWindow restored(cpu_settings());
            restored.show();
            QApplication::processEvents();

            auto* tools =
                restored.findChild<QDockWidget*>(QStringLiteral("ToolsDock"));
            QVERIFY(tools != nullptr);
            QVERIFY(tools->isFloating());
            QVERIFY(!tools->isHidden());
            QCOMPARE(tools->geometry(), detached_geometry);
            QVERIFY(restored.close());
        }

        state.sync();
        QVERIFY(state.value(QStringLiteral("dockWindows/ToolsDock/floating")).toBool());
        QVERIFY(state.value(QStringLiteral("dockWindows/ToolsDock/visible")).toBool());
        {
            QtMainWindow restored(cpu_settings());
            restored.show();
            QApplication::processEvents();

            auto* tools =
                restored.findChild<QDockWidget*>(QStringLiteral("ToolsDock"));
            QVERIFY(tools != nullptr);
            QVERIFY(tools->isFloating());
            QVERIFY(!tools->isHidden());
            QCOMPARE(tools->geometry(), detached_geometry);
            QVERIFY(restored.close());
        }

        state.remove(QStringLiteral("windowGeometry"));
        state.remove(QStringLiteral("windowState"));
        state.remove(QStringLiteral("dockWindows"));
        state.sync();
    }

    void raster_text_supports_real_fonts_sizes_alignment_and_undo() {
        Document document = Document::create(96, 48);
        RasterTextOptions options;
        options.text = QStringLiteral("Pixel\nArt");
        options.font_family = QApplication::font().family();
        options.pixel_size = 14;
        options.box_width = 72;
        options.box_height = 32;
        options.alignment = RasterTextAlignment::Center;
        options.bold = true;
        options.antialias = false;
        const RasterTextImage image = rasterize_text(options, rgba(240, 20, 10, 255), 72, 48);
        QVERIFY(image.width == 72);
        QCOMPARE(image.height, 32);
        QVERIFY(std::any_of(image.pixels.begin(), image.pixels.end(),
                            [](Pixel pixel) { return a(pixel) != 0; }));
        std::string error;
        QVERIFY(stamp_raster_text(document, 4, 3, options, rgba(240, 20, 10, 255), &error));
        QVERIFY(std::any_of(document.active_cel().pixels.begin(), document.active_cel().pixels.end(),
                            [](Pixel pixel) { return a(pixel) != 0; }));
        QVERIFY(document.undo());
        QVERIFY(std::all_of(document.active_cel().pixels.begin(), document.active_cel().pixels.end(),
                            [](Pixel pixel) { return pixel == 0; }));
        QVERIFY(document.redo());
        QVERIFY(std::any_of(document.active_cel().pixels.begin(), document.active_cel().pixels.end(),
                            [](Pixel pixel) { return a(pixel) != 0; }));
    }

    void raster_text_dock_updates_the_canvas_live_before_apply() {
        QtMainWindow window(cpu_settings());
        window.replace_document_for_testing(Document::create(32, 24));
        window.show();
        QApplication::processEvents();

        auto* canvas = dynamic_cast<QtCanvasWidget*>(
            window.findChild<QWidget*>(QStringLiteral("CanvasWidget")));
        auto* tools = window.findChild<QButtonGroup*>();
        auto* dock = window.findChild<QDockWidget*>(QStringLiteral("TextDock"));
        auto* input = window.findChild<QPlainTextEdit*>(QStringLiteral("RasterTextInput"));
        auto* size = window.findChild<QSpinBox*>(QStringLiteral("RasterTextSize"));
        auto* antialias =
            window.findChild<QCheckBox*>(QStringLiteral("RasterTextAntialias"));
        auto* apply = window.findChild<QPushButton*>(QStringLiteral("RasterTextApply"));
        auto* cancel = window.findChild<QPushButton*>(QStringLiteral("RasterTextCancel"));
        QVERIFY(canvas != nullptr);
        QVERIFY(tools != nullptr);
        QVERIFY(dock != nullptr);
        QVERIFY(input != nullptr);
        QVERIFY(size != nullptr);
        QVERIFY(antialias != nullptr);
        QVERIFY(apply != nullptr);
        QVERIFY(cancel != nullptr);
        QVERIFY(window.findChild<QSpinBox*>(QStringLiteral("RasterTextWidth")) == nullptr);
        QVERIFY(window.findChild<QDialog*>(QStringLiteral("RasterTextDialog")) == nullptr);

        QAbstractButton* text_tool = tools->button(static_cast<int>(ToolType::Text));
        QVERIFY(text_tool != nullptr);
        text_tool->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
        QApplication::processEvents();
        QVERIFY(dock->isVisible());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QVERIFY(antialias->width() >= antialias->sizeHint().width());
        QVERIFY(!canvas->has_raster_text_preview());
        QVERIFY(canvas->has_raster_text_box());
        size->setValue(8);

        input->setPlainText(QStringLiteral("Live preview"));
        QTRY_VERIFY(canvas->has_raster_text_preview());
        QVERIFY(apply->isEnabled());
        QVERIFY(std::all_of(window.document().active_cel().pixels.begin(),
                            window.document().active_cel().pixels.end(),
                            [](Pixel pixel) { return pixel == 0; }));
        QVERIFY(window.document().undo_history_for_recovery().empty());

        const int revision = canvas->raster_text_preview_revision();
        size->setValue(std::min(size->maximum(), size->value() + 7));
        QTRY_VERIFY(canvas->raster_text_preview_revision() > revision);
        QVERIFY(canvas->has_raster_text_preview());
        QVERIFY(std::any_of(canvas->raster_text_preview().pixels.begin(),
                            canvas->raster_text_preview().pixels.end(),
                            [](Pixel pixel) { return a(pixel) != 0; }));

        const QRect initial_box = canvas->raster_text_box();
        const auto canvas_boundary = [canvas](double x, double y) {
            const QPointF center(static_cast<double>(canvas->width()) * 0.5,
                                 static_cast<double>(canvas->height()) * 0.5);
            return QPoint(
                static_cast<int>(std::lround(center.x() + (x - 16.0) * canvas->zoom())),
                static_cast<int>(std::lround(center.y() + (y - 12.0) * canvas->zoom())));
        };
        const QPoint south_east = canvas_boundary(
            initial_box.x() + initial_box.width(),
            initial_box.y() + initial_box.height());
        const QPoint smaller = canvas_boundary(
            initial_box.x() + initial_box.width() - 4,
            initial_box.y() + initial_box.height() - 3);
        const int resize_revision = canvas->raster_text_preview_revision();
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, south_east);
        QTest::mouseMove(canvas, smaller);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, smaller);
        const QRect resized_box = canvas->raster_text_box();
        QVERIFY(resized_box.width() < initial_box.width());
        QVERIFY(resized_box.height() < initial_box.height());
        QVERIFY(canvas->raster_text_preview_revision() > resize_revision);
        QCOMPARE(canvas->raster_text_preview().width, resized_box.width());
        QCOMPARE(canvas->raster_text_preview().height, resized_box.height());

        apply->click();
        QVERIFY(!canvas->has_raster_text_preview());
        QVERIFY(window.document().undo_history_for_recovery().size() == 1U);
        QVERIFY(std::any_of(window.document().active_cel().pixels.begin(),
                            window.document().active_cel().pixels.end(),
                            [](Pixel pixel) { return a(pixel) != 0; }));
        const std::vector<Pixel> applied = window.document().active_cel().pixels;

        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                          canvas->rect().center() - QPoint(80, 40));
        input->setPlainText(QStringLiteral("Cancel this preview"));
        QTRY_VERIFY(canvas->has_raster_text_preview());
        cancel->click();
        QVERIFY(!canvas->has_raster_text_preview());
        QVERIFY(!canvas->has_raster_text_box());
        QVERIFY(window.document().active_cel().pixels == applied);
        QVERIFY(window.document().undo_history_for_recovery().size() == 1U);
    }

    void document_geometry_dialogs_expose_complete_controls() {
        QtMainWindow window(cpu_settings());
        struct DialogCase { const char* action; const char* dialog; const char* control; };
        const std::array<DialogCase, 3> cases = {{{"ImageResize", "ImageResizeDialog", "ImageResizeResampling"},
                                                  {"CanvasResize", "CanvasResizeDialog", "CanvasResizeAnchor"},
                                                  {"ImageCrop", "ImageCropDialog", "ImageCropWidth"}}};
        for (const DialogCase& test : cases) {
            auto* action = window.findChild<QAction*>(QString::fromLatin1(test.action));
            QVERIFY(action != nullptr);
            QTimer::singleShot(0, &window, [&window, test] {
                auto* dialog = window.findChild<QDialog*>(QString::fromLatin1(test.dialog));
                QVERIFY(dialog != nullptr);
                QVERIFY(dialog->findChild<QWidget*>(QString::fromLatin1(test.control)) != nullptr);
                dialog->reject();
            });
            action->trigger();
        }
    }

    void option_changes_are_saved_immediately_in_pixelart_home() {
        AppSettings settings = cpu_settings();
        QtMainWindow window(settings);
        QAction* heavy_gpu = window.findChild<QAction*>(QStringLiteral("OptionsHeavyGpu"));
        QVERIFY(heavy_gpu != nullptr);
        heavy_gpu->setChecked(true);
#if defined(Q_OS_MACOS)
        QAction* mps = window.findChild<QAction*>(QStringLiteral("OptionsMpsBackend"));
        QVERIFY(mps != nullptr);
        mps->setChecked(true);
#endif
        const AppSettings stored = load_app_settings();
        QVERIFY(stored.heavy_gpu_optimization);
#if defined(Q_OS_MACOS)
        QVERIFY(stored.mps_backend);
#endif
        const std::filesystem::path expected_root(
            settings_dir_.filePath(QStringLiteral("pixelart98-home")).toStdString());
        QVERIFY(default_app_data_directory() == expected_root);
        QVERIFY(std::filesystem::exists(default_app_settings_path()));
    }

    void crash_recovery_restores_document_and_undo_history() {
        const std::filesystem::path recovery_path = default_recovery_session_path();
        std::filesystem::create_directories(recovery_path.parent_path());
        Document document = patterned_document();
        const Pixel before = document.active_cel().pixels[0];
        std::vector<Pixel> edited = document.active_cel().pixels;
        edited[0] = rgba(250, 2, 3, 255);
        document.replace_active_pixels(std::move(edited), "Recovery test edit");
        std::string error;
        QVERIFY2(save_recovery_project(recovery_path, document, ModelDocument::create_default(), &error),
                 error.c_str());

        QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
        state.setValue(QStringLiteral("recovery/cleanShutdown"), false);
        state.setValue(QStringLiteral("recovery/sourcePath"), QStringLiteral("/tmp/recovered.pixart"));
        state.sync();

        bool prompt_seen = false;
        QTimer::singleShot(0, qApp, [&prompt_seen] {
            auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(message != nullptr);
            QCOMPARE(message->objectName(), QStringLiteral("CrashRecoveryDialog"));
            prompt_seen = true;
            QVERIFY(message->defaultButton() != nullptr);
            message->defaultButton()->click();
        });
        {
            QtMainWindow window(cpu_settings());
            QVERIFY(prompt_seen);
            QCOMPARE(window.document().active_cel().pixels[0], rgba(250, 2, 3, 255));
            QVERIFY(window.document().active_cel().pixels[0] != before);
            QVERIFY(window.document().undo_history_for_recovery().size() == 1U);
        }
        QVERIFY(!std::filesystem::exists(recovery_path));
        QSettings clean_state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
        QVERIFY(clean_state.value(QStringLiteral("recovery/cleanShutdown")).toBool());
    }

    void edits_are_atomically_autosaved_for_crash_recovery();
    void animation_export_dialogs_expose_format_specific_settings();
    void graph_workspace_hides_canvas_only_docks_and_restores_them();
    void canvas_reports_pointer_and_live_rectangular_selection();
    void history_selection_transform_and_brush_controls_are_real();
    void selection_transform_frame_does_not_cover_floating_content();
    void layer_visibility_is_synchronized_in_both_directions();
    void clipboard_keeps_pixels_floating_until_deselect();
    void levels_dialog_has_live_histograms_and_parameter_grid();
    void curves_dialog_starts_as_a_true_identity_and_can_be_edited();
    void graph_effect_tab_previews_round_trips_and_applies();
    void graph_effect_source_refresh_is_deferred_until_the_tab_is_visible();
    void zoom_shortcuts_only_affect_the_active_workspace_tab();
    void configurable_adjustments_preview_on_cpu_and_cancel_cleanly();
    void animation_buttons_keep_frame_operations_working();

private:
    QTemporaryDir settings_dir_;
};

#include "qt_ui_advanced_tests.inc"

QTEST_MAIN(QtUiTests)
#include "qt_ui_tests.moc"
