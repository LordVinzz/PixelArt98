// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "core/GraphEffect.hpp"
#include "ui/AdjustmentWidgets.hpp"
#include "ui/GraphEffectWidget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPointF>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace px;

namespace {

constexpr int kPreviewTimeoutMilliseconds = 3000;

struct GraphAnchors {
    GraphEffectNodeId source_id = 0;
    GraphEffectNodeId output_id = 0;
    const GraphEffectNodeSpec* source_spec = nullptr;
    const GraphEffectNodeSpec* output_spec = nullptr;
};

bool contains_fragment(std::string_view value, std::string_view fragment) {
    return value.find(fragment) != std::string_view::npos;
}

bool contains_any_fragment(std::string_view value,
                           std::initializer_list<std::string_view> fragments) {
    for (std::string_view fragment : fragments) {
        if (contains_fragment(value, fragment)) return true;
    }
    return false;
}

const GraphEffectNodeSpec* adjustment_spec() {
    const auto& catalog = graph_effect_catalog();
    const auto preferred = std::find_if(catalog.begin(), catalog.end(), [](const GraphEffectNodeSpec& spec) {
        return contains_any_fragment(spec.type_id, {"brightness", "contrast"}) &&
               spec.inputs.size() == 1U && spec.outputs.size() == 1U;
    });
    if (preferred != catalog.end()) return &*preferred;

    const auto fallback = std::find_if(catalog.begin(), catalog.end(), [](const GraphEffectNodeSpec& spec) {
        const bool has_numeric_parameter =
            std::any_of(spec.parameters.begin(), spec.parameters.end(), [](const GraphEffectParameterSpec& parameter) {
                return parameter.kind == GraphEffectParameterKind::Integer ||
                       parameter.kind == GraphEffectParameterKind::Number;
            });
        return spec.category == "Adjustments" && spec.inputs.size() == 1U &&
               spec.outputs.size() == 1U && has_numeric_parameter;
    });
    return fallback == catalog.end() ? nullptr : &*fallback;
}

const GraphEffectNodeSpec* generator_spec() {
    const auto& catalog = graph_effect_catalog();
    const auto preferred = std::find_if(catalog.begin(), catalog.end(), [](const GraphEffectNodeSpec& spec) {
        return contains_any_fragment(spec.type_id, {"generator.cloud", "cloud"}) &&
               spec.inputs.empty() && !spec.outputs.empty();
    });
    if (preferred != catalog.end()) return &*preferred;

    const auto fallback = std::find_if(catalog.begin(), catalog.end(), [](const GraphEffectNodeSpec& spec) {
        return spec.category == "Generators" && spec.inputs.empty() && !spec.outputs.empty();
    });
    return fallback == catalog.end() ? nullptr : &*fallback;
}

const GraphEffectNodeSpec* mix_spec() {
    const auto& catalog = graph_effect_catalog();
    const auto preferred = std::find_if(catalog.begin(), catalog.end(), [](const GraphEffectNodeSpec& spec) {
        return contains_any_fragment(spec.type_id, {"composite.mix", "mix"}) &&
               spec.inputs.size() >= 2U && !spec.outputs.empty();
    });
    if (preferred != catalog.end()) return &*preferred;

    const auto fallback = std::find_if(catalog.begin(), catalog.end(), [](const GraphEffectNodeSpec& spec) {
        return spec.category == "Mix" && spec.inputs.size() >= 2U && !spec.outputs.empty();
    });
    return fallback == catalog.end() ? nullptr : &*fallback;
}

const GraphEffectNodeSpec* spec_with_parameter_kind(GraphEffectParameterKind kind,
                                                     std::string_view preferred_type_fragment = {}) {
    const auto& catalog = graph_effect_catalog();
    const auto has_kind = [kind](const GraphEffectNodeSpec& spec) {
        return std::any_of(spec.parameters.begin(), spec.parameters.end(),
                           [kind](const GraphEffectParameterSpec& parameter) {
                               return parameter.kind == kind;
                           });
    };
    if (!preferred_type_fragment.empty()) {
        const auto preferred = std::find_if(
            catalog.begin(), catalog.end(), [&](const GraphEffectNodeSpec& spec) {
                return contains_fragment(spec.type_id, preferred_type_fragment) && has_kind(spec);
            });
        if (preferred != catalog.end()) return &*preferred;
    }
    const auto fallback = std::find_if(catalog.begin(), catalog.end(), has_kind);
    return fallback == catalog.end() ? nullptr : &*fallback;
}

const GraphEffectParameterSpec* numeric_parameter(const GraphEffectNodeSpec& spec,
                                                  std::string_view preferred_fragment = {}) {
    if (!preferred_fragment.empty()) {
        const auto preferred = std::find_if(
            spec.parameters.begin(), spec.parameters.end(),
            [preferred_fragment](const GraphEffectParameterSpec& parameter) {
                return contains_fragment(parameter.id, preferred_fragment) &&
                       (parameter.kind == GraphEffectParameterKind::Integer ||
                        parameter.kind == GraphEffectParameterKind::Number);
            });
        if (preferred != spec.parameters.end()) return &*preferred;
    }
    const auto fallback = std::find_if(
        spec.parameters.begin(), spec.parameters.end(), [](const GraphEffectParameterSpec& parameter) {
            return parameter.kind == GraphEffectParameterKind::Integer ||
                   parameter.kind == GraphEffectParameterKind::Number;
        });
    return fallback == spec.parameters.end() ? nullptr : &*fallback;
}

double parameter_number(const GraphEffectParameter& parameter, double fallback) {
    if (const auto* integer = std::get_if<std::int64_t>(&parameter)) {
        return static_cast<double>(*integer);
    }
    if (const auto* number = std::get_if<double>(&parameter)) return *number;
    return fallback;
}

std::int64_t luminance_histogram_checksum(const std::vector<Pixel>& pixels) {
    std::int64_t checksum = 0;
    for (const Pixel pixel : pixels) {
        if (a(pixel) == 0) continue;
        const int bin = std::clamp(static_cast<int>(luminance(pixel) + 0.5F), 0, 255);
        checksum += static_cast<std::int64_t>(bin + 1);
    }
    return checksum;
}

const std::vector<double>* curve_parameter(const GraphEffectNode& node,
                                           std::string_view parameter_id) {
    const auto found = node.parameters.find(std::string(parameter_id));
    return found == node.parameters.end()
        ? nullptr
        : std::get_if<std::vector<double>>(&found->second);
}

GraphEffectParameter strong_parameter_value(const GraphEffectParameterSpec& parameter) {
    const double default_value = parameter_number(parameter.default_value, 0.0);
    double candidate = parameter.maximum.value_or(default_value + 1.0);
    if (std::abs(candidate - default_value) < 0.000001) {
        candidate = parameter.minimum.value_or(default_value - 1.0);
    }
    if (parameter.kind == GraphEffectParameterKind::Integer) {
        return static_cast<std::int64_t>(std::llround(candidate));
    }
    return candidate;
}

std::optional<GraphAnchors> graph_anchors(const GraphEffectGraph& graph) {
    for (const GraphEffectLink& link : graph.links) {
        const GraphEffectNode* from = find_graph_effect_node(graph, link.from_node);
        const GraphEffectNode* to = find_graph_effect_node(graph, link.to_node);
        if (from == nullptr || to == nullptr) continue;
        const GraphEffectNodeSpec* from_spec = find_graph_effect_node_spec(from->type_id);
        const GraphEffectNodeSpec* to_spec = find_graph_effect_node_spec(to->type_id);
        if (from_spec == nullptr || to_spec == nullptr) continue;
        const bool source_shape = from_spec->inputs.empty() && !from_spec->outputs.empty();
        const bool output_shape = !to_spec->inputs.empty();
        const bool source_type = contains_any_fragment(from_spec->type_id, {"input", "source"}) ||
                                 from_spec->category == "Input";
        const bool output_type = contains_fragment(to_spec->type_id, "output") ||
                                 to_spec->category == "Output";
        if (source_shape && output_shape && source_type && output_type) {
            return GraphAnchors{from->id, to->id, from_spec, to_spec};
        }
    }
    return std::nullopt;
}

Document source_document() {
    Document document = Document::create(5, 4);
    std::vector<Pixel>& pixels = document.active_cel().pixels;
    for (int y = 0; y < document.height; ++y) {
        for (int x = 0; x < document.width; ++x) {
            const int red = 28 + x * 31 + y * 7;
            const int green = 36 + x * 13 + y * 29;
            const int blue = 44 + x * 17 + y * 19;
            pixels[static_cast<std::size_t>(document.pixel_index(x, y))] =
                rgba(static_cast<std::uint8_t>(red),
                     static_cast<std::uint8_t>(green),
                     static_cast<std::uint8_t>(blue), 255);
        }
    }
    document.clear_history();
    return document;
}

Document channel_source_document() {
    Document document = Document::create(4, 2);
    document.active_cel().pixels = {
        rgba(12, 34, 56, 78),
        rgba(220, 13, 41, 255),
        rgba(99, 88, 77, 0),
        rgba(0, 255, 128, 1),
        rgba(255, 0, 17, 64),
        rgba(3, 250, 129, 127),
        rgba(61, 62, 63, 254),
        rgba(201, 151, 101, 200),
    };
    document.clear_history();
    return document;
}

bool nodes_equal(const GraphEffectNode& left, const GraphEffectNode& right) {
    return left.id == right.id && left.type_id == right.type_id && left.x == right.x &&
           left.y == right.y && left.enabled == right.enabled &&
           left.parameters == right.parameters;
}

bool graphs_equal(const GraphEffectGraph& left, const GraphEffectGraph& right) {
    if (left.name != right.name || left.nodes.size() != right.nodes.size() ||
        left.links.size() != right.links.size()) {
        return false;
    }
    for (const GraphEffectNode& node : left.nodes) {
        const GraphEffectNode* other = find_graph_effect_node(right, node.id);
        if (other == nullptr || !nodes_equal(node, *other)) return false;
    }
    return std::is_permutation(left.links.begin(), left.links.end(),
                               right.links.begin(), right.links.end());
}

bool insert_adjustment(GraphEffectWidget& widget,
                       GraphEffectNodeId* out_id,
                       QString* error) {
    const std::optional<GraphAnchors> anchors = graph_anchors(widget.graph());
    const GraphEffectNodeSpec* effect = adjustment_spec();
    if (!anchors.has_value() || effect == nullptr || effect->inputs.empty() ||
        effect->outputs.empty()) {
        if (error != nullptr) *error = QStringLiteral("Required graph catalog entries are unavailable");
        return false;
    }

    GraphEffectNodeId effect_id = 0;
    if (!widget.add_node(effect->type_id, QPointF(-40.0, 40.0), &effect_id, error)) return false;
    if (!widget.connect_nodes(anchors->source_id, anchors->source_spec->outputs.front().id,
                              effect_id, effect->inputs.front().id, error)) {
        return false;
    }
    if (!widget.connect_nodes(effect_id, effect->outputs.front().id,
                              anchors->output_id, anchors->output_spec->inputs.front().id,
                              error)) {
        return false;
    }

    const GraphEffectParameterSpec* parameter = numeric_parameter(*effect, "brightness");
    if (parameter == nullptr) {
        if (error != nullptr) *error = QStringLiteral("Adjustment has no numeric parameter");
        return false;
    }
    if (!widget.set_node_parameter(effect_id, parameter->id,
                                   strong_parameter_value(*parameter), error)) {
        return false;
    }
    if (out_id != nullptr) *out_id = effect_id;
    return true;
}

} // namespace

