void QtMainWindow::mark_graph_effect_source_changed() {
    graph_effect_source_dirty_ = true;
    sync_graph_effect_source();
}

void QtMainWindow::sync_graph_effect_source() {
    if (!graph_effect_source_dirty_ || graph_effect_widget_ == nullptr || workspace_tabs_ == nullptr ||
        workspace_tabs_->currentWidget() != graph_effect_widget_) {
        return;
    }

    const Document& source = controller_.document();
    if (!source.has_active_cel()) {
        graph_effect_widget_->set_source_document(source);
        graph_effect_source_dirty_ = false;
        return;
    }

    Document snapshot;
    snapshot.width = source.width;
    snapshot.height = source.height;
    snapshot.active_layer = 0;
    snapshot.active_frame = 0;
    snapshot.palette = source.palette;
    snapshot.selection = source.selection;
    snapshot.layers.push_back(Layer{});
    Frame frame;
    Cel cel;
    cel.x = source.active_cel().x;
    cel.y = source.active_cel().y;
    cel.pixels = source.active_cel().pixels;
    frame.cels.push_back(std::move(cel));
    snapshot.frames.push_back(std::move(frame));
    graph_effect_widget_->set_source_document(snapshot);
    graph_effect_source_dirty_ = false;
}

void QtMainWindow::update_workspace_dock_visibility() {
    const bool graph_is_active = workspace_tabs_ != nullptr && graph_effect_widget_ != nullptr &&
                                 workspace_tabs_->currentWidget() == graph_effect_widget_;
    if (graph_is_active) {
        if (canvas_docks_hidden_for_graph_) return;
        canvas_dock_visibility_before_graph_.clear();
        canvas_dock_visibility_before_graph_.reserve(canvas_only_docks_.size());
        for (QDockWidget* dock : canvas_only_docks_) {
            canvas_dock_visibility_before_graph_.push_back(dock != nullptr && !dock->isHidden());
            if (dock != nullptr) dock->hide();
        }
        canvas_docks_hidden_for_graph_ = true;
        return;
    }

    if (!canvas_docks_hidden_for_graph_) return;
    const std::size_t count = std::min(canvas_only_docks_.size(),
                                       canvas_dock_visibility_before_graph_.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (QDockWidget* dock = canvas_only_docks_[index]; dock != nullptr) {
            dock->setVisible(canvas_dock_visibility_before_graph_[index]);
        }
    }
    canvas_dock_visibility_before_graph_.clear();
    canvas_docks_hidden_for_graph_ = false;
}

void QtMainWindow::zoom_active_workspace(bool zoom_in) {
    if (workspace_tabs_ != nullptr && graph_effect_widget_ != nullptr &&
        workspace_tabs_->currentWidget() == graph_effect_widget_) {
        if (zoom_in) graph_effect_widget_->zoom_in();
        else graph_effect_widget_->zoom_out();
    } else if (canvas_ != nullptr) {
        if (zoom_in) canvas_->zoom_in();
        else canvas_->zoom_out();
    }
    refresh_zoom_label();
}

void QtMainWindow::actual_size_active_workspace() {
    if (workspace_tabs_ != nullptr && graph_effect_widget_ != nullptr &&
        workspace_tabs_->currentWidget() == graph_effect_widget_) {
        graph_effect_widget_->actual_size();
    } else if (canvas_ != nullptr) {
        canvas_->actual_size();
    }
    refresh_zoom_label();
}

void QtMainWindow::fit_active_workspace() {
    if (workspace_tabs_ != nullptr && graph_effect_widget_ != nullptr &&
        workspace_tabs_->currentWidget() == graph_effect_widget_) {
        graph_effect_widget_->fit_graph();
    } else if (canvas_ != nullptr) {
        canvas_->fit_to_canvas();
    }
    refresh_zoom_label();
}

void QtMainWindow::refresh_zoom_label() {
    if (zoom_label_ == nullptr) return;
    const bool graph_is_active = workspace_tabs_ != nullptr && graph_effect_widget_ != nullptr &&
                                 workspace_tabs_->currentWidget() == graph_effect_widget_;
    const double current_zoom = graph_is_active ? graph_effect_widget_->zoom()
                                                : (canvas_ != nullptr ? canvas_->zoom() : 1.0);
    zoom_label_->setText(tr("%1%").arg(static_cast<int>(std::lround(current_zoom * 100.0))));
}

