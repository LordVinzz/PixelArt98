namespace {

QString dock_settings_prefix(const QDockWidget* dock) {
    return QStringLiteral("dockWindows/") + dock->objectName() + QLatin1Char('/');
}

} // namespace

void QtMainWindow::restore_ui_state() {
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    if (state.contains(QStringLiteral("windowGeometry"))) {
        restoreGeometry(state.value(QStringLiteral("windowGeometry")).toByteArray());
    }

    const bool has_saved_state = state.contains(QStringLiteral("windowState"));
    const QByteArray window_state = has_saved_state
                                        ? state.value(QStringLiteral("windowState")).toByteArray()
                                        : bundled_default_window_state();
    if (!restoreState(window_state, kWindowStateVersion) && has_saved_state) {
        state.remove(QStringLiteral("windowState"));
    }

    // QMainWindow restores the logical dock layout before the native windows
    // exist. On some platforms, notably macOS, a visible floating QDockWidget
    // can consequently remain absent. Keep explicit floating-window state and
    // show those docks once the main window has received its first showEvent.
    floating_docks_to_show_.clear();
    floating_dock_restore_scheduled_ = false;
    for (QDockWidget* dock : docks_) {
        if (dock == nullptr || dock->objectName().isEmpty()) continue;
        const QString prefix = dock_settings_prefix(dock);
        const bool floating = has_saved_state
                                  ? state.value(prefix + QStringLiteral("floating"),
                                                dock->isFloating())
                                        .toBool()
                                  : dock->isFloating();
        if (!floating) continue;

        const bool was_visible = !dock->isHidden();
        dock->setFloating(true);
        const QByteArray geometry =
            has_saved_state
                ? state.value(prefix + QStringLiteral("geometry")).toByteArray()
                : QByteArray{};
        if (!geometry.isEmpty()) dock->restoreGeometry(geometry);
        dock->hide();
        const bool visible =
            has_saved_state
                ? state.value(prefix + QStringLiteral("visible"), was_visible).toBool()
                : was_visible;
        if (visible) {
            floating_docks_to_show_.push_back(dock);
        }
    }
}

void QtMainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    shutdown_state_saved_ = false;
    if (floating_dock_restore_scheduled_ || floating_docks_to_show_.empty()) return;

    floating_dock_restore_scheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
        for (QDockWidget* dock : floating_docks_to_show_) {
            if (dock == nullptr || !dock->isFloating()) continue;
            dock->show();
            const QByteArray geometry =
                state.value(dock_settings_prefix(dock) + QStringLiteral("geometry"))
                    .toByteArray();
            if (!geometry.isEmpty()) dock->restoreGeometry(geometry);
        }
        floating_docks_to_show_.clear();
        floating_dock_restore_scheduled_ = false;
    });
}

QString QtMainWindow::remembered_directory(const QString& key) const {
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    const QString directory = state.value(QStringLiteral("paths/") + key, QDir::homePath()).toString();
    return QDir(directory).exists() ? directory : QDir::homePath();
}

void QtMainWindow::remember_file_directory(const QString& key, const QString& path) {
    if (path.isEmpty()) return;
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    state.setValue(QStringLiteral("paths/") + key, QFileInfo(path).absolutePath());
    state.sync();
}

void QtMainWindow::persist_app_settings() {
    std::string error;
    if (!save_app_settings(settings_, &error) && console_list_ != nullptr) {
        console_list_->addItem(QString::fromStdString(error));
    }
}

void QtMainWindow::select_ffmpeg_executable() {
    QString initial_path = QString::fromStdString(settings_.ffmpeg_path);
    if (initial_path.isEmpty()) initial_path = ffmpeg_executable_;
    if (initial_path.isEmpty()) initial_path = remembered_directory(QStringLiteral("tools"));
#if defined(Q_OS_WIN)
    const QString filter = tr("Executables (*.exe);;All files (*)");
#else
    const QString filter = tr("All files (*)");
#endif
    const QString selected = QFileDialog::getOpenFileName(
        this, tr("Select FFmpeg executable"), initial_path, filter);
    if (selected.isEmpty()) return;
    const QFileInfo info(selected);
    if (!info.isFile() || !info.isExecutable()) {
        QMessageBox::warning(this, tr("FFmpeg executable"),
                             tr("The selected file is not executable."));
        return;
    }
    ffmpeg_executable_ = info.absoluteFilePath();
    settings_.ffmpeg_path = ffmpeg_executable_.toUtf8().toStdString();
    setProperty("ffmpegAvailable", true);
    setProperty("ffmpegExecutable", ffmpeg_executable_);
    remember_file_directory(QStringLiteral("tools"), ffmpeg_executable_);
    persist_app_settings();
    set_status(tr("Custom FFmpeg executable: %1").arg(ffmpeg_executable_));
}

