void QtMainWindow::show_image_resize_dialog() {
    const Document& document = controller_.document();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ImageResizeDialog"));
    dialog.setWindowTitle(tr("Resize Image"));
    QFormLayout form(&dialog);
    QSpinBox width(&dialog);
    width.setObjectName(QStringLiteral("ImageResizeWidth"));
    width.setRange(1, 32768);
    width.setValue(document.width);
    QSpinBox height(&dialog);
    height.setObjectName(QStringLiteral("ImageResizeHeight"));
    height.setRange(1, 32768);
    height.setValue(document.height);
    QCheckBox aspect(tr("Preserve aspect ratio"), &dialog);
    aspect.setObjectName(QStringLiteral("ImageResizeAspect"));
    aspect.setChecked(true);
    QComboBox resampling(&dialog);
    resampling.setObjectName(QStringLiteral("ImageResizeResampling"));
    resampling.addItem(tr("Nearest neighbor (pixel art)"), static_cast<int>(ResamplingMode::Nearest));
    resampling.addItem(tr("Bilinear"), static_cast<int>(ResamplingMode::Bilinear));
    resampling.addItem(tr("Bicubic"), static_cast<int>(ResamplingMode::Bicubic));
    bool syncing = false;
    connect(&width, &QSpinBox::valueChanged, &dialog, [&](int value) {
        if (!aspect.isChecked() || syncing) return;
        syncing = true;
        height.setValue(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(value) * document.height / document.width))));
        syncing = false;
    });
    connect(&height, &QSpinBox::valueChanged, &dialog, [&](int value) {
        if (!aspect.isChecked() || syncing) return;
        syncing = true;
        width.setValue(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(value) * document.width / document.height))));
        syncing = false;
    });
    form.addRow(tr("Width"), &width);
    form.addRow(tr("Height"), &height);
    form.addRow(&aspect);
    form.addRow(tr("Resampling"), &resampling);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.setObjectName(QStringLiteral("ImageResizeButtons"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!resize_document_image(controller_.document(), width.value(), height.value(),
                               static_cast<ResamplingMode>(resampling.currentData().toInt()),
                               &controller_.model())) {
        set_status(tr("Resize Image: dimensions are unchanged or exceed the supported pixel limit"));
        return;
    }
    controller_.mark_changed("Resize Image");
    canvas_->fit_to_canvas();
    refresh_all();
}

