// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "ui/AppSettings.hpp"
#include "ui/EditorController.hpp"
#include "render/GpuEffectRenderer.hpp"
#include "render/MpsEffectRenderer.hpp"

#include <QMainWindow>
#include <QPoint>
#include <QRect>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QButtonGroup;
class QComboBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QVBoxLayout;
class QSpinBox;
class QTimer;
class QToolButton;

namespace px {

class QtCanvasWidget;
class QtColorPanel;
class QtModelPreviewWidget;

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
    void build_console_dock();
    void rebuild_tool_options();
    void restore_ui_state();
    void save_ui_state();
    void refresh_all();
    void refresh_layers();
    void refresh_frames();
    void refresh_model();
    void refresh_selection_status();
    void update_selection_status(const QRect& bounds);
    void update_pointer_status(std::optional<QPoint> coordinates);
    void set_status(const QString& status);
    void report_error(const QString& operation, const std::string& error);
    [[nodiscard]] bool confirm_discard();

    void new_document();
    void save_project_as();
    void load_project_from();
    void import_layer();
    void export_current_png();
    void export_spritesheet_file();
    void generate_depth_map();
    void import_model(const QString& kind);
    void export_model(const QString& kind);
    void apply_transform(const QString& name, const std::function<void(Document&)>& operation);
    void show_effect_preview(const QString& name, const EffectOperation& operation,
                             const GpuEffectRequestFactory& gpu_request = {});
    void show_adjustment_dialog(const AdjustmentSpec& adjustment);
    void show_levels_dialog(const AdjustmentSpec& adjustment);
    void show_curves_dialog(const QString& name);
    bool apply_mps_preview(Document& document, const GpuEffectRequest& request);
    QAction* add_effect_action(QMenu* menu, const QString& name, EffectOperation operation,
                              GpuEffectRequestFactory gpu_request = {});
    QAction* add_effect_action(QMenu* menu, AdjustmentSpec effect);
    void update_playback();
    void show_about_dialog();
    void check_for_updates();

    EditorController controller_;
    AppSettings settings_;
    QtCanvasWidget* canvas_ = nullptr;
    QtColorPanel* color_panel_ = nullptr;
    QtModelPreviewWidget* model_preview_ = nullptr;
    QButtonGroup* tool_buttons_ = nullptr;
    std::vector<QToolButton*> tool_button_widgets_;
    QListWidget* layers_list_ = nullptr;
    QListWidget* frames_list_ = nullptr;
    QListWidget* model_list_ = nullptr;
    QListWidget* console_list_ = nullptr;
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
    bool playing_ = false;
    int playback_direction_ = 1;
    int error_sequence_ = 0;
    QString project_path_;
    QNetworkAccessManager* network_manager_ = nullptr;
    bool update_check_in_progress_ = false;
    MpsEffectRenderer mps_effect_renderer_;
    std::string last_effect_backend_ = "none";
    std::vector<QDockWidget*> docks_;
};

} // namespace px