void QtMainWindow::refresh_all(bool graph_source_changed) {
    if (graph_source_changed) graph_effect_source_dirty_ = true;
    controller_.invalidate_display();
    canvas_->update();
    sync_graph_effect_source();
    if (model_preview_ != nullptr) model_preview_->update();
    refresh_layers(); refresh_frames(); refresh_model(); refresh_history(); refresh_selection_status();
    if (color_panel_ != nullptr) color_panel_->refresh_from_tool();
    if (tool_buttons_ != nullptr) {
        if (QAbstractButton* button = tool_buttons_->button(static_cast<int>(controller_.tool().tool)); button != nullptr) {
            const QSignalBlocker blocker(button);
            button->setChecked(true);
        }
    }
    rebuild_tool_options();
    refresh_zoom_label();
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
    if (controller_.modified()) has_recoverable_session_ = true;
    if (has_recoverable_session_) schedule_recovery_save();
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
void QtMainWindow::refresh_history() {
    if (history_list_ == nullptr) return;
    const QSignalBlocker blocker(history_list_);
    history_list_->setUpdatesEnabled(false);
    history_list_->clear();
    auto* initial = new QListWidgetItem(tr("History baseline"));
    initial->setData(Qt::UserRole, 0);
    history_list_->addItem(initial);
    int state = 0;
    for (const UndoCommand& command : controller_.document().undo_history_for_recovery()) {
        ++state;
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(state).arg(QString::fromStdString(command.name)));
        item->setData(Qt::UserRole, state);
        history_list_->addItem(item);
    }
    const int current_state = state;
    const auto& redo = controller_.document().redo_history_for_recovery();
    for (auto command = redo.rbegin(); command != redo.rend(); ++command) {
        ++state;
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(state).arg(QString::fromStdString(command->name)));
        item->setData(Qt::UserRole, state);
        item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        QFont font = item->font();
        font.setItalic(true);
        item->setFont(font);
        history_list_->addItem(item);
    }
    history_list_->setCurrentRow(current_state);
    if (QListWidgetItem* current = history_list_->currentItem(); current != nullptr) {
        QFont font = current->font();
        font.setBold(true);
        current->setFont(font);
    }
    history_list_->setUpdatesEnabled(true);
}
void QtMainWindow::refresh_selection_status() {
    if (selection_geometry_label_ == nullptr) return;

    const FloatingSelection& floating = controller_.document().floating_selection;
    if (floating.active && floating.width > 0 && floating.height > 0) {
        const int x = floating.source_x + floating.offset_x;
        const int y = floating.source_y + floating.offset_y;
        update_selection_status(QRect(x, y, floating.width, floating.height));
        return;
    }

    const auto bounds = controller_.document().selection.bounds();
    if (!bounds.has_value()) {
        selection_geometry_label_->setText(tr("Selection: none"));
        return;
    }

    update_selection_status(QRect((*bounds)[0], (*bounds)[1],
                                 (*bounds)[2] - (*bounds)[0] + 1,
                                 (*bounds)[3] - (*bounds)[1] + 1));
}

void QtMainWindow::update_selection_status(const QRect& bounds) {
    if (selection_geometry_label_ == nullptr) return;
    selection_geometry_label_->setText(
        tr("Selection: (%1, %2) → (%3, %4) · %5 × %6")
            .arg(bounds.left()).arg(bounds.top()).arg(bounds.left() + bounds.width() - 1)
            .arg(bounds.top() + bounds.height() - 1).arg(bounds.width()).arg(bounds.height()));
}

void QtMainWindow::update_pointer_status(std::optional<QPoint> coordinates) {
    if (pointer_coordinates_label_ == nullptr) return;
    if (!coordinates.has_value()) {
        pointer_coordinates_label_->setText(tr("Pointer: (—, —)"));
        return;
    }
    pointer_coordinates_label_->setText(
        tr("Pointer: (%1, %2)").arg(coordinates->x()).arg(coordinates->y()));
}

void QtMainWindow::set_status(const QString& status) { statusBar()->showMessage(status); }
void QtMainWindow::report_error(const QString& operation, const std::string& error) { const QString text = tr("%1: %2").arg(operation, QString::fromStdString(error)); if (console_list_ != nullptr) console_list_->addItem(tr("%1. %2").arg(++error_sequence_).arg(text)); QMessageBox::critical(this, operation, text); if (settings_.auto_open_error_console && console_list_ != nullptr) console_list_->parentWidget()->show(); }

