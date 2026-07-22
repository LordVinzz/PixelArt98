// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "core/Document.hpp"
#include "render/MpsEffectRenderer.hpp"
#include "ui/QtCanvasWidget.hpp"
#include "ui/QtMainWindow.hpp"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QApplication>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace px;

namespace {

Document source_document() {
    Document document = Document::create(16, 16);
    for (int y = 0; y < document.height; ++y) {
        for (int x = 0; x < document.width; ++x) {
            document.active_cel().pixels[static_cast<std::size_t>(y * document.width + x)] =
                rgba(static_cast<std::uint8_t>(x * 13 + y),
                     static_cast<std::uint8_t>(x * 3 + y * 11),
                     static_cast<std::uint8_t>(255 - x * 7 - y * 5),
                     static_cast<std::uint8_t>(80 + ((x * 9 + y * 7) % 176)));
        }
    }
    return document;
}

void apply_effect_from_ui(QtMainWindow& window, const QString& name, const QString& id,
                          const std::vector<std::pair<QString, int>>& values = {}) {
    QAction* action = window.findChild<QAction*>(QStringLiteral("effect.") + id);
    QVERIFY2(action != nullptr, qPrintable(QStringLiteral("Missing effect action: ") + name));

    QTimer::singleShot(0, &window, [&window, id, values] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.") + id);
        QVERIFY(dialog != nullptr);
        for (const auto& [control_id, value] : values) {
            auto* slider = dialog->findChild<QSlider*>(QStringLiteral("AdjustmentControl.") + control_id);
            QVERIFY(slider != nullptr);
            slider->setValue(value);
        }
        auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentApply"));
        QVERIFY(apply != nullptr);
        QTest::mouseClick(apply, Qt::LeftButton);
    });
    action->trigger();
}

struct AdjustmentObservation {
    int control_count = -1;
    bool apply_is_default = false;
    bool cancel_is_default = true;
};

AdjustmentObservation use_adjustment_from_ui(
    QtMainWindow& window, const QString& id,
    const std::vector<std::pair<QString, int>>& values, bool apply) {
    auto* adjustment_button = window.findChild<QPushButton*>(QStringLiteral("adjustment.") + id);
    AdjustmentObservation observation;
    if (adjustment_button == nullptr) {
        const QByteArray message = (QStringLiteral("Missing adjustment button: ") + id).toLocal8Bit();
        QTest::qFail(message.constData(), __FILE__, __LINE__);
        return observation;
    }
    QTimer::singleShot(0, &window, [&window, &observation, id, values, apply] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.") + id);
        QVERIFY(dialog != nullptr);
        observation.control_count = static_cast<int>(dialog->findChildren<QSlider*>().size());
        auto* apply_button = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentApply"));
        auto* cancel_button = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"));
        QVERIFY(apply_button != nullptr);
        QVERIFY(cancel_button != nullptr);
        observation.apply_is_default = apply_button->isDefault();
        observation.cancel_is_default = cancel_button->isDefault();
        for (const auto& [control_id, value] : values) {
            auto* slider = dialog->findChild<QSlider*>(QStringLiteral("AdjustmentControl.") + control_id);
            QVERIFY2(slider != nullptr, qPrintable(QStringLiteral("Missing adjustment control: ") + control_id));
            slider->setValue(value);
            QCOMPARE(slider->value(), value);
        }
        QTest::mouseClick(apply ? apply_button : cancel_button, Qt::LeftButton);
    });
    adjustment_button->click();
    return observation;
}

} // namespace

