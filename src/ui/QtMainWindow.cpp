// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/QtMainWindow.hpp"

#include "io/ProjectIO.hpp"
#include "depth/DepthMapExtractor.hpp"
#include "ui/QtCanvasWidget.hpp"
#include "ui/QtModelPreviewWidget.hpp"
#include "PixelArtVersion.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDateTime>
#include <QDockWidget>
#include <QFileDialog>
#include <QEventLoop>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QProgressDialog>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace px {

namespace {

QString image_filter() { return QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif);;All files (*)"); }
QString project_filter() { return QStringLiteral("PixelArt98 Projects (*.pixart);;All files (*)"); }

QVersionNumber semantic_version(QString version) {
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) version.remove(0, 1);
    return QVersionNumber::fromString(version);
}

QColor qcolor(Pixel pixel) { return QColor(r(pixel), g(pixel), b(pixel), a(pixel)); }
Pixel pixel_color(const QColor& color) { return rgba(static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()), static_cast<std::uint8_t>(color.blue()), static_cast<std::uint8_t>(color.alpha())); }
float percentage(int value) { return static_cast<float>(value) / 100.0f; }

std::filesystem::path filesystem_path(const QString& path) {
#if defined(_WIN32)
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QWidget* vertical_panel(std::initializer_list<QWidget*> widgets) {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    for (QWidget* widget : widgets) layout->addWidget(widget);
    layout->addStretch(1);
    return panel;
}

QString tool_icon_path(ToolType tool) {
    const char* file = "";
    switch (tool) {
        case ToolType::Pencil: file = "pencil.png"; break;
        case ToolType::Brush: file = "brush.png"; break;
        case ToolType::Eraser: file = "eraser.png"; break;
        case ToolType::Line: file = "line.png"; break;
        case ToolType::Rectangle: file = "rectangle.png"; break;
        case ToolType::Ellipse: file = "ellipse.png"; break;
        case ToolType::Bucket: file = "bucket.png"; break;
        case ToolType::Gradient: file = "gradient.png"; break;
        case ToolType::Eyedropper: file = "eyedropper.png"; break;
        case ToolType::CloneStamp: file = "clone_stamp.png"; break;
        case ToolType::RectSelect: file = "rectangle_select.png"; break;
        case ToolType::LassoSelect: file = "lasso.png"; break;
        case ToolType::MagicWand: file = "magic_wand.png"; break;
        case ToolType::MovePixels: file = "move_pixels.png"; break;
        case ToolType::Text: file = "text.png"; break;
    }
    return QStringLiteral(":/icons/") + QString::fromLatin1(file);
}

QIcon tool_icon(ToolType tool) {
    return QIcon(tool_icon_path(tool));
}

QIcon icon_resource(const QString& file_name) {
    return QIcon(QStringLiteral(":/icons/") + file_name);
}

QIcon rotated_icon_resource(const QString& file_name, qreal degrees) {
    QPixmap source(QStringLiteral(":/icons/") + file_name);
    return QIcon(source.transformed(QTransform().rotate(degrees), Qt::SmoothTransformation));
}

QToolButton* icon_button(const QIcon& icon, const QString& label) {
    auto* button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(22, 22));
    button->setFixedSize(QSize(30, 30));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setToolTip(label);
    button->setStatusTip(label);
    button->setAccessibleName(label);
    return button;
}

void clear_layout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        if (QLayout* child = item->layout()) clear_layout(child);
        delete item;
    }
}

void draw_alpha_checker(QPainter& painter, const QRect& rect, int cell = 6) {
    for (int y = rect.top(); y < rect.bottom(); y += cell) {
        for (int x = rect.left(); x < rect.right(); x += cell) {
            const bool dark = (((x - rect.left()) / cell) + ((y - rect.top()) / cell)) % 2 == 0;
            painter.fillRect(QRect(x, y, cell, cell), dark ? QColor(178, 178, 178) : QColor(220, 220, 220));
        }
    }
}

QString rgba_hex(const QColor& color) {
    return QStringLiteral("#%1%2%3%4")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.alpha(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

std::optional<QColor> parse_rgba_hex(QString text) {
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('#'))) text.remove(0, 1);
    if (text.size() != 6 && text.size() != 8) return std::nullopt;
    bool ok = false;
    const uint value = text.toUInt(&ok, 16);
    if (!ok) return std::nullopt;
    if (text.size() == 6) {
        return QColor(static_cast<int>((value >> 16) & 0xffU),
                      static_cast<int>((value >> 8) & 0xffU),
                      static_cast<int>(value & 0xffU), 255);
    }
    return QColor(static_cast<int>((value >> 24) & 0xffU),
                  static_cast<int>((value >> 16) & 0xffU),
                  static_cast<int>((value >> 8) & 0xffU),
                  static_cast<int>(value & 0xffU));
}

class CurveEditorWidget final : public QWidget {
public:
    explicit CurveEditorWidget(std::array<std::array<int, 256>, 4> histograms, QWidget* parent = nullptr)
        : QWidget(parent), histograms_(std::move(histograms)) {
        setObjectName(QStringLiteral("CurvesGraph"));
        setMinimumSize(420, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::CrossCursor);
        reset_curve();
        setProperty("histogramBins", 256);
        set_channel(0);
    }

    [[nodiscard]] QSize sizeHint() const override { return {560, 340}; }

    void set_channel(int channel) {
        channel_ = std::clamp(channel, 0, 3);
        setProperty("channel", channel_);
        const auto& histogram = histograms_[static_cast<std::size_t>(channel_)];
        setProperty("histogramMaximum", std::max(0, *std::max_element(histogram.begin(), histogram.end())));
        update();
    }

    void reset_curve() {
        points_ = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
        setProperty("curvePointCount", static_cast<int>(points_.size()));
        update();
        if (changed) changed();
    }

    [[nodiscard]] CurvesSettings settings() const {
        CurvesSettings result;
        result.point_count = static_cast<int>(points_.size());
        result.x.fill(0.0f);
        result.y.fill(0.0f);
        for (std::size_t index = 0; index < points_.size(); ++index) {
            result.x[index] = static_cast<float>(points_[index].x());
            result.y[index] = static_cast<float>(points_[index].y());
        }
        result.luma = channel_ == 0;
        result.red = channel_ == 1;
        result.green = channel_ == 2;
        result.blue = channel_ == 3;
        return result;
    }

    [[nodiscard]] bool is_identity() const {
        return points_.size() == 3U && points_[0] == QPointF(0.0, 0.0) &&
               points_[1] == QPointF(0.5, 0.5) && points_[2] == QPointF(1.0, 1.0);
    }

    std::function<void()> changed;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(22, 25, 30));
        const QRectF graph = graph_rect();
        painter.fillRect(graph, QColor(12, 14, 18));

        painter.setPen(QPen(QColor(58, 63, 72), 1.0));
        for (int step = 0; step <= 4; ++step) {
            const qreal t = static_cast<qreal>(step) / 4.0;
            painter.drawLine(QPointF(graph.left() + graph.width() * t, graph.top()),
                             QPointF(graph.left() + graph.width() * t, graph.bottom()));
            painter.drawLine(QPointF(graph.left(), graph.top() + graph.height() * t),
                             QPointF(graph.right(), graph.top() + graph.height() * t));
        }

        const auto& histogram = histograms_[static_cast<std::size_t>(channel_)];
        const int maximum = std::max(1, *std::max_element(histogram.begin(), histogram.end()));
        QColor histogram_color = channel_color();
        histogram_color.setAlpha(105);
        painter.setPen(Qt::NoPen);
        painter.setBrush(histogram_color);
        const qreal bin_width = graph.width() / 256.0;
        for (int bin = 0; bin < 256; ++bin) {
            const qreal normalized = std::sqrt(static_cast<qreal>(histogram[static_cast<std::size_t>(bin)]) /
                                               static_cast<qreal>(maximum));
            const qreal height = normalized * graph.height();
            painter.drawRect(QRectF(graph.left() + static_cast<qreal>(bin) * bin_width,
                                    graph.bottom() - height, std::max(1.0, bin_width), height));
        }

        QPainterPath curve;
        for (int sample = 0; sample <= 255; ++sample) {
            const qreal x = static_cast<qreal>(sample) / 255.0;
            const QPointF mapped = widget_point({x, evaluate(x)});
            if (sample == 0) curve.moveTo(mapped); else curve.lineTo(mapped);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(10, 10, 12, 180), 5.0));
        painter.drawPath(curve);
        painter.setPen(QPen(channel_color(), 2.5));
        painter.drawPath(curve);

        for (std::size_t index = 0; index < points_.size(); ++index) {
            const QPointF center = widget_point(points_[index]);
            painter.setPen(QPen(index == static_cast<std::size_t>(selected_point_) ? Qt::white : QColor(18, 20, 24), 2.0));
            painter.setBrush(index == static_cast<std::size_t>(selected_point_) ? Qt::white : channel_color());
            painter.drawEllipse(center, 5.5, 5.5);
        }
        painter.setPen(QPen(QColor(105, 110, 120), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(graph);
    }

    void mousePressEvent(QMouseEvent* event) override {
        const int nearest = nearest_point(event->position());
        if (event->button() == Qt::RightButton) {
            if (nearest > 0 && nearest + 1 < static_cast<int>(points_.size())) {
                points_.erase(points_.begin() + nearest);
                setProperty("curvePointCount", static_cast<int>(points_.size()));
                notify_changed();
            }
            return;
        }
        if (event->button() != Qt::LeftButton) return;
        if (nearest >= 0) {
            selected_point_ = nearest;
        } else if (points_.size() < static_cast<std::size_t>(kMaxCurvePoints) && graph_rect().contains(event->position())) {
            const QPointF normalized = normalized_point(event->position());
            const auto position = std::lower_bound(points_.begin(), points_.end(), normalized.x(),
                [](const QPointF& point, qreal x) { return point.x() < x; });
            selected_point_ = static_cast<int>(std::distance(points_.begin(), position));
            points_.insert(position, normalized);
            setProperty("curvePointCount", static_cast<int>(points_.size()));
            notify_changed();
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (selected_point_ < 0 || selected_point_ >= static_cast<int>(points_.size())) return;
        QPointF normalized = normalized_point(event->position());
        if (selected_point_ == 0) normalized.setX(0.0);
        else if (selected_point_ + 1 == static_cast<int>(points_.size())) normalized.setX(1.0);
        else normalized.setX(std::clamp(normalized.x(), points_[static_cast<std::size_t>(selected_point_ - 1)].x() + 0.01,
                                        points_[static_cast<std::size_t>(selected_point_ + 1)].x() - 0.01));
        points_[static_cast<std::size_t>(selected_point_)] = normalized;
        notify_changed();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            selected_point_ = -1;
            update();
            event->accept();
        }
    }

private:
    [[nodiscard]] QRectF graph_rect() const { return QRectF(rect()).adjusted(14.0, 14.0, -14.0, -14.0); }

    [[nodiscard]] QColor channel_color() const {
        if (channel_ == 1) return QColor(245, 82, 82);
        if (channel_ == 2) return QColor(76, 210, 112);
        if (channel_ == 3) return QColor(78, 142, 255);
        return QColor(225, 228, 235);
    }

    [[nodiscard]] QPointF widget_point(const QPointF& normalized) const {
        const QRectF graph = graph_rect();
        return {graph.left() + normalized.x() * graph.width(), graph.bottom() - normalized.y() * graph.height()};
    }

    [[nodiscard]] QPointF normalized_point(const QPointF& point) const {
        const QRectF graph = graph_rect();
        return {std::clamp((point.x() - graph.left()) / graph.width(), 0.0, 1.0),
                std::clamp((graph.bottom() - point.y()) / graph.height(), 0.0, 1.0)};
    }

    [[nodiscard]] int nearest_point(const QPointF& position) const {
        int nearest = -1;
        qreal distance = 11.0;
        for (std::size_t index = 0; index < points_.size(); ++index) {
            const qreal candidate = QLineF(position, widget_point(points_[index])).length();
            if (candidate < distance) { distance = candidate; nearest = static_cast<int>(index); }
        }
        return nearest;
    }

    [[nodiscard]] qreal evaluate(qreal value) const {
        if (value <= points_.front().x()) return points_.front().y();
        for (std::size_t index = 1; index < points_.size(); ++index) {
            if (value <= points_[index].x() || index + 1 == points_.size()) {
                const QPointF& left = points_[index - 1];
                const QPointF& right = points_[index];
                const qreal t = std::clamp((value - left.x()) / std::max(0.001, right.x() - left.x()), 0.0, 1.0);
                const qreal smooth = t * t * (3.0 - 2.0 * t);
                return left.y() + (right.y() - left.y()) * smooth;
            }
        }
        return points_.back().y();
    }

    void notify_changed() {
        update();
        if (changed) changed();
    }

    std::array<std::array<int, 256>, 4> histograms_{};
    std::vector<QPointF> points_;
    int channel_ = 0;
    int selected_point_ = -1;
};

} // namespace

class ColorChip final : public QWidget {
public:
    explicit ColorChip(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(48, 28);
        setCursor(Qt::PointingHandCursor);
    }

    void set_color(QColor color) { color_ = color; update(); }
    void set_selected(bool selected) { selected_ = selected; update(); }
    std::function<void()> clicked;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRect chip = rect().adjusted(1, 1, -1, -1);
        draw_alpha_checker(painter, chip);
        painter.fillRect(chip, color_);
        painter.setPen(QPen(selected_ ? QColor(30, 144, 255) : QColor(70, 74, 82), selected_ ? 2 : 1));
        painter.drawRect(chip.adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && clicked) clicked();
    }

private:
    QColor color_ = Qt::black;
    bool selected_ = false;
};

class ColorSquare final : public QWidget {
public:
    explicit ColorSquare(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(140, 96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setCursor(Qt::CrossCursor);
    }

    void set_hsv(int hue, int saturation, int value) {
        hue_ = std::clamp(hue, 0, 359);
        saturation_ = std::clamp(saturation, 0, 255);
        value_ = std::clamp(value, 0, 255);
        update();
    }

    std::function<void(int, int)> changed;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QImage image(size(), QImage::Format_RGB32);
        const int w = std::max(1, width() - 1);
        const int h = std::max(1, height() - 1);
        for (int y = 0; y < height(); ++y) {
            const int value = std::clamp(255 - (y * 255 / h), 0, 255);
            for (int x = 0; x < width(); ++x) {
                const int saturation = std::clamp(x * 255 / w, 0, 255);
                image.setPixelColor(x, y, QColor::fromHsv(hue_, saturation, value));
            }
        }
        painter.drawImage(rect(), image);
        const QPoint cursor(saturation_ * w / 255, (255 - value_) * h / 255);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawEllipse(cursor, 5, 5);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(cursor, 6, 6);
    }

