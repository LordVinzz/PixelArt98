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