class QtMpsEffectsE2e final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settings_dir_.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
        MpsEffectRenderer renderer;
        if (!renderer.available()) QSKIP("These end-to-end tests require an available Metal/MPS device");
    }

    void grayscale_action_applies_pixels_through_mps() {
        run_exact_effect(QStringLiteral("Black and White"), QStringLiteral("black-white"), [](Pixel source) {
            const auto gray = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(static_cast<float>(r(source)) * 0.299f +
                                            static_cast<float>(g(source)) * 0.587f +
                                            static_cast<float>(b(source)) * 0.114f + 0.5f), 0, 255));
            return rgba(gray, gray, gray, a(source));
        });
    }

    void invert_action_applies_pixels_through_mps() {
        run_exact_effect(QStringLiteral("Invert Colors"), QStringLiteral("invert-colors"), [](Pixel source) {
            return rgba(static_cast<std::uint8_t>(255 - r(source)),
                        static_cast<std::uint8_t>(255 - g(source)),
                        static_cast<std::uint8_t>(255 - b(source)), a(source));
        });
    }

    void gaussian_blur_action_changes_image_through_mps() {
        AppSettings settings;
        settings.show_splash_screen = false;
        settings.heavy_gpu_optimization = true;
        settings.mps_backend = true;
        QtMainWindow window(settings);
        Document source = source_document();
        const std::vector<Pixel> before = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));

        apply_effect_from_ui(window, QStringLiteral("Gaussian Blur"), QStringLiteral("gaussian-blur"),
                             {{QStringLiteral("radius"), 24}});

        QCOMPARE(window.last_effect_backend(), std::string("mps"));
        const auto& after = window.document().active_cel().pixels;
        QCOMPARE(after.size(), before.size());
        QVERIFY(after != before);
        QVERIFY(std::any_of(after.begin(), after.end(), [](Pixel pixel) {
            return r(pixel) != g(pixel) || g(pixel) != b(pixel);
        }));
    }

    void hsv_exposes_hue_saturation_and_value_and_applies_all_three() {
        QtMainWindow window(mps_settings());
        Document source = source_document();
        const std::vector<Pixel> before = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));

        MpsEffectRenderer renderer;
        GpuEffectRequest request;
        request.mode = GpuEffectMode::Hsv;
        request.params = {90.0f / 360.0f, 0.30f, -0.20f, 0.0f};
        QVERIFY(renderer.render_active_cel(window.document(), request));
        std::vector<Pixel> expected;
        QVERIFY(renderer.read_output_pixels(expected));

        const AdjustmentObservation observation = use_adjustment_from_ui(
            window, QStringLiteral("hsv"),
            {{QStringLiteral("hue"), 90}, {QStringLiteral("saturation"), 30}, {QStringLiteral("value"), -20}}, true);

        QCOMPARE(observation.control_count, 3);
        QVERIFY(observation.apply_is_default);
        QVERIFY(!observation.cancel_is_default);
        QCOMPARE(window.last_effect_backend(), std::string("mps"));
        QVERIFY(window.document().active_cel().pixels != before);
        QVERIFY(window.document().active_cel().pixels == expected);
    }

    void cancel_restores_the_original_pixels() {
        QtMainWindow window(mps_settings());
        Document source = source_document();
        const std::vector<Pixel> before = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));

        const AdjustmentObservation observation = use_adjustment_from_ui(
            window, QStringLiteral("hsv"),
            {{QStringLiteral("hue"), -120}, {QStringLiteral("saturation"), 55}, {QStringLiteral("value"), 25}}, false);

        QCOMPARE(observation.control_count, 3);
        QVERIFY(observation.apply_is_default);
        QVERIFY(!observation.cancel_is_default);
        QVERIFY(window.document().active_cel().pixels == before);
    }

    void curves_shows_histogram_edits_curve_and_selects_color_channel() {
        QtMainWindow window(mps_settings());
        Document source = source_document();
        const std::vector<Pixel> before = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));
        auto* curves_button = window.findChild<QPushButton*>(QStringLiteral("adjustment.curves"));
        QVERIFY(curves_button != nullptr);

        bool apply_default = false;
        QTimer::singleShot(0, &window, [&window, &apply_default] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.curves"));
            QVERIFY(dialog != nullptr);
            auto* graph = dialog->findChild<QWidget*>(QStringLiteral("CurvesGraph"));
            QVERIFY(graph != nullptr);
            QCOMPARE(graph->property("histogramBins").toInt(), 256);
            QVERIFY(graph->property("histogramMaximum").toInt() > 0);
            QCOMPARE(graph->property("curvePointCount").toInt(), 3);

            auto* luma = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesChannel.luma"));
            auto* red = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesChannel.red"));
            auto* green = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesChannel.green"));
            auto* blue = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesChannel.blue"));
            QVERIFY(luma != nullptr);
            QVERIFY(red != nullptr);
            QVERIFY(green != nullptr);
            QVERIFY(blue != nullptr);
            QVERIFY(luma->isChecked());
            QTest::mouseClick(red, Qt::LeftButton);
            QVERIFY(red->isChecked());
            QVERIFY(!luma->isChecked());
            QCOMPARE(graph->property("channel").toInt(), 1);

            const QPoint middle(graph->width() / 2, graph->height() / 2);
            const QPoint raised(graph->width() / 2, graph->height() / 4);
            QTest::mousePress(graph, Qt::LeftButton, Qt::NoModifier, middle);
            QTest::mouseMove(graph, raised, 10);
            QTest::mouseRelease(graph, Qt::LeftButton, Qt::NoModifier, raised);
            QCOMPARE(graph->property("curvePointCount").toInt(), 3);
            const QPoint extra(graph->width() * 3 / 4, graph->height() / 3);
            QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier, extra);
            QCOMPARE(graph->property("curvePointCount").toInt(), 4);
            QTest::mouseClick(graph, Qt::RightButton, Qt::NoModifier, extra);
            QCOMPARE(graph->property("curvePointCount").toInt(), 3);

            auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentApply"));
            auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"));
            QVERIFY(apply != nullptr);
            QVERIFY(cancel != nullptr);
            apply_default = apply->isDefault() && !cancel->isDefault();
            QTest::mouseClick(apply, Qt::LeftButton);
        });
        curves_button->click();

        QVERIFY(apply_default);
        QCOMPARE(window.last_effect_backend(), std::string("mps"));
        const auto& after = window.document().active_cel().pixels;
        QVERIFY(after != before);
        bool red_changed = false;
        for (std::size_t index = 0; index < before.size(); ++index) {
            red_changed = red_changed || r(after[index]) != r(before[index]);
            QCOMPARE(g(after[index]), g(before[index]));
            QCOMPARE(b(after[index]), b(before[index]));
            QCOMPARE(a(after[index]), a(before[index]));
        }
        QVERIFY(red_changed);

        source = source_document();
        window.replace_document_for_testing(std::move(source));
        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.curves"));
            QVERIFY(dialog != nullptr);
            auto* graph = dialog->findChild<QWidget*>(QStringLiteral("CurvesGraph"));
            auto* green = dialog->findChild<QRadioButton*>(QStringLiteral("CurvesChannel.green"));
            auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"));
            QVERIFY(graph != nullptr);
            QVERIFY(green != nullptr);
            QVERIFY(cancel != nullptr);
            QTest::mouseClick(green, Qt::LeftButton);
            const QPoint middle(graph->width() / 2, graph->height() / 2);
            const QPoint lowered(graph->width() / 2, graph->height() * 3 / 4);
            QTest::mousePress(graph, Qt::LeftButton, Qt::NoModifier, middle);
            QTest::mouseMove(graph, lowered, 10);
            QTest::mouseRelease(graph, Qt::LeftButton, Qt::NoModifier, lowered);
            QTest::mouseClick(cancel, Qt::LeftButton);
        });
        curves_button->click();
        QVERIFY(window.document().active_cel().pixels == before);
    }

    void curves_apply_without_edit_keeps_image_untouched() {
        QtMainWindow window(mps_settings());
        Document source = source_document();
        const std::vector<Pixel> before = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));
        QVERIFY(!window.isWindowModified());
        auto* curves_button = window.findChild<QPushButton*>(QStringLiteral("adjustment.curves"));
        QVERIFY(curves_button != nullptr);

        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.curves"));
            QVERIFY(dialog != nullptr);
            auto* graph = dialog->findChild<QWidget*>(QStringLiteral("CurvesGraph"));
            auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentApply"));
            QVERIFY(graph != nullptr);
            QVERIFY(apply != nullptr);
            QCOMPARE(graph->property("curvePointCount").toInt(), 3);
            QVERIFY(apply->isDefault());
            QTest::mouseClick(apply, Qt::LeftButton);
        });
        curves_button->click();

        QVERIFY(window.document().active_cel().pixels == before);
        QCOMPARE(window.last_effect_backend(), std::string("none"));
        QVERIFY(!window.isWindowModified());
    }

    void animation_actions_use_icons_and_keep_frame_operations_working() {
        QtMainWindow window(mps_settings());
        struct Action {
            const char* object_name;
            const char* accessible_name;
        };
        const std::array<Action, 5> actions = {{
            {"animation.playPause", "Play / Pause"},
            {"animation.stop", "Stop"},
            {"animation.newFrame", "New Frame"},
            {"animation.duplicateFrame", "Duplicate Frame"},
            {"animation.deleteFrame", "Delete Frame"}
        }};
        for (const Action& action : actions) {
            auto* button = window.findChild<QToolButton*>(QString::fromLatin1(action.object_name));
            QVERIFY2(button != nullptr, action.object_name);
            QVERIFY2(!button->icon().isNull(), action.object_name);
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(button->text().isEmpty());
            QCOMPARE(button->accessibleName(), QString::fromLatin1(action.accessible_name));
            QCOMPARE(button->toolTip(), QString::fromLatin1(action.accessible_name));
        }

        auto* add = window.findChild<QToolButton*>(QStringLiteral("animation.newFrame"));
        auto* duplicate = window.findChild<QToolButton*>(QStringLiteral("animation.duplicateFrame"));
        auto* remove = window.findChild<QToolButton*>(QStringLiteral("animation.deleteFrame"));
        QVERIFY(add != nullptr);
        QVERIFY(duplicate != nullptr);
        QVERIFY(remove != nullptr);
        QCOMPARE(window.document().frames.size(), std::size_t{1});
        add->click();
        QCOMPARE(window.document().frames.size(), std::size_t{2});
        duplicate->click();
        QCOMPARE(window.document().frames.size(), std::size_t{3});
        remove->click();
        QCOMPARE(window.document().frames.size(), std::size_t{2});

        window.show();
        QApplication::processEvents();
        auto* timeline = window.findChild<QListWidget*>(QStringLiteral("AnimationFrames"));
        QVERIFY(timeline != nullptr);
        QVERIFY(timeline->count() >= 1);
        QListWidgetItem* first = timeline->item(0);
        QVERIFY(first != nullptr);
        const QRect initial_rect = timeline->visualItemRect(first);
        QVERIFY(initial_rect.isValid());
        const QPoint initial_handle(initial_rect.right() - 2, initial_rect.center().y());
        QTest::mousePress(timeline->viewport(), Qt::LeftButton, Qt::NoModifier, initial_handle);
        QTest::mouseMove(timeline->viewport(), initial_handle + QPoint(80, 0), 10);
        QCOMPARE(timeline->property("previewDurationMs").toInt(), 420);
        QTest::mouseRelease(timeline->viewport(), Qt::LeftButton, Qt::NoModifier, initial_handle + QPoint(80, 0));
        QCOMPARE(window.document().frames[0].duration_ms, 420);
        QCOMPARE(timeline->property("lastCommittedDurationMs").toInt(), 420);

        first = timeline->item(0);
        QVERIFY(first != nullptr);
        const QRect stretched_rect = timeline->visualItemRect(first);
        QVERIFY(stretched_rect.width() > initial_rect.width());
        const QPoint stretched_handle(stretched_rect.right() - 2, stretched_rect.center().y());
        QTest::mousePress(timeline->viewport(), Qt::LeftButton, Qt::NoModifier, stretched_handle);
        QTest::mouseMove(timeline->viewport(), stretched_handle - QPoint(50, 0), 10);
        QCOMPARE(timeline->property("previewDurationMs").toInt(), 220);
        QTest::mouseRelease(timeline->viewport(), Qt::LeftButton, Qt::NoModifier, stretched_handle - QPoint(50, 0));
        QCOMPARE(window.document().frames[0].duration_ms, 220);
        QVERIFY(timeline->visualItemRect(timeline->item(0)).width() < stretched_rect.width());

        auto* onion = window.findChild<QCheckBox*>(QStringLiteral("animation.onionSkin"));
        auto* canvas = dynamic_cast<QtCanvasWidget*>(window.centralWidget());
        QVERIFY(onion != nullptr);
        QVERIFY(canvas != nullptr);
        QVERIFY(onion->isChecked());
        QVERIFY(canvas->onion_visible());
        canvas->update();
        QApplication::processEvents();
        const QImage onion_enabled_framebuffer = canvas->grabFramebuffer();
        QVERIFY(!onion_enabled_framebuffer.isNull());
        onion->click();
        QVERIFY(!onion->isChecked());
        QVERIFY(!canvas->onion_visible());
        QApplication::processEvents();
        const QImage onion_disabled_framebuffer = canvas->grabFramebuffer();
        QVERIFY(!onion_disabled_framebuffer.isNull());
        QVERIFY(onion_enabled_framebuffer != onion_disabled_framebuffer);
        onion->click();
        QVERIFY(onion->isChecked());
        QVERIFY(canvas->onion_visible());
    }

    void every_effect_exposes_its_specific_parameters() {
        struct Case { const char* id; std::vector<const char*> controls; };
        const std::vector<Case> cases = {
            {"ink-sketch", {"outline", "coloring"}}, {"oil-painting", {"brush-size", "coarseness"}},
            {"pencil-sketch", {"tip-size", "range"}}, {"gaussian-blur", {"radius"}},
            {"motion-blur", {"distance", "angle"}}, {"radial-blur", {"amount", "center-x", "center-y"}},
            {"zoom-blur", {"amount", "center-x", "center-y"}}, {"median-blur", {"radius"}},
            {"surface-blur", {"radius", "threshold"}}, {"auto-level-effect", {}}, {"black-white", {}},
            {"sepia", {}}, {"invert-colors", {}}, {"invert-alpha", {}}, {"posterize-effect", {"levels"}},
            {"bulge", {"strength", "center-x", "center-y"}}, {"crystalize", {"cell-size"}},
            {"dents", {"scale", "amount"}}, {"frosted-glass", {"scatter-radius"}},
            {"pixelate", {"cell-size"}}, {"polar-inversion", {"scale"}}, {"tile-reflection", {"tile-size"}},
            {"twist", {"turns", "size", "center-x", "center-y"}},
            {"add-noise", {"intensity", "coverage", "color-saturation"}}, {"median", {"radius"}},
            {"reduce-noise", {"radius"}}, {"feather", {"radius"}},
            {"outline-object", {"thickness", "intensity"}}, {"glow", {"radius", "brightness", "contrast"}},
            {"red-eye-removal", {"strength"}}, {"sharpen", {"amount"}},
            {"soften-portrait", {"softness", "lighting", "warmth"}},
            {"vignette", {"radius", "strength", "center-x", "center-y"}},
            {"clouds", {"scale", "roughness"}}, {"julia-fractal", {"zoom", "angle"}},
            {"mandelbrot-fractal", {"zoom", "angle", "invert"}}, {"turbulence", {"scale", "octaves"}},
            {"edge-detect", {"strength"}}, {"emboss", {"angle"}},
            {"outline-stylize", {"thickness", "intensity"}}, {"relief", {"angle"}}
        };

        QtMainWindow window(mps_settings());
        for (const Case& test : cases) {
            window.replace_document_for_testing(source_document());
            const QString id = QString::fromLatin1(test.id);
            auto* action = window.findChild<QAction*>(QStringLiteral("effect.") + id);
            QVERIFY2(action != nullptr, qPrintable(id + QStringLiteral(": missing action")));
            QTimer::singleShot(0, &window, [&window, id, controls = test.controls] {
                auto* dialog = window.findChild<QDialog*>(QStringLiteral("AdjustmentDialog.") + id);
                QVERIFY(dialog != nullptr);
                QCOMPARE(static_cast<int>(dialog->findChildren<QSlider*>().size()), static_cast<int>(controls.size()));
                for (const char* control : controls) {
                    QVERIFY2(dialog->findChild<QSlider*>(QStringLiteral("AdjustmentControl.") + QString::fromLatin1(control)) != nullptr,
                             qPrintable(id + QStringLiteral(": missing control ") + QString::fromLatin1(control)));
                }
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentApply"));
                auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("AdjustmentCancel"));
                QVERIFY(apply != nullptr);
                QVERIFY(cancel != nullptr);
                QVERIFY(apply->isDefault());
                QVERIFY(!cancel->isDefault());
                QTest::mouseClick(cancel, Qt::LeftButton);
            });
            action->trigger();
        }
    }

    void spatial_effect_center_controls_change_the_mps_result() {
        QtMainWindow window(mps_settings());
        window.replace_document_for_testing(source_document());
        apply_effect_from_ui(window, QStringLiteral("Radial Blur"), QStringLiteral("radial-blur"),
                             {{QStringLiteral("amount"), 64}, {QStringLiteral("center-x"), 50},
                              {QStringLiteral("center-y"), 50}});
        QCOMPARE(window.last_effect_backend(), std::string("mps"));
        const std::vector<Pixel> centered = window.document().active_cel().pixels;

        window.replace_document_for_testing(source_document());
        apply_effect_from_ui(window, QStringLiteral("Radial Blur"), QStringLiteral("radial-blur"),
                             {{QStringLiteral("amount"), 64}, {QStringLiteral("center-x"), 15},
                              {QStringLiteral("center-y"), 85}});
        QCOMPARE(window.last_effect_backend(), std::string("mps"));
        const std::vector<Pixel> offset = window.document().active_cel().pixels;
        QVERIFY(centered != offset);
    }

    void every_adjustment_has_complete_controls_and_apply_cancel_semantics() {
        struct Case {
            QString id;
            int controls;
            std::vector<std::pair<QString, int>> values;
        };
        const std::vector<Case> cases = {
            {QStringLiteral("brightness-contrast"), 2, {{QStringLiteral("brightness"), 35}, {QStringLiteral("contrast"), 28}}},
            {QStringLiteral("hsv"), 3, {{QStringLiteral("hue"), 45}, {QStringLiteral("saturation"), 20}, {QStringLiteral("value"), 15}}},
            {QStringLiteral("temperature"), 1, {{QStringLiteral("temperature"), 60}}},
            {QStringLiteral("levels"), 5, {{QStringLiteral("input-black"), 20}, {QStringLiteral("input-white"), 225},
                                                   {QStringLiteral("gamma"), 140}, {QStringLiteral("output-black"), 8},
                                                   {QStringLiteral("output-white"), 245}}},
            {QStringLiteral("tonal-range"), 4, {{QStringLiteral("white-point"), 20}, {QStringLiteral("highlights"), -25},
                                                        {QStringLiteral("shadows"), 30}, {QStringLiteral("black-point"), -15}}},
            {QStringLiteral("auto-level"), 0, {}},
            {QStringLiteral("posterize"), 1, {{QStringLiteral("levels"), 5}}},
            {QStringLiteral("quantize"), 0, {}},
            {QStringLiteral("dither"), 0, {}}
        };

        QtMainWindow window(mps_settings());
        for (const Case& test : cases) {
            Document source = source_document();
            const std::vector<Pixel> before = source.active_cel().pixels;
            window.replace_document_for_testing(std::move(source));
            const AdjustmentObservation applied = use_adjustment_from_ui(window, test.id, test.values, true);
            QCOMPARE(applied.control_count, test.controls);
            QVERIFY2(applied.apply_is_default, qPrintable(test.id + QStringLiteral(": Apply is not the default button")));
            QVERIFY2(!applied.cancel_is_default, qPrintable(test.id + QStringLiteral(": Cancel is incorrectly the default button")));
            QCOMPARE(window.last_effect_backend(), std::string("mps"));
            QVERIFY2(window.document().active_cel().pixels != before,
                     qPrintable(test.id + QStringLiteral(": Apply did not change the image")));

            source = source_document();
            window.replace_document_for_testing(std::move(source));
            const AdjustmentObservation cancelled = use_adjustment_from_ui(window, test.id, test.values, false);
            QCOMPARE(cancelled.control_count, test.controls);
            QVERIFY2(window.document().active_cel().pixels == before,
                     qPrintable(test.id + QStringLiteral(": Cancel did not restore the image")));
        }
    }

private:
    static AppSettings mps_settings() {
        AppSettings settings;
        settings.show_splash_screen = false;
        settings.heavy_gpu_optimization = true;
        settings.mps_backend = true;
        return settings;
    }

    template<typename Expected>
    void run_exact_effect(const QString& name, const QString& id, Expected expected) {
        AppSettings settings;
        settings.show_splash_screen = false;
        settings.heavy_gpu_optimization = true;
        settings.mps_backend = true;
        QtMainWindow window(settings);
        Document source = source_document();
        const std::vector<Pixel> before = source.active_cel().pixels;
        window.replace_document_for_testing(std::move(source));

        apply_effect_from_ui(window, name, id);

        QCOMPARE(window.last_effect_backend(), std::string("mps"));
        const auto& after = window.document().active_cel().pixels;
        QCOMPARE(after.size(), before.size());
        for (std::size_t index = 0; index < before.size(); ++index) {
            QCOMPARE(after[index], expected(before[index]));
        }
    }

    QTemporaryDir settings_dir_;
};

QTEST_MAIN(QtMpsEffectsE2e)
#include "qt_mps_effects_e2e.moc"
