// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/GraphEffectWidget.hpp"

#include "ui/AdjustmentWidgets.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace px {

namespace {

constexpr int kPreviewDebounceMilliseconds = 70;
constexpr qreal kNodeWidth = 196.0;
constexpr qreal kNodeHeaderHeight = 32.0;
constexpr qreal kPortRowHeight = 24.0;
constexpr int kPortItemType = QGraphicsItem::UserType + 101;
constexpr int kNodeItemType = QGraphicsItem::UserType + 102;
constexpr int kLinkItemType = QGraphicsItem::UserType + 103;

std::filesystem::path filesystem_path(const QString& path) {
#if defined(_WIN32)
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QString graph_file_filter() {
    return GraphEffectWidget::tr("PixelArt98 GraphEffect (*.pxgraph);;JSON files (*.json);;All files (*)");
}

QString node_object_name(GraphEffectNodeId node_id) {
    return QStringLiteral("GraphEffectNode.%1").arg(static_cast<qulonglong>(node_id));
}

QColor port_color(GraphEffectPortType type) {
    switch (type) {
        case GraphEffectPortType::Image:
            return QColor(78, 173, 255);
    }
    return QColor(180, 180, 180);
}

QColor category_color(const std::string& category) {
    if (category == "Input") return QColor(72, 132, 190);
    if (category == "Output") return QColor(80, 166, 116);
    if (category == "Generators") return QColor(176, 113, 55);
    if (category == "Mix") return QColor(152, 91, 184);
    if (category == "Channels") return QColor(174, 91, 168);
    if (category == "Adjustments") return QColor(63, 151, 150);
    return QColor(111, 113, 130);
}

QString diagnostic_text(const std::vector<GraphEffectDiagnostic>& diagnostics) {
    QStringList messages;
    for (const GraphEffectDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != GraphEffectDiagnosticSeverity::Error) continue;
        messages.push_back(QString::fromStdString(diagnostic.message));
        if (messages.size() == 3) break;
    }
    if (messages.isEmpty()) {
        for (const GraphEffectDiagnostic& diagnostic : diagnostics) {
            messages.push_back(QString::fromStdString(diagnostic.message));
            if (messages.size() == 3) break;
        }
    }
    return messages.join(QStringLiteral(" · "));
}

QString parameter_text(const std::vector<double>& values) {
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(values.size()));
    for (double value : values) parts.push_back(QString::number(value, 'g', 8));
    return parts.join(QStringLiteral(", "));
}

std::optional<std::vector<double>> parse_number_array(const QString& text) {
    std::vector<double> result;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    result.reserve(static_cast<std::size_t>(parts.size()));
    for (const QString& part : parts) {
        bool ok = false;
        const double value = part.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value)) return std::nullopt;
        result.push_back(value);
    }
    if (result.empty()) return std::nullopt;
    return result;
}

QColor color_from_pixel(Pixel pixel) {
    return QColor(static_cast<int>(r(pixel)), static_cast<int>(g(pixel)),
                  static_cast<int>(b(pixel)), static_cast<int>(a(pixel)));
}

Pixel pixel_from_color(const QColor& color) {
    return rgba(static_cast<std::uint8_t>(color.red()),
                static_cast<std::uint8_t>(color.green()),
                static_cast<std::uint8_t>(color.blue()),
                static_cast<std::uint8_t>(color.alpha()));
}

CurveEditorWidget::Histograms histograms_from_pixels(const std::vector<Pixel>& pixels) {
    CurveEditorWidget::Histograms histograms{};
    for (const Pixel pixel : pixels) {
        if (a(pixel) == 0U) continue;
        const int luma = std::clamp(static_cast<int>(luminance(pixel) + 0.5F), 0, 255);
        ++histograms[0][static_cast<std::size_t>(luma)];
        ++histograms[1][static_cast<std::size_t>(r(pixel))];
        ++histograms[2][static_cast<std::size_t>(g(pixel))];
        ++histograms[3][static_cast<std::size_t>(b(pixel))];
    }
    return histograms;
}

qlonglong histogram_checksum(const CurveEditorWidget::Histogram& histogram) {
    qlonglong checksum = 0;
    for (std::size_t bin = 0; bin < histogram.size(); ++bin) {
        checksum += static_cast<qlonglong>(bin + 1U) * histogram[bin];
    }
    return checksum;
}