    void mousePressEvent(QMouseEvent* event) override { update_from_position(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override { update_from_position(event->position()); }

private:
    void update_from_position(const QPointF& position) {
        const int w = std::max(1, width() - 1);
        const int h = std::max(1, height() - 1);
        saturation_ = std::clamp(static_cast<int>(std::round(position.x() * 255.0 / static_cast<double>(w))), 0, 255);
        value_ = std::clamp(255 - static_cast<int>(std::round(position.y() * 255.0 / static_cast<double>(h))), 0, 255);
        if (changed) changed(saturation_, value_);
        update();
    }

    int hue_ = 0;
    int saturation_ = 0;
    int value_ = 0;
};

constexpr int kFrameDurationRole = Qt::UserRole + 1;
constexpr int kFrameHandleWidth = 9;
constexpr int kFrameBaseWidth = 42;
constexpr int kFrameMaximumWidth = 600;
constexpr int kFrameItemHeight = 54;
constexpr int kFrameMillisecondsPerPixel = 4;
constexpr int kFrameDurationSnapMs = 10;

int frame_item_width(int duration_ms) {
    const int duration = std::clamp(duration_ms, kMinimumFrameDurationMs, kMaximumFrameDurationMs);
    return std::clamp(kFrameBaseWidth + duration / kFrameMillisecondsPerPixel,
                      kFrameBaseWidth + kMinimumFrameDurationMs / kFrameMillisecondsPerPixel,
                      kFrameMaximumWidth);
}

QString frame_item_label(int frame_index, int duration_ms) {
    return QObject::tr("Frame %1\n%2 ms").arg(frame_index + 1).arg(duration_ms);
}

void configure_frame_item(QListWidgetItem& item, int frame_index, int duration_ms) {
    const int duration = std::clamp(duration_ms, kMinimumFrameDurationMs, kMaximumFrameDurationMs);
    item.setText(frame_item_label(frame_index, duration));
    item.setData(kFrameDurationRole, duration);
    item.setSizeHint(QSize(frame_item_width(duration), kFrameItemHeight));
    item.setToolTip(QObject::tr("Drag the right edge to change this frame's duration"));
    item.setTextAlignment(Qt::AlignCenter);
}

class FrameTimelineDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        painter->save();
        QRect handle = option.rect;
        handle.setLeft(handle.right() - kFrameHandleWidth + 1);
        QColor handle_color = option.palette.mid().color();
        if (option.state.testFlag(QStyle::State_Selected)) {
            handle_color = option.palette.highlightedText().color();
        }
        handle_color.setAlpha(150);
        painter->fillRect(handle, handle_color);
        painter->setPen(QPen(option.palette.base().color(), 1.0));
        const int center = handle.center().x();
        painter->drawLine(center - 2, handle.top() + 10, center - 2, handle.bottom() - 10);
        painter->drawLine(center + 1, handle.top() + 10, center + 1, handle.bottom() - 10);
        painter->restore();
    }
};

class FrameTimelineWidget final : public QListWidget {
public:
    explicit FrameTimelineWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setFlow(QListView::LeftToRight);
        setWrapping(false);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSpacing(2);
        setMouseTracking(true);
        setMinimumHeight(kFrameItemHeight + 12);
        setItemDelegate(new FrameTimelineDelegate(this));
        setAccessibleDescription(tr("Timeline. Drag the right edge of a frame to stretch or shorten it."));
    }

    std::function<void(int, int)> duration_committed;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            const QPoint position = event->position().toPoint();
            if (QListWidgetItem* item = itemAt(position); item != nullptr && on_resize_handle(item, position)) {
                resize_row_ = row(item);
                resize_start_x_ = position.x();
                resize_start_duration_ms_ = item->data(kFrameDurationRole).toInt();
                resize_duration_ms_ = resize_start_duration_ms_;
                setCurrentRow(resize_row_);
                setCursor(Qt::SplitHCursor);
                setProperty("resizingFrame", resize_row_);
                setProperty("previewDurationMs", resize_duration_ms_);
                event->accept();
                return;
            }
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint position = event->position().toPoint();
        if (resize_row_ >= 0) {
            const double raw_duration = static_cast<double>(resize_start_duration_ms_) +
                                        static_cast<double>(position.x() - resize_start_x_) *
                                            static_cast<double>(kFrameMillisecondsPerPixel);
            const int bounded = static_cast<int>(std::lround(std::clamp(
                raw_duration,
                static_cast<double>(kMinimumFrameDurationMs),
                static_cast<double>(kMaximumFrameDurationMs))));
            resize_duration_ms_ = std::clamp(
                ((bounded + kFrameDurationSnapMs / 2) / kFrameDurationSnapMs) * kFrameDurationSnapMs,
                kMinimumFrameDurationMs,
                kMaximumFrameDurationMs);
            if (QListWidgetItem* item = this->item(resize_row_); item != nullptr) {
                configure_frame_item(*item, resize_row_, resize_duration_ms_);
            }
            setProperty("previewDurationMs", resize_duration_ms_);
            viewport()->update();
            event->accept();
            return;
        }

        QListWidgetItem* hovered = itemAt(position);
        setCursor(hovered != nullptr && on_resize_handle(hovered, position) ? Qt::SplitHCursor : Qt::ArrowCursor);
        QListWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (resize_row_ >= 0 && event->button() == Qt::LeftButton) {
            const int committed_row = resize_row_;
            const int committed_duration = resize_duration_ms_;
            const bool changed = committed_duration != resize_start_duration_ms_;
            resize_row_ = -1;
            setProperty("resizingFrame", -1);
            setProperty("lastCommittedDurationMs", committed_duration);
            setCursor(Qt::ArrowCursor);
            event->accept();
            if (changed && duration_committed) duration_committed(committed_row, committed_duration);
            return;
        }
        QListWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (resize_row_ < 0) setCursor(Qt::ArrowCursor);
        QListWidget::leaveEvent(event);
    }

private:
    [[nodiscard]] bool on_resize_handle(const QListWidgetItem* item, const QPoint& position) const {
        const QRect item_rect = visualItemRect(item);
        return item_rect.contains(position) && position.x() >= item_rect.right() - kFrameHandleWidth + 1;
    }

    int resize_row_ = -1;
    int resize_start_x_ = 0;
    int resize_start_duration_ms_ = 0;
    int resize_duration_ms_ = 0;
};

class PaletteList final : public QListWidget {
public:
    explicit PaletteList(QWidget* parent = nullptr) : QListWidget(parent) {}
    std::function<void(Pixel, bool)> color_chosen;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (QListWidgetItem* item = itemAt(event->position().toPoint()); item != nullptr) {
            const Pixel color = static_cast<Pixel>(item->data(Qt::UserRole).toULongLong());
            const bool secondary = event->button() == Qt::RightButton;
            if (color_chosen) color_chosen(color, secondary);
            if (secondary) {
                event->accept();
                return;
            }
        }
        QListWidget::mousePressEvent(event);
    }
};

class QtColorPanel final : public QWidget {
public:
    explicit QtColorPanel(EditorController& controller, QWidget* parent = nullptr)
        : QWidget(parent), controller_(controller) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(8);
        setMinimumHeight(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        auto* chips = new QHBoxLayout;
        primary_chip_ = new ColorChip;
        secondary_chip_ = new ColorChip;
        auto* swap = new QPushButton(tr("Swap"));
        chips->addWidget(primary_chip_);
        chips->addWidget(secondary_chip_);
        chips->addWidget(swap);
        chips->addStretch(1);
        root->addLayout(chips);

        color_square_ = new ColorSquare;
        root->addWidget(color_square_);

        auto* rgba_row = new QHBoxLayout;
        rgba_row->setContentsMargins(0, 0, 0, 0);
        rgba_row->setSpacing(6);
        red_slider_ = component_slider(255);
        green_slider_ = component_slider(255);
        blue_slider_ = component_slider(255);
        alpha_component_slider_ = component_slider(255);
        add_component_slider(rgba_row, tr("R"), red_slider_);
        add_component_slider(rgba_row, tr("G"), green_slider_);
        add_component_slider(rgba_row, tr("B"), blue_slider_);
        add_component_slider(rgba_row, tr("A"), alpha_component_slider_);
        rgba_row->addStretch(1);
        root->addLayout(rgba_row);

        auto* hsv_row = new QHBoxLayout;
        hsv_row->setContentsMargins(0, 0, 0, 0);
        hsv_row->setSpacing(6);
        hue_component_slider_ = component_slider(359);
        saturation_slider_ = component_slider(255);
        value_slider_ = component_slider(255);
        add_component_slider(hsv_row, tr("H"), hue_component_slider_);
        add_component_slider(hsv_row, tr("S"), saturation_slider_);
        add_component_slider(hsv_row, tr("V"), value_slider_);
        hsv_row->addStretch(1);
        root->addLayout(hsv_row);

        hex_edit_ = new QLineEdit;
        hex_edit_->setMaxLength(9);
        auto* form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->addRow(tr("Hex"), hex_edit_);
        root->addLayout(form);

        palette_ = new PaletteList;
        palette_->setViewMode(QListView::IconMode);
        palette_->setResizeMode(QListView::Adjust);
        palette_->setIconSize(QSize(12, 12));
        palette_->setGridSize(QSize(16, 16));
        palette_->setMinimumHeight(0);
        palette_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        root->addWidget(palette_);

        auto* palette_actions = new QHBoxLayout;
        palette_actions->setContentsMargins(0, 0, 0, 0);
        palette_actions->setSpacing(4);
        auto* add_active = new QPushButton(tr("+ Active"));
        auto* add_both = new QPushButton(tr("+ Both"));
        auto* build_ramp = new QPushButton(tr("Build Ramp"));
        auto* remove_selected = new QPushButton(tr("Remove"));
        palette_actions->addWidget(add_active);
        palette_actions->addWidget(add_both);
        palette_actions->addWidget(build_ramp);
        palette_actions->addWidget(remove_selected);
        root->addLayout(palette_actions);
        root->addStretch(1);

        primary_chip_->clicked = [this] { set_active(false); };
        secondary_chip_->clicked = [this] { set_active(true); };
        connect(swap, &QPushButton::clicked, this, [this] {
            std::swap(controller_.tool().primary, controller_.tool().secondary);
            refresh_from_tool();
            notify_changed();
        });
        color_square_->changed = [this](int saturation, int value) {
            QColor next = current_;
            next.setHsv(hue_, saturation, value, current_.alpha());
            set_current_color(next, true);
        };
        connect_rgb_slider(red_slider_);
        connect_rgb_slider(green_slider_);
        connect_rgb_slider(blue_slider_);
        connect_rgb_slider(alpha_component_slider_);
        connect_hsv_slider(hue_component_slider_);
        connect_hsv_slider(saturation_slider_);
        connect_hsv_slider(value_slider_);
        connect(hex_edit_, &QLineEdit::editingFinished, this, [this] {
            if (updating_) return;
            if (const std::optional<QColor> parsed = parse_rgba_hex(hex_edit_->text())) {
                set_current_color(*parsed, true);
            } else {
                sync_controls();
            }
        });
        palette_->color_chosen = [this](Pixel color, bool secondary) {
            if (secondary) set_active(true);
            set_current_color(qcolor(color), true);
        };
        connect(add_active, &QPushButton::clicked, this, [this] { add_palette_color(active_secondary_ ? controller_.tool().secondary : controller_.tool().primary); });
        connect(add_both, &QPushButton::clicked, this, [this] {
            const Palette before = controller_.document().palette;
            append_palette_color(controller_.tool().primary);
            append_palette_color(controller_.tool().secondary);
            commit_palette_change("Add Current Colors", before);
        });
        connect(build_ramp, &QPushButton::clicked, this, [this] { build_palette_from_current_colors(); });
        connect(remove_selected, &QPushButton::clicked, this, [this] { remove_selected_palette_colors(); });

        rebuild_palette();
        refresh_from_tool();
    }

    void refresh_from_tool() {
        rebuild_palette();
        primary_chip_->set_color(qcolor(controller_.tool().primary));
        secondary_chip_->set_color(qcolor(controller_.tool().secondary));
        primary_chip_->set_selected(!active_secondary_);
        secondary_chip_->set_selected(active_secondary_);
        set_current_color(qcolor(active_secondary_ ? controller_.tool().secondary : controller_.tool().primary), false);
    }

