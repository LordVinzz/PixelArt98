void QtMainWindow::export_current_png() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PNG"),
        QDir(remembered_directory(QStringLiteral("export"))).filePath(QStringLiteral("export.png")),
        tr("PNG (*.png)"));
    std::string error;
    if (!path.isEmpty() && !export_png(path.toStdString(), controller_.document(),
                                       controller_.document().active_frame, &error)) {
        report_error(tr("Export PNG"), error);
    } else if (!path.isEmpty()) {
        remember_file_directory(QStringLiteral("export"), path);
    }
}

void QtMainWindow::export_spritesheet_file() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Spritesheet"),
        QDir(remembered_directory(QStringLiteral("export"))).filePath(QStringLiteral("spritesheet.png")),
        tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    const QString json = QFileInfo(path).absolutePath() + QLatin1Char('/') +
                         QFileInfo(path).completeBaseName() + QStringLiteral(".json");
    std::string error;
    if (!export_spritesheet(path.toStdString(), json.toStdString(), controller_.document(), &error)) {
        report_error(tr("Export Spritesheet"), error);
    } else {
        remember_file_directory(QStringLiteral("export"), path);
    }
}

bool QtMainWindow::ensure_ffmpeg_available() {
    ffmpeg_executable_ = detect_ffmpeg_executable(QString::fromStdString(settings_.ffmpeg_path));
    setProperty("ffmpegAvailable", !ffmpeg_executable_.isEmpty());
    setProperty("ffmpegExecutable", ffmpeg_executable_);
    if (!ffmpeg_executable_.isEmpty()) return true;

    const QString install_command = ffmpeg_install_command();
    while (ffmpeg_executable_.isEmpty()) {
        QMessageBox message(this);
        message.setObjectName(QStringLiteral("FfmpegMissingDialog"));
        message.setIcon(QMessageBox::Information);
        message.setWindowTitle(tr("FFmpeg is required"));
        message.setText(tr("FFmpeg was not found on this computer."));
        message.setInformativeText(
            tr("PixelArt98 uses FFmpeg to encode GIF and MP4 animations. Install it, then choose "
               "\"Detect again\" to continue the export."));
        QPushButton* retry = message.addButton(tr("Detect again"), QMessageBox::AcceptRole);
        QPushButton* copy_command = nullptr;
        if (!install_command.isEmpty()) {
            copy_command = message.addButton(tr("Copy install command"), QMessageBox::ActionRole);
            message.setDetailedText(install_command);
        }
        QPushButton* download = message.addButton(tr("Open FFmpeg download page"), QMessageBox::ActionRole);
        QPushButton* locate = message.addButton(tr("Locate FFmpeg..."), QMessageBox::ActionRole);
        message.addButton(QMessageBox::Cancel);
        message.exec();

        if (copy_command != nullptr && message.clickedButton() == copy_command) {
            QApplication::clipboard()->setText(install_command);
            set_status(tr("FFmpeg install command copied to the clipboard"));
            continue;
        }
        if (message.clickedButton() == download) {
            if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://ffmpeg.org/download.html")))) {
                report_error(tr("Open FFmpeg download page"),
                             "Could not open the FFmpeg download page.");
            }
            continue;
        }
        if (message.clickedButton() == locate) {
            select_ffmpeg_executable();
            if (!ffmpeg_executable_.isEmpty()) return true;
            continue;
        }
        if (message.clickedButton() != retry) return false;

        ffmpeg_executable_ = detect_ffmpeg_executable(QString::fromStdString(settings_.ffmpeg_path));
        setProperty("ffmpegAvailable", !ffmpeg_executable_.isEmpty());
        setProperty("ffmpegExecutable", ffmpeg_executable_);
    }
    set_status(tr("FFmpeg detected: %1").arg(ffmpeg_executable_));
    return true;
}