void QtMainWindow::initialize_crash_recovery() {
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    const bool previous_shutdown_was_clean =
        state.value(QStringLiteral("recovery/cleanShutdown"), true).toBool();
    const QString recovery_path = qstring_from_filesystem_path(default_recovery_session_path());
    if (!previous_shutdown_was_clean && QFileInfo::exists(recovery_path)) {
        QMessageBox message(this);
        message.setObjectName(QStringLiteral("CrashRecoveryDialog"));
        message.setIcon(QMessageBox::Warning);
        message.setWindowTitle(tr("Recover previous session"));
        message.setText(tr("PixelArt98 did not close normally."));
        message.setInformativeText(
            tr("A recovery session containing the last edited image and its Undo/Redo history is available."));
        QPushButton* restore = message.addButton(tr("Restore session"), QMessageBox::AcceptRole);
        QPushButton* discard = message.addButton(tr("Discard recovery"), QMessageBox::DestructiveRole);
        message.setDefaultButton(restore);
        message.exec();
        if (message.clickedButton() == restore) {
            ProjectBundle bundle;
            std::string error;
            if (load_recovery_project(default_recovery_session_path(), bundle, &error)) {
                controller_.replace_project(std::move(bundle.document), std::move(bundle.model));
                controller_.set_modified(true);
                project_path_ = state.value(QStringLiteral("recovery/sourcePath")).toString();
                has_recoverable_session_ = true;
                set_status(tr("Previous session restored"));
            } else {
                QMessageBox::warning(
                    this, tr("Recover previous session"),
                    tr("The recovery archive could not be loaded. It has been kept at:\n%1\n\n%2")
                        .arg(recovery_path, QString::fromStdString(error)));
            }
        } else if (message.clickedButton() == discard) {
            QFile::remove(recovery_path);
            state.remove(QStringLiteral("recovery/sourcePath"));
        }
    }
    state.setValue(QStringLiteral("recovery/cleanShutdown"), false);
    state.sync();
}

void QtMainWindow::schedule_recovery_save() {
    if (recovery_shutting_down_ || recovery_timer_ == nullptr || !has_recoverable_session_) return;
    if (recovery_save_in_progress_) {
        recovery_save_pending_ = true;
        return;
    }
    recovery_timer_->start();
}

void QtMainWindow::finish_recovery_worker() {
    if (recovery_worker_.joinable()) recovery_worker_.join();
    recovery_save_in_progress_ = false;
}

void QtMainWindow::save_recovery_session_now() {
    if (recovery_shutting_down_ || !has_recoverable_session_) return;
    if (recovery_save_in_progress_) {
        recovery_save_pending_ = true;
        return;
    }
    finish_recovery_worker();
    Document document = controller_.document();
    ModelDocument model = controller_.model();
    const QString target_path = qstring_from_filesystem_path(default_recovery_session_path());
    const QString source_path = project_path_;
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    state.setValue(QStringLiteral("recovery/sourcePath"), source_path);
    state.sync();

    recovery_save_in_progress_ = true;
    recovery_save_pending_ = false;
    recovery_worker_ = std::thread(
        [this, document = std::move(document), model = std::move(model), target_path]() mutable {
            QString failure;
            const QFileInfo target_info(target_path);
            if (!QDir().mkpath(target_info.absolutePath())) {
                failure = tr("Could not create the recovery directory.");
            } else {
                QFile::setPermissions(target_info.absolutePath(),
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner);
            }
            QTemporaryFile temporary(
                QDir(target_info.absolutePath()).filePath(QStringLiteral(".session-XXXXXX")));
            temporary.setAutoRemove(true);
            if (failure.isEmpty() && !temporary.open()) {
                failure = tr("Could not create a temporary recovery archive.");
            }
            const QString temporary_path = temporary.fileName();
            temporary.close();
            if (failure.isEmpty()) {
                std::string error;
                if (!save_recovery_project(filesystem_path(temporary_path), document, model, &error)) {
                    failure = QString::fromStdString(error);
                }
            }
            if (failure.isEmpty()) {
                QFile input(temporary_path);
                QSaveFile output(target_path);
                if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
                    failure = tr("Could not open the atomic recovery output.");
                } else {
                    while (!input.atEnd() && failure.isEmpty()) {
                        const QByteArray chunk = input.read(1024 * 1024);
                        if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
                            failure = input.errorString();
                        } else if (output.write(chunk) != chunk.size()) {
                            failure = output.errorString();
                        }
                    }
                    if (failure.isEmpty() && !output.commit()) failure = output.errorString();
                }
            }
            if (failure.isEmpty()) {
                QFile::setPermissions(target_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            }
            QMetaObject::invokeMethod(
                this,
                [this, failure] {
                    recovery_save_in_progress_ = false;
                    if (recovery_worker_.joinable()) recovery_worker_.join();
                    if (!failure.isEmpty() && console_list_ != nullptr) {
                        console_list_->addItem(tr("Crash recovery: %1").arg(failure));
                    }
                    if (recovery_save_pending_ && !recovery_shutting_down_ && recovery_timer_ != nullptr) {
                        recovery_save_pending_ = false;
                        recovery_timer_->start(0);
                    }
                },
                Qt::QueuedConnection);
        });
}

