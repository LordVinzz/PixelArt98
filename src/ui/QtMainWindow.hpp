// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "ui/AppSettings.hpp"
#include "ui/EditorController.hpp"
#include "ui/TextRasterizer.hpp"
#include "render/GpuEffectRenderer.hpp"
#include "render/MpsEffectRenderer.hpp"

#include <QMainWindow>
#include <QPoint>
#include <QRect>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QFontComboBox;
class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QPlainTextEdit;
class QPushButton;
class QShowEvent;
class QVBoxLayout;
class QSpinBox;
class QTabWidget;
class QTimer;
class QToolButton;

namespace px {

enum class AnimationExportFormat;
class QtCanvasWidget;
class QtColorPanel;
class QtModelPreviewWidget;
class GraphEffectWidget;

class QtMainWindow final : public QMainWindow {
public:
    explicit QtMainWindow(AppSettings settings, QWidget* parent = nullptr);
    ~QtMainWindow() override;

    void import_image_document(const QString& path);
    void replace_document_for_testing(Document document);
    [[nodiscard]] const Document& document() const noexcept { return controller_.document(); }
    [[nodiscard]] const std::string& last_effect_backend() const noexcept { return last_effect_backend_; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    using EffectOperation = std::function<void(Document&, int)>;
    using GpuEffectRequestFactory = std::function<GpuEffectRequest(int)>;
    struct AdjustmentControl {
        QString id;
        QString label;
        int minimum = 0;
        int maximum = 100;
        int initial = 0;
    };
    struct AdjustmentSpec {
        QString id;
        QString name;
        std::vector<AdjustmentControl> controls;
        std::function<void(Document&, const std::vector<int>&)> operation;
        std::function<GpuEffectRequest(const std::vector<int>&)> gpu_request;
    };

    void build_actions();
    void build_menus();
    void build_docks();
    void build_tools_dock();
    void build_colors_dock();
    void build_layers_dock();
    void build_adjustments_menu();
    void build_animation_dock();
    void build_history_dock();
    void build_model_dock();
    void build_preview_docks();
    void build_text_dock();
    void build_console_dock();
    void rebuild_tool_options();
    void restore_ui_state();
    void save_ui_state();
    void persist_app_settings();
    void select_ffmpeg_executable();
    void initialize_crash_recovery();
    void schedule_recovery_save();
    void save_recovery_session_now();
    void finish_recovery_worker();
    void mark_clean_shutdown();
    [[nodiscard]] QString remembered_directory(const QString& key) const;
    void remember_file_directory(const QString& key, const QString& path);
    void refresh_all(bool graph_source_changed = true);
    void mark_graph_effect_source_changed();
    void sync_graph_effect_source();
    void update_workspace_dock_visibility();
    void zoom_active_workspace(bool zoom_in);
    void actual_size_active_workspace();
    void fit_active_workspace();
    void refresh_zoom_label();
    void refresh_layers();
    void refresh_frames();
    void refresh_model();
    void refresh_history();
    void refresh_selection_status();
    void update_selection_status(const QRect& bounds);
    void update_pointer_status(std::optional<QPoint> coordinates);
    void set_status(const QString& status);
    void report_error(const QString& operation, const std::string& error);
    [[nodiscard]] bool confirm_discard();
    [[nodiscard]] bool confirm_graph_discard();

    void new_document();
    [[nodiscard]] bool copy_selection_to_clipboard();
    void cut_selection_to_clipboard();
    void paste_from_clipboard();
    void save_project_as();
    void load_project_from();
    void import_layer();
    void export_current_png();
    void export_spritesheet_file();
    void export_animation(AnimationExportFormat format);
    [[nodiscard]] bool ensure_ffmpeg_available();
    [[nodiscard]] bool run_ffmpeg(const QStringList& arguments, const QString& operation,
                                  QString* error);
    void generate_depth_map();
    void begin_raster_text_edit(int x, int y, bool secondary);
    void update_raster_text_preview();
    void apply_raster_text_edit();
    void cancel_raster_text_edit(bool hide_dock = false);
    [[nodiscard]] RasterTextOptions current_raster_text_options() const;
    void persist_raster_text_options(const RasterTextOptions& options) const;
    void show_image_resize_dialog();
    void show_canvas_resize_dialog();
    void show_crop_dialog();
    void import_model(const QString& kind);
    void export_model(const QString& kind);
    void apply_transform(const QString& name, const std::function<void(Document&)>& operation);
    void show_effect_preview(const QString& name, const EffectOperation& operation,
                             const GpuEffectRequestFactory& gpu_request = {});
    void show_adjustment_dialog(const AdjustmentSpec& adjustment);
    void show_levels_dialog(const AdjustmentSpec& adjustment);
    void show_curves_dialog(const QString& name);
    bool apply_accelerated_preview(Document& document,
                                   const GpuEffectRequest& request,
                                   const QString& undo_name);
    QAction* add_effect_action(QMenu* menu, const QString& name, EffectOperation operation,
                              GpuEffectRequestFactory gpu_request = {});
    QAction* add_effect_action(QMenu* menu, AdjustmentSpec effect);
    void update_playback();
    void show_about_dialog();
    void check_for_updates();

    EditorController controller_;
    AppSettings settings_;
    QTabWidget* workspace_tabs_ = nullptr;
    QtCanvasWidget* canvas_ = nullptr;
    GraphEffectWidget* graph_effect_widget_ = nullptr;
    QtColorPanel* color_panel_ = nullptr;
    QtModelPreviewWidget* model_preview_ = nullptr;
    QButtonGroup* tool_buttons_ = nullptr;
    std::vector<QToolButton*> tool_button_widgets_;
    QListWidget* layers_list_ = nullptr;
    QListWidget* frames_list_ = nullptr;
    QListWidget* model_list_ = nullptr;
    QListWidget* history_list_ = nullptr;
    QListWidget* console_list_ = nullptr;
    QDockWidget* text_dock_ = nullptr;
    QPlainTextEdit* raster_text_input_ = nullptr;
    QFontComboBox* raster_text_font_ = nullptr;
    QSpinBox* raster_text_size_ = nullptr;
    QComboBox* raster_text_alignment_ = nullptr;
    QCheckBox* raster_text_antialias_ = nullptr;
    QCheckBox* raster_text_bold_ = nullptr;
    QCheckBox* raster_text_italic_ = nullptr;
    QPushButton* raster_text_apply_ = nullptr;
    QPushButton* raster_text_cancel_ = nullptr;
    QLabel* zoom_label_ = nullptr;
    QLabel* pointer_coordinates_label_ = nullptr;
    QLabel* selection_geometry_label_ = nullptr;
    QLabel* tile_preview_label_ = nullptr;
    QVBoxLayout* tool_options_layout_ = nullptr;
    QSpinBox* tool_size_spin_ = nullptr;
    QSpinBox* tolerance_spin_ = nullptr;
    QLabel* clone_source_label_ = nullptr;
    QComboBox* blend_mode_ = nullptr;
    QTimer* playback_timer_ = nullptr;
    QTimer* recovery_timer_ = nullptr;
    bool playing_ = false;
    int playback_direction_ = 1;
    int error_sequence_ = 0;
    QString project_path_;
    QString ffmpeg_executable_;
    QNetworkAccessManager* network_manager_ = nullptr;
    bool update_check_in_progress_ = false;
    std::thread recovery_worker_;
    bool recovery_save_in_progress_ = false;
    bool recovery_save_pending_ = false;
    bool recovery_shutting_down_ = false;
    bool has_recoverable_session_ = false;
    MpsEffectRenderer mps_effect_renderer_;
    GpuEffectRenderer gpu_effect_renderer_;
    std::string last_effect_backend_ = "none";
    std::vector<QDockWidget*> docks_;
    std::vector<QDockWidget*> canvas_only_docks_;
    std::vector<bool> canvas_dock_visibility_before_graph_;
    std::vector<QDockWidget*> floating_docks_to_show_;
    bool canvas_docks_hidden_for_graph_ = false;
    bool floating_dock_restore_scheduled_ = false;
    bool shutdown_state_saved_ = false;
    bool graph_effect_source_dirty_ = true;
    bool raster_text_edit_active_ = false;
    bool raster_text_secondary_ = false;
    int raster_text_x_ = 0;
    int raster_text_y_ = 0;
    int raster_text_box_width_ = 128;
    int raster_text_box_height_ = 64;
};

} // namespace px