bool QtMainWindow::run_ffmpeg(const QStringList& arguments, const QString& operation, QString* error) {
    QProcess process;
    process.setProgram(ffmpeg_executable_);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);

    QProgressDialog progress(tr("FFmpeg is encoding the animation..."), tr("Cancel"), 0, 0, this);
    progress.setObjectName(QStringLiteral("AnimationExportProgress"));
    progress.setWindowTitle(operation);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.show();

    process.start();
    if (!process.waitForStarted(5'000)) {
        progress.close();
        if (error != nullptr) {
            *error = tr("Could not start FFmpeg at %1: %2")
                         .arg(ffmpeg_executable_, process.errorString());
        }
        return false;
    }

    QByteArray output;
    while (process.state() != QProcess::NotRunning) {
        (void)process.waitForFinished(40);
        output += process.readAll();
        if (output.size() > 65'536) output = output.right(65'536);
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            process.kill();
            (void)process.waitForFinished(2'000);
            progress.close();
            if (error != nullptr) error->clear();
            set_status(tr("Animation export cancelled"));
            return false;
        }
    }
    output += process.readAll();
    progress.close();

    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) return true;
    if (error != nullptr) {
        QString details = QString::fromUtf8(output).trimmed();
        const QStringList lines = details.split(QLatin1Char('\n'));
        if (lines.size() > 16) details = lines.sliced(lines.size() - 16).join(QLatin1Char('\n'));
        *error = tr("FFmpeg exited with code %1.").arg(process.exitCode());
        if (!details.isEmpty()) *error += QStringLiteral("\n\n") + details;
    }
    return false;
}