void QtMainWindow::mark_clean_shutdown() {
    const QString recovery_path = qstring_from_filesystem_path(default_recovery_session_path());
    QFile::remove(recovery_path);
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    state.setValue(QStringLiteral("recovery/cleanShutdown"), true);
    state.remove(QStringLiteral("recovery/sourcePath"));
    state.sync();
}

void QtMainWindow::save_ui_state() {
    QSettings state(QStringLiteral("PixelArt98"), QStringLiteral("PixelArt98"));
    state.setValue(QStringLiteral("windowGeometry"), saveGeometry());

    const auto visibility_for_persistence = [this](QDockWidget* dock) -> bool {
        if (std::find(floating_docks_to_show_.begin(), floating_docks_to_show_.end(), dock) !=
            floating_docks_to_show_.end()) {
            return true;
        }
        if (canvas_docks_hidden_for_graph_) {
            const auto iterator =
                std::find(canvas_only_docks_.begin(), canvas_only_docks_.end(), dock);
            if (iterator != canvas_only_docks_.end()) {
                const std::size_t index =
                    static_cast<std::size_t>(std::distance(canvas_only_docks_.begin(), iterator));
                if (index < canvas_dock_visibility_before_graph_.size()) {
                    return canvas_dock_visibility_before_graph_[index];
                }
            }
        }
        return dock != nullptr && !dock->isHidden();
    };

    for (QDockWidget* dock : docks_) {
        if (dock == nullptr || dock->objectName().isEmpty()) continue;
        const QString prefix = dock_settings_prefix(dock);
        const bool floating = dock->isFloating();
        state.setValue(prefix + QStringLiteral("floating"), floating);
        state.setValue(prefix + QStringLiteral("visible"),
                       visibility_for_persistence(dock));
        if (floating) {
            state.setValue(prefix + QStringLiteral("geometry"), dock->saveGeometry());
        } else {
            state.remove(prefix + QStringLiteral("geometry"));
        }
    }

    // GraphEffect temporarily hides canvas-only docks. Persist the canvas
    // layout instead, otherwise closing from that tab would hide them on the
    // next launch as well.
    if (canvas_docks_hidden_for_graph_) {
        const std::size_t count = std::min(canvas_only_docks_.size(),
                                           canvas_dock_visibility_before_graph_.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (QDockWidget* dock = canvas_only_docks_[index]; dock != nullptr) {
                dock->setVisible(canvas_dock_visibility_before_graph_[index]);
            }
        }
    }
    state.setValue(QStringLiteral("windowState"), saveState(kWindowStateVersion));
    if (canvas_docks_hidden_for_graph_) {
        for (QDockWidget* dock : canvas_only_docks_) {
            if (dock != nullptr) dock->hide();
        }
    }
    state.sync();
    persist_app_settings();
}
void QtMainWindow::closeEvent(QCloseEvent* event) {
    if (confirm_discard() && confirm_graph_discard()) {
        recovery_shutting_down_ = true;
        if (recovery_timer_ != nullptr) recovery_timer_->stop();
        finish_recovery_worker();
        save_ui_state();
        mark_clean_shutdown();
        shutdown_state_saved_ = true;
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace px
