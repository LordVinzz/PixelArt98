// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/AdjustmentWidgets.hpp"

#include <QColor>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizePolicy>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace px {

CurveEditorWidget::CurveEditorWidget(Histograms histograms, QWidget* parent)
    : QWidget(parent), histograms_(std::move(histograms)) {
    setObjectName(QStringLiteral("CurvesGraph"));
    setMinimumSize(420, 260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
    reset_curve();
    setProperty("histogramBins", 256);
    set_targets(true, false, false, false);
}

QSize CurveEditorWidget::sizeHint() const {
    return {560, 340};
}

void CurveEditorWidget::set_targets(bool luma, bool red, bool green, bool blue) {
    luma_ = luma;
    red_ = !luma && red;
    green_ = !luma && green;
    blue_ = !luma && blue;
    rebuild_active_histogram();
}

void CurveEditorWidget::set_histograms(Histograms histograms) {
    histograms_ = std::move(histograms);
    rebuild_active_histogram();
}

bool CurveEditorWidget::set_curve_points(const std::vector<double>& x,
                                         const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2U ||
        x.size() > static_cast<std::size_t>(kMaxCurvePoints)) {
        return false;
    }

    std::vector<QPointF> points;
    points.reserve(x.size());
    for (std::size_t index = 0; index < x.size(); ++index) {
        if (!std::isfinite(x[index]) || !std::isfinite(y[index]) ||
            x[index] < 0.0 || x[index] > 1.0 || y[index] < 0.0 || y[index] > 1.0 ||
            (index > 0U && x[index] <= x[index - 1U])) {
            return false;
        }
        points.emplace_back(x[index], y[index]);
    }

    points_ = std::move(points);
    selected_point_ = -1;
    update_curve_properties();
    update();
    return true;
}

void CurveEditorWidget::reset_curve() {
    points_ = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
    update_curve_properties();
    update();
    if (changed) changed();
}

CurvesSettings CurveEditorWidget::settings() const {
    CurvesSettings result;
    result.point_count = static_cast<int>(points_.size());
    result.x.fill(0.0F);
    result.y.fill(0.0F);
    for (std::size_t index = 0; index < points_.size(); ++index) {
        result.x[index] = static_cast<float>(points_[index].x());
        result.y[index] = static_cast<float>(points_[index].y());
    }
    result.luma = luma_;
    result.red = red_;
    result.green = green_;
    result.blue = blue_;
    return result;
}

bool CurveEditorWidget::is_identity() const {
    return points_.size() == 3U && points_[0] == QPointF(0.0, 0.0) &&
           points_[1] == QPointF(0.5, 0.5) && points_[2] == QPointF(1.0, 1.0);
}

