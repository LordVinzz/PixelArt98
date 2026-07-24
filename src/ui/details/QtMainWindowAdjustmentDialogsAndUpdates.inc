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
    channel_layout->addWidget(new QLabel(tr("Mode:")));
    QButtonGroup mode_group(&dialog);
    auto* luminance_mode = new QRadioButton(tr("Luminance"));
    auto* rgb_mode = new QRadioButton(tr("RGB"));
    luminance_mode->setObjectName(QStringLiteral("CurvesMode.luminance"));
    rgb_mode->setObjectName(QStringLiteral("CurvesMode.rgb"));
    mode_group.addButton(luminance_mode, 0);
    mode_group.addButton(rgb_mode, 1);
    luminance_mode->setChecked(true);
    channel_layout->addWidget(luminance_mode);
    channel_layout->addWidget(rgb_mode);
    channel_layout->addSpacing(12);
    auto* red_channel = new QCheckBox(tr("R"));
    auto* green_channel = new QCheckBox(tr("G"));
    auto* blue_channel = new QCheckBox(tr("B"));
    red_channel->setObjectName(QStringLiteral("CurvesChannel.red"));
    green_channel->setObjectName(QStringLiteral("CurvesChannel.green"));
    blue_channel->setObjectName(QStringLiteral("CurvesChannel.blue"));
    for (QCheckBox* channel : {red_channel, green_channel, blue_channel}) {
        channel->setChecked(true);
        channel->setEnabled(false);
        channel_layout->addWidget(channel);
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

    const auto preview = [this, &original, editor, name] {
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
        if (!apply_accelerated_preview(controller_.document(), request, name)) {
            apply_curves(controller_.document(), settings);
            last_effect_backend_ = "cpu";
        }
        controller_.invalidate_display();
        canvas_->update();
    };
    editor->changed = preview;
    const auto update_targets = [editor, preview, luminance_mode, red_channel, green_channel, blue_channel] {
        const bool luminance = luminance_mode->isChecked();
        for (QCheckBox* channel : {red_channel, green_channel, blue_channel}) {
            channel->setEnabled(!luminance);
        }
        editor->set_targets(luminance, red_channel->isChecked(), green_channel->isChecked(),
                            blue_channel->isChecked());
        preview();
    };
    connect(&mode_group, &QButtonGroup::idClicked, this, [update_targets](int) { update_targets(); });
    for (QCheckBox* channel : {red_channel, green_channel, blue_channel}) {
        connect(channel, &QCheckBox::toggled, this,
                [channel, rgb_mode, red_channel, green_channel, blue_channel, update_targets](bool) {
            if (rgb_mode->isChecked() && !red_channel->isChecked() &&
                !green_channel->isChecked() && !blue_channel->isChecked()) {
                const QSignalBlocker blocker(channel);
                channel->setChecked(true);
            }
            update_targets();
        });
    }
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
    if (adjustment.id == QStringLiteral("levels")) {
        show_levels_dialog(adjustment);
        return;
    }
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
        if (!adjustment.gpu_request ||
            !apply_accelerated_preview(controller_.document(), adjustment.gpu_request(values),
                                       adjustment.name)) {
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

void QtMainWindow::show_levels_dialog(const AdjustmentSpec& adjustment) {
    Document original = controller_.document();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AdjustmentDialog.levels"));
    dialog.setWindowTitle(tr("Adjustment: %1").arg(adjustment.name));
    dialog.resize(760, 520);
    QVBoxLayout layout(&dialog);

    auto* histograms = new QWidget;
    histograms->setObjectName(QStringLiteral("LevelsHistograms"));
    auto* histogram_grid = new QGridLayout(histograms);
    histogram_grid->setContentsMargins(0, 0, 0, 0);
    histogram_grid->setColumnStretch(0, 1);
    histogram_grid->setColumnStretch(1, 1);
    auto* input_label = new QLabel(tr("Input histogram"));
    auto* output_label = new QLabel(tr("Output histogram"));
    input_label->setAlignment(Qt::AlignCenter);
    output_label->setAlignment(Qt::AlignCenter);
    auto* input_histogram = new LevelsHistogramWidget;
    auto* output_histogram = new LevelsHistogramWidget;
    input_histogram->setObjectName(QStringLiteral("LevelsInputHistogram"));
    output_histogram->setObjectName(QStringLiteral("LevelsOutputHistogram"));
    input_histogram->set_histogram(original.histogram_luma());
    histogram_grid->addWidget(input_label, 0, 0);
    histogram_grid->addWidget(output_label, 0, 1);
    histogram_grid->addWidget(input_histogram, 1, 0);
    histogram_grid->addWidget(output_histogram, 1, 1);
    layout.addWidget(histograms, 1);

    auto* parameters = new QWidget;
    parameters->setObjectName(QStringLiteral("LevelsParameters"));
    auto* parameter_grid = new QGridLayout(parameters);
    parameter_grid->setObjectName(QStringLiteral("LevelsParametersGrid"));
    parameter_grid->setContentsMargins(0, 8, 0, 0);
    parameter_grid->setColumnStretch(1, 1);
    parameter_grid->setHorizontalSpacing(10);
    parameter_grid->setVerticalSpacing(6);
    std::vector<QSlider*> sliders;
    sliders.reserve(adjustment.controls.size());

    const auto preview = [this, &original, &adjustment, &sliders, output_histogram] {
        controller_.document() = original;
        std::vector<int> values;
        values.reserve(sliders.size());
        for (const QSlider* slider : sliders) values.push_back(slider->value());
        if (!adjustment.gpu_request ||
            !apply_accelerated_preview(controller_.document(), adjustment.gpu_request(values),
                                       adjustment.name)) {
            adjustment.operation(controller_.document(), values);
            last_effect_backend_ = "cpu";
        }
        output_histogram->set_histogram(controller_.document().histogram_luma());
        controller_.invalidate_display();
        canvas_->update();
    };

    for (int row = 0; row < static_cast<int>(adjustment.controls.size()); ++row) {
        const AdjustmentControl& control = adjustment.controls[static_cast<std::size_t>(row)];
        auto* label = new QLabel(control.label);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(QStringLiteral("AdjustmentControl.") + control.id);
        slider->setRange(control.minimum, control.maximum);
        slider->setValue(control.initial);
        auto* spin = new QSpinBox;
        spin->setObjectName(QStringLiteral("AdjustmentValue.") + control.id);
        spin->setRange(control.minimum, control.maximum);
        spin->setValue(control.initial);
        spin->setMinimumWidth(72);
        parameter_grid->addWidget(label, row, 0);
        parameter_grid->addWidget(slider, row, 1);
        parameter_grid->addWidget(spin, row, 2);
        sliders.push_back(slider);
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
        connect(slider, &QSlider::valueChanged, this, [preview](int) { preview(); });
    }
    layout.addWidget(parameters);

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

void QtMainWindow::update_playback() { if (!playing_ || controller_.document().frames.empty()) return; int next = controller_.document().active_frame + playback_direction_; const int last = static_cast<int>(controller_.document().frames.size()) - 1; if (controller_.document().playback_mode == PlaybackMode::Loop) next = next > last ? 0 : next; else if (next > last || next < 0) { playback_direction_ *= -1; next = std::clamp(controller_.document().active_frame + playback_direction_, 0, last); } controller_.document().active_frame = next; controller_.invalidate_display(); playback_timer_->setInterval(std::max(20, controller_.document().frames[static_cast<std::size_t>(next)].duration_ms)); refresh_frames(); canvas_->update(); mark_graph_effect_source_changed(); }

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