void QtMainWindow::export_animation(AnimationExportFormat format) {
    if (!ensure_ffmpeg_available()) return;

    const bool gif = format == AnimationExportFormat::Gif;
    QSettings preferences;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("AnimationExportDialog"));
    dialog.setWindowTitle(gif ? tr("Export GIF animation") : tr("Export MP4 animation"));
    QFormLayout form(&dialog);

    auto* format_label = new QLabel(gif ? tr("Animated GIF (FFmpeg palette encoding)")
                                        : tr("MP4 / H.264 (FFmpeg libx264)"), &dialog);
    format_label->setWordWrap(true);
    form.addRow(tr("Format"), format_label);

    QSpinBox scale(&dialog);
    scale.setObjectName(QStringLiteral("AnimationExportScale"));
    scale.setRange(1, 32);
    scale.setValue(std::clamp(preferences.value(QStringLiteral("animationExport/scale"), 1).toInt(),
                              1, 32));
    scale.setSuffix(QStringLiteral("×"));
    form.addRow(tr("Pixel scale"), &scale);

    QCheckBox preserve_timing(tr("Use each frame's timeline duration"), &dialog);
    preserve_timing.setObjectName(QStringLiteral("AnimationExportPreserveTiming"));
    preserve_timing.setChecked(
        preferences.value(QStringLiteral("animationExport/preserveTiming"), true).toBool());
    form.addRow(tr("Timing"), &preserve_timing);

    QDoubleSpinBox frames_per_second(&dialog);
    frames_per_second.setObjectName(QStringLiteral("AnimationExportFps"));
    frames_per_second.setRange(1.0, 120.0);
    frames_per_second.setDecimals(2);
    frames_per_second.setValue(std::clamp(
        preferences.value(QStringLiteral("animationExport/fps"), 12.0).toDouble(), 1.0, 120.0));
    frames_per_second.setSuffix(tr(" fps"));
    frames_per_second.setEnabled(!preserve_timing.isChecked());
    connect(&preserve_timing, &QCheckBox::toggled, &frames_per_second,
            [&frames_per_second](bool checked) { frames_per_second.setEnabled(!checked); });
    form.addRow(tr("Constant frame rate"), &frames_per_second);

    QSpinBox bitrate(&dialog);
    QComboBox preset(&dialog);
    QComboBox background(&dialog);
    QSpinBox max_colors(&dialog);
    QComboBox dither(&dialog);
    QComboBox loop(&dialog);
    if (gif) {
        max_colors.setObjectName(QStringLiteral("AnimationExportGifColors"));
        max_colors.setRange(2, 256);
        max_colors.setValue(std::clamp(
            preferences.value(QStringLiteral("animationExport/gifColors"), 256).toInt(), 2, 256));
        form.addRow(tr("Palette colors"), &max_colors);

        dither.setObjectName(QStringLiteral("AnimationExportGifDither"));
        dither.addItem(tr("Sierra 2-4A (recommended)"), static_cast<int>(GifDither::Sierra2_4A));
        dither.addItem(tr("Bayer"), static_cast<int>(GifDither::Bayer));
        dither.addItem(tr("None"), static_cast<int>(GifDither::None));
        dither.setCurrentIndex(std::clamp(
            preferences.value(QStringLiteral("animationExport/gifDither"), 0).toInt(),
            0, dither.count() - 1));
        form.addRow(tr("Dithering"), &dither);

        loop.setObjectName(QStringLiteral("AnimationExportGifLoop"));
        loop.addItem(tr("Forever"), true);
        loop.addItem(tr("Play once"), false);
        loop.setCurrentIndex(
            preferences.value(QStringLiteral("animationExport/gifLoop"), true).toBool() ? 0 : 1);
        form.addRow(tr("Loop"), &loop);
    } else {
        bitrate.setObjectName(QStringLiteral("AnimationExportBitrate"));
        bitrate.setRange(64, 100'000);
        bitrate.setValue(std::clamp(
            preferences.value(QStringLiteral("animationExport/mp4Bitrate"), 8'000).toInt(),
            64, 100'000));
        bitrate.setSuffix(tr(" kbps"));
        bitrate.setSingleStep(500);
        form.addRow(tr("Video bitrate"), &bitrate);

        preset.setObjectName(QStringLiteral("AnimationExportPreset"));
        for (const QString& value : {QStringLiteral("ultrafast"), QStringLiteral("veryfast"),
                                     QStringLiteral("fast"), QStringLiteral("medium"),
                                     QStringLiteral("slow"), QStringLiteral("veryslow")}) {
            preset.addItem(value);
        }
        const int preset_index = preset.findText(
            preferences.value(QStringLiteral("animationExport/mp4Preset"),
                              QStringLiteral("medium")).toString());
        preset.setCurrentIndex(preset_index < 0 ? 3 : preset_index);
        form.addRow(tr("Encoding preset"), &preset);

        background.setObjectName(QStringLiteral("AnimationExportBackground"));
        background.addItem(tr("Black"), QColor(Qt::black));
        background.addItem(tr("White"), QColor(Qt::white));
        background.setCurrentIndex(std::clamp(
            preferences.value(QStringLiteral("animationExport/mp4Background"), 0).toInt(), 0, 1));
        form.addRow(tr("Transparency background"), &background);
    }

    QLabel ffmpeg_path(tr("Encoder: %1").arg(ffmpeg_executable_), &dialog);
    ffmpeg_path.setTextInteractionFlags(Qt::TextSelectableByMouse);
    ffmpeg_path.setWordWrap(true);
    form.addRow(&ffmpeg_path);

    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons.setObjectName(QStringLiteral("AnimationExportButtons"));
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    AnimationExportOptions options;
    options.format = format;
    options.scale = scale.value();
    options.preserve_frame_timing = preserve_timing.isChecked();
    options.frames_per_second = frames_per_second.value();
    if (gif) {
        options.gif_max_colors = max_colors.value();
        options.gif_dither = static_cast<GifDither>(dither.currentData().toInt());
        options.gif_loop_forever = loop.currentData().toBool();
    } else {
        options.video_bitrate_kbps = bitrate.value();
        options.video_preset = preset.currentText();
        options.video_background = background.currentData().value<QColor>();
    }

    preferences.setValue(QStringLiteral("animationExport/scale"), options.scale);
    preferences.setValue(QStringLiteral("animationExport/preserveTiming"),
                         options.preserve_frame_timing);
    preferences.setValue(QStringLiteral("animationExport/fps"), options.frames_per_second);
    if (gif) {
        preferences.setValue(QStringLiteral("animationExport/gifColors"), options.gif_max_colors);
        preferences.setValue(QStringLiteral("animationExport/gifDither"), dither.currentIndex());
        preferences.setValue(QStringLiteral("animationExport/gifLoop"), options.gif_loop_forever);
    } else {
        preferences.setValue(QStringLiteral("animationExport/mp4Bitrate"),
                             options.video_bitrate_kbps);
        preferences.setValue(QStringLiteral("animationExport/mp4Preset"), options.video_preset);
        preferences.setValue(QStringLiteral("animationExport/mp4Background"),
                             background.currentIndex());
    }

    const QString extension = gif ? QStringLiteral("gif") : QStringLiteral("mp4");
    QString path = QFileDialog::getSaveFileName(
        this, gif ? tr("Export GIF animation") : tr("Export MP4 animation"),
        QDir(remembered_directory(QStringLiteral("export"))).filePath(
            QStringLiteral("animation.") + extension),
        gif ? tr("GIF animation (*.gif)") : tr("MP4 video (*.mp4)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().compare(extension, Qt::CaseInsensitive) != 0) {
        path += QLatin1Char('.') + extension;
    }

    QTemporaryDir temporary_directory;
    if (!temporary_directory.isValid()) {
        report_error(tr("Export animation"),
                     "Could not create a temporary directory for the animation frames.");
        return;
    }
    PreparedAnimationExport prepared;
    QString error;
    if (!prepare_animation_export(controller_.document(), temporary_directory.path(), options,
                                  prepared, &error)) {
        report_error(tr("Export animation"), error.toStdString());
        return;
    }

    if (gif && !run_ffmpeg(gif_palette_ffmpeg_arguments(prepared, options),
                           tr("Building GIF palette"), &error)) {
        if (!error.isEmpty()) report_error(tr("Export GIF"), error.toStdString());
        return;
    }
    const QStringList arguments = gif ? gif_encode_ffmpeg_arguments(prepared, path, options)
                                      : mp4_ffmpeg_arguments(prepared, path, options);
    if (!run_ffmpeg(arguments, gif ? tr("Encoding GIF") : tr("Encoding MP4"), &error)) {
        if (!error.isEmpty()) {
            report_error(gif ? tr("Export GIF") : tr("Export MP4"), error.toStdString());
        }
        return;
    }
    set_status(gif ? tr("GIF animation exported to %1").arg(path)
                   : tr("MP4 animation exported to %1").arg(path));
    remember_file_directory(QStringLiteral("export"), path);
}

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

void QtMainWindow::import_model(const QString& kind) { const QString filter = kind.startsWith(QStringLiteral("STL")) ? tr("STL (*.stl)") : tr("JSON (*.json)"); const QString path = QFileDialog::getOpenFileName(this, tr("Import %1").arg(kind), {}, filter); if (path.isEmpty()) return; ModelDocument before = controller_.model(); std::string error; bool ok = false; if (kind.startsWith(QStringLiteral("STL"))) ok = import_stl_model(path.toStdString(), controller_.model(), &error); else if (kind.startsWith(QStringLiteral("Minecraft"))) ok = import_minecraft_model(path.toStdString(), controller_.model(), &error); else ok = import_model_json(path.toStdString(), controller_.model(), &error); if (!ok) { controller_.model() = std::move(before); report_error(tr("Import %1").arg(kind), error); } else { controller_.commit_model_edit("Import Model", std::move(before)); refresh_all(); } }
void QtMainWindow::export_model(const QString& kind) { QString extension = QStringLiteral("json"); if (kind.startsWith(QStringLiteral("glTF"))) extension = QStringLiteral("gltf"); if (kind.startsWith(QStringLiteral("Three"))) extension = QStringLiteral("zip"); if (kind.startsWith(QStringLiteral("STL"))) extension = QStringLiteral("stl"); const QString path = QFileDialog::getSaveFileName(this, tr("Export %1").arg(kind), QStringLiteral("model.") + extension); if (path.isEmpty()) return; std::string error; bool ok = false; if (kind.startsWith(QStringLiteral("glTF"))) ok = export_gltf_model(path.toStdString(), controller_.model(), "texture.png", &error); else if (kind.startsWith(QStringLiteral("Three"))) ok = export_threejs_pack(path.toStdString(), controller_.document(), controller_.model(), &error); else if (kind.startsWith(QStringLiteral("STL"))) ok = export_stl_model(path.toStdString(), controller_.model(), &error); else if (kind.startsWith(QStringLiteral("Minecraft"))) ok = export_minecraft_model(path.toStdString(), controller_.model(), "texture.png", &error); else ok = export_model_json(path.toStdString(), controller_.model(), &error); if (!ok) report_error(tr("Export %1").arg(kind), error); }