qlonglong histogram_sample_count(const CurveEditorWidget::Histogram& histogram) {
    qlonglong count = 0;
    for (const int samples : histogram) count += samples;
    return count;
}

bool graph_boolean_parameter(const GraphEffectNode& node,
                             std::string_view parameter_id,
                             bool fallback) {
    const auto found = node.parameters.find(std::string(parameter_id));
    if (found == node.parameters.end()) return fallback;
    const bool* value = std::get_if<bool>(&found->second);
    return value != nullptr ? *value : fallback;
}

std::vector<double> graph_array_parameter(const GraphEffectNode& node,
                                          std::string_view parameter_id,
                                          std::vector<double> fallback) {
    const auto found = node.parameters.find(std::string(parameter_id));
    if (found == node.parameters.end()) return fallback;
    const auto* value = std::get_if<std::vector<double>>(&found->second);
    return value != nullptr ? *value : std::move(fallback);
}

template <typename Widget>
Widget* find_widget(QObject* parent, const QString& object_name) {
    return dynamic_cast<Widget*>(parent->findChild<QWidget*>(object_name));
}

void style_color_button(QPushButton* button, Pixel pixel) {
    const QColor color = color_from_pixel(pixel);
    const QColor text = color.lightnessF() < 0.5F ? Qt::white : Qt::black;
    button->setText(color.name(QColor::HexArgb).toUpper());
    button->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; color: %2; }")
            .arg(color.name(QColor::HexArgb), text.name()));
}

class GraphNodeItem;

class GraphPortItem final : public QGraphicsEllipseItem {
public:
    GraphPortItem(GraphEffectNodeId node_id,
                  GraphEffectPortSpec port,
                  bool input,
                  QGraphicsItem* parent)
        : QGraphicsEllipseItem(parent), node_id_(node_id), port_(std::move(port)), input_(input) {
        setRect(-6.0, -6.0, 12.0, 12.0);
        setPen(QPen(QColor(24, 26, 32), 2.0));
        setBrush(port_color(port_.type));
        setZValue(4.0);
        setToolTip(QStringLiteral("%1: %2")
                       .arg(QString::fromStdString(port_.label),
                            input_ ? GraphEffectWidget::tr("input") : GraphEffectWidget::tr("output")));

        auto* label_item = new QGraphicsSimpleTextItem(QString::fromStdString(port_.label), this);
        label_item->setBrush(QColor(218, 220, 228));
        const QRectF label_bounds = label_item->boundingRect();
        label_item->setPos(input_ ? 10.0 : -10.0 - label_bounds.width(),
                           -label_bounds.height() * 0.5);
        label_item->setAcceptedMouseButtons(Qt::NoButton);
    }

    [[nodiscard]] int type() const override { return kPortItemType; }
    [[nodiscard]] GraphEffectNodeId node_id() const noexcept { return node_id_; }
    [[nodiscard]] const std::string& port_id() const noexcept { return port_.id; }
    [[nodiscard]] GraphEffectPortType port_type() const noexcept { return port_.type; }
    [[nodiscard]] bool is_input() const noexcept { return input_; }

private:
    GraphEffectNodeId node_id_ = 0;
    GraphEffectPortSpec port_;
    bool input_ = false;
};

class GraphNodeItem final : public QGraphicsObject {
public:
    GraphNodeItem(const GraphEffectNode& node, const GraphEffectNodeSpec& spec)
        : node_id_(node.id), spec_(&spec),
          enabled_(spec.bypassable ? node.enabled : true) {
        const std::size_t rows = std::max(spec.inputs.size(), spec.outputs.size());
        height_ = kNodeHeaderHeight + 18.0 +
                  static_cast<qreal>(std::max<std::size_t>(rows, 1U)) * kPortRowHeight;
        setPos(node.x, node.y);
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setCacheMode(DeviceCoordinateCache);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QString::fromStdString(spec.type_id));

