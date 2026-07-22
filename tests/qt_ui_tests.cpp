// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "core/Document.hpp"
#include "ui/QtCanvasWidget.hpp"
#include "ui/QtMainWindow.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDockWidget>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <cstdint>
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
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
    }

    void main_window_builds_all_platform_neutral_controls() {
        QtMainWindow window(cpu_settings());
        QVERIFY(window.findChild<QMenu*>(QStringLiteral("AdjustmentsMenu")) != nullptr);
        for (const char* name : {"ToolsDock", "ColorsDock", "LayersDock", "AnimationDock",
                                 "HistoryDock", "ModelDock", "ErrorConsoleDock"}) {
            QVERIFY2(window.findChild<QDockWidget*>(QString::fromLatin1(name)) != nullptr, name);
        }
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("PointerCoordinatesLabel")) != nullptr);
        QVERIFY(window.findChild<QLabel*>(QStringLiteral("SelectionGeometryLabel")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("EditCut")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("EditCopy")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("EditPaste")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("adjustment.levels")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("effect.gaussian-blur")) != nullptr);
        window.show();
        QApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void canvas_reports_pointer_and_live_rectangular_selection() {
        QtMainWindow window(cpu_settings());
        Document document = patterned_document();
        document.selection.select_rect(2, 3, 6, 8, SelectionCombineMode::Replace);
        window.replace_document_for_testing(std::move(document));
        window.show();
        QApplication::processEvents();

        auto* canvas = static_cast<QtCanvasWidget*>(window.centralWidget());
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

    void layer_visibility_is_synchronized_in_both_directions() {
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

    void clipboard_keeps_pixels_floating_until_deselect() {
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

    void levels_dialog_has_live_histograms_and_parameter_grid() {
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

    void curves_dialog_starts_as_a_true_identity_and_can_be_edited() {
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
            auto* red = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesChannel.red"));
            QVERIFY(red != nullptr);
            red->click();
            QCOMPARE(graph->property("channel").toInt(), 1);
            QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier,
                              QPoint(graph->width() * 3 / 4, graph->height() / 3));
            QCOMPARE(graph->property("curvePointCount").toInt(), 4);
            dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"))->click();
        });
        action->trigger();
    }

    void configurable_adjustments_preview_on_cpu_and_cancel_cleanly() {
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

    void animation_buttons_keep_frame_operations_working() {
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

private:
    QTemporaryDir settings_dir_;
};

QTEST_MAIN(QtUiTests)
#include "qt_ui_tests.moc"
