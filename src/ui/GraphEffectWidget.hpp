// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Document.hpp"
#include "core/GraphEffect.hpp"

#include <QPointF>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace px {

// Visual editor for a non-destructive GraphEffect graph. The widget always
// evaluates a private copy of the source document; applying a preview is left
// to its owner through apply_requested so document history remains centralized.
class GraphEffectWidget final : public QWidget {
public:
    explicit GraphEffectWidget(QWidget* parent = nullptr);
    ~GraphEffectWidget() override;

    GraphEffectWidget(const GraphEffectWidget&) = delete;
    GraphEffectWidget& operator=(const GraphEffectWidget&) = delete;

    void set_source_document(const Document& document);
    void reset_graph();

    [[nodiscard]] const GraphEffectGraph& graph() const noexcept;
    [[nodiscard]] bool preview_available() const noexcept;
    [[nodiscard]] const std::vector<Pixel>& preview_pixels() const noexcept;
    [[nodiscard]] int preview_width() const noexcept;
    [[nodiscard]] int preview_height() const noexcept;

    bool add_node(const std::string& type_id,
                  const QPointF& scene_position,
                  GraphEffectNodeId* out_id = nullptr,
                  QString* error = nullptr);
    bool remove_node(GraphEffectNodeId node_id, QString* error = nullptr);
    bool connect_nodes(GraphEffectNodeId from_node,
                       const std::string& from_port,
                       GraphEffectNodeId to_node,
                       const std::string& to_port,
                       QString* error = nullptr);
    bool set_node_enabled(GraphEffectNodeId node_id, bool enabled, QString* error = nullptr);
    bool set_node_parameter(GraphEffectNodeId node_id,
                            const std::string& parameter_id,
                            GraphEffectParameter value,
                            QString* error = nullptr);

    bool save_graph(const QString& path, QString* error = nullptr);
    bool load_graph(const QString& path, QString* error = nullptr);
    void zoom_in();
    void zoom_out();
    void actual_size();
    void fit_graph();
    [[nodiscard]] double zoom() const noexcept;
    void schedule_preview();
    bool apply_preview();

    std::function<void(const std::vector<Pixel>&, int, int)> apply_requested;
    std::function<void(const QString&)> status_changed;
    std::function<void()> graph_changed;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace px