void QtMainWindow::show_canvas_resize_dialog() {
    const Document& document = controller_.document();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("CanvasResizeDialog"));
    dialog.setWindowTitle(tr("Canvas Size"));
    QFormLayout form(&dialog);
    QSpinBox width(&dialog);
    width.setObjectName(QStringLiteral("CanvasResizeWidth"));
    width.setRange(1, 32768);
    width.setValue(document.width);
    QSpinBox height(&dialog);
    height.setObjectName(QStringLiteral("CanvasResizeHeight"));
    height.setRange(1, 32768);
    height.setValue(document.height);
    QComboBox anchor(&dialog);
    anchor.setObjectName(QStringLiteral("CanvasResizeAnchor"));
    for (const QString& label : {tr("Top left"), tr("Top center"), tr("Top right"),
                                 tr("Middle left"), tr("Center"), tr("Middle right"),
                                 tr("Bottom left"), tr("Bottom center"), tr("Bottom right")}) {
        anchor.addItem(label);
    }
    anchor.setCurrentIndex(4);
    form.addRow(tr("Width"), &width);
    form.addRow(tr("Height"), &height);
    form.addRow(tr("Anchor"), &anchor);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.setObjectName(QStringLiteral("CanvasResizeButtons"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const int column = anchor.currentIndex() % 3;
    const int row = anchor.currentIndex() / 3;
    const int horizontal_space = width.value() - document.width;
    const int vertical_space = height.value() - document.height;
    const int offset_x = column == 0 ? 0 : column == 1 ? horizontal_space / 2 : horizontal_space;
    const int offset_y = row == 0 ? 0 : row == 1 ? vertical_space / 2 : vertical_space;
    if (!resize_document_canvas(controller_.document(), width.value(), height.value(),
                                offset_x, offset_y, &controller_.model())) {
        set_status(tr("Canvas Size: dimensions are unchanged or exceed the supported pixel limit"));
        return;
    }
    controller_.mark_changed("Resize Canvas");
    canvas_->fit_to_canvas();
    refresh_all();
}

void QtMainWindow::show_crop_dialog() {
    const Document& document = controller_.document();
    int initial_x = 0;
    int initial_y = 0;
    int initial_width = document.width;
    int initial_height = document.height;
    if (const auto bounds = document.selection.bounds(); bounds.has_value()) {
        initial_x = (*bounds)[0];
        initial_y = (*bounds)[1];
        initial_width = (*bounds)[2] - (*bounds)[0] + 1;
        initial_height = (*bounds)[3] - (*bounds)[1] + 1;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ImageCropDialog"));
    dialog.setWindowTitle(tr("Crop Image"));
    QFormLayout form(&dialog);
    QSpinBox x(&dialog);
    x.setObjectName(QStringLiteral("ImageCropX"));
    x.setRange(0, document.width - 1);
    x.setValue(initial_x);
    QSpinBox y(&dialog);
    y.setObjectName(QStringLiteral("ImageCropY"));
    y.setRange(0, document.height - 1);
    y.setValue(initial_y);
    QSpinBox width(&dialog);
    width.setObjectName(QStringLiteral("ImageCropWidth"));
    width.setRange(1, document.width - initial_x);
    width.setValue(initial_width);
    QSpinBox height(&dialog);
    height.setObjectName(QStringLiteral("ImageCropHeight"));
    height.setRange(1, document.height - initial_y);
    height.setValue(initial_height);
    connect(&x, &QSpinBox::valueChanged, &dialog, [&](int value) {
        width.setMaximum(document.width - value);
    });
    connect(&y, &QSpinBox::valueChanged, &dialog, [&](int value) {
        height.setMaximum(document.height - value);
    });
    form.addRow(tr("Left"), &x);
    form.addRow(tr("Top"), &y);
    form.addRow(tr("Width"), &width);
    form.addRow(tr("Height"), &height);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons.setObjectName(QStringLiteral("ImageCropButtons"));
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(&buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    if (!crop_document(controller_.document(), x.value(), y.value(), width.value(), height.value(),
                       &controller_.model())) {
        set_status(tr("Crop Image: the crop is invalid or leaves the document unchanged"));
        return;
    }
    controller_.mark_changed("Crop Image");
    canvas_->fit_to_canvas();
    refresh_all();
}

RasterTextOptions QtMainWindow::current_raster_text_options() const {
    RasterTextOptions options;
    if (raster_text_input_ == nullptr || raster_text_font_ == nullptr ||
        raster_text_size_ == nullptr || raster_text_alignment_ == nullptr ||
        raster_text_antialias_ == nullptr ||
        raster_text_bold_ == nullptr || raster_text_italic_ == nullptr) {
        return options;
    }
    options.text = raster_text_input_->toPlainText();
    options.font_family = raster_text_font_->currentFont().family();
    options.pixel_size = raster_text_size_->value();
    options.box_width = raster_text_box_width_;
    options.box_height = raster_text_box_height_;
    options.alignment = static_cast<RasterTextAlignment>(
        raster_text_alignment_->currentData().toInt());
    options.antialias = raster_text_antialias_->isChecked();
    options.bold = raster_text_bold_->isChecked();
    options.italic = raster_text_italic_->isChecked();
    return options;
}

void QtMainWindow::persist_raster_text_options(const RasterTextOptions& options) const {
    QSettings preferences;
    preferences.setValue(QStringLiteral("text/fontFamily"), options.font_family);
    preferences.setValue(QStringLiteral("text/pixelSize"), options.pixel_size);
    preferences.setValue(QStringLiteral("text/boxWidth"), options.box_width);
    preferences.setValue(QStringLiteral("text/boxHeight"), options.box_height);
    preferences.setValue(QStringLiteral("text/alignment"),
                         raster_text_alignment_ == nullptr
                             ? 0
                             : raster_text_alignment_->currentIndex());
    preferences.setValue(QStringLiteral("text/antialias"), options.antialias);
    preferences.setValue(QStringLiteral("text/bold"), options.bold);
    preferences.setValue(QStringLiteral("text/italic"), options.italic);
}

void QtMainWindow::begin_raster_text_edit(int x, int y, bool secondary) {
    Document& document = controller_.document();
    if (!document.in_bounds(x, y) || text_dock_ == nullptr ||
        raster_text_input_ == nullptr) {
        return;
    }
    if (document.floating_selection.active) {
        static_cast<void>(controller_.commit_pasted_selection());
    }
    raster_text_x_ = x;
    raster_text_y_ = y;
    raster_text_secondary_ = secondary;
    raster_text_edit_active_ = true;
    raster_text_box_width_ = std::clamp(
        raster_text_box_width_, 1, std::max(1, document.width - x));
    raster_text_box_height_ = std::clamp(
        raster_text_box_height_, 1, std::max(1, document.height - y));
    raster_text_cancel_->setEnabled(true);
    text_dock_->show();
    text_dock_->raise();
    raster_text_input_->setFocus();
    update_raster_text_preview();
}

void QtMainWindow::update_raster_text_preview() {
    if (canvas_ == nullptr || raster_text_apply_ == nullptr ||
        raster_text_cancel_ == nullptr) {
        return;
    }
    const RasterTextOptions options = current_raster_text_options();
    if (raster_text_edit_active_) {
        canvas_->set_raster_text_box(raster_text_x_, raster_text_y_,
                                     raster_text_box_width_,
                                     raster_text_box_height_);
    }
    const bool valid = raster_text_edit_active_ && !options.text.trimmed().isEmpty() &&
                       controller_.document().in_bounds(raster_text_x_, raster_text_y_);
    raster_text_apply_->setEnabled(valid);
    raster_text_cancel_->setEnabled(raster_text_edit_active_);
    if (!valid) {
        canvas_->clear_raster_text_preview();
        return;
    }

    const Document& document = controller_.document();
    const Pixel color = raster_text_secondary_ ? controller_.tool().secondary
                                               : controller_.tool().primary;
    RasterTextImage preview = rasterize_text(
        options, color, document.width - raster_text_x_,
        document.height - raster_text_y_);
    if (document.selection.active) {
        for (int source_y = 0; source_y < preview.height; ++source_y) {
            for (int source_x = 0; source_x < preview.width; ++source_x) {
                if (!document.selection.contains(raster_text_x_ + source_x,
                                                 raster_text_y_ + source_y)) {
                    preview.pixels[static_cast<std::size_t>(
                        source_y * preview.width + source_x)] = 0;
                }
            }
        }
    }
    canvas_->set_raster_text_preview(raster_text_x_, raster_text_y_,
                                     std::move(preview));
}

void QtMainWindow::apply_raster_text_edit() {
    if (!raster_text_edit_active_) return;
    const RasterTextOptions options = current_raster_text_options();
    const Pixel color = raster_text_secondary_ ? controller_.tool().secondary
                                               : controller_.tool().primary;
    std::string error;
    if (!stamp_raster_text(controller_.document(), raster_text_x_, raster_text_y_,
                           options, color, &error)) {
        report_error(tr("Raster Text"), error);
        return;
    }
    persist_raster_text_options(options);
    raster_text_edit_active_ = false;
    canvas_->clear_raster_text_preview();
    canvas_->clear_raster_text_box();
    canvas_->unsetCursor();
    raster_text_apply_->setEnabled(false);
    raster_text_cancel_->setEnabled(false);
    controller_.mark_changed("Raster Text");
    refresh_all();
}

void QtMainWindow::cancel_raster_text_edit(bool hide_dock) {
    raster_text_edit_active_ = false;
    if (canvas_ != nullptr) canvas_->clear_raster_text_preview();
    if (canvas_ != nullptr) canvas_->clear_raster_text_box();
    if (canvas_ != nullptr) canvas_->unsetCursor();
    if (raster_text_apply_ != nullptr) raster_text_apply_->setEnabled(false);
    if (raster_text_cancel_ != nullptr) raster_text_cancel_->setEnabled(false);
    if (hide_dock && text_dock_ != nullptr) text_dock_->hide();
}

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