        for (std::size_t index = 0; index < spec.inputs.size(); ++index) {
            auto* port_item = new GraphPortItem(node.id, spec.inputs[index], true, this);
            port_item->setPos(0.0, port_y(index));
            inputs_[spec.inputs[index].id] = port_item;
        }
        for (std::size_t index = 0; index < spec.outputs.size(); ++index) {
            auto* port_item = new GraphPortItem(node.id, spec.outputs[index], false, this);
            port_item->setPos(kNodeWidth, port_y(index));
            outputs_[spec.outputs[index].id] = port_item;
        }
    }

    [[nodiscard]] int type() const override { return kNodeItemType; }
    [[nodiscard]] QRectF boundingRect() const override { return QRectF(0.0, 0.0, kNodeWidth, height_); }

    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem*,
               QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = boundingRect();
        QColor body_color(43, 45, 53);
        if (!enabled_) body_color = QColor(50, 50, 54);
        painter->setPen(QPen(isSelected() ? QColor(255, 190, 72) : QColor(24, 26, 32),
                             isSelected() ? 2.0 : 1.0));
        painter->setBrush(body_color);
        painter->drawRoundedRect(bounds, 7.0, 7.0);

        const QColor header_color = enabled_ ? category_color(spec_->category) : QColor(78, 78, 82);
        painter->setPen(Qt::NoPen);
        painter->setBrush(header_color);
        painter->drawRoundedRect(QRectF(0.0, 0.0, kNodeWidth, kNodeHeaderHeight), 7.0, 7.0);
        painter->drawRect(QRectF(0.0, kNodeHeaderHeight - 7.0, kNodeWidth, 7.0));

        QFont title_font = painter->font();
        title_font.setBold(true);
        painter->setFont(title_font);
        painter->setPen(enabled_ ? Qt::white : QColor(190, 190, 194));
        painter->drawText(QRectF(12.0, 0.0, kNodeWidth - 24.0, kNodeHeaderHeight),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          QString::fromStdString(spec_->label));
    }

    [[nodiscard]] GraphEffectNodeId node_id() const noexcept { return node_id_; }
    [[nodiscard]] const GraphEffectNodeSpec& spec() const noexcept { return *spec_; }

    [[nodiscard]] GraphPortItem* input_port(const std::string& id) const {
        const auto found = inputs_.find(id);
        return found == inputs_.end() ? nullptr : found->second;
    }

    [[nodiscard]] GraphPortItem* output_port(const std::string& id) const {
        const auto found = outputs_.find(id);
        return found == outputs_.end() ? nullptr : found->second;
    }

    void set_node_enabled(bool enabled) {
        enabled_ = spec_->bypassable ? enabled : true;
        update();
    }

    std::function<void(GraphEffectNodeId, const QPointF&)> position_changed;
    std::function<void(GraphEffectNodeId)> selected;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        const QVariant result = QGraphicsObject::itemChange(change, value);
        if (change == ItemPositionHasChanged && position_changed) {
            position_changed(node_id_, value.toPointF());
        } else if (change == ItemSelectedHasChanged && value.toBool() && selected) {
            selected(node_id_);
        }
        return result;
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        setCursor(Qt::ClosedHandCursor);
        QGraphicsObject::mousePressEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        setCursor(Qt::OpenHandCursor);
        QGraphicsObject::mouseReleaseEvent(event);
    }

private:
    [[nodiscard]] qreal port_y(std::size_t index) const {
        return kNodeHeaderHeight + 18.0 + static_cast<qreal>(index) * kPortRowHeight;
    }

    GraphEffectNodeId node_id_ = 0;
    const GraphEffectNodeSpec* spec_ = nullptr;
    qreal height_ = 90.0;
    bool enabled_ = true;
    std::map<std::string, GraphPortItem*> inputs_;
    std::map<std::string, GraphPortItem*> outputs_;
};

class GraphLinkItem final : public QGraphicsPathItem {
public:
    GraphLinkItem(GraphPortItem* output, GraphPortItem* input)
        : output_(output), input_(input) {
        setZValue(-1.0);
        setAcceptedMouseButtons(Qt::NoButton);
        setPen(QPen(port_color(output_->port_type()), 3.0, Qt::SolidLine, Qt::RoundCap));
        refresh_path();
    }

    [[nodiscard]] int type() const override { return kLinkItemType; }

    void refresh_path() {
        const QPointF start = output_->sceneBoundingRect().center();
        const QPointF end = input_->sceneBoundingRect().center();
        const qreal control_distance = std::max(48.0, std::abs(end.x() - start.x()) * 0.5);
        QPainterPath link_path(start);
        link_path.cubicTo(start + QPointF(control_distance, 0.0),
                         end - QPointF(control_distance, 0.0), end);
        setPath(link_path);
    }

private:
    GraphPortItem* output_ = nullptr;
    GraphPortItem* input_ = nullptr;
};