bool QtMainWindow::confirm_discard() { if (!controller_.modified()) return true; const auto answer = QMessageBox::warning(this, tr("Unsaved Changes"), tr("Discard changes to the current project?"), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel); return answer == QMessageBox::Discard; }

bool QtMainWindow::confirm_graph_discard() {
    if (graph_effect_widget_ == nullptr ||
        !graph_effect_widget_->property("graphDirty").toBool()) {
        return true;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved GraphEffect"),
        tr("Discard changes to the current GraphEffect graph?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return answer == QMessageBox::Discard;
}

void QtMainWindow::new_document() { if (!confirm_discard()) return; QDialog dialog(this); dialog.setWindowTitle(tr("New Document")); QFormLayout form(&dialog); QSpinBox width; QSpinBox height; width.setRange(1, 32768); height.setRange(1, 32768); width.setValue(64); height.setValue(64); form.addRow(tr("Width"), &width); form.addRow(tr("Height"), &height); QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form.addRow(&buttons); connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); if (dialog.exec() == QDialog::Accepted) { controller_.new_document(width.value(), height.value()); project_path_.clear(); has_recoverable_session_ = true; canvas_->fit_to_canvas(); refresh_all(); } }

bool QtMainWindow::copy_selection_to_clipboard() {
    const Document& document = controller_.document();
    if (!document.has_active_cel()) return false;
    ClipboardSelection selection;
    const FloatingSelection& floating = document.floating_selection;
    if (floating.active) {
        selection.x = floating.source_x + floating.offset_x;
        selection.y = floating.source_y + floating.offset_y;
        selection.width = floating.width;
        selection.height = floating.height;
        selection.pixels = floating.pixels;
        selection.mask = floating.mask;
    } else {
        const auto bounds = document.selection.bounds();
        if (!bounds.has_value()) {
            set_status(tr("Nothing selected to copy"));
            return false;
        }
        selection.x = (*bounds)[0];
        selection.y = (*bounds)[1];
        selection.width = (*bounds)[2] - (*bounds)[0] + 1;
        selection.height = (*bounds)[3] - (*bounds)[1] + 1;
        const std::size_t pixel_count = static_cast<std::size_t>(selection.width) *
                                        static_cast<std::size_t>(selection.height);
        selection.pixels.assign(pixel_count, 0);
        selection.mask.assign(pixel_count, 0);
        const auto& source_pixels = document.active_cel().pixels;
        for (int y = 0; y < selection.height; ++y) {
            for (int x = 0; x < selection.width; ++x) {
                const int source_x = selection.x + x;
                const int source_y = selection.y + y;
                if (!document.selection.contains(source_x, source_y)) continue;
                const std::size_t source_index = static_cast<std::size_t>(document.pixel_index(source_x, source_y));
                const std::size_t local_index = static_cast<std::size_t>(y * selection.width + x);
                selection.pixels[local_index] = source_pixels[source_index];
                selection.mask[local_index] = 1;
            }
        }
    }

    QImage image(selection.width, selection.height, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (int y = 0; y < selection.height; ++y) {
        for (int x = 0; x < selection.width; ++x) {
            const std::size_t local_index = static_cast<std::size_t>(y * selection.width + x);
            if (local_index < selection.mask.size() && selection.mask[local_index] != 0 &&
                local_index < selection.pixels.size()) {
                image.setPixelColor(x, y, qcolor(selection.pixels[local_index]));
            }
        }
    }

    auto* mime_data = new QMimeData;
    mime_data->setImageData(image);
    mime_data->setData(kSelectionMimeType, encode_clipboard_selection(selection));
    QApplication::clipboard()->setMimeData(mime_data);
    set_status(tr("Copied selection at (%1, %2)").arg(selection.x).arg(selection.y));
    return true;
}

void QtMainWindow::cut_selection_to_clipboard() {
    if (!copy_selection_to_clipboard()) return;
    if (controller_.document().floating_selection.active) {
        controller_.discard_pasted_selection();
        refresh_all();
        return;
    }
    if (controller_.document().delete_selected_pixels("Cut")) {
        controller_.mark_changed("Cut");
        refresh_all();
    }
}

void QtMainWindow::paste_from_clipboard() {
    const QMimeData* mime_data = QApplication::clipboard()->mimeData();
    if (mime_data == nullptr) return;

    std::optional<ClipboardSelection> clipboard_selection;
    if (mime_data->hasFormat(kSelectionMimeType)) {
        clipboard_selection = decode_clipboard_selection(mime_data->data(kSelectionMimeType));
    }
    if (!clipboard_selection.has_value() && mime_data->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime_data->imageData()).convertToFormat(QImage::Format_RGBA8888);
        const qint64 pixel_count = static_cast<qint64>(image.width()) * static_cast<qint64>(image.height());
        if (!image.isNull() && pixel_count > 0 && pixel_count <= 67108864LL) {
            ClipboardSelection external;
            const auto current_bounds = controller_.document().selection.bounds();
            if (current_bounds.has_value()) {
                external.x = (*current_bounds)[0];
                external.y = (*current_bounds)[1];
            }
            external.width = image.width();
            external.height = image.height();
            external.pixels.resize(static_cast<std::size_t>(pixel_count));
            external.mask.assign(static_cast<std::size_t>(pixel_count), 1);
            for (int y = 0; y < external.height; ++y) {
                for (int x = 0; x < external.width; ++x) {
                    external.pixels[static_cast<std::size_t>(y * external.width + x)] = pixel_color(image.pixelColor(x, y));
                }
            }
            clipboard_selection = std::move(external);
        }
    }
    if (!clipboard_selection.has_value()) {
        set_status(tr("Clipboard does not contain an image selection"));
        return;
    }

    const ClipboardSelection& source = *clipboard_selection;
    FloatingSelection floating;
    floating.active = true;
    floating.source_x = source.x;
    floating.source_y = source.y;
    floating.width = source.width;
    floating.height = source.height;
    floating.pixels = source.pixels;
    floating.mask = source.mask;
    controller_.begin_pasted_selection(std::move(floating));
    controller_.tool().tool = ToolType::MovePixels;
    refresh_all();
    set_status(tr("Pasted selection at (%1, %2)").arg(source.x).arg(source.y));
}

void QtMainWindow::save_project_as() {
    QString path = project_path_;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save Project"),
            QDir(remembered_directory(QStringLiteral("save"))).filePath(QStringLiteral("untitled.pixart")),
            project_filter());
    }
    if (path.isEmpty()) return;
    std::string error;
    if (!save_project(filesystem_path(path), controller_.document(), controller_.model(), &error)) {
        report_error(tr("Save Project"), error);
    } else {
        project_path_ = path;
        has_recoverable_session_ = true;
        remember_file_directory(QStringLiteral("save"), path);
        controller_.set_modified(false);
        set_status(tr("Saved %1").arg(path));
        refresh_all();
    }
}

