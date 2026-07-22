// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Filters.hpp"

#include <QPointF>
#include <QWidget>

#include <array>
#include <functional>
#include <vector>

class QMouseEvent;
class QPaintEvent;

namespace px {

class CurveEditorWidget final : public QWidget {
public:
    using Histogram = std::array<int, 256>;
    using Histograms = std::array<Histogram, 4>;

    explicit CurveEditorWidget(Histograms histograms, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

    void set_targets(bool luma, bool red, bool green, bool blue);
    void set_histograms(Histograms histograms);
    [[nodiscard]] bool set_curve_points(const std::vector<double>& x,
                                        const std::vector<double>& y);
    void reset_curve();

    [[nodiscard]] CurvesSettings settings() const;
    [[nodiscard]] bool is_identity() const;

    std::function<void()> changed;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void rebuild_active_histogram();
    void update_curve_properties();
    void notify_changed();

    [[nodiscard]] QRectF graph_rect() const;
    [[nodiscard]] QColor channel_color() const;
    [[nodiscard]] QPointF widget_point(const QPointF& normalized) const;
    [[nodiscard]] QPointF normalized_point(const QPointF& point) const;
    [[nodiscard]] int nearest_point(const QPointF& position) const;
    [[nodiscard]] qreal evaluate(qreal value) const;
    [[nodiscard]] qreal slope_at(std::size_t segment) const;
    [[nodiscard]] qreal tangent_at(std::size_t point) const;

    Histograms histograms_{};
    Histogram active_histogram_{};
    std::vector<QPointF> points_;
    bool luma_ = true;
    bool red_ = false;
    bool green_ = false;
    bool blue_ = false;
    int selected_point_ = -1;
};

class LevelsHistogramWidget final : public QWidget {
public:
    using Histogram = std::array<int, 256>;

    explicit LevelsHistogramWidget(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    void set_histogram(Histogram histogram);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Histogram histogram_{};
};

} // namespace px