    std::function<void()> changed;

private:
    static QSlider* component_slider(int maximum) {
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, maximum);
        slider->setMinimumWidth(42);
        slider->setFixedHeight(22);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        slider->setSingleStep(1);
        slider->setPageStep(maximum == 359 ? 30 : 16);
        return slider;
    }

    static void add_component_slider(QHBoxLayout* row, const QString& label, QSlider* slider) {
        auto* control = new QWidget;
        auto* layout = new QHBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(3);
        auto* text = new QLabel(label);
        text->setFixedWidth(10);
        text->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(text);
        layout->addWidget(slider);
        row->addWidget(control, 1);
    }

    static void set_slider_gradient(QSlider* slider, const QString& stops) {
        slider->setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal {"
            "border: 1px solid #555; height: 9px;"
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, %1);"
            "}"
            "QSlider::handle:horizontal {"
            "background: #f2f2f2; border: 1px solid #222; width: 8px; margin: -5px 0;"
            "}").arg(stops));
    }

    static QString color_stop(double position, const QColor& color) {
        return QStringLiteral("stop:%1 %2").arg(position, 0, 'f', 3).arg(color.name());
    }

    void connect_rgb_slider(QSlider* slider) {
        connect(slider, &QSlider::valueChanged, this, [this] {
            if (updating_) return;
            set_current_color(QColor(red_slider_->value(), green_slider_->value(), blue_slider_->value(), alpha_component_slider_->value()), true);
        });
    }

    void connect_hsv_slider(QSlider* slider) {
        connect(slider, &QSlider::valueChanged, this, [this] {
            if (updating_) return;
            QColor next;
            next.setHsv(hue_component_slider_->value(), saturation_slider_->value(), value_slider_->value(), alpha_component_slider_->value());
            set_current_color(next, true);
        });
    }

    void set_active(bool secondary) {
        active_secondary_ = secondary;
        refresh_from_tool();
    }

    void set_current_color(QColor color, bool push_to_tool) {
        current_ = color.toRgb();
        const QColor hsv = current_.toHsv();
        if (hsv.hue() >= 0) hue_ = hsv.hue();
        saturation_ = hsv.saturation();
        value_ = hsv.value();
        if (push_to_tool) {
            (active_secondary_ ? controller_.tool().secondary : controller_.tool().primary) = pixel_color(current_);
            primary_chip_->set_color(qcolor(controller_.tool().primary));
            secondary_chip_->set_color(qcolor(controller_.tool().secondary));
            notify_changed();
        }
        sync_controls();
    }

    void sync_controls() {
        updating_ = true;
        red_slider_->setValue(current_.red());
        green_slider_->setValue(current_.green());
        blue_slider_->setValue(current_.blue());
        alpha_component_slider_->setValue(current_.alpha());
        hue_component_slider_->setValue(hue_);
        saturation_slider_->setValue(saturation_);
        value_slider_->setValue(value_);
        hex_edit_->setText(rgba_hex(current_));
        color_square_->set_hsv(hue_, saturation_, value_);
        set_slider_gradient(red_slider_, QStringLiteral("stop:0 #000000, stop:1 #FF0000"));
        set_slider_gradient(green_slider_, QStringLiteral("stop:0 #000000, stop:1 #00FF00"));
        set_slider_gradient(blue_slider_, QStringLiteral("stop:0 #000000, stop:1 #0000FF"));
        set_slider_gradient(alpha_component_slider_, QStringLiteral("stop:0 #000000, stop:1 #FFFFFF"));
        set_slider_gradient(hue_component_slider_, QStringLiteral("stop:0 #FF0000, stop:0.167 #FFFF00, stop:0.333 #00FF00, stop:0.500 #00FFFF, stop:0.667 #0000FF, stop:0.833 #FF00FF, stop:1 #FF0000"));
        set_slider_gradient(saturation_slider_,
                            color_stop(0.0, QColor::fromHsv(hue_, 0, value_)) + QStringLiteral(", ") +
                            color_stop(1.0, QColor::fromHsv(hue_, 255, value_)));
        set_slider_gradient(value_slider_,
                            color_stop(0.0, QColor::fromHsv(hue_, saturation_, 0)) + QStringLiteral(", ") +
                            color_stop(1.0, QColor::fromHsv(hue_, saturation_, 255)));
        primary_chip_->set_selected(!active_secondary_);
        secondary_chip_->set_selected(active_secondary_);
        updating_ = false;
    }

    void notify_changed() {
        if (changed) changed();
    }

    void append_palette_color(Pixel color) {
        auto& colors = controller_.document().palette.colors;
        if (std::find(colors.begin(), colors.end(), color) == colors.end()) {
            colors.push_back(color);
            controller_.document().palette.active = static_cast<int>(colors.size()) - 1;
        }
    }

    void commit_palette_change(const char* name, const Palette& before) {
        if (before.colors == controller_.document().palette.colors && before.active == controller_.document().palette.active) {
            return;
        }
        controller_.document().commit_palette_edit(name, before);
        controller_.mark_changed(name);
        rebuild_palette();
        notify_changed();
    }

    void add_palette_color(Pixel color) {
        const Palette before = controller_.document().palette;
        append_palette_color(color);
        commit_palette_change("Add Palette Color", before);
    }

    void build_palette_from_current_colors() {
        constexpr int steps = 8;
        const Palette before = controller_.document().palette;
        const Pixel first = controller_.tool().primary;
        const Pixel second = controller_.tool().secondary;
        auto& colors = controller_.document().palette.colors;
        colors.clear();
        colors.reserve(steps);
        for (int i = 0; i < steps; ++i) {
            const int denominator = steps - 1;
            const auto interpolate = [i](int a_value, int b_value) {
                return static_cast<std::uint8_t>((a_value * (denominator - i) + b_value * i + denominator / 2) / denominator);
            };
            colors.push_back(rgba(interpolate(r(first), r(second)),
                                  interpolate(g(first), g(second)),
                                  interpolate(b(first), b(second)),
                                  interpolate(a(first), a(second))));
        }
        controller_.document().palette.active = 0;
        commit_palette_change("Build Palette Ramp", before);
    }

    void remove_selected_palette_colors() {
        const QList<QListWidgetItem*> selected = palette_->selectedItems();
        if (selected.empty()) {
            return;
        }
        const Palette before = controller_.document().palette;
        auto& colors = controller_.document().palette.colors;
        for (QListWidgetItem* item : selected) {
            const Pixel color = static_cast<Pixel>(item->data(Qt::UserRole).toULongLong());
            const auto found = std::find(colors.begin(), colors.end(), color);
            if (found != colors.end()) {
                colors.erase(found);
            }
        }
        if (colors.empty()) {
            controller_.document().palette.active = 0;
        } else {
            controller_.document().palette.active = std::clamp(controller_.document().palette.active, 0, static_cast<int>(colors.size()) - 1);
        }
        commit_palette_change("Remove Palette Colors", before);
    }

    void rebuild_palette() {
        const QSignalBlocker blocker(palette_);
        palette_->clear();
        for (Pixel color : controller_.document().palette.colors) {
            QPixmap icon(12, 12);
            icon.fill(qcolor(color));
            auto* item = new QListWidgetItem(QIcon(icon), QString());
            item->setData(Qt::UserRole, static_cast<qulonglong>(color));
            palette_->addItem(item);
        }
    }

    EditorController& controller_;
    ColorChip* primary_chip_ = nullptr;
    ColorChip* secondary_chip_ = nullptr;
    ColorSquare* color_square_ = nullptr;
    PaletteList* palette_ = nullptr;
    QSlider* red_slider_ = nullptr;
    QSlider* green_slider_ = nullptr;
    QSlider* blue_slider_ = nullptr;
    QSlider* alpha_component_slider_ = nullptr;
    QSlider* hue_component_slider_ = nullptr;
    QSlider* saturation_slider_ = nullptr;
    QSlider* value_slider_ = nullptr;
    QLineEdit* hex_edit_ = nullptr;
    QColor current_ = Qt::black;
    int hue_ = 0;
    int saturation_ = 0;
    int value_ = 0;
    bool active_secondary_ = false;
    bool updating_ = false;
};

QtMainWindow::QtMainWindow(AppSettings settings, QWidget* parent)
    : QMainWindow(parent), settings_(settings) {
    setObjectName(QStringLiteral("PixelArt98MainWindow"));
    setWindowTitle(QStringLiteral("PixelArt98"));
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    resize(1440, 900);

    network_manager_ = new QNetworkAccessManager(this);
    canvas_ = new QtCanvasWidget(controller_, this);
    canvas_->editor_changed = [this] { refresh_all(); };
    setCentralWidget(canvas_);
    build_actions();
    build_menus();
    build_docks();
    restore_ui_state();

    playback_timer_ = new QTimer(this);
    playback_timer_->setTimerType(Qt::PreciseTimer);
    connect(playback_timer_, &QTimer::timeout, this, [this] { update_playback(); });
    refresh_all();
}

QtMainWindow::~QtMainWindow() { save_ui_state(); }