void QtMainWindow::load_project_from() {
    if (!confirm_discard()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Project"), remembered_directory(QStringLiteral("open")), project_filter());
    if (path.isEmpty()) return;
    ProjectBundle bundle;
    std::string error;
    if (!load_project(filesystem_path(path), bundle, &error)) {
        report_error(tr("Load Project"), error);
    } else {
        controller_.replace_project(std::move(bundle.document), std::move(bundle.model));
        project_path_ = path;
        has_recoverable_session_ = true;
        remember_file_directory(QStringLiteral("open"), path);
        canvas_->fit_to_canvas();
        refresh_all();
    }
}

void QtMainWindow::import_image_document(const QString& path) {
    if (!confirm_discard()) return;
    Document document;
    std::string error;
    if (!import_image(path.toStdString(), document, &error)) {
        report_error(tr("Import Image"), error);
    } else {
        controller_.replace_document(std::move(document));
        project_path_.clear();
        has_recoverable_session_ = true;
        remember_file_directory(QStringLiteral("import"), path);
        canvas_->fit_to_canvas();
        refresh_all();
    }
}

void QtMainWindow::import_layer() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Image as Layer"), remembered_directory(QStringLiteral("import")),
        image_filter());
    if (path.isEmpty()) return;
    std::string error;
    if (!import_image_as_layer(path.toStdString(), controller_.document(),
                               QFileInfo(path).baseName().toStdString(), &error)) {
        report_error(tr("Import Layer"), error);
    } else {
        has_recoverable_session_ = true;
        remember_file_directory(QStringLiteral("import"), path);
        controller_.mark_changed("Import Layer");
        refresh_all();
    }
}

