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