GraphPortItem* port_at(QGraphicsScene* scene, const QPointF& position) {
    QGraphicsItem* item = scene->itemAt(position, QTransform());
    while (item != nullptr) {
        if (item->type() == kPortItemType) return static_cast<GraphPortItem*>(item);
        item = item->parentItem();
    }
    return nullptr;
}

class GraphScene final : public QGraphicsScene {
public:
    explicit GraphScene(QObject* parent = nullptr) : QGraphicsScene(parent) {
        setObjectName(QStringLiteral("GraphEffectScene"));
        setSceneRect(-4000.0, -3000.0, 8000.0, 6000.0);
        setItemIndexMethod(QGraphicsScene::NoIndex);
    }

    void rebuild(const GraphEffectGraph& graph) {
        cancel_link_drag();
        clear();
        nodes_.clear();
        links_.clear();

        for (const GraphEffectNode& node : graph.nodes) {
            const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node.type_id);
            if (spec == nullptr) continue;
            auto* item = new GraphNodeItem(node, *spec);
            item->position_changed = [this](GraphEffectNodeId id, const QPointF& position) {
                refresh_links();
                if (node_moved) node_moved(id, position);
            };
            item->selected = [this](GraphEffectNodeId id) {
                if (node_selected) node_selected(id);
            };
            addItem(item);
            nodes_[node.id] = item;
        }

        for (const GraphEffectLink& link : graph.links) {
            const auto from_found = nodes_.find(link.from_node);
            const auto to_found = nodes_.find(link.to_node);
            if (from_found == nodes_.end() || to_found == nodes_.end()) continue;
            GraphPortItem* output = from_found->second->output_port(link.from_port);
            GraphPortItem* input = to_found->second->input_port(link.to_port);
            if (output == nullptr || input == nullptr) continue;
            auto* link_item = new GraphLinkItem(output, input);
            addItem(link_item);
            links_.push_back(link_item);
        }
    }

    [[nodiscard]] GraphNodeItem* node_item(GraphEffectNodeId id) const {
        const auto found = nodes_.find(id);
        return found == nodes_.end() ? nullptr : found->second;
    }

    [[nodiscard]] std::vector<GraphEffectNodeId> selected_node_ids() const {
        std::vector<GraphEffectNodeId> result;
        for (QGraphicsItem* item : selectedItems()) {
            if (item->type() == kNodeItemType) {
                result.push_back(static_cast<GraphNodeItem*>(item)->node_id());
            }
        }
        return result;
    }

    void refresh_links() {
        for (GraphLinkItem* link : links_) link->refresh_path();
    }

    std::function<void(GraphEffectNodeId, const QPointF&)> node_moved;
    std::function<void(GraphEffectNodeId)> node_selected;
    std::function<void(GraphEffectNodeId,
                       const std::string&,
                       GraphEffectNodeId,
                       const std::string&)> connection_requested;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        GraphPortItem* port = port_at(this, event->scenePos());
        if (event->button() == Qt::LeftButton && port != nullptr && !port->is_input()) {
            dragged_output_ = port;
            temporary_link_ = addPath(QPainterPath(),
                                      QPen(port_color(port->port_type()), 3.0, Qt::DashLine,
                                           Qt::RoundCap));
            temporary_link_->setZValue(-0.5);
            update_temporary_link(event->scenePos());
            event->accept();
            return;
        }
        QGraphicsScene::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (dragged_output_ != nullptr) {
            update_temporary_link(event->scenePos());
            event->accept();
            return;
        }
        QGraphicsScene::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (dragged_output_ != nullptr) {
            GraphPortItem* input = port_at(this, event->scenePos());
            if (input != nullptr && input->is_input() &&
                input->port_type() == dragged_output_->port_type() && connection_requested) {
                connection_requested(dragged_output_->node_id(), dragged_output_->port_id(),
                                     input->node_id(), input->port_id());
            }
            cancel_link_drag();
            event->accept();
            return;
        }
        QGraphicsScene::mouseReleaseEvent(event);
    }