void QtMainWindow::build_actions() {
    auto* toolbar = addToolBar(tr("Canvas"));
    toolbar->setObjectName(QStringLiteral("CanvasToolbar"));
    const auto add = [this, toolbar](const QString& text, const QKeySequence& shortcut, auto callback) {
        QAction* action = toolbar->addAction(text);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    add(tr("Undo"), QKeySequence::Undo, [this] { if (controller_.undo()) refresh_all(); });
    add(tr("Redo"), QKeySequence::Redo, [this] { if (controller_.redo()) refresh_all(); });
    toolbar->addSeparator();
    add(tr("-"), QKeySequence::ZoomOut, [this] { canvas_->zoom_out(); refresh_all(); });
    zoom_label_ = new QLabel(toolbar);
    zoom_label_->setMinimumWidth(82);
    zoom_label_->setAlignment(Qt::AlignCenter);
    toolbar->addWidget(zoom_label_);
    add(tr("+"), QKeySequence::ZoomIn, [this] { canvas_->zoom_in(); refresh_all(); });
    add(tr("1:1"), QKeySequence(QStringLiteral("Ctrl+1")), [this] { canvas_->actual_size(); refresh_all(); });
    add(tr("Fit"), QKeySequence(QStringLiteral("Ctrl+0")), [this] { canvas_->fit_to_canvas(); refresh_all(); });
}

void QtMainWindow::build_menus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    auto add_action = [this](QMenu* menu, const QString& label, const QKeySequence& shortcut, auto callback) {
        QAction* action = menu->addAction(label);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    add_action(file, tr("New Document..."), QKeySequence::New, [this] { new_document(); });
    add_action(file, tr("Save Project..."), QKeySequence::Save, [this] { save_project_as(); });
    add_action(file, tr("Load Project..."), QKeySequence::Open, [this] { load_project_from(); });
    file->addSeparator();
    add_action(file, tr("Import Image as Document..."), {}, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import Image as Document"), {}, image_filter());
        if (!path.isEmpty()) import_image_document(path);
    });
    add_action(file, tr("Import Image as Layer..."), {}, [this] { import_layer(); });
    add_action(file, tr("Export Current PNG..."), {}, [this] { export_current_png(); });
    add_action(file, tr("Export Spritesheet..."), {}, [this] { export_spritesheet_file(); });
    add_action(file, tr("Export GIF..."), {}, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export GIF"), QStringLiteral("animation.gif"), tr("GIF (*.gif)"));
        std::string error; if (!path.isEmpty() && !export_gif(path.toStdString(), controller_.document(), &error)) report_error(tr("Export GIF"), error);
    });
    add_action(file, tr("Export APNG..."), {}, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export APNG"), QStringLiteral("animation.png"), tr("PNG (*.png)"));
        std::string error; if (!path.isEmpty() && !export_apng(path.toStdString(), controller_.document(), &error)) report_error(tr("Export APNG"), error);
    });
    add_action(file, tr("Import Aseprite..."), {}, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import Aseprite"), {}, tr("Aseprite (*.ase *.aseprite)"));
        Document document; std::string error; if (!path.isEmpty() && import_aseprite(path.toStdString(), document, &error)) { controller_.replace_document(std::move(document)); refresh_all(); } else if (!path.isEmpty()) report_error(tr("Import Aseprite"), error);
    });
    add_action(file, tr("Export Aseprite..."), {}, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Aseprite"), QStringLiteral("sprite.aseprite"), tr("Aseprite (*.aseprite)"));
        std::string error; if (!path.isEmpty() && !export_aseprite(path.toStdString(), controller_.document(), &error)) report_error(tr("Export Aseprite"), error);
    });
    file->addSeparator();
    for (const QString& kind : {QStringLiteral("Model JSON"), QStringLiteral("STL Model"), QStringLiteral("Minecraft Model")}) add_action(file, tr("Import %1...").arg(kind), {}, [this, kind] { import_model(kind); });
    for (const QString& kind : {QStringLiteral("Model JSON"), QStringLiteral("glTF Model"), QStringLiteral("ThreeJSPack"), QStringLiteral("STL Model"), QStringLiteral("Minecraft Model")}) add_action(file, tr("Export %1...").arg(kind), {}, [this, kind] { export_model(kind); });
    file->addSeparator();
    add_action(file, tr("Quit"), QKeySequence::Quit, [this] { close(); });

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    add_action(edit, tr("Undo"), QKeySequence::Undo, [this] { if (controller_.undo()) refresh_all(); });
    add_action(edit, tr("Redo"), QKeySequence::Redo, [this] { if (controller_.redo()) refresh_all(); });
    edit->addSeparator();
    add_action(edit, tr("Select All"), QKeySequence::SelectAll, [this] { controller_.select_all(); refresh_all(); });
    add_action(edit, tr("Deselect"), QKeySequence(QStringLiteral("Ctrl+D")), [this] { controller_.clear_selection(); refresh_all(); });
    add_action(edit, tr("Invert Selection"), QKeySequence(QStringLiteral("Ctrl+I")), [this] { controller_.invert_selection(); refresh_all(); });
    add_action(edit, tr("Delete Selection"), QKeySequence::Delete, [this] { controller_.delete_selection(); refresh_all(); });

    QMenu* image = menuBar()->addMenu(tr("&Image"));
    add_action(image, tr("Flip Horizontal"), {}, [this] { apply_transform(tr("Flip Horizontal"), apply_flip_horizontal); });
    add_action(image, tr("Flip Vertical"), {}, [this] { apply_transform(tr("Flip Vertical"), apply_flip_vertical); });
    add_action(image, tr("Rotate 90 Clockwise"), {}, [this] { apply_transform(tr("Rotate 90 Clockwise"), apply_rotate_90_clockwise); });
    add_action(image, tr("Rotate 90 Counter-Clockwise"), {}, [this] { apply_transform(tr("Rotate 90 Counter-Clockwise"), apply_rotate_90_counter_clockwise); });
    add_action(image, tr("Rotate 180"), {}, [this] { apply_transform(tr("Rotate 180"), apply_rotate_180); });
    add_action(image, tr("Straighten..."), {}, [this] { show_effect_preview(tr("Straighten"), [](Document& doc, int amount) { apply_straighten(doc, static_cast<float>(amount - 50) * 0.4f, ResamplingMode::Bicubic); }); });
    add_action(image, tr("Rotate / Zoom..."), {}, [this] { show_effect_preview(tr("Rotate / Zoom"), [](Document& doc, int amount) { apply_rotate_zoom(doc, static_cast<float>(amount - 50) * 3.6f, 1.0f, 0, 0, ResamplingMode::Bicubic); }); });

    QMenu* effects = menuBar()->addMenu(tr("E&ffects"));
    const auto effect_control = [](const char* id, const char* label, int minimum, int maximum, int initial) {
        return AdjustmentControl{QString::fromLatin1(id), QString::fromUtf8(label), minimum, maximum, initial};
    };
    const auto add_configurable_effect = [this](QMenu* menu, const char* id, const QString& name,
                                                 std::vector<AdjustmentControl> controls,
                                                 std::function<void(Document&, const std::vector<int>&)> operation,
                                                 std::function<GpuEffectRequest(const std::vector<int>&)> gpu_request) {
        add_effect_action(menu, AdjustmentSpec{QString::fromLatin1(id), name, std::move(controls),
                                               std::move(operation), std::move(gpu_request)});
    };
    const auto request_for = [](GpuEffectMode mode) {
        return [mode](const std::vector<int>&) { GpuEffectRequest request; request.mode = mode; return request; };
    };
    QMenu* artistic = effects->addMenu(tr("Artistic"));
    add_configurable_effect(artistic, "ink-sketch", tr("Ink Sketch"), {effect_control("outline", "Outline", 0, 100, 50), effect_control("coloring", "Coloring", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_ink_sketch(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::InkSketch; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(artistic, "oil-painting", tr("Oil Painting"), {effect_control("brush-size", "Brush size", 1, 16, 6), effect_control("coarseness", "Coarseness", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_oil_painting(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::OilPainting; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(artistic, "pencil-sketch", tr("Pencil Sketch"), {effect_control("tip-size", "Pencil tip size", 1, 10, 5), effect_control("range", "Color range", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_pencil_sketch(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::PencilSketch; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* blurs = effects->addMenu(tr("Blurs"));
    add_configurable_effect(blurs, "gaussian-blur", tr("Gaussian Blur"), {effect_control("radius", "Radius", 1, 32, 4)},
        [](Document& d, const std::vector<int>& v) { apply_gaussian_blur(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::GaussianBlur; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(blurs, "motion-blur", tr("Motion Blur"), {effect_control("distance", "Distance", 1, 24, 8), effect_control("angle", "Angle", -180, 180, 0)},
        [](Document& d, const std::vector<int>& v) { apply_motion_blur(d, v[0], static_cast<float>(v[1])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MotionBlur; r.params = {static_cast<float>(v[0]), 0, static_cast<float>(v[1]) * 3.14159265f / 180.0f, 0}; return r; });
    add_configurable_effect(blurs, "radial-blur", tr("Radial Blur"), {effect_control("amount", "Amount", 2, 64, 20), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_radial_blur(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::RadialBlur; r.params[1] = static_cast<float>(v[0]); r.params2 = {percentage(v[1]), percentage(v[2]), 0, 0}; return r; });
    add_configurable_effect(blurs, "zoom-blur", tr("Zoom Blur"), {effect_control("amount", "Amount", 2, 64, 20), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_zoom_blur(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::ZoomBlur; r.params[1] = static_cast<float>(v[0]); r.params2 = {percentage(v[1]), percentage(v[2]), 0, 0}; return r; });
    add_configurable_effect(blurs, "median-blur", tr("Median Blur"), {effect_control("radius", "Radius", 1, 8, 2)},
        [](Document& d, const std::vector<int>& v) { apply_median_blur(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MedianBlur; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(blurs, "surface-blur", tr("Surface Blur"), {effect_control("radius", "Radius", 1, 8, 3), effect_control("threshold", "Threshold", 0, 255, 32)},
        [](Document& d, const std::vector<int>& v) { apply_surface_blur(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::SurfaceBlur; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* color = effects->addMenu(tr("Color"));
    add_configurable_effect(color, "auto-level-effect", tr("Auto-Level"), {}, [](Document& d, const std::vector<int>&) { apply_auto_level(d); }, request_for(GpuEffectMode::AutoLevel));
    add_configurable_effect(color, "black-white", tr("Black and White"), {}, [](Document& d, const std::vector<int>&) { apply_grayscale(d); }, request_for(GpuEffectMode::Grayscale));
    add_configurable_effect(color, "sepia", tr("Sepia"), {}, [](Document& d, const std::vector<int>&) { apply_sepia(d); }, request_for(GpuEffectMode::Sepia));
    add_configurable_effect(color, "invert-colors", tr("Invert Colors"), {}, [](Document& d, const std::vector<int>&) { apply_invert(d, false); }, request_for(GpuEffectMode::InvertColors));
    add_configurable_effect(color, "invert-alpha", tr("Invert Alpha"), {}, [](Document& d, const std::vector<int>&) { apply_invert(d, true); }, request_for(GpuEffectMode::InvertAlpha));
    add_configurable_effect(color, "posterize-effect", tr("Posterize"), {effect_control("levels", "Levels", 2, 32, 8)}, [](Document& d, const std::vector<int>& v) { apply_posterize(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Posterize; r.params[0] = static_cast<float>(v[0]); return r; });
    QMenu* distort = effects->addMenu(tr("Distort"));
    add_configurable_effect(distort, "bulge", tr("Bulge"), {effect_control("strength", "Strength", -200, 200, 50), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_bulge(d, percentage(v[0]), v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Bulge; r.params[0] = percentage(v[0]); r.params2 = {percentage(v[1]), percentage(v[2]), 0, 0}; return r; });
    add_configurable_effect(distort, "crystalize", tr("Crystalize"), {effect_control("cell-size", "Cell size", 2, 64, 12)},
        [](Document& d, const std::vector<int>& v) { apply_crystalize(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Crystalize; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "dents", tr("Dents"), {effect_control("scale", "Scale", 2, 256, 64), effect_control("amount", "Amount", 1, 64, 20)},
        [](Document& d, const std::vector<int>& v) { apply_dents(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Dents; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(distort, "frosted-glass", tr("Frosted Glass"), {effect_control("scatter-radius", "Scatter radius", 1, 32, 5)},
        [](Document& d, const std::vector<int>& v) { apply_frosted_glass(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::FrostedGlass; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "pixelate", tr("Pixelate"), {effect_control("cell-size", "Cell size", 2, 64, 8)},
        [](Document& d, const std::vector<int>& v) { apply_pixelate(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Pixelate; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "polar-inversion", tr("Polar Inversion"), {effect_control("scale", "Scale (%)", 10, 200, 100)},
        [](Document& d, const std::vector<int>& v) { apply_polar_inversion(d, percentage(v[0])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::PolarInversion; r.params[0] = percentage(v[0]); return r; });
    add_configurable_effect(distort, "tile-reflection", tr("Tile Reflection"), {effect_control("tile-size", "Tile size", 2, 256, 32)},
        [](Document& d, const std::vector<int>& v) { apply_tile_reflection(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::TileReflection; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(distort, "twist", tr("Twist"), {effect_control("turns", "Turns × 100", -400, 400, 100), effect_control("size", "Effect size (%)", 10, 200, 100), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_twist(d, percentage(v[0]), v[2], v[3], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Twist; r.params = {percentage(v[0]), percentage(v[1]), 0, 0}; r.params2 = {percentage(v[2]), percentage(v[3]), 0, 0}; return r; });
    QMenu* noise = effects->addMenu(tr("Noise"));
    add_configurable_effect(noise, "add-noise", tr("Add Noise"), {effect_control("intensity", "Intensity", 0, 255, 64), effect_control("coverage", "Coverage", 0, 100, 100), effect_control("color-saturation", "Color saturation", 0, 100, 100)},
        [](Document& d, const std::vector<int>& v) { apply_add_noise(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::AddNoise; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), 0}; return r; });
    add_configurable_effect(noise, "median", tr("Median"), {effect_control("radius", "Radius", 1, 8, 2)},
        [](Document& d, const std::vector<int>& v) { apply_median_blur(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MedianBlur; r.params[0] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(noise, "reduce-noise", tr("Reduce Noise"), {effect_control("radius", "Radius", 1, 8, 2)},
        [](Document& d, const std::vector<int>& v) { apply_reduce_noise(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::ReduceNoise; r.params[0] = static_cast<float>(v[0]); return r; });
    QMenu* object = effects->addMenu(tr("Object"));
    add_configurable_effect(object, "feather", tr("Feather"), {effect_control("radius", "Radius", 1, 16, 3)},
        [](Document& d, const std::vector<int>& v) { apply_morphology(d, v[0], true); }, {});
    add_configurable_effect(object, "outline-object", tr("Outline"), {effect_control("thickness", "Thickness", 1, 16, 3), effect_control("intensity", "Intensity", 0, 255, 255)},
        [](Document& d, const std::vector<int>& v) { apply_outline(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Outline; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* photo = effects->addMenu(tr("Photo"));
    add_configurable_effect(photo, "glow", tr("Glow"), {effect_control("radius", "Radius", 1, 16, 5), effect_control("brightness", "Brightness", -100, 100, 30), effect_control("contrast", "Contrast", -100, 100, 0)},
        [](Document& d, const std::vector<int>& v) { apply_glow(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Glow; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), 0}; return r; });
    add_configurable_effect(photo, "red-eye-removal", tr("Red Eye Removal"), {effect_control("strength", "Strength", 0, 100, 70)},
        [](Document& d, const std::vector<int>& v) { apply_red_eye_removal(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::RedEyeRemoval; r.params[1] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(photo, "sharpen", tr("Sharpen"), {effect_control("amount", "Amount", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_sharpen(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Sharpen; r.params[1] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(photo, "soften-portrait", tr("Soften Portrait"), {effect_control("softness", "Softness", 0, 100, 50), effect_control("lighting", "Lighting", -100, 100, 0), effect_control("warmth", "Warmth", -100, 100, 0)},
        [](Document& d, const std::vector<int>& v) { apply_soften_portrait(d, v[0], v[1], v[2]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::SoftenPortrait; r.params = {static_cast<float>(std::max(1, v[0] / 12)), static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])}; return r; });
    add_configurable_effect(photo, "vignette", tr("Vignette"), {effect_control("radius", "Radius (%)", 10, 200, 70), effect_control("strength", "Strength", 0, 100, 60), effect_control("center-x", "Center X (%)", 0, 100, 50), effect_control("center-y", "Center Y (%)", 0, 100, 50)},
        [](Document& d, const std::vector<int>& v) { apply_vignette(d, v[0], v[1], v[2], v[3]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Vignette; r.params = {percentage(v[0]), static_cast<float>(v[1]), 0, 0}; r.params2 = {percentage(v[2]), percentage(v[3]), 0, 0}; return r; });
    QMenu* render = effects->addMenu(tr("Render"));
    add_configurable_effect(render, "clouds", tr("Clouds"), {effect_control("scale", "Scale", 2, 512, 96), effect_control("roughness", "Roughness / octaves", 1, 8, 4)},
        [this](Document& d, const std::vector<int>& v) { apply_clouds(d, v[0], v[1], controller_.tool().primary, controller_.tool().secondary); }, [this](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Clouds; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; r.primary = controller_.tool().primary; r.secondary = controller_.tool().secondary; return r; });
    add_configurable_effect(render, "julia-fractal", tr("Julia Fractal"), {effect_control("zoom", "Zoom × 100", 10, 1000, 100), effect_control("angle", "Angle", -180, 180, 0)},
        [](Document& d, const std::vector<int>& v) { apply_julia_fractal(d, percentage(v[0]), static_cast<float>(v[1])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::JuliaFractal; r.params = {percentage(v[0]), 0, static_cast<float>(v[1]) * 3.14159265f / 180.0f, 0}; return r; });
    add_configurable_effect(render, "mandelbrot-fractal", tr("Mandelbrot Fractal"), {effect_control("zoom", "Zoom × 100", 10, 1000, 100), effect_control("angle", "Angle", -180, 180, 0), effect_control("invert", "Invert (0 / 1)", 0, 1, 0)},
        [](Document& d, const std::vector<int>& v) { apply_mandelbrot_fractal(d, percentage(v[0]), static_cast<float>(v[1]), v[2] != 0); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::MandelbrotFractal; r.params = {percentage(v[0]), static_cast<float>(v[2]), static_cast<float>(v[1]) * 3.14159265f / 180.0f, 0}; return r; });
    add_configurable_effect(render, "turbulence", tr("Turbulence"), {effect_control("scale", "Scale", 2, 512, 96), effect_control("octaves", "Octaves", 1, 8, 4)},
        [](Document& d, const std::vector<int>& v) { apply_turbulence(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Turbulence; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    QMenu* stylize = effects->addMenu(tr("Stylize"));
    add_configurable_effect(stylize, "edge-detect", tr("Edge Detect"), {effect_control("strength", "Strength", 1, 200, 100)},
        [](Document& d, const std::vector<int>& v) { apply_edge_detect(d, v[0]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::EdgeDetect; r.params[1] = static_cast<float>(v[0]); return r; });
    add_configurable_effect(stylize, "emboss", tr("Emboss"), {effect_control("angle", "Angle", -180, 180, 45)},
        [](Document& d, const std::vector<int>& v) { apply_emboss(d, static_cast<float>(v[0])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Emboss; r.params[0] = static_cast<float>(v[0]) * 3.14159265f / 180.0f; return r; });
    add_configurable_effect(stylize, "outline-stylize", tr("Outline"), {effect_control("thickness", "Thickness", 1, 16, 3), effect_control("intensity", "Intensity", 0, 255, 255)},
        [](Document& d, const std::vector<int>& v) { apply_outline(d, v[0], v[1]); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Outline; r.params = {static_cast<float>(v[0]), static_cast<float>(v[1]), 0, 0}; return r; });
    add_configurable_effect(stylize, "relief", tr("Relief"), {effect_control("angle", "Angle", -180, 180, 45)},
        [](Document& d, const std::vector<int>& v) { apply_relief(d, static_cast<float>(v[0])); }, [](const std::vector<int>& v) { GpuEffectRequest r; r.mode = GpuEffectMode::Relief; r.params[0] = static_cast<float>(v[0]) * 3.14159265f / 180.0f; return r; });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QAction* grid = add_action(view, tr("Grid"), {}, [this](bool checked) { canvas_->set_grid_visible(checked); }); grid->setCheckable(true); grid->setChecked(true);
    QAction* checker = add_action(view, tr("Checkerboard"), {}, [this](bool checked) { canvas_->set_checker_visible(checked); }); checker->setCheckable(true); checker->setChecked(true);
    view->addSeparator();
    add_action(view, tr("Zoom In"), QKeySequence::ZoomIn, [this] { canvas_->zoom_in(); refresh_all(); });
    add_action(view, tr("Zoom Out"), QKeySequence::ZoomOut, [this] { canvas_->zoom_out(); refresh_all(); });
    add_action(view, tr("Actual Size"), QKeySequence(QStringLiteral("Ctrl+1")), [this] { canvas_->actual_size(); refresh_all(); });
    add_action(view, tr("Fit to Canvas"), QKeySequence(QStringLiteral("Ctrl+0")), [this] { canvas_->fit_to_canvas(); refresh_all(); });

    QMenu* options = menuBar()->addMenu(tr("&Options"));
    auto option = [this, options](const QString& label, bool& setting) {
        QAction* action = options->addAction(label); action->setCheckable(true); action->setChecked(setting);
        connect(action, &QAction::toggled, this, [&setting](bool checked) { setting = checked; });
    };
    option(tr("Show Splash Screen"), settings_.show_splash_screen);
    option(tr("Auto-open Error Console"), settings_.auto_open_error_console);
    option(tr("Heavy GPU Optimization"), settings_.heavy_gpu_optimization);
#if defined(__APPLE__)
    option(tr("MPS Backend"), settings_.mps_backend);
#endif

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    add_action(help, tr("Check for Updates..."), {}, [this] { check_for_updates(); });
    help->addSeparator();
    add_action(help, tr("About PixelArt98"), {}, [this] { show_about_dialog(); });
}

void QtMainWindow::build_docks() {
    build_tools_dock(); build_colors_dock(); build_layers_dock(); build_adjustments_dock();
    build_animation_dock(); build_history_dock(); build_model_dock(); build_preview_docks(); build_console_dock();
    auto* window = new QMenu(tr("&Window"), this);
    menuBar()->insertMenu(menuBar()->actions().back(), window);
    for (QDockWidget* dock : docks_) window->addAction(dock->toggleViewAction());
}

void QtMainWindow::build_tools_dock() {
    auto* dock = new QDockWidget(tr("Tools"), this);
    dock->setObjectName(QStringLiteral("ToolsDock"));

    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    auto* tools_panel = new QWidget;
    auto* tools_grid = new QGridLayout(tools_panel);
    tools_grid->setContentsMargins(0, 0, 0, 0);
    tools_grid->setHorizontalSpacing(4);
    tools_grid->setVerticalSpacing(4);
    tools_grid->setSizeConstraint(QLayout::SetFixedSize);
    constexpr int tool_button_size = 40;
    constexpr int tool_grid_spacing = 4;
    constexpr int tool_grid_columns = 2;
    constexpr int tool_grid_width = tool_grid_columns * tool_button_size + (tool_grid_columns - 1) * tool_grid_spacing;
    tools_panel->setFixedWidth(tool_grid_width);
    tool_buttons_ = new QButtonGroup(this);
    tool_buttons_->setExclusive(true);

    constexpr std::array<ToolType, 15> tools = {
        ToolType::Pencil, ToolType::Brush, ToolType::Eraser, ToolType::Line, ToolType::Rectangle,
        ToolType::Ellipse, ToolType::Bucket, ToolType::Gradient, ToolType::Eyedropper, ToolType::CloneStamp,
        ToolType::RectSelect, ToolType::LassoSelect, ToolType::MagicWand, ToolType::MovePixels, ToolType::Text
    };
    for (int index = 0; index < static_cast<int>(tools.size()); ++index) {
        const ToolType tool = tools[static_cast<std::size_t>(index)];
        auto* button = new QToolButton;
        button->setCheckable(true);
        button->setIcon(tool_icon(tool));
        button->setIconSize(QSize(28, 28));
        button->setFixedSize(QSize(tool_button_size, tool_button_size));
        button->setToolTip(QString::fromUtf8(tool_name(tool)));
        button->setStatusTip(QString::fromUtf8(tool_name(tool)));
        tool_buttons_->addButton(button, static_cast<int>(tool));
        tool_button_widgets_.push_back(button);
        tools_grid->addWidget(button, index / 2, index % 2);
    }

    connect(tool_buttons_, &QButtonGroup::idClicked, this, [this](int tool_id) {
        controller_.tool().tool = static_cast<ToolType>(tool_id);
        rebuild_tool_options();
        set_status(QString::fromUtf8(tool_name(controller_.tool().tool)));
    });

    auto* tool_options_panel = new QWidget;
    tool_options_panel->setFixedWidth(tool_grid_width);
    tool_options_layout_ = new QVBoxLayout(tool_options_panel);
    tool_options_layout_->setContentsMargins(0, 0, 0, 0);
    tool_options_layout_->setSpacing(6);

    layout->addWidget(tools_panel, 0, Qt::AlignHCenter);
    layout->addWidget(tool_options_panel);
    layout->addStretch(1);

    panel->setFixedWidth(tool_grid_width + layout->contentsMargins().left() + layout->contentsMargins().right());
    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    docks_.push_back(dock);

    if (QAbstractButton* pencil_button = tool_buttons_->button(static_cast<int>(ToolType::Pencil)); pencil_button != nullptr) {
        pencil_button->setChecked(true);
    }
    rebuild_tool_options();
}

void QtMainWindow::build_colors_dock() {
    auto* dock = new QDockWidget(tr("Colors"), this);
    dock->setObjectName(QStringLiteral("ColorsDock"));
    color_panel_ = new QtColorPanel(controller_);
    color_panel_->changed = [this] {
        canvas_->update();
        set_status(tr("Primary %1  Secondary %2")
                       .arg(rgba_hex(qcolor(controller_.tool().primary)), rgba_hex(qcolor(controller_.tool().secondary))));
    };
    dock->setWidget(color_panel_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    docks_.push_back(dock);
}

void QtMainWindow::build_layers_dock() {
    auto* dock = new QDockWidget(tr("Layers"), this); dock->setObjectName(QStringLiteral("LayersDock")); layers_list_ = new QListWidget;
    auto* add = icon_button(icon_resource(QStringLiteral("new_layer.png")), tr("New Layer"));
    auto* duplicate = icon_button(icon_resource(QStringLiteral("duplicate_layer.png")), tr("Duplicate Layer"));
    auto* remove = icon_button(icon_resource(QStringLiteral("remove_layer.png")), tr("Remove Layer"));
    auto* up = icon_button(icon_resource(QStringLiteral("up.png")), tr("Move Layer Up"));
    auto* down = icon_button(rotated_icon_resource(QStringLiteral("up.png"), 180.0), tr("Move Layer Down"));
    auto* layer_actions = new QWidget;
    auto* layer_actions_layout = new QHBoxLayout(layer_actions);
    layer_actions_layout->setContentsMargins(0, 0, 0, 0);
    layer_actions_layout->setSpacing(4);
    layer_actions_layout->addWidget(add);
    layer_actions_layout->addWidget(duplicate);
    layer_actions_layout->addWidget(remove);
    layer_actions_layout->addWidget(up);
    layer_actions_layout->addWidget(down);
    layer_actions_layout->addStretch(1);
    blend_mode_ = new QComboBox; for (int i = 0; i <= static_cast<int>(LayerBlendMode::Xor); ++i) blend_mode_->addItem(QString::fromUtf8(layer_blend_mode_name(static_cast<LayerBlendMode>(i))), i);
    auto* opacity = new QSlider(Qt::Horizontal); opacity->setRange(0, 100); opacity->setValue(100);
    auto* visible = new QCheckBox(tr("Visible")); visible->setChecked(true); auto* clip = new QCheckBox(tr("Clip to below")); auto* mask = new QCheckBox(tr("Layer mask"));
    auto* reveal_mask = icon_button(icon_resource(QStringLiteral("reveal_mask.png")), tr("Reveal Mask"));
    auto* hide_mask = icon_button(icon_resource(QStringLiteral("hide_mask.png")), tr("Hide Mask"));
    auto* selection_mask = icon_button(icon_resource(QStringLiteral("mask_from_selection.png")), tr("Mask from Selection"));
    auto* clear_mask = icon_button(icon_resource(QStringLiteral("clear_from_selection.png")), tr("Clear Mask"));
    auto* mask_actions = new QWidget;
    auto* mask_actions_layout = new QHBoxLayout(mask_actions);
    mask_actions_layout->setContentsMargins(0, 0, 0, 0);
    mask_actions_layout->setSpacing(4);
    mask_actions_layout->addWidget(reveal_mask);
    mask_actions_layout->addWidget(hide_mask);
    mask_actions_layout->addWidget(selection_mask);
    mask_actions_layout->addWidget(clear_mask);
    mask_actions_layout->addStretch(1);
    connect(layers_list_, &QListWidget::currentRowChanged, this, [this, opacity, visible, clip, mask](int row) { if (row >= 0 && row < static_cast<int>(controller_.document().layers.size())) { controller_.document().active_layer = row; const Layer& layer = controller_.document().layers[static_cast<std::size_t>(row)]; const QSignalBlocker block_opacity(opacity); const QSignalBlocker block_visible(visible); const QSignalBlocker block_clip(clip); const QSignalBlocker block_mask(mask); blend_mode_->setCurrentIndex(static_cast<int>(layer.blend_mode)); opacity->setValue(static_cast<int>(layer.opacity * 100.0f)); visible->setChecked(layer.visible); clip->setChecked(layer.clip_to_below); mask->setChecked(layer.mask_enabled); controller_.invalidate_display(); canvas_->update(); } });
    connect(layers_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { bool ok = false; const QString name = QInputDialog::getText(this, tr("Rename Layer"), tr("Name"), QLineEdit::Normal, item->text(), &ok); if (ok && !name.isEmpty()) { controller_.document().layers[static_cast<std::size_t>(layers_list_->row(item))].name = name.toStdString(); controller_.mark_changed("Rename Layer"); refresh_all(); } });
    connect(add, &QToolButton::clicked, this, [this] { controller_.document().add_layer("Layer"); controller_.mark_changed("Add Layer"); refresh_all(); });
    connect(duplicate, &QToolButton::clicked, this, [this] { controller_.document().duplicate_layer(controller_.document().active_layer); controller_.mark_changed("Duplicate Layer"); refresh_all(); });
    connect(remove, &QToolButton::clicked, this, [this] { if (controller_.document().remove_layer(controller_.document().active_layer)) controller_.mark_changed("Remove Layer"); refresh_all(); });
    connect(up, &QToolButton::clicked, this, [this] { const int from = controller_.document().active_layer; controller_.document().move_layer(from, std::max(0, from - 1)); controller_.mark_changed("Move Layer"); refresh_all(); });
    connect(down, &QToolButton::clicked, this, [this] { const int from = controller_.document().active_layer; controller_.document().move_layer(from, std::min(static_cast<int>(controller_.document().layers.size()) - 1, from + 1)); controller_.mark_changed("Move Layer"); refresh_all(); });
    connect(blend_mode_, &QComboBox::currentIndexChanged, this, [this](int index) { if (controller_.document().active_layer < static_cast<int>(controller_.document().layers.size())) { controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)].blend_mode = static_cast<LayerBlendMode>(index); controller_.mark_changed("Blend Mode"); refresh_all(); } });
    connect(opacity, &QSlider::valueChanged, this, [this](int value) { controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)].opacity = static_cast<float>(value) / 100.0f; controller_.mark_changed("Layer Opacity"); refresh_all(); });
    connect(visible, &QCheckBox::toggled, this, [this](bool checked) { controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)].visible = checked; controller_.mark_changed("Layer Visibility"); refresh_all(); });
    connect(clip, &QCheckBox::toggled, this, [this](bool checked) { controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)].clip_to_below = checked; controller_.mark_changed("Layer Clipping"); refresh_all(); });
    connect(mask, &QCheckBox::toggled, this, [this](bool checked) { Layer& layer = controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)]; layer.mask_enabled = checked; if (checked && layer.mask.size() != controller_.document().active_cel().pixels.size()) layer.mask.assign(controller_.document().active_cel().pixels.size(), 255); controller_.mark_changed("Layer Mask"); refresh_all(); });
    connect(reveal_mask, &QToolButton::clicked, this, [this] { Layer& layer = controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)]; layer.mask.assign(controller_.document().active_cel().pixels.size(), 255); layer.mask_enabled = true; controller_.mark_changed("Reveal Mask"); refresh_all(); });
    connect(hide_mask, &QToolButton::clicked, this, [this] { Layer& layer = controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)]; layer.mask.assign(controller_.document().active_cel().pixels.size(), 0); layer.mask_enabled = true; controller_.mark_changed("Hide Mask"); refresh_all(); });
    connect(selection_mask, &QToolButton::clicked, this, [this] { Layer& layer = controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)]; layer.mask.resize(controller_.document().selection.mask.size()); for (std::size_t i = 0; i < layer.mask.size(); ++i) layer.mask[i] = controller_.document().selection.active && controller_.document().selection.mask[i] != 0 ? 255 : 0; layer.mask_enabled = true; controller_.mark_changed("Mask from Selection"); refresh_all(); });
    connect(clear_mask, &QToolButton::clicked, this, [this] { Layer& layer = controller_.document().layers[static_cast<std::size_t>(controller_.document().active_layer)]; layer.mask.clear(); layer.mask_enabled = false; controller_.mark_changed("Clear Mask"); refresh_all(); });
    dock->setWidget(vertical_panel({layers_list_, layer_actions, visible, clip, blend_mode_, opacity, mask, mask_actions})); addDockWidget(Qt::RightDockWidgetArea, dock); docks_.push_back(dock);
}

void QtMainWindow::build_adjustments_dock() {
    auto control = [](const char* id, const char* label, int minimum, int maximum, int initial) {
        return AdjustmentControl{QString::fromLatin1(id), QString::fromUtf8(label), minimum, maximum, initial};
    };
    std::vector<AdjustmentSpec> adjustments;
    adjustments.push_back({QStringLiteral("brightness-contrast"), tr("Brightness / Contrast"),
        {control("brightness", "Brightness", -255, 255, 0), control("contrast", "Contrast", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) { apply_brightness_contrast(document, value[0], value[1]); },
        [](const std::vector<int>& value) {
            const float contrast = static_cast<float>(value[1]);
            const float factor = (259.0f * (contrast + 255.0f)) / (255.0f * (259.0f - contrast));
            GpuEffectRequest request; request.mode = GpuEffectMode::BrightnessContrast;
            request.params = {static_cast<float>(value[0]) / 255.0f, (factor - 1.0f) * 0.5f, 0.0f, 0.0f}; return request;
        }});
    adjustments.push_back({QStringLiteral("hsv"), tr("HSV"),
        {control("hue", "Hue", -180, 180, 0), control("saturation", "Saturation", -100, 100, 0),
         control("value", "Value", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) {
            apply_hsv(document, static_cast<float>(value[0]), static_cast<float>(value[1]) / 100.0f,
                      static_cast<float>(value[2]) / 100.0f);
        },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::Hsv;
            request.params = {static_cast<float>(value[0]) / 360.0f, static_cast<float>(value[1]) / 100.0f,
                              static_cast<float>(value[2]) / 100.0f, 0.0f}; return request;
        }});
    adjustments.push_back({QStringLiteral("temperature"), tr("Temperature"),
        {control("temperature", "Temperature", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) { apply_temperature(document, value[0]); },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::Temperature;
            request.params[0] = static_cast<float>(value[0]) / 100.0f; return request;
        }});
    adjustments.push_back({QStringLiteral("levels"), tr("Levels"),
        {control("input-black", "Input black", 0, 254, 0), control("input-white", "Input white", 1, 255, 255),
         control("gamma", "Gamma × 100", 5, 300, 100), control("output-black", "Output black", 0, 255, 0),
         control("output-white", "Output white", 0, 255, 255)},
        [](Document& document, const std::vector<int>& value) {
            LevelsSettings settings; settings.in_black = value[0]; settings.in_white = value[1];
            settings.gamma = static_cast<float>(value[2]) / 100.0f; settings.out_black = value[3]; settings.out_white = value[4];
            apply_levels(document, settings);
        },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::Levels;
            request.params = {static_cast<float>(value[0]) / 255.0f, static_cast<float>(value[1]) / 255.0f,
                              static_cast<float>(value[2]) / 100.0f, static_cast<float>(value[3]) / 255.0f};
            request.params2[0] = static_cast<float>(value[4]) / 255.0f; return request;
        }});
    adjustments.push_back({QStringLiteral("tonal-range"), tr("Tonal Range"),
        {control("white-point", "White point", -100, 100, 0), control("highlights", "Highlights", -100, 100, 0),
         control("shadows", "Shadows", -100, 100, 0), control("black-point", "Black point", -100, 100, 0)},
        [](Document& document, const std::vector<int>& value) { apply_tonal_range(document, value[0], value[1], value[2], value[3]); },
        [](const std::vector<int>& value) {
            GpuEffectRequest request; request.mode = GpuEffectMode::TonalRange;
            request.params = {static_cast<float>(value[0]) / 100.0f, static_cast<float>(value[1]) / 100.0f,
                              static_cast<float>(value[2]) / 100.0f, static_cast<float>(value[3]) / 100.0f}; return request;
        }});
    adjustments.push_back({QStringLiteral("curves"), tr("Curves"), {}, {}, {}});
    adjustments.push_back({QStringLiteral("auto-level"), tr("Auto-Level"), {},
        [](Document& document, const std::vector<int>&) { apply_auto_level(document); },
        [](const std::vector<int>&) { GpuEffectRequest request; request.mode = GpuEffectMode::AutoLevel; return request; }});
    adjustments.push_back({QStringLiteral("posterize"), tr("Posterize"), {control("levels", "Levels", 2, 32, 8)},
        [](Document& document, const std::vector<int>& value) { apply_posterize(document, value[0]); },
        [](const std::vector<int>& value) { GpuEffectRequest request; request.mode = GpuEffectMode::Posterize; request.params[0] = static_cast<float>(value[0]); return request; }});
    adjustments.push_back({QStringLiteral("quantize"), tr("Quantize"), {},
        [this](Document& document, const std::vector<int>&) { apply_palette_quantize(document, controller_.document().palette, false); },
        [this](const std::vector<int>&) { GpuEffectRequest request; request.mode = GpuEffectMode::PaletteQuantize; request.params[0] = static_cast<float>(std::max<std::size_t>(2, controller_.document().palette.colors.size())); return request; }});
    adjustments.push_back({QStringLiteral("dither"), tr("Dither"), {},
        [this](Document& document, const std::vector<int>&) { apply_palette_quantize(document, controller_.document().palette, true); },
        [this](const std::vector<int>&) { GpuEffectRequest request; request.mode = GpuEffectMode::PaletteDither; request.params[0] = static_cast<float>(std::max<std::size_t>(2, controller_.document().palette.colors.size())); return request; }});

    auto* dock = new QDockWidget(tr("Adjustments"), this);
    dock->setObjectName(QStringLiteral("AdjustmentsDock"));
    auto* panel = new QWidget;
    auto* grid = new QGridLayout(panel);
    int index = 0;
    for (const AdjustmentSpec& adjustment : adjustments) {
        auto* button = new QPushButton(adjustment.name);
        button->setObjectName(QStringLiteral("adjustment.") + adjustment.id);
        grid->addWidget(button, index / 2, index % 2);
        connect(button, &QPushButton::clicked, this, [this, adjustment] { show_adjustment_dialog(adjustment); });
        ++index;
    }
    auto* depth = new QPushButton(tr("Generate Depth Map..."));
    grid->addWidget(depth, index / 2, 0, 1, 2);
    connect(depth, &QPushButton::clicked, this, [this] { generate_depth_map(); });
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    docks_.push_back(dock);
}

void QtMainWindow::build_animation_dock() {
    auto* dock = new QDockWidget(tr("Animation"), this);
    dock->setObjectName(QStringLiteral("AnimationDock"));
    auto* timeline = new FrameTimelineWidget;
    frames_list_ = timeline;
    frames_list_->setObjectName(QStringLiteral("AnimationFrames"));

    auto* play = icon_button(icon_resource(QStringLiteral("play_pause_animation.png")), tr("Play / Pause"));
    auto* stop = icon_button(icon_resource(QStringLiteral("stop_animation.png")), tr("Stop"));
    auto* add = icon_button(icon_resource(QStringLiteral("new_frame_animation.png")), tr("New Frame"));
    auto* duplicate = icon_button(icon_resource(QStringLiteral("duplicate_layer.png")), tr("Duplicate Frame"));
    auto* remove = icon_button(icon_resource(QStringLiteral("remove_layer.png")), tr("Delete Frame"));
    play->setObjectName(QStringLiteral("animation.playPause"));
    stop->setObjectName(QStringLiteral("animation.stop"));
    add->setObjectName(QStringLiteral("animation.newFrame"));
    duplicate->setObjectName(QStringLiteral("animation.duplicateFrame"));
    remove->setObjectName(QStringLiteral("animation.deleteFrame"));
    play->setCheckable(true);

    auto* onion = new QCheckBox(tr("Onion Skin"));
    onion->setObjectName(QStringLiteral("animation.onionSkin"));
    onion->setChecked(true);

    auto* actions = new QWidget;
    actions->setObjectName(QStringLiteral("AnimationActions"));
    auto* actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(4);
    actions_layout->addWidget(play);
    actions_layout->addWidget(stop);
    actions_layout->addWidget(add);
    actions_layout->addWidget(duplicate);
    actions_layout->addWidget(remove);
    actions_layout->addStretch(1);
    actions_layout->addWidget(onion);

    timeline->duration_committed = [this](int frame_index, int duration_ms) {
        if (!controller_.document().set_frame_duration(frame_index, duration_ms)) return;
        controller_.mark_changed("Change Frame Duration");
        if (playing_ && frame_index == controller_.document().active_frame) {
            playback_timer_->setInterval(duration_ms);
        }
        refresh_frames();
        setWindowModified(controller_.modified());
        setWindowTitle(QStringLiteral("PixelArt98[*]"));
        set_status(QString::fromStdString(controller_.status()));
    };
    connect(frames_list_, &QListWidget::currentRowChanged, this, [this](int row) { if (row >= 0) { controller_.document().active_frame = row; controller_.invalidate_display(); canvas_->update(); } });
    connect(play, &QToolButton::toggled, this, [this](bool checked) { playing_ = checked; if (playing_) playback_timer_->start(std::max(20, controller_.document().frames[static_cast<std::size_t>(controller_.document().active_frame)].duration_ms)); else playback_timer_->stop(); });
    connect(stop, &QToolButton::clicked, this, [this, play] { playing_ = false; playback_timer_->stop(); play->setChecked(false); });
    connect(add, &QToolButton::clicked, this, [this] { controller_.document().add_frame(false); controller_.mark_changed("Add Frame"); refresh_all(); });
    connect(duplicate, &QToolButton::clicked, this, [this] { controller_.document().duplicate_frame(controller_.document().active_frame); controller_.mark_changed("Duplicate Frame"); refresh_all(); });
    connect(remove, &QToolButton::clicked, this, [this] { if (controller_.document().remove_frame(controller_.document().active_frame)) controller_.mark_changed("Delete Frame"); refresh_all(); });
    connect(onion, &QCheckBox::toggled, canvas_, &QtCanvasWidget::set_onion_visible);
    dock->setWidget(vertical_panel({frames_list_, actions}));
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    docks_.push_back(dock);
}

void QtMainWindow::build_history_dock() {
    auto* dock = new QDockWidget(tr("History"), this); dock->setObjectName(QStringLiteral("HistoryDock")); auto* undo = new QPushButton(tr("Undo")); auto* redo = new QPushButton(tr("Redo"));
    connect(undo, &QPushButton::clicked, this, [this] { if (controller_.undo()) refresh_all(); }); connect(redo, &QPushButton::clicked, this, [this] { if (controller_.redo()) refresh_all(); });
    dock->setWidget(vertical_panel({undo, redo, new QLabel(tr("Document history preserves operation labels and redo branches."))})); addDockWidget(Qt::LeftDockWidgetArea, dock); docks_.push_back(dock);
}

void QtMainWindow::build_model_dock() {
    auto* dock = new QDockWidget(tr("Cuboid / UV Editor"), this); dock->setObjectName(QStringLiteral("ModelDock")); model_list_ = new QListWidget; auto* add = new QPushButton(tr("Add Cuboid")); auto* remove = new QPushButton(tr("Remove")); auto* face = new QComboBox; for (int i = 0; i < 6; ++i) face->addItem(QString::fromUtf8(model_face_name(i)), i);
    connect(model_list_, &QListWidget::currentRowChanged, this, [this](int row) { if (row >= 0) controller_.model().selected_cuboid = row; });
    connect(add, &QPushButton::clicked, this, [this] { controller_.model().add_cuboid(); controller_.mark_changed("Add Cuboid"); refresh_model(); });
    connect(remove, &QPushButton::clicked, this, [this] { if (controller_.model().remove_selected()) controller_.mark_changed("Remove Cuboid"); refresh_model(); });
    connect(face, &QComboBox::currentIndexChanged, this, [this](int index) { controller_.model().selected_face = index; });
    dock->setWidget(vertical_panel({model_list_, add, remove, face})); addDockWidget(Qt::LeftDockWidgetArea, dock); docks_.push_back(dock);
}

void QtMainWindow::build_preview_docks() {
    auto* tile = new QDockWidget(tr("Tile Preview"), this); tile->setObjectName(QStringLiteral("TilePreviewDock")); tile_preview_label_ = new QLabel(tr("Tile preview follows the active canvas.")); tile_preview_label_->setAlignment(Qt::AlignCenter); tile->setWidget(tile_preview_label_); addDockWidget(Qt::BottomDockWidgetArea, tile); docks_.push_back(tile); tile->hide();
    auto* model = new QDockWidget(tr("3D Preview"), this); model->setObjectName(QStringLiteral("ModelPreviewDock")); model_preview_ = new QtModelPreviewWidget(controller_); model_preview_->model_changed = [this] { refresh_model(); }; model->setWidget(model_preview_); addDockWidget(Qt::RightDockWidgetArea, model); docks_.push_back(model); model->hide();
}

void QtMainWindow::build_console_dock() { auto* dock = new QDockWidget(tr("Error Console"), this); dock->setObjectName(QStringLiteral("ErrorConsoleDock")); console_list_ = new QListWidget; dock->setWidget(console_list_); addDockWidget(Qt::BottomDockWidgetArea, dock); docks_.push_back(dock); dock->hide(); }

void QtMainWindow::rebuild_tool_options() {
    if (tool_options_layout_ == nullptr) return;
    clear_layout(tool_options_layout_);

    tool_size_spin_ = nullptr;
    tolerance_spin_ = nullptr;
    clone_source_label_ = nullptr;

    const ToolType tool = controller_.tool().tool;

    auto add_size_control = [this](const QString& label) {
        auto* control = new QWidget;
        auto* row = new QHBoxLayout(control);
        row->setContentsMargins(0, 0, 0, 0);
        auto* text = new QLabel(label);
        tool_size_spin_ = new QSpinBox;
        tool_size_spin_->setRange(1, 32);
        tool_size_spin_->setValue(controller_.tool().brush_size);
        connect(tool_size_spin_, &QSpinBox::valueChanged, this, [this](int value) { controller_.tool().brush_size = value; });
        row->addWidget(text);
        row->addStretch(1);
        row->addWidget(tool_size_spin_);
        tool_options_layout_->addWidget(control);
    };

    auto add_tolerance_control = [this]() {
        auto* control = new QWidget;
        auto* row = new QHBoxLayout(control);
        row->setContentsMargins(0, 0, 0, 0);
        auto* text = new QLabel(tr("Tolerance"));
        tolerance_spin_ = new QSpinBox;
        tolerance_spin_->setRange(0, 442);
        tolerance_spin_->setValue(controller_.tool().tolerance);
        connect(tolerance_spin_, &QSpinBox::valueChanged, this, [this](int value) { controller_.tool().tolerance = value; });
        row->addWidget(text);
        row->addStretch(1);
        row->addWidget(tolerance_spin_);
        tool_options_layout_->addWidget(control);

        auto* contiguous = new QCheckBox(tr("Contiguous"));
        contiguous->setChecked(controller_.tool().contiguous);
        connect(contiguous, &QCheckBox::toggled, this, [this](bool value) { controller_.tool().contiguous = value; });
        tool_options_layout_->addWidget(contiguous);
    };

    switch (tool) {
        case ToolType::Pencil:
            break;
        case ToolType::Brush:
            add_size_control(tr("Brush size"));
            break;
        case ToolType::Eraser:
            add_size_control(tr("Brush size"));
            break;
        case ToolType::Line:
            add_size_control(tr("Stroke width"));
            break;
        case ToolType::Rectangle:
            add_size_control(tr("Stroke width"));
            break;
        case ToolType::Ellipse:
            add_size_control(tr("Stroke width"));
            break;
        case ToolType::Bucket:
            add_tolerance_control();
            break;
        case ToolType::Gradient:
            break;
        case ToolType::Eyedropper:
            break;
        case ToolType::CloneStamp:
            add_size_control(tr("Brush size"));
            clone_source_label_ = new QLabel;
            clone_source_label_->setWordWrap(true);
            tool_options_layout_->addWidget(clone_source_label_);
            break;
        case ToolType::RectSelect:
            break;
        case ToolType::LassoSelect:
            break;
        case ToolType::MagicWand:
            add_tolerance_control();
            break;
        case ToolType::MovePixels:
            break;
        case ToolType::Text:
            break;
    }

    if (clone_source_label_ != nullptr) {
        if (controller_.tool().clone_source_x >= 0 && controller_.tool().clone_source_y >= 0) {
            clone_source_label_->setText(tr("Source: (%1, %2)").arg(controller_.tool().clone_source_x).arg(controller_.tool().clone_source_y));
        } else {
            clone_source_label_->setText(tr("Source: not set"));
        }
    }
    tool_options_layout_->addStretch(1);
}

void QtMainWindow::refresh_all() {
    controller_.invalidate_display();
    canvas_->update();
    if (model_preview_ != nullptr) model_preview_->update();
    refresh_layers(); refresh_frames(); refresh_model();
    if (color_panel_ != nullptr) color_panel_->refresh_from_tool();
    if (tool_buttons_ != nullptr) {
        if (QAbstractButton* button = tool_buttons_->button(static_cast<int>(controller_.tool().tool)); button != nullptr) {
            const QSignalBlocker blocker(button);
            button->setChecked(true);
        }
    }
    rebuild_tool_options();
    if (zoom_label_ != nullptr) zoom_label_->setText(tr("%1%").arg(static_cast<int>(canvas_->zoom() * 100.0)));
    if (tile_preview_label_ != nullptr) {
        const auto& pixels = controller_.display_pixels();
        if (!pixels.empty()) {
            const QImage source(reinterpret_cast<const uchar*>(pixels.data()), controller_.document().width,
                                controller_.document().height, controller_.document().width * static_cast<int>(sizeof(Pixel)),
                                QImage::Format_RGBA8888);
            const QImage tile = source.scaled(64, 64, Qt::KeepAspectRatio, Qt::FastTransformation);
            QPixmap preview(192, 192); preview.fill(QColor(45, 48, 54)); QPainter painter(&preview);
            for (int y = 0; y < preview.height(); y += tile.height()) for (int x = 0; x < preview.width(); x += tile.width()) painter.drawImage(x, y, tile);
            tile_preview_label_->setPixmap(preview);
        }
    }
    setWindowModified(controller_.modified());
    setWindowTitle(QStringLiteral("PixelArt98[*]"));
    set_status(QString::fromStdString(controller_.status()));
}

void QtMainWindow::refresh_layers() { if (layers_list_ == nullptr) return; const QSignalBlocker blocker(layers_list_); layers_list_->clear(); for (const Layer& layer : controller_.document().layers) { auto* item = new QListWidgetItem(QString::fromStdString(layer.name)); item->setCheckState(layer.visible ? Qt::Checked : Qt::Unchecked); layers_list_->addItem(item); } layers_list_->setCurrentRow(controller_.document().active_layer); }
void QtMainWindow::refresh_frames() {
    if (frames_list_ == nullptr) return;
    const QSignalBlocker blocker(frames_list_);
    frames_list_->clear();
    for (int index = 0; index < static_cast<int>(controller_.document().frames.size()); ++index) {
        const int duration = controller_.document().frames[static_cast<std::size_t>(index)].duration_ms;
        auto* item = new QListWidgetItem;
        configure_frame_item(*item, index, duration);
        frames_list_->addItem(item);
    }
    frames_list_->setCurrentRow(controller_.document().active_frame);
}
void QtMainWindow::refresh_model() { if (model_list_ == nullptr) return; const QSignalBlocker blocker(model_list_); model_list_->clear(); for (const Cuboid& cuboid : controller_.model().cuboids) model_list_->addItem(QString::fromStdString(cuboid.name)); model_list_->setCurrentRow(controller_.model().selected_cuboid); }
void QtMainWindow::set_status(const QString& status) { statusBar()->showMessage(status); }
void QtMainWindow::report_error(const QString& operation, const std::string& error) { const QString text = tr("%1: %2").arg(operation, QString::fromStdString(error)); if (console_list_ != nullptr) console_list_->addItem(tr("%1. %2").arg(++error_sequence_).arg(text)); QMessageBox::critical(this, operation, text); if (settings_.auto_open_error_console && console_list_ != nullptr) console_list_->parentWidget()->show(); }

bool QtMainWindow::confirm_discard() { if (!controller_.modified()) return true; const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"), tr("Discard changes to the current project?"), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel); return answer == QMessageBox::Discard; }
void QtMainWindow::new_document() { if (!confirm_discard()) return; QDialog dialog(this); dialog.setWindowTitle(tr("New Document")); QFormLayout form(&dialog); QSpinBox width; QSpinBox height; width.setRange(1, 32768); height.setRange(1, 32768); width.setValue(64); height.setValue(64); form.addRow(tr("Width"), &width); form.addRow(tr("Height"), &height); QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form.addRow(&buttons); connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); if (dialog.exec() == QDialog::Accepted) { controller_.new_document(width.value(), height.value()); project_path_.clear(); canvas_->fit_to_canvas(); refresh_all(); } }
void QtMainWindow::save_project_as() { QString path = project_path_; if (path.isEmpty()) path = QFileDialog::getSaveFileName(this, tr("Save Project"), QStringLiteral("untitled.pixart"), project_filter()); if (path.isEmpty()) return; std::string error; if (!save_project(filesystem_path(path), controller_.document(), controller_.model(), &error)) report_error(tr("Save Project"), error); else { project_path_ = path; controller_.set_modified(false); set_status(tr("Saved %1").arg(path)); refresh_all(); } }
void QtMainWindow::load_project_from() { if (!confirm_discard()) return; const QString path = QFileDialog::getOpenFileName(this, tr("Load Project"), {}, project_filter()); if (path.isEmpty()) return; ProjectBundle bundle; std::string error; if (!load_project(filesystem_path(path), bundle, &error)) report_error(tr("Load Project"), error); else { controller_.replace_project(std::move(bundle.document), std::move(bundle.model)); project_path_ = path; canvas_->fit_to_canvas(); refresh_all(); } }
void QtMainWindow::import_image_document(const QString& path) { if (!confirm_discard()) return; Document document; std::string error; if (!import_image(path.toStdString(), document, &error)) report_error(tr("Import Image"), error); else { controller_.replace_document(std::move(document)); project_path_.clear(); canvas_->fit_to_canvas(); refresh_all(); } }
void QtMainWindow::import_layer() { const QString path = QFileDialog::getOpenFileName(this, tr("Import Image as Layer"), {}, image_filter()); if (path.isEmpty()) return; std::string error; if (!import_image_as_layer(path.toStdString(), controller_.document(), QFileInfo(path).baseName().toStdString(), &error)) report_error(tr("Import Layer"), error); else { controller_.mark_changed("Import Layer"); refresh_all(); } }
void QtMainWindow::export_current_png() { const QString path = QFileDialog::getSaveFileName(this, tr("Export PNG"), QStringLiteral("export.png"), tr("PNG (*.png)")); std::string error; if (!path.isEmpty() && !export_png(path.toStdString(), controller_.document(), controller_.document().active_frame, &error)) report_error(tr("Export PNG"), error); }
void QtMainWindow::export_spritesheet_file() { const QString path = QFileDialog::getSaveFileName(this, tr("Export Spritesheet"), QStringLiteral("spritesheet.png"), tr("PNG (*.png)")); if (path.isEmpty()) return; const QString json = QFileInfo(path).absolutePath() + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral(".json"); std::string error; if (!export_spritesheet(path.toStdString(), json.toStdString(), controller_.document(), &error)) report_error(tr("Export Spritesheet"), error); }

void QtMainWindow::generate_depth_map() {
    if (controller_.document().layers.empty()) return;
    QDialog settings_dialog(this); settings_dialog.setWindowTitle(tr("Generate Depth Map")); QFormLayout form(&settings_dialog);
    auto* backend = new QLabel(QString::fromStdString(depth_backend_build_description())); auto* source = new QComboBox;
    for (const Layer& layer : controller_.document().layers) source->addItem(QString::fromStdString(layer.name));
    source->setCurrentIndex(controller_.document().active_layer);
    auto* tile = new QSpinBox; tile->setRange(64, 4096); tile->setValue(settings_.depth_tile_size);
    auto* overlap = new QSpinBox; overlap->setRange(0, 2048); overlap->setValue(settings_.depth_tile_overlap);
    auto* cpu = new QCheckBox(tr("Allow real CPU model fallback")); cpu->setChecked(settings_.depth_allow_cpu_fallback);
    form.addRow(tr("Compiled backends"), backend); form.addRow(tr("Source layer"), source); form.addRow(tr("Tile size"), tile); form.addRow(tr("Overlap"), overlap); form.addRow(cpu);
    if (!depth_backend_compiled()) form.addRow(new QLabel(tr("No real depth backend is compiled. Install ONNX Runtime or OpenCV DNN support and reconfigure.")));
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form.addRow(&buttons); buttons.button(QDialogButtonBox::Ok)->setEnabled(depth_backend_compiled());
    connect(&buttons, &QDialogButtonBox::accepted, &settings_dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &settings_dialog, &QDialog::reject);
    if (settings_dialog.exec() != QDialog::Accepted) return;
    settings_.depth_tile_size = tile->value(); settings_.depth_tile_overlap = std::min(overlap->value(), tile->value() / 2); settings_.depth_allow_cpu_fallback = cpu->isChecked();
    const int source_layer = source->currentIndex(); const std::vector<Pixel> source_pixels = controller_.document().cel(controller_.document().active_frame, source_layer).pixels;
    DepthExtractionSettings extraction_settings; extraction_settings.cache_dir = default_depth_model_cache_dir(); extraction_settings.tile_size = settings_.depth_tile_size; extraction_settings.overlap = settings_.depth_tile_overlap; extraction_settings.allow_cpu_fallback = settings_.depth_allow_cpu_fallback; extraction_settings.acceleration = settings_.heavy_gpu_optimization ? (settings_.mps_backend ? DepthAccelerationPreference::Metal : DepthAccelerationPreference::Gpu) : DepthAccelerationPreference::Cpu;
    std::atomic_bool cancel = false; std::atomic_bool done = false; std::mutex progress_mutex; DepthExtractionProgress current_progress; DepthExtractionResult result; DepthExtractionError error; bool succeeded = false;
    std::thread worker([&] { DepthMapExtractor extractor; succeeded = extractor.extract(source_pixels, controller_.document().width, controller_.document().height, extraction_settings, cancel, [&](const DepthExtractionProgress& value) { std::lock_guard lock(progress_mutex); current_progress = value; }, result, error); done.store(true); });
    QProgressDialog progress(tr("Starting depth extraction"), tr("Cancel"), 0, 1000, this); progress.setWindowTitle(tr("Generate Depth Map")); progress.setWindowModality(Qt::WindowModal); progress.setAutoClose(false); progress.show();
    QEventLoop loop; QTimer poll; poll.setInterval(40); connect(&poll, &QTimer::timeout, this, [&] { { std::lock_guard lock(progress_mutex); progress.setLabelText(QString::fromStdString(current_progress.status)); progress.setValue(static_cast<int>(std::clamp(current_progress.fraction, 0.0f, 1.0f) * 1000.0f)); } if (done.load()) loop.quit(); }); connect(&progress, &QProgressDialog::canceled, this, [&] { cancel.store(true); }); poll.start(); loop.exec(); worker.join(); progress.close();
    if (succeeded) { controller_.document().insert_layer(std::min(source_layer + 1, static_cast<int>(controller_.document().layers.size())), "Depth Map", std::move(result.pixels), "Generate Depth Map"); controller_.mark_changed("Generate Depth Map"); refresh_all(); set_status(QString::fromStdString(result.status)); }
    else if (!cancel.load()) report_error(tr("Generate Depth Map"), error.message);
}

void QtMainWindow::import_model(const QString& kind) { const QString filter = kind.startsWith(QStringLiteral("STL")) ? tr("STL (*.stl)") : tr("JSON (*.json)"); const QString path = QFileDialog::getOpenFileName(this, tr("Import %1").arg(kind), {}, filter); if (path.isEmpty()) return; std::string error; bool ok = false; if (kind.startsWith(QStringLiteral("STL"))) ok = import_stl_model(path.toStdString(), controller_.model(), &error); else if (kind.startsWith(QStringLiteral("Minecraft"))) ok = import_minecraft_model(path.toStdString(), controller_.model(), &error); else ok = import_model_json(path.toStdString(), controller_.model(), &error); if (!ok) report_error(tr("Import %1").arg(kind), error); else { controller_.mark_changed("Import Model"); refresh_all(); } }
void QtMainWindow::export_model(const QString& kind) { QString extension = QStringLiteral("json"); if (kind.startsWith(QStringLiteral("glTF"))) extension = QStringLiteral("gltf"); if (kind.startsWith(QStringLiteral("Three"))) extension = QStringLiteral("zip"); if (kind.startsWith(QStringLiteral("STL"))) extension = QStringLiteral("stl"); const QString path = QFileDialog::getSaveFileName(this, tr("Export %1").arg(kind), QStringLiteral("model.") + extension); if (path.isEmpty()) return; std::string error; bool ok = false; if (kind.startsWith(QStringLiteral("glTF"))) ok = export_gltf_model(path.toStdString(), controller_.model(), "texture.png", &error); else if (kind.startsWith(QStringLiteral("Three"))) ok = export_threejs_pack(path.toStdString(), controller_.document(), controller_.model(), &error); else if (kind.startsWith(QStringLiteral("STL"))) ok = export_stl_model(path.toStdString(), controller_.model(), &error); else if (kind.startsWith(QStringLiteral("Minecraft"))) ok = export_minecraft_model(path.toStdString(), controller_.model(), "texture.png", &error); else ok = export_model_json(path.toStdString(), controller_.model(), &error); if (!ok) report_error(tr("Export %1").arg(kind), error); }

void QtMainWindow::apply_transform(const QString& name, const std::function<void(Document&)>& operation) { operation(controller_.document()); controller_.mark_changed(name.toStdString()); refresh_all(); }
QAction* QtMainWindow::add_effect_action(QMenu* menu, const QString& name, EffectOperation operation,
                                         GpuEffectRequestFactory gpu_request) {
    QAction* action = menu->addAction(name);
    action->setObjectName(QStringLiteral("effect.") + name);
    connect(action, &QAction::triggered, this, [this, name, operation, gpu_request] {
        show_effect_preview(name, operation, gpu_request);
    });
    return action;
}

QAction* QtMainWindow::add_effect_action(QMenu* menu, AdjustmentSpec effect) {
    QAction* action = menu->addAction(effect.name);
    action->setObjectName(QStringLiteral("effect.") + effect.id);
    connect(action, &QAction::triggered, this, [this, effect] { show_adjustment_dialog(effect); });
    return action;
}

void QtMainWindow::show_effect_preview(const QString& name, const EffectOperation& operation,
                                       const GpuEffectRequestFactory& gpu_request) {
    Document original = controller_.document();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("EffectPreviewDialog"));
    dialog.setWindowTitle(tr("Effect Preview: %1").arg(name));
    QVBoxLayout layout(&dialog);
    auto* amount = new QSlider(Qt::Horizontal);
    amount->setObjectName(QStringLiteral("EffectAmountSlider"));
    amount->setRange(0, 100);
    amount->setValue(50);
    auto* value = new QLabel(QStringLiteral("50"));
    layout.addWidget(new QLabel(name));
    layout.addWidget(amount);
    layout.addWidget(value);
    QDialogButtonBox buttons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons.setObjectName(QStringLiteral("EffectDialogButtons"));
    layout.addWidget(&buttons);
    QPushButton* apply_button = buttons.button(QDialogButtonBox::Apply);
    QPushButton* cancel_button = buttons.button(QDialogButtonBox::Cancel);
    apply_button->setDefault(true);
    apply_button->setAutoDefault(true);
    cancel_button->setDefault(false);
    cancel_button->setAutoDefault(false);
    const auto preview = [this, &original, operation, gpu_request](int setting) {
        controller_.document() = original;
        if (!gpu_request || !apply_mps_preview(controller_.document(), gpu_request(setting))) {
            operation(controller_.document(), setting);
            last_effect_backend_ = "cpu";
        }
        controller_.invalidate_display();
        canvas_->update();
    };
    connect(amount, &QSlider::valueChanged, this, [preview, value](int setting) {
        value->setText(QString::number(setting));
        preview(setting);
    });
    connect(apply_button, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);
    preview(50);
    if (dialog.exec() == QDialog::Accepted) {
        controller_.mark_changed(name.toStdString());
    } else {
        controller_.document() = std::move(original);
        controller_.invalidate_display();
    }
    refresh_all();
}

bool QtMainWindow::apply_mps_preview(Document& document, const GpuEffectRequest& request) {
    if (!settings_.heavy_gpu_optimization || !settings_.mps_backend) return false;
    std::vector<Pixel> output;
    const bool rendered = mps_effect_renderer_.render_active_cel(document, request) &&
                          mps_effect_renderer_.read_output_pixels(output) &&
                          output.size() == document.active_cel().pixels.size();
    if (!rendered) return false;
    document.active_cel().pixels = std::move(output);
    last_effect_backend_ = "mps";
    return true;
}

void QtMainWindow::show_curves_dialog(const QString& name) {
    Document original = controller_.document();
    std::array<std::array<int, 256>, 4> histograms = {
        original.histogram_luma(), original.histogram_channel(0),
        original.histogram_channel(1), original.histogram_channel(2)};

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AdjustmentDialog.curves"));
    dialog.setWindowTitle(tr("Adjustment: %1").arg(name));
    dialog.resize(640, 500);
    QVBoxLayout layout(&dialog);

    auto* channel_row = new QWidget;
    auto* channel_layout = new QHBoxLayout(channel_row);
    channel_layout->setContentsMargins(0, 0, 0, 0);
    channel_layout->addWidget(new QLabel(tr("Channel:")));
    QButtonGroup channel_group(&dialog);
    const std::array<std::pair<const char*, const char*>, 4> channels = {{{"luma", "Luminosity"}, {"red", "Red"},
                                                                          {"green", "Green"}, {"blue", "Blue"}}};
    for (int index = 0; index < static_cast<int>(channels.size()); ++index) {
        auto* radio = new QRadioButton(tr(channels[static_cast<std::size_t>(index)].second));
        radio->setObjectName(QStringLiteral("CurvesChannel.") + QString::fromLatin1(channels[static_cast<std::size_t>(index)].first));
        channel_group.addButton(radio, index);
        channel_layout->addWidget(radio);
        if (index == 0) radio->setChecked(true);
    }
    channel_layout->addStretch(1);
    layout.addWidget(channel_row);

    auto* editor = new CurveEditorWidget(std::move(histograms));
    layout.addWidget(editor, 1);
    auto* help_row = new QWidget;
    auto* help_layout = new QHBoxLayout(help_row);
    help_layout->setContentsMargins(0, 0, 0, 0);
    auto* help = new QLabel(tr("Drag points to shape the curve. Click to add a point; right-click an interior point to remove it."));
    help->setWordWrap(true);
    auto* reset = new QPushButton(tr("Reset"));
    reset->setObjectName(QStringLiteral("CurvesReset"));
    help_layout->addWidget(help, 1);
    help_layout->addWidget(reset);
    layout.addWidget(help_row);

    const auto preview = [this, &original, editor] {
        controller_.document() = original;
        if (editor->is_identity()) {
            last_effect_backend_ = "none";
            controller_.invalidate_display();
            canvas_->update();
            return;
        }
        const CurvesSettings settings = editor->settings();
        GpuEffectRequest request;
        request.mode = GpuEffectMode::Curves;
        request.curve_x = settings.x;
        request.curve_y = settings.y;
        request.curve_point_count = settings.point_count;
        request.params2 = {settings.luma ? 1.0f : 0.0f, settings.red ? 1.0f : 0.0f,
                           settings.green ? 1.0f : 0.0f, settings.blue ? 1.0f : 0.0f};
        if (!apply_mps_preview(controller_.document(), request)) {
            apply_curves(controller_.document(), settings);
            last_effect_backend_ = "cpu";
        }
        controller_.invalidate_display();
        canvas_->update();
    };
    editor->changed = preview;
    connect(&channel_group, &QButtonGroup::idClicked, this, [editor, preview](int channel) {
        editor->set_channel(channel);
        preview();
    });
    connect(reset, &QPushButton::clicked, editor, &CurveEditorWidget::reset_curve);

    QDialogButtonBox buttons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons.setObjectName(QStringLiteral("AdjustmentDialogButtons"));
    QPushButton* apply_button = buttons.button(QDialogButtonBox::Apply);
    QPushButton* cancel_button = buttons.button(QDialogButtonBox::Cancel);
    apply_button->setObjectName(QStringLiteral("AdjustmentApply"));
    cancel_button->setObjectName(QStringLiteral("AdjustmentCancel"));
    apply_button->setDefault(true);
    apply_button->setAutoDefault(true);
    cancel_button->setDefault(false);
    cancel_button->setAutoDefault(false);
    layout.addWidget(&buttons);
    connect(apply_button, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);

    preview();
    if (dialog.exec() == QDialog::Accepted) {
        if (controller_.document().active_cel().pixels != original.active_cel().pixels) {
            controller_.mark_changed(name.toStdString());
        }
    } else {
        controller_.document() = std::move(original);
        controller_.invalidate_display();
    }
    refresh_all();
}

void QtMainWindow::show_adjustment_dialog(const AdjustmentSpec& adjustment) {
    if (adjustment.id == QStringLiteral("curves")) {
        show_curves_dialog(adjustment.name);
        return;
    }
    Document original = controller_.document();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AdjustmentDialog.") + adjustment.id);
    dialog.setWindowTitle(tr("Adjustment: %1").arg(adjustment.name));
    QFormLayout form(&dialog);
    std::vector<QSlider*> sliders;
    sliders.reserve(adjustment.controls.size());

    const auto preview = [this, &original, &adjustment, &sliders] {
        controller_.document() = original;
        std::vector<int> values;
        values.reserve(sliders.size());
        for (const QSlider* slider : sliders) values.push_back(slider->value());
        if (!adjustment.gpu_request || !apply_mps_preview(controller_.document(), adjustment.gpu_request(values))) {
            adjustment.operation(controller_.document(), values);
            last_effect_backend_ = "cpu";
        }
        controller_.invalidate_display();
        canvas_->update();
    };

    for (const AdjustmentControl& control : adjustment.controls) {
        auto* row = new QWidget;
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(QStringLiteral("AdjustmentControl.") + control.id);
        slider->setRange(control.minimum, control.maximum);
        slider->setValue(control.initial);
        auto* spin = new QSpinBox;
        spin->setObjectName(QStringLiteral("AdjustmentValue.") + control.id);
        spin->setRange(control.minimum, control.maximum);
        spin->setValue(control.initial);
        spin->setMinimumWidth(72);
        row_layout->addWidget(slider, 1);
        row_layout->addWidget(spin);
        form.addRow(control.label, row);
        sliders.push_back(slider);
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, this, [preview](int) { preview(); });
    }

    QDialogButtonBox buttons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons.setObjectName(QStringLiteral("AdjustmentDialogButtons"));
    QPushButton* apply_button = buttons.button(QDialogButtonBox::Apply);
    QPushButton* cancel_button = buttons.button(QDialogButtonBox::Cancel);
    apply_button->setObjectName(QStringLiteral("AdjustmentApply"));
    cancel_button->setObjectName(QStringLiteral("AdjustmentCancel"));
    apply_button->setDefault(true);
    apply_button->setAutoDefault(true);
    cancel_button->setDefault(false);
    cancel_button->setAutoDefault(false);
    form.addRow(&buttons);
    connect(apply_button, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);

    preview();
    if (dialog.exec() == QDialog::Accepted) {
        controller_.mark_changed(adjustment.name.toStdString());
    } else {
        controller_.document() = std::move(original);
        controller_.invalidate_display();
    }
    refresh_all();
}

void QtMainWindow::replace_document_for_testing(Document document) {
    controller_.replace_document(std::move(document));
    refresh_all();
}

void QtMainWindow::update_playback() { if (!playing_ || controller_.document().frames.empty()) return; int next = controller_.document().active_frame + playback_direction_; const int last = static_cast<int>(controller_.document().frames.size()) - 1; if (controller_.document().playback_mode == PlaybackMode::Loop) next = next > last ? 0 : next; else if (next > last || next < 0) { playback_direction_ *= -1; next = std::clamp(controller_.document().active_frame + playback_direction_, 0, last); } controller_.document().active_frame = next; controller_.invalidate_display(); playback_timer_->setInterval(std::max(20, controller_.document().frames[static_cast<std::size_t>(next)].duration_ms)); refresh_frames(); canvas_->update(); }

void QtMainWindow::show_about_dialog() {
    const QString version_text = QString::fromLatin1(version::kVersion);
    const QString revision_text = QString::fromLatin1(version::kShortCommit);
    const QString build_text = QString::fromLatin1(version::kDescription);
    QMessageBox::about(
        this,
        tr("About PixelArt98"),
        tr("<h3>PixelArt98</h3>"
           "<p>Version %1<br>Revision %2<br>Build %3</p>"
           "<p>A native pixel-art editor inspired by classic Paint workflows.</p>")
            .arg(version_text.toHtmlEscaped(), revision_text.toHtmlEscaped(), build_text.toHtmlEscaped()));
}

void QtMainWindow::check_for_updates() {
    if (update_check_in_progress_) {
        set_status(tr("An update check is already in progress"));
        return;
    }

    update_check_in_progress_ = true;
    set_status(tr("Checking for updates..."));

    const char* api_url = version::kIsRelease
        ? version::kLatestReleaseApiUrl
        : version::kContinuousReleaseApiUrl;
    QNetworkRequest request{QUrl(QString::fromLatin1(api_url))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "PixelArt98-UpdateChecker");
    request.setTransferTimeout(15'000);

    QNetworkReply* reply = network_manager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        update_check_in_progress_ = false;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            set_status(tr("Update check failed"));
            QMessageBox::warning(
                this, tr("Check for Updates"),
                tr("PixelArt98 could not check for updates.\n\n%1").arg(reply->errorString()));
            return;
        }

        QJsonParseError parse_error;
        const QJsonDocument response = QJsonDocument::fromJson(reply->readAll(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !response.isObject()) {
            set_status(tr("Invalid update response"));
            QMessageBox::warning(
                this, tr("Check for Updates"),
                tr("GitHub returned an invalid update response."));
            return;
        }

        const QJsonObject release = response.object();
        const QString release_tag = release.value(QStringLiteral("tag_name")).toString();
        QString available_label = release_tag;
        bool update_available = false;
        if (version::kIsRelease) {
            const QVersionNumber current = semantic_version(QString::fromLatin1(version::kVersion));
            const QVersionNumber available = semantic_version(release_tag);
            if (release_tag.isEmpty() || current.isNull() || available.isNull()) {
                set_status(tr("Invalid release version"));
                QMessageBox::warning(
                    this, tr("Check for Updates"),
                    tr("The latest GitHub release does not contain a valid semantic version."));
                return;
            }
            update_available = QVersionNumber::compare(available, current) > 0;
        } else {
            const QString remote_commit =
                release.value(QStringLiteral("target_commitish")).toString();
            if (remote_commit.isEmpty()) {
                set_status(tr("Invalid continuous release"));
                QMessageBox::warning(
                    this, tr("Check for Updates"),
                    tr("The continuous GitHub release does not identify its commit."));
                return;
            }
            available_label = tr("continuous build %1").arg(remote_commit.left(12));
            if (remote_commit.compare(
                    QString::fromLatin1(version::kCommit), Qt::CaseInsensitive) != 0) {
                const QDateTime updated_at = QDateTime::fromString(
                    release.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
                update_available = version::kCommitTimestamp == 0 || !updated_at.isValid() ||
                    updated_at.toSecsSinceEpoch() > version::kCommitTimestamp;
            }
        }

        if (!update_available) {
            set_status(tr("PixelArt98 is up to date"));
            QMessageBox::information(
                this, tr("Check for Updates"),
                tr("This PixelArt98 build is up to date."));
            return;
        }

        QUrl release_url(release.value(QStringLiteral("html_url")).toString());
        if (!release_url.isValid() || release_url.scheme() != QStringLiteral("https")) {
            release_url = QUrl(QString::fromLatin1(version::kReleasesUrl));
        }

        set_status(tr("PixelArt98 %1 is available").arg(available_label));
        QMessageBox message(this);
        message.setIcon(QMessageBox::Information);
        message.setWindowTitle(tr("Update Available"));
        message.setText(tr("PixelArt98 %1 is available.").arg(available_label));
        message.setInformativeText(
            tr("You are currently using version %1 (%2).")
                .arg(QString::fromLatin1(version::kVersion),
                     QString::fromLatin1(version::kShortCommit)));
        QPushButton* download = message.addButton(tr("Open Download Page"), QMessageBox::AcceptRole);
        message.addButton(QMessageBox::Cancel);
        message.exec();
        if (message.clickedButton() == download && !QDesktopServices::openUrl(release_url)) {
            QMessageBox::warning(
                this, tr("Update Available"),
                tr("The download page could not be opened."));
        }
    });
}

void QtMainWindow::restore_ui_state() { QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98")); restoreGeometry(state.value(QStringLiteral("windowGeometry")).toByteArray()); restoreState(state.value(QStringLiteral("windowState")).toByteArray()); }
void QtMainWindow::save_ui_state() { QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98")); state.setValue(QStringLiteral("windowGeometry"), saveGeometry()); state.setValue(QStringLiteral("windowState"), saveState()); std::string error; if (!save_app_settings(settings_, &error) && console_list_ != nullptr) console_list_->addItem(QString::fromStdString(error)); }
void QtMainWindow::closeEvent(QCloseEvent* event) { if (confirm_discard()) { save_ui_state(); event->accept(); } else event->ignore(); }

} // namespace px