void CurveEditorWidget::paintEvent(QPaintEvent*) {
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

    const int maximum = std::max(1, *std::max_element(active_histogram_.begin(),
                                                       active_histogram_.end()));
    QColor histogram_color = channel_color();
    histogram_color.setAlpha(105);
    painter.setPen(Qt::NoPen);
    painter.setBrush(histogram_color);
    const qreal bin_width = graph.width() / 256.0;
    for (int bin = 0; bin < 256; ++bin) {
        const qreal normalized = std::sqrt(
            static_cast<qreal>(active_histogram_[static_cast<std::size_t>(bin)]) /
            static_cast<qreal>(maximum));
        const qreal height = normalized * graph.height();
        painter.drawRect(QRectF(graph.left() + static_cast<qreal>(bin) * bin_width,
                                graph.bottom() - height, std::max(1.0, bin_width), height));
    }

    QPainterPath curve;
    for (int sample = 0; sample <= 255; ++sample) {
        const qreal x = static_cast<qreal>(sample) / 255.0;
        const QPointF mapped = widget_point({x, evaluate(x)});
        if (sample == 0) curve.moveTo(mapped);
        else curve.lineTo(mapped);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(10, 10, 12, 180), 5.0));
    painter.drawPath(curve);
    painter.setPen(QPen(channel_color(), 2.5));
    painter.drawPath(curve);

    for (std::size_t index = 0; index < points_.size(); ++index) {
        const QPointF center = widget_point(points_[index]);
        painter.setPen(QPen(index == static_cast<std::size_t>(selected_point_)
                                ? Qt::white
                                : QColor(18, 20, 24),
                            2.0));
        painter.setBrush(index == static_cast<std::size_t>(selected_point_)
                             ? Qt::white
                             : channel_color());
        painter.drawEllipse(center, 5.5, 5.5);
    }
    painter.setPen(QPen(QColor(105, 110, 120), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(graph);
}

void CurveEditorWidget::mousePressEvent(QMouseEvent* event) {
    const int nearest = nearest_point(event->position());
    if (event->button() == Qt::RightButton) {
        if (nearest > 0 && nearest + 1 < static_cast<int>(points_.size())) {
            points_.erase(points_.begin() + nearest);
            notify_changed();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (nearest >= 0) {
        selected_point_ = nearest;
    } else if (points_.size() < static_cast<std::size_t>(kMaxCurvePoints) &&
               graph_rect().contains(event->position())) {
        const QPointF normalized = normalized_point(event->position());
        const auto position = std::lower_bound(
            points_.begin(), points_.end(), normalized.x(),
            [](const QPointF& point, qreal x_position) { return point.x() < x_position; });
        selected_point_ = static_cast<int>(std::distance(points_.begin(), position));
        points_.insert(position, normalized);
        notify_changed();
    }
    event->accept();
}

void CurveEditorWidget::mouseMoveEvent(QMouseEvent* event) {
    if (selected_point_ < 0 || selected_point_ >= static_cast<int>(points_.size())) return;
    QPointF normalized = normalized_point(event->position());
    if (selected_point_ == 0) {
        normalized.setX(0.0);
    } else if (selected_point_ + 1 == static_cast<int>(points_.size())) {
        normalized.setX(1.0);
    } else {
        normalized.setX(std::clamp(
            normalized.x(), points_[static_cast<std::size_t>(selected_point_ - 1)].x() + 0.01,
            points_[static_cast<std::size_t>(selected_point_ + 1)].x() - 0.01));
    }
    points_[static_cast<std::size_t>(selected_point_)] = normalized;
    notify_changed();
    event->accept();
}

void CurveEditorWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        selected_point_ = -1;
        update();
        event->accept();
    }
}

void CurveEditorWidget::rebuild_active_histogram() {
    active_histogram_.fill(0);
    if (luma_) {
        active_histogram_ = histograms_[0];
    } else {
        for (int channel = 1; channel <= 3; ++channel) {
            const bool enabled = (channel == 1 && red_) || (channel == 2 && green_) ||
                                 (channel == 3 && blue_);
            if (!enabled) continue;
            for (std::size_t bin = 0; bin < active_histogram_.size(); ++bin) {
                active_histogram_[bin] += histograms_[static_cast<std::size_t>(channel)][bin];
            }
        }
    }
    const int selected_channels = static_cast<int>(red_) + static_cast<int>(green_) +
                                  static_cast<int>(blue_);
    const int channel = luma_ ? 0 : selected_channels == 1 ? (red_ ? 1 : green_ ? 2 : 3) : -1;
    setProperty("channel", channel);
    setProperty("luminanceMode", luma_);
    setProperty("redEnabled", red_);
    setProperty("greenEnabled", green_);
    setProperty("blueEnabled", blue_);
    setProperty("histogramMaximum",
                std::max(0, *std::max_element(active_histogram_.begin(),
                                               active_histogram_.end())));
    update();
}

void CurveEditorWidget::update_curve_properties() {
    setProperty("curvePointCount", static_cast<int>(points_.size()));
    setProperty("curveQuarterOutput", qRound(evaluate(0.25) * 1000.0));
}

void CurveEditorWidget::notify_changed() {
    update_curve_properties();
    update();
    if (changed) changed();
}

QRectF CurveEditorWidget::graph_rect() const {
    return QRectF(rect()).adjusted(14.0, 14.0, -14.0, -14.0);
}

QColor CurveEditorWidget::channel_color() const {
    const int selected_channels = static_cast<int>(red_) + static_cast<int>(green_) +
                                  static_cast<int>(blue_);
    if (!luma_ && selected_channels == 1 && red_) return QColor(245, 82, 82);
    if (!luma_ && selected_channels == 1 && green_) return QColor(76, 210, 112);
    if (!luma_ && selected_channels == 1 && blue_) return QColor(78, 142, 255);
    return QColor(225, 228, 235);
}

QPointF CurveEditorWidget::widget_point(const QPointF& normalized) const {
    const QRectF graph = graph_rect();
    return {graph.left() + normalized.x() * graph.width(),
            graph.bottom() - normalized.y() * graph.height()};
}

QPointF CurveEditorWidget::normalized_point(const QPointF& point) const {
    const QRectF graph = graph_rect();
    return {std::clamp((point.x() - graph.left()) / graph.width(), 0.0, 1.0),
            std::clamp((graph.bottom() - point.y()) / graph.height(), 0.0, 1.0)};
}

int CurveEditorWidget::nearest_point(const QPointF& position) const {
    int nearest = -1;
    qreal distance = 11.0;
    for (std::size_t index = 0; index < points_.size(); ++index) {
        const qreal candidate = QLineF(position, widget_point(points_[index])).length();
        if (candidate < distance) {
            distance = candidate;
            nearest = static_cast<int>(index);
        }
    }
    return nearest;
}

qreal CurveEditorWidget::evaluate(qreal value) const {
    if (value <= points_.front().x()) return points_.front().y();
    for (std::size_t index = 1; index < points_.size(); ++index) {
        if (value <= points_[index].x() || index + 1 == points_.size()) {
            const QPointF& left = points_[index - 1];
            const QPointF& right = points_[index];
            const qreal width = std::max(0.001, right.x() - left.x());
            const qreal t = std::clamp((value - left.x()) / width, 0.0, 1.0);
            const qreal t2 = t * t;
            const qreal t3 = t2 * t;
            const qreal left_tangent = tangent_at(index - 1);
            const qreal right_tangent = tangent_at(index);
            return std::clamp((2.0 * t3 - 3.0 * t2 + 1.0) * left.y() +
                              (t3 - 2.0 * t2 + t) * width * left_tangent +
                              (-2.0 * t3 + 3.0 * t2) * right.y() +
                              (t3 - t2) * width * right_tangent,
                              0.0, 1.0);
        }
    }
    return points_.back().y();
}

qreal CurveEditorWidget::slope_at(std::size_t segment) const {
    const QPointF& left = points_[segment];
    const QPointF& right = points_[segment + 1];
    return (right.y() - left.y()) / std::max(0.001, right.x() - left.x());
}

qreal CurveEditorWidget::tangent_at(std::size_t point) const {
    if (point == 0U) return slope_at(0U);
    if (point + 1U == points_.size()) return slope_at(points_.size() - 2U);
    const qreal left_slope = slope_at(point - 1U);
    const qreal right_slope = slope_at(point);
    if (left_slope * right_slope <= 0.0) return 0.0;
    const qreal left_width = points_[point].x() - points_[point - 1U].x();
    const qreal right_width = points_[point + 1U].x() - points_[point].x();
    const qreal first_weight = 2.0 * right_width + left_width;
    const qreal second_weight = right_width + 2.0 * left_width;
    return (first_weight + second_weight) /
           (first_weight / left_slope + second_weight / right_slope);
}

LevelsHistogramWidget::LevelsHistogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(260, 170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setProperty("histogramBins", 256);
}

QSize LevelsHistogramWidget::sizeHint() const {
    return {340, 210};
}

void LevelsHistogramWidget::set_histogram(Histogram histogram) {
    histogram_ = std::move(histogram);
    const int maximum = std::max(0, *std::max_element(histogram_.begin(), histogram_.end()));
    qlonglong checksum = 0;
    for (int bin = 0; bin < 256; ++bin) {
        checksum += static_cast<qlonglong>(bin + 1) *
                    histogram_[static_cast<std::size_t>(bin)];
    }
    setProperty("histogramMaximum", maximum);
    setProperty("histogramChecksum", checksum);
    setProperty("histogramRevision", property("histogramRevision").toInt() + 1);
    update();
}

void LevelsHistogramWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor(22, 25, 30));
    const QRectF graph = QRectF(rect()).adjusted(12.0, 12.0, -12.0, -12.0);
    painter.fillRect(graph, QColor(12, 14, 18));

    painter.setPen(QPen(QColor(52, 57, 66), 1.0));
    for (int step = 1; step < 4; ++step) {
        const qreal x = graph.left() + graph.width() * static_cast<qreal>(step) / 4.0;
        painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
    }

    const int maximum = std::max(1, *std::max_element(histogram_.begin(), histogram_.end()));
    const qreal bin_width = graph.width() / 256.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(205, 211, 222, 190));
    for (int bin = 0; bin < 256; ++bin) {
        const qreal normalized = std::sqrt(
            static_cast<qreal>(histogram_[static_cast<std::size_t>(bin)]) /
            static_cast<qreal>(maximum));
        const qreal height = normalized * graph.height();
        painter.drawRect(QRectF(graph.left() + static_cast<qreal>(bin) * bin_width,
                                graph.bottom() - height, std::max(1.0, bin_width), height));
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(105, 110, 120), 1.0));
    painter.drawRect(graph);
}

} // namespace px