private:
    void update_temporary_link(const QPointF& end) {
        if (temporary_link_ == nullptr || dragged_output_ == nullptr) return;
        const QPointF start = dragged_output_->sceneBoundingRect().center();
        const qreal distance = std::max(48.0, std::abs(end.x() - start.x()) * 0.5);
        QPainterPath link_path(start);
        link_path.cubicTo(start + QPointF(distance, 0.0), end - QPointF(distance, 0.0), end);
        temporary_link_->setPath(link_path);
    }

    void cancel_link_drag() {
        if (temporary_link_ != nullptr) {
            removeItem(temporary_link_);
            delete temporary_link_;
        }
        temporary_link_ = nullptr;
        dragged_output_ = nullptr;
    }

    std::map<GraphEffectNodeId, GraphNodeItem*> nodes_;
    std::vector<GraphLinkItem*> links_;
    GraphPortItem* dragged_output_ = nullptr;
    QGraphicsPathItem* temporary_link_ = nullptr;
};

class GraphView final : public QGraphicsView {
public:
    explicit GraphView(QWidget* parent = nullptr) : QGraphicsView(parent) {
        setObjectName(QStringLiteral("GraphEffectView"));
        setRenderHint(QPainter::Antialiasing, true);
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setDragMode(QGraphicsView::RubberBandDrag);
        setBackgroundBrush(QColor(27, 29, 35));
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void()> delete_requested;

    void zoom_by(qreal factor) {
        constexpr qreal minimum_scale = 0.2;
        constexpr qreal maximum_scale = 3.5;
        const qreal current_scale = transform().m11();
        if (!std::isfinite(current_scale) || current_scale <= 0.0 ||
            !std::isfinite(factor) || factor <= 0.0) {
            return;
        }
        const qreal target_scale = std::clamp(current_scale * factor,
                                              minimum_scale, maximum_scale);
        const qreal applied_factor = target_scale / current_scale;
        if (std::abs(applied_factor - 1.0) > 0.000001) {
            scale(applied_factor, applied_factor);
        }
    }

    void actual_size() {
        const QPoint viewport_center = viewport()->rect().center();
        const QPointF scene_center = mapToScene(viewport_center);
        resetTransform();
        centerOn(scene_center);
    }

    [[nodiscard]] double zoom() const noexcept { return transform().m11(); }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const int delta = event->angleDelta().y();
        if (delta != 0) zoom_by(delta > 0 ? 1.15 : (1.0 / 1.15));
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
            delete_requested) {
            delete_requested();
            event->accept();
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }

    void drawBackground(QPainter* painter, const QRectF& rect) override {
        QGraphicsView::drawBackground(painter, rect);
        constexpr qreal minor_step = 24.0;
        constexpr qreal major_step = 120.0;
        const qreal left = std::floor(rect.left() / minor_step) * minor_step;
        const qreal top = std::floor(rect.top() / minor_step) * minor_step;

        painter->setPen(QPen(QColor(37, 40, 47), 0.0));
        for (qreal x = left; x < rect.right(); x += minor_step) {
            painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (qreal y = top; y < rect.bottom(); y += minor_step) {
            painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
        }

        const qreal major_left = std::floor(rect.left() / major_step) * major_step;
        const qreal major_top = std::floor(rect.top() / major_step) * major_step;
        painter->setPen(QPen(QColor(47, 50, 59), 0.0));
        for (qreal x = major_left; x < rect.right(); x += major_step) {
            painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (qreal y = major_top; y < rect.bottom(); y += major_step) {
            painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
        }
    }
};

class GraphPreview final : public QWidget {
public:
    explicit GraphPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("GraphEffectPreview"));
        setMinimumSize(220, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void set_pixels(const std::vector<Pixel>& pixels, int width, int height) {
        const std::size_t expected = width > 0 && height > 0
            ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            : 0U;
        if (expected == 0U || pixels.size() != expected) {
            image_ = QImage();
        } else {
            const QImage source(reinterpret_cast<const uchar*>(pixels.data()), width, height,
                                width * static_cast<int>(sizeof(Pixel)), QImage::Format_RGBA8888);
            image_ = source.copy();
        }
        update();
    }

    void clear() {
        image_ = QImage();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(31, 33, 39));
        constexpr int checker_size = 10;
        for (int y = 0; y < height(); y += checker_size) {
            for (int x = 0; x < width(); x += checker_size) {
                const bool dark = ((x / checker_size) + (y / checker_size)) % 2 == 0;
                painter.fillRect(QRect(x, y, checker_size, checker_size),
                                 dark ? QColor(87, 89, 96) : QColor(119, 121, 128));
            }
        }
        if (image_.isNull()) {
            painter.setPen(QColor(220, 220, 224));
            painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                             GraphEffectWidget::tr("Preview unavailable"));
            return;
        }
        const QSize target_size = image_.size().scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - target_size.width()) / 2,
                                  (height() - target_size.height()) / 2), target_size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(target, image_);
        painter.setPen(QPen(QColor(20, 22, 26), 1.0));
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }

private:
    QImage image_;
};

} // namespace

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

namespace {

bool parameter_matches_spec(const GraphEffectParameter& value,
                            const GraphEffectParameterSpec& spec,
                            QString* error) {
    const auto fail = [error](const QString& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    double number = 0.0;
    switch (spec.kind) {
        case GraphEffectParameterKind::Integer: {
            const auto* integer = std::get_if<std::int64_t>(&value);
            if (integer == nullptr) return fail(GraphEffectWidget::tr("Expected an integer."));
            number = static_cast<double>(*integer);
            break;
        }
        case GraphEffectParameterKind::Number: {
            const auto* real = std::get_if<double>(&value);
            if (real == nullptr || !std::isfinite(*real)) {
                return fail(GraphEffectWidget::tr("Expected a finite number."));
            }
            number = *real;
            break;
        }
        case GraphEffectParameterKind::Boolean:
            if (std::get_if<bool>(&value) == nullptr) {
                return fail(GraphEffectWidget::tr("Expected a boolean value."));
            }
            return true;
        case GraphEffectParameterKind::Color:
            if (std::get_if<Pixel>(&value) == nullptr) {
                return fail(GraphEffectWidget::tr("Expected a color value."));
            }
            return true;
        case GraphEffectParameterKind::Choice: {
            const auto* choice = std::get_if<std::string>(&value);
            if (choice == nullptr ||
                std::find(spec.choices.begin(), spec.choices.end(), *choice) == spec.choices.end()) {
                return fail(GraphEffectWidget::tr("Unknown choice."));
            }
            return true;
        }
        case GraphEffectParameterKind::NumberArray: {
            const auto* values = std::get_if<std::vector<double>>(&value);
            if (values == nullptr || values->empty() ||
                std::any_of(values->begin(), values->end(),
                            [](double element) { return !std::isfinite(element); })) {
                return fail(GraphEffectWidget::tr("Expected a non-empty list of finite numbers."));
            }
            return true;
        }
    }
    if (spec.minimum.has_value() && number < *spec.minimum) {
        return fail(GraphEffectWidget::tr("Value is below the minimum."));
    }
    if (spec.maximum.has_value() && number > *spec.maximum) {
        return fail(GraphEffectWidget::tr("Value is above the maximum."));
    }
    return true;
}

} // namespace

GraphEffectWidget::GraphEffectWidget(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {
    setObjectName(QStringLiteral("GraphEffectWidget"));
    setMinimumSize(840, 520);
}

GraphEffectWidget::~GraphEffectWidget() = default;

void GraphEffectWidget::set_source_document(const Document& document) {
    impl_->source_document_ = document;
    schedule_preview();
}

void GraphEffectWidget::reset_graph() {
    impl_->graph_ = make_default_graph_effect();
    impl_->graph_path_.clear();
    impl_->dirty_ = false;
    setProperty("graphDirty", false);
    setProperty("graphPath", QString());
    impl_->rebuild_scene(std::nullopt);
    impl_->rebuild_inspector(std::nullopt);
    if (graph_changed) graph_changed();
    schedule_preview();
    impl_->publish_status(tr("New GraphEffect graph"), false);
}

const GraphEffectGraph& GraphEffectWidget::graph() const noexcept {
    return impl_->graph_;
}

bool GraphEffectWidget::preview_available() const noexcept {
    return impl_->evaluation_.success && !impl_->evaluation_.pixels.empty();
}

const std::vector<Pixel>& GraphEffectWidget::preview_pixels() const noexcept {
    return impl_->evaluation_.pixels;
}

int GraphEffectWidget::preview_width() const noexcept {
    return impl_->evaluation_.width;
}

int GraphEffectWidget::preview_height() const noexcept {
    return impl_->evaluation_.height;
}

bool GraphEffectWidget::add_node(const std::string& type_id,
                                 const QPointF& scene_position,
                                 GraphEffectNodeId* out_id,
                                 QString* error) {
    GraphEffectNodeId node_id = 0;
    std::string core_error;
    if (!add_graph_effect_node(impl_->graph_, type_id, scene_position.x(), scene_position.y(),
                               &node_id, &core_error)) {
        const QString message = QString::fromStdString(core_error);
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    if (out_id != nullptr) *out_id = node_id;
    impl_->rebuild_scene(node_id);
    impl_->rebuild_inspector(node_id);
    impl_->mark_graph_changed(true);
    impl_->publish_status(tr("Added %1").arg(QString::fromStdString(type_id)), false);
    return true;
}

bool GraphEffectWidget::remove_node(GraphEffectNodeId node_id, QString* error) {
    if (!remove_graph_effect_node(impl_->graph_, node_id)) {
        const QString message = tr("Node %1 does not exist.").arg(static_cast<qulonglong>(node_id));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    impl_->rebuild_scene(std::nullopt);
    impl_->rebuild_inspector(std::nullopt);
    impl_->mark_graph_changed(true);
    impl_->publish_status(tr("Node removed"), false);
    return true;
}

bool GraphEffectWidget::connect_nodes(GraphEffectNodeId from_node,
                                      const std::string& from_port,
                                      GraphEffectNodeId to_node,
                                      const std::string& to_port,
                                      QString* error) {
    GraphEffectGraph before = impl_->graph_;
    (void)disconnect_graph_effect_input(impl_->graph_, to_node, to_port);
    std::string core_error;
    if (!connect_graph_effect_nodes(impl_->graph_, from_node, from_port, to_node, to_port,
                                    &core_error)) {
        impl_->graph_ = std::move(before);
        const QString message = QString::fromStdString(core_error);
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    impl_->rebuild_scene(to_node);
    impl_->rebuild_inspector(to_node);
    impl_->mark_graph_changed(true);
    impl_->publish_status(tr("Nodes connected"), false);
    return true;
}

bool GraphEffectWidget::set_node_enabled(GraphEffectNodeId node_id,
                                         bool enabled,
                                         QString* error) {
    GraphEffectNode* node = find_graph_effect_node(impl_->graph_, node_id);
    if (node == nullptr) {
        const QString message = tr("Node %1 does not exist.").arg(static_cast<qulonglong>(node_id));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    const GraphEffectNodeSpec* spec = find_graph_effect_node_spec(node->type_id);
    if (spec == nullptr) {
        const QString message = tr("Unknown node type: %1").arg(QString::fromStdString(node->type_id));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    if (!spec->bypassable) {
        const QString message = tr("%1 cannot be enabled or disabled.")
                                    .arg(QString::fromStdString(spec->label));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    if (node->enabled == enabled) return true;
    node->enabled = enabled;
    if (GraphNodeItem* item = impl_->scene_->node_item(node_id); item != nullptr) {
        item->set_node_enabled(enabled);
    }
    impl_->mark_graph_changed(true);
    return true;
}

bool GraphEffectWidget::set_node_parameter(GraphEffectNodeId node_id,
                                           const std::string& parameter_id,
                                           GraphEffectParameter value,
                                           QString* error) {
    GraphEffectNode* node = find_graph_effect_node(impl_->graph_, node_id);
    if (node == nullptr) {
        const QString message = tr("Node %1 does not exist.").arg(static_cast<qulonglong>(node_id));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    const GraphEffectNodeSpec* node_spec = find_graph_effect_node_spec(node->type_id);
    if (node_spec == nullptr) {
        const QString message = tr("Unknown node type: %1").arg(QString::fromStdString(node->type_id));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    const auto parameter_spec = std::find_if(
        node_spec->parameters.begin(), node_spec->parameters.end(),
        [&parameter_id](const GraphEffectParameterSpec& spec) { return spec.id == parameter_id; });
    if (parameter_spec == node_spec->parameters.end()) {
        const QString message = tr("Unknown parameter: %1").arg(QString::fromStdString(parameter_id));
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    QString validation_error;
    if (!parameter_matches_spec(value, *parameter_spec, &validation_error)) {
        const QString message = tr("%1: %2")
                                    .arg(QString::fromStdString(parameter_spec->label),
                                         validation_error);
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }

    const bool curves_target = node->type_id == "adjustment.curves" &&
        (parameter_id == "luminance" || parameter_id == "red" ||
         parameter_id == "green" || parameter_id == "blue");
    if (curves_target) {
        const bool requested = *std::get_if<bool>(&value);
        const auto boolean_parameter = [node](const char* id, bool fallback) {
            const auto found = node->parameters.find(id);
            if (found == node->parameters.end()) return fallback;
            const bool* current = std::get_if<bool>(&found->second);
            return current != nullptr ? *current : fallback;
        };
        const std::array<bool, 4> before = {
            boolean_parameter("luminance", true), boolean_parameter("red", false),
            boolean_parameter("green", false), boolean_parameter("blue", false)};
        std::array<bool, 4> after = before;

        if (parameter_id == "luminance") {
            if (requested) {
                after = {true, false, false, false};
            } else if (after[1] || after[2] || after[3]) {
                after[0] = false;
            } else {
                after[0] = true;
            }
        } else {
            const std::size_t channel = parameter_id == "red" ? 1U :
                                        parameter_id == "green" ? 2U : 3U;
            after[channel] = requested;
            if (requested) {
                after[0] = false;
            } else if (after[1] || after[2] || after[3]) {
                after[0] = false;
            } else if (before[0]) {
                after[0] = true;
            } else {
                after[channel] = true;
            }
        }

        if (after != before) {
            node->parameters["luminance"] = after[0];
            node->parameters["red"] = after[1];
            node->parameters["green"] = after[2];
            node->parameters["blue"] = after[3];
            impl_->sync_curves_target_editors(node_id);
            impl_->mark_graph_changed(true);
        } else {
            impl_->sync_curves_target_editors(node_id);
        }
        return true;
    }
    node->parameters[parameter_id] = std::move(value);
    if (node->type_id == "adjustment.curves" &&
        (parameter_id == "curve_x" || parameter_id == "curve_y")) {
        impl_->sync_curves_points_editor(node_id);
    }
    impl_->mark_graph_changed(true);
    return true;
}

bool GraphEffectWidget::save_graph(const QString& path, QString* error) {
    std::string core_error;
    if (!save_graph_effect(filesystem_path(path), impl_->graph_, &core_error)) {
        const QString message = QString::fromStdString(core_error);
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    impl_->graph_path_ = path;
    impl_->dirty_ = false;
    setProperty("graphDirty", false);
    setProperty("graphPath", path);
    impl_->publish_status(tr("Graph saved to %1").arg(path), false);
    return true;
}

bool GraphEffectWidget::load_graph(const QString& path, QString* error) {
    GraphEffectGraph loaded;
    std::string core_error;
    if (!load_graph_effect(filesystem_path(path), loaded, &core_error)) {
        const QString message = QString::fromStdString(core_error);
        if (error != nullptr) *error = message;
        impl_->publish_status(message, true);
        return false;
    }
    impl_->graph_ = std::move(loaded);
    impl_->graph_path_ = path;
    impl_->dirty_ = false;
    setProperty("graphDirty", false);
    setProperty("graphPath", path);
    impl_->rebuild_scene(std::nullopt);
    impl_->rebuild_inspector(std::nullopt);
    impl_->frame_graph();
    if (graph_changed) graph_changed();
    schedule_preview();
    impl_->publish_status(tr("Graph loaded from %1").arg(path), false);
    return true;
}

void GraphEffectWidget::zoom_in() {
    impl_->view_->zoom_by(1.15);
}

void GraphEffectWidget::zoom_out() {
    impl_->view_->zoom_by(1.0 / 1.15);
}

void GraphEffectWidget::actual_size() {
    impl_->view_->actual_size();
}

void GraphEffectWidget::fit_graph() {
    impl_->frame_graph();
}

double GraphEffectWidget::zoom() const noexcept {
    return impl_->view_->zoom();
}

void GraphEffectWidget::schedule_preview() {
    impl_->preview_timer_->start();
}

bool GraphEffectWidget::apply_preview() {
    if (!preview_available()) {
        impl_->publish_status(tr("There is no valid preview to apply."), true);
        return false;
    }
    if (!apply_requested) {
        impl_->publish_status(tr("No image application handler is configured."), true);
        return false;
    }
    apply_requested(impl_->evaluation_.pixels, impl_->evaluation_.width, impl_->evaluation_.height);
    impl_->publish_status(tr("GraphEffect applied to the image"), false);
    return true;
}

} // namespace px