class GraphEffectWidgetTests final : public QObject {
    Q_OBJECT

private slots:
    void initial_graph_is_source_connected_to_output() {
        GraphEffectWidget widget;
        const GraphEffectGraph& graph = widget.graph();
        const std::optional<GraphAnchors> anchors = graph_anchors(graph);

        QVERIFY2(anchors.has_value(), "The default graph must connect its input node to its output node");
        QCOMPARE(graph.nodes.size(), std::size_t{2});
        QCOMPARE(graph.links.size(), std::size_t{1});
        QCOMPARE(graph.links.front().from_node, anchors->source_id);
        QCOMPARE(graph.links.front().to_node, anchors->output_id);
        QCOMPARE(graph.links.front().from_port, anchors->source_spec->outputs.front().id);
        QCOMPARE(graph.links.front().to_port, anchors->output_spec->inputs.front().id);
        QVERIFY(!anchors->source_spec->bypassable);
        QVERIFY(!anchors->output_spec->bypassable);
        const GraphEffectGraph before_disable = graph;
        QString error;
        QVERIFY(!widget.set_node_enabled(anchors->source_id, false, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(graphs_equal(widget.graph(), before_disable));
        error.clear();
        QVERIFY(!widget.set_node_enabled(anchors->output_id, false, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(graphs_equal(widget.graph(), before_disable));
        QVERIFY(validate_graph_effect(graph, GraphEffectValidationMode::Evaluable).ok());
    }

    void channel_nodes_are_exposed_in_the_palette_with_rgba_ports() {
        const GraphEffectNodeSpec* split_spec =
            find_graph_effect_node_spec("channel.split-rgba");
        const GraphEffectNodeSpec* merge_spec =
            find_graph_effect_node_spec("channel.merge-rgba");
        QVERIFY(split_spec != nullptr);
        QVERIFY(merge_spec != nullptr);
        QCOMPARE(split_spec->label, std::string("Split Channels"));
        QCOMPARE(merge_spec->label, std::string("Merge Channels"));
        QCOMPARE(split_spec->category, std::string("Channels"));
        QCOMPARE(merge_spec->category, std::string("Channels"));
        QVERIFY(!split_spec->bypassable);
        QVERIFY(!merge_spec->bypassable);

        QCOMPARE(split_spec->inputs.size(), std::size_t{1});
        QCOMPARE(split_spec->inputs.front().id, std::string("image"));
        QCOMPARE(split_spec->outputs.size(), std::size_t{4});
        QCOMPARE(merge_spec->inputs.size(), std::size_t{4});
        QCOMPARE(merge_spec->outputs.size(), std::size_t{1});
        QCOMPARE(merge_spec->outputs.front().id, std::string("image"));
        const std::array<std::string, 4> channel_ids = {
            "red", "green", "blue", "alpha"};
        for (std::size_t index = 0; index < channel_ids.size(); ++index) {
            QCOMPARE(split_spec->outputs[index].id, channel_ids[index]);
            QCOMPARE(merge_spec->inputs[index].id, channel_ids[index]);
            QCOMPARE(split_spec->outputs[index].type, GraphEffectPortType::Image);
            QCOMPARE(merge_spec->inputs[index].type, GraphEffectPortType::Image);
        }

        GraphEffectWidget widget;
        auto* palette = widget.findChild<QTreeWidget*>(QStringLiteral("GraphEffectPalette"));
        QVERIFY(palette != nullptr);
        QTreeWidgetItem* channels = nullptr;
        for (int index = 0; index < palette->topLevelItemCount(); ++index) {
            QTreeWidgetItem* candidate = palette->topLevelItem(index);
            if (candidate != nullptr && candidate->text(0) == QStringLiteral("Channels")) {
                channels = candidate;
                break;
            }
        }
        QVERIFY(channels != nullptr);
        QCOMPARE(channels->childCount(), 2);

        bool found_split = false;
        bool found_merge = false;
        for (int index = 0; index < channels->childCount(); ++index) {
            QTreeWidgetItem* item = channels->child(index);
            QVERIFY(item != nullptr);
            const QString type_id = item->data(0, Qt::UserRole).toString();
            if (type_id == QStringLiteral("channel.split-rgba")) {
                QCOMPARE(item->text(0), QStringLiteral("Split Channels"));
                found_split = true;
            } else if (type_id == QStringLiteral("channel.merge-rgba")) {
                QCOMPARE(item->text(0), QStringLiteral("Merge Channels"));
                found_merge = true;
            } else {
                QFAIL(qPrintable(QStringLiteral("Unexpected Channels palette entry: %1")
                                     .arg(type_id)));
            }
        }
        QVERIFY(found_split);
        QVERIFY(found_merge);
    }

    void channel_nodes_connect_roundtrip_and_cannot_be_bypassed() {
        GraphEffectWidget widget;
        widget.resize(1400, 850);
        widget.show();
        const std::optional<GraphAnchors> anchors = graph_anchors(widget.graph());
        const GraphEffectNodeSpec* split_spec =
            find_graph_effect_node_spec("channel.split-rgba");
        const GraphEffectNodeSpec* merge_spec =
            find_graph_effect_node_spec("channel.merge-rgba");
        QVERIFY(anchors.has_value());
        QVERIFY(split_spec != nullptr);
        QVERIFY(merge_spec != nullptr);

        QString error;
        GraphEffectNodeId split_id = 0;
        GraphEffectNodeId merge_id = 0;
        QVERIFY2(widget.add_node(split_spec->type_id, QPointF(-80.0, 80.0),
                                 &split_id, &error), qPrintable(error));
        const QString split_name = QString::number(static_cast<qulonglong>(split_id));
        QVERIFY(widget.findChild<QObject*>(
                    QStringLiteral("GraphEffectNode.%1").arg(split_name)) != nullptr);
        QVERIFY(widget.findChild<QCheckBox*>(
                    QStringLiteral("GraphEffectEnabled.%1").arg(split_name)) == nullptr);

        QVERIFY2(widget.add_node(merge_spec->type_id, QPointF(240.0, 80.0),
                                 &merge_id, &error), qPrintable(error));
        const QString merge_name = QString::number(static_cast<qulonglong>(merge_id));
        QVERIFY(widget.findChild<QObject*>(
                    QStringLiteral("GraphEffectNode.%1").arg(merge_name)) != nullptr);
        QVERIFY(widget.findChild<QCheckBox*>(
                    QStringLiteral("GraphEffectEnabled.%1").arg(merge_name)) == nullptr);

        const GraphEffectGraph before_bypass_attempts = widget.graph();
        error.clear();
        QVERIFY(!widget.set_node_enabled(split_id, false, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(graphs_equal(widget.graph(), before_bypass_attempts));
        error.clear();
        QVERIFY(!widget.set_node_enabled(merge_id, false, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(graphs_equal(widget.graph(), before_bypass_attempts));

        QVERIFY2(widget.connect_nodes(anchors->source_id,
                                      anchors->source_spec->outputs.front().id,
                                      split_id, "image", &error), qPrintable(error));
        for (const std::string& channel :
             std::array<std::string, 4>{"red", "green", "blue", "alpha"}) {
            QVERIFY2(widget.connect_nodes(split_id, channel, merge_id, channel, &error),
                     qPrintable(error));
        }
        QVERIFY2(widget.connect_nodes(merge_id, "image", anchors->output_id,
                                      anchors->output_spec->inputs.front().id, &error),
                 qPrintable(error));

        QCOMPARE(widget.graph().links.size(), std::size_t{6});
        QCOMPARE(widget.property("linkCount").toInt(), 6);
        QVERIFY(validate_graph_effect(widget.graph(),
                                      GraphEffectValidationMode::Evaluable).ok());

        const GraphEffectGraph before_bad_port = widget.graph();
        error.clear();
        QVERIFY(!widget.connect_nodes(split_id, "missing-channel",
                                      merge_id, "red", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(graphs_equal(widget.graph(), before_bad_port));

        const Document source = channel_source_document();
        widget.set_source_document(source);
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);
        QCOMPARE(widget.preview_width(), source.width);
        QCOMPARE(widget.preview_height(), source.height);
        QVERIFY(widget.preview_pixels() == source.active_cel().pixels);
        QCOMPARE(widget.preview_pixels()[2], rgba(99, 88, 77, 0));
    }

    void preview_tracks_the_source_document_live() {
        GraphEffectWidget widget;
        widget.show();
        const Document source = source_document();

        widget.set_source_document(source);
        widget.schedule_preview();

        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);
        QCOMPARE(widget.preview_width(), source.width);
        QCOMPARE(widget.preview_height(), source.height);
        QVERIFY(widget.preview_pixels() == source.active_cel().pixels);
    }

    void adjustment_can_be_added_connected_and_edited() {
        GraphEffectWidget widget;
        widget.show();
        const Document source = source_document();
        widget.set_source_document(source);
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available(), kPreviewTimeoutMilliseconds);
        const std::vector<Pixel> identity_preview = widget.preview_pixels();

        int graph_change_count = 0;
        widget.graph_changed = [&graph_change_count] { ++graph_change_count; };
        QString error;
        GraphEffectNodeId effect_id = 0;
        QVERIFY2(insert_adjustment(widget, &effect_id, &error), qPrintable(error));

        const GraphEffectNode* effect_node = find_graph_effect_node(widget.graph(), effect_id);
        QVERIFY(effect_node != nullptr);
        QVERIFY(!effect_node->parameters.empty());
        QVERIFY(graph_change_count >= 4);
        QVERIFY(validate_graph_effect(widget.graph(), GraphEffectValidationMode::Evaluable).ok());
        QTRY_VERIFY_WITH_TIMEOUT(widget.preview_available() &&
                                     widget.preview_pixels() != identity_preview,
                                 kPreviewTimeoutMilliseconds);
    }

    void levels_inspector_uses_custom_port_histograms_and_parameter_grid() {
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

    void curves_inspector_uses_custom_graph_and_updates_point_arrays_atomically() {
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

    void curves_inspector_keeps_luminance_and_rgb_targets_exclusive() {
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

    void generator_can_feed_second_mix_input() {
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

    void cycle_connection_is_refused_without_mutating_graph() {
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

    void graph_roundtrips_through_pxgraph_file() {
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

    void invalid_load_is_transactional() {
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

    void removing_node_removes_all_incident_links() {
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

    void apply_preview_invokes_owner_callback_with_current_pixels() {
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
};

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QApplication application(argc, argv);
    static_cast<void>(application);
    GraphEffectWidgetTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "graph_effect_ui_tests.moc"
