// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.
// See LICENSE for details.

#pragma once

#include "core/Document.hpp"
#include "core/Filters.hpp"
#include "core/Model.hpp"
#include "core/Tools.hpp"
#include "depth/DepthMapExtractor.hpp"
#include "render/GpuEffectRenderer.hpp"
#include "render/GLCanvasTexture.hpp"
#include "render/GLTiledCanvasTexture.hpp"
#include "render/MpsEffectRenderer.hpp"
#include "render/Renderer3D.hpp"
#include "ui/AppSettings.hpp"
#include "ui/FileDialogs.hpp"

#include <imgui.h>

#include <atomic>
#include <array>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace px {

enum class EffectPreviewKind {
    None,
    InkSketch,
    OilPainting,
    PencilSketch,
    GaussianBlur,
    MotionBlur,
    RadialBlur,
    ZoomBlur,
    MedianBlur,
    SurfaceBlur,
    BrightnessContrast,
    Hsv,
    Temperature,
    Levels,
    TonalRange,
    Curves,
    PaletteQuantize,
    PaletteDither,
    AutoLevel,
    Grayscale,
    Sepia,
    InvertColors,
    InvertAlpha,
    Posterize,
    Bulge,
    Crystalize,
    Dents,
    FrostedGlass,
    Pixelate,
    PolarInversion,
    TileReflection,
    Twist,
    AddNoise,
    ReduceNoise,
    Feather,
    Outline,
    Glow,
    RedEyeRemoval,
    Sharpen,
    SoftenPortrait,
    Vignette,
    Clouds,
    JuliaFractal,
    MandelbrotFractal,
    Turbulence,
    EdgeDetect,
    Emboss,
    Relief,
    DepthOfField
};

struct ErrorConsoleEntry {
    int sequence = 0;
    std::string context;
    std::string details;
};

struct ImageImportJobProgress {
    float fraction = 0.0f;
    int done = 0;
    int total = 0;
    bool indeterminate = false;
    std::string phase;
    std::string status;
};

struct EditorHistoryEntry {
    std::string name;
    Document before_document;
    Document after_document;
    ModelDocument before_model;
    ModelDocument after_model;
};

struct EditorHistoryNode {
    int id = 0;
    int parent = -1;
    std::vector<int> children;
    std::string name;
    Document before_document;
    Document after_document;
    ModelDocument before_model;
    ModelDocument after_model;
    int preferred_child = -1;
};

class EditorApp {
public:
    explicit EditorApp(FileDialogProvider* dialogs = nullptr, AppSettings settings = {});
    ~EditorApp();

    void render();
    bool wants_quit() const { return wants_quit_; }
    void import_image_document(const std::string& path);
    void debug_replace_document_for_memory_test(Document document);
    bool debug_huge_document_history_mode() const;
    bool debug_canvas_uses_active_cel() const;
    std::size_t debug_composite_pixel_capacity() const;
    std::size_t debug_history_document_pixel_capacity() const;
    void debug_update_histogram_cache_for_memory_test();
    bool debug_histogram_cache_approximate() const;

private:
    Document document_;
    ModelDocument model_;
    ToolContext tool_;
    GLCanvasTexture canvas_texture_;
    GLCanvasTexture transform_preview_texture_;
    GLTiledCanvasTexture tiled_canvas_texture_;
    GLTiledCanvasTexture tiled_onion_texture_;
    GpuEffectRenderer gpu_effect_renderer_;
    MpsEffectRenderer mps_effect_renderer_;
    std::array<GLCanvasTexture, 4> transform_icon_textures_;
    std::array<GLCanvasTexture, 6> skybox_face_textures_;
    Renderer3D renderer3d_;
    FileDialogProvider* dialogs_ = nullptr;
    AppSettings settings_;
    std::vector<Pixel> composite_;
    bool composite_uses_active_cel_ = false;
    bool texture_dirty_ = true;
    bool full_canvas_texture_dirty_ = true;
    bool wants_quit_ = false;

    float zoom_ = 12.0f;
    bool show_grid_ = true;
    bool show_checker_ = true;
    bool show_tile_preview_ = false;
    bool show_all_cuboid_wireframes_ = false;
    bool edit_layer_mask_ = false;
    bool show_mask_overlay_ = false;
    bool onion_skin_ = true;
    bool playing_ = false;
    float playback_accum_ = 0.0f;
    int playback_direction_ = 1;
    int animation_timeline_fps_ = 12;
    float animation_timeline_pixels_per_second_ = 180.0f;
    int timeline_trim_frame_ = -1;
    int timeline_trim_neighbor_frame_ = -1;
    int timeline_trim_start_duration_ms_ = 0;
    int timeline_trim_neighbor_start_duration_ms_ = 0;
    float timeline_trim_start_mouse_x_ = 0.0f;
    int timeline_rename_cue_ = -1;
    bool timeline_rename_popup_requested_ = false;
    char timeline_rename_cue_name_[96] = "";
    ModelViewportState model_viewport_;

    bool drag_active_ = false;
    bool stroke_active_ = false;
    bool clone_drag_active_ = false;
    bool move_drag_active_ = false;
    bool pixel_drag_preview_active_ = false;
    bool lasso_active_ = false;
    bool uv_drag_active_ = false;
    bool model_transform_drag_active_ = false;
    bool model_view_gizmo_drag_active_ = false;
    bool effect_preview_active_ = false;
    bool effect_preview_dirty_ = false;
    bool effect_preview_popup_requested_ = false;
    bool straighten_active_ = false;
    bool rotate_zoom_popup_requested_ = false;
    bool rotate_zoom_open_ = false;
    bool rotate_zoom_preview_dirty_ = false;
    bool depth_map_popup_requested_ = false;
    bool depth_map_open_ = false;
    bool depth_job_running_ = false;
    bool depth_result_pending_ = false;
    bool image_import_job_running_ = false;
    bool image_import_result_pending_ = false;
    bool error_console_open_ = false;
    bool error_console_scroll_to_bottom_ = false;
    bool model_render_error_reported_ = false;
    bool transform_icons_loaded_ = false;
    bool canvas_fit_requested_ = false;
    bool new_document_popup_requested_ = false;
    bool history_pending_ = false;
    bool history_suppress_frame_ = false;
    bool history_playback_frame_change_ = false;
    bool history_lightweight_mode_ = false;
    int uv_drag_mode_ = 0;
    int model_transform_mode_ = 0;
    int model_transform_axis_ = 0;
    int model_transform_hover_axis_ = -1;
    int skybox_index_ = 0;
    int loaded_skybox_index_ = -1;
    int canvas_drag_button_ = ImGuiMouseButton_Left;
    int straighten_drag_point_ = -1;
    int image_transform_resampling_ = 2;
    int depth_source_layer_ = 0;
    int depth_tile_size_ = 1024;
    int depth_tile_overlap_ = 128;
    int depth_of_field_layer_ = 0;
    int depth_of_field_focus_ = 128;
    int depth_of_field_aperture_ = 45;
    int depth_of_field_falloff_ = 35;
    int depth_of_field_max_radius_ = 12;
    SelectionCombineMode selection_drag_mode_ = SelectionCombineMode::Replace;
    EffectPreviewKind effect_preview_kind_ = EffectPreviewKind::None;
    int drag_start_x_ = 0;
    int drag_start_y_ = 0;
    int drag_current_x_ = 0;
    int drag_current_y_ = 0;
    int last_x_ = 0;
    int last_y_ = 0;
    int move_start_x_ = 0;
    int move_start_y_ = 0;
    ImVec2 canvas_pan_ = ImVec2(0, 0);
    ImVec2 uv_drag_start_mouse_ = ImVec2(0, 0);
    ImVec2 model_transform_start_mouse_ = ImVec2(0, 0);
    ImVec2 model_transform_drag_center_ = ImVec2(0, 0);
    ImVec2 model_view_gizmo_drag_start_mouse_ = ImVec2(0, 0);
    float model_view_gizmo_start_yaw_ = 0.0f;
    float model_view_gizmo_start_pitch_ = 0.0f;
    float model_transform_start_angle_radians_ = 0.0f;
    float model_transform_rotation_delta_degrees_ = 0.0f;
    UvRect uv_drag_start_rect_;
    Cuboid model_transform_start_cuboid_;
    std::array<ImVec2, 4> straighten_points_ = {};
    std::vector<Pixel> stroke_before_;
    std::vector<Pixel> clone_source_pixels_;
    SelectionMask selection_before_;
    std::vector<std::array<int, 2>> lasso_points_;
    std::vector<ErrorConsoleEntry> error_console_entries_;
    std::vector<EditorHistoryNode> history_nodes_;
    int history_current_node_ = 0;
    int next_history_node_id_ = 1;
    TextBox text_box_;
    Document effect_preview_document_;
    Document rotate_zoom_preview_document_;
    Document history_before_document_;
    ModelDocument history_before_model_;
    std::array<float, 256> histogram_luma_values_ = {};
    std::array<float, 256> histogram_red_values_ = {};
    std::array<float, 256> histogram_green_values_ = {};
    std::array<float, 256> histogram_blue_values_ = {};
    bool histogram_cache_valid_ = false;
    bool histogram_cache_approximate_ = false;
    int histogram_cache_width_ = 0;
    int histogram_cache_height_ = 0;
    std::thread depth_thread_;
    std::atomic_bool depth_cancel_requested_ = false;
    std::mutex depth_mutex_;
    DepthExtractionProgress depth_progress_;
    DepthExtractionResult depth_result_;
    std::string depth_error_;
    std::thread image_import_thread_;
    std::mutex image_import_mutex_;
    ImageImportJobProgress image_import_progress_;
    Document image_import_result_;
    GLTiledCanvasTexture::CpuPyramid image_import_pyramid_;
    std::string image_import_error_;
    std::string image_import_path_;

    char project_path_[512] = "untitled.pixart";
    char image_path_[512] = "import.png";
    char png_path_[512] = "export.png";
    char spritesheet_path_[512] = "spritesheet.png";
    char spritesheet_json_path_[512] = "spritesheet.json";
    char gif_path_[512] = "animation.gif";
    char apng_path_[512] = "animation.png";
    char aseprite_path_[512] = "sprite.aseprite";
    char model_path_[512] = "model.json";
    char gltf_model_path_[512] = "model.gltf";
    char gltf_texture_path_[512] = "texture.png";
    char threejs_pack_path_[512] = "model.zip";
    char minecraft_model_path_[512] = "minecraft_model.json";
    char minecraft_texture_path_[512] = "texture.png";
    char text_buffer_[128] = "TEXT";
    char status_[512] = "Ready";
    int error_console_sequence_ = 0;

    float new_document_width_ = 64.0f;
    float new_document_height_ = 64.0f;
    float new_document_resolution_ = 96.0f;
    int new_document_size_preset_ = 0;
    int new_document_size_unit_ = 0;
    int new_document_resolution_unit_ = 0;
    int new_document_aspect_preset_ = 0;
    int new_document_aspect_width_ = 1;
    int new_document_aspect_height_ = 1;
    bool new_document_lock_aspect_ = false;
    int brightness_ = 0;
    int contrast_ = 0;
    float hue_ = 0.0f;
    float saturation_ = 0.0f;
    float value_ = 0.0f;
    int temperature_ = 0;
    LevelsSettings levels_;
    CurvesSettings curves_;
    int histogram_curve_drag_handle_ = 0;
    int white_point_ = 0;
    int highlights_ = 0;
    int shadows_ = 0;
    int black_point_ = 0;
    int posterize_levels_ = 4;
    int effect_radius_ = 3;
    int effect_amount_ = 50;
    int effect_angle_ = 0;
    int effect_cell_size_ = 8;
    int effect_scale_ = 64;
    int effect_noise_ = 32;
    float effect_strength_ = 0.65f;
    float effect_zoom_ = 1.0f;
    float rotate_zoom_angle_ = 0.0f;
    float rotate_zoom_zoom_ = 1.0f;
    int rotate_zoom_pan_x_ = 0;
    int rotate_zoom_pan_y_ = 0;
    bool histogram_luma_visible_ = true;
    bool histogram_red_visible_ = true;
    bool histogram_green_visible_ = true;
    bool histogram_blue_visible_ = true;
    bool depth_of_field_pick_focus_ = false;

    void update_playback();
    void refresh_texture();
    const std::vector<Pixel>& canvas_pixels() const;
    bool ensure_full_canvas_texture();
    void invalidate_histogram_cache();
    void update_histogram_cache();
    void set_status(const std::string& status);
    void report_error(std::string_view context, std::string_view details);
    void save_settings();
    void handle_global_shortcuts();
    void begin_history_frame();
    void end_history_frame();
    bool huge_document_history_mode() const;
    bool editor_state_changed_from_history_baseline() const;
    bool history_interaction_in_progress() const;
    std::string history_label_from_changes(const Document& before_document,
                                           const ModelDocument& before_model,
                                           const Document& after_document,
                                           const ModelDocument& after_model,
                                           const std::vector<std::string>& document_labels) const;
    void reset_history_tree();
    EditorHistoryNode* history_node_by_id(int id);
    const EditorHistoryNode* history_node_by_id(int id) const;
    void restore_history_node(int node_id);
    void prune_history_tree();
    void push_history_entry(const std::string& name,
                            const Document& before_document,
                            const ModelDocument& before_model,
                            const Document& after_document,
                            const ModelDocument& after_model);
    bool undo_editor();
    bool redo_editor();
    void sync_model_texture_metadata();

    void draw_dockspace();
    void draw_main_menu();
    void draw_new_document_dialog();
    void draw_toolbar();
    void draw_canvas();
    void draw_tool_options_bar();
    void draw_color_panel();
    void draw_layers_panel();
    void draw_timeline_panel();
    void draw_adjustments_panel();
    void draw_model_panel();
    void draw_model_preview_window();
    void draw_tile_preview_window();
    void draw_effect_preview_popup();
    void draw_rotate_zoom_popup();
    void draw_depth_map_popup();
    void draw_image_import_popup();
    void draw_undo_tree_window();
    void draw_error_console();
    void draw_status_bar();

    DialogResult open_file_dialog(FileFilterList filters, const char* remembered_path);
    DialogResult save_file_dialog(FileFilterList filters, const char* remembered_path);
    bool accept_dialog_result(const DialogResult& result, std::string& out_path);
    void clear_selection(const char* undo_name);
    void nudge_canvas_selection(int dx, int dy);

    void clamp_canvas_pan(const ImVec2& viewport_size);
    void zoom_canvas_at(const ImVec2& viewport_origin, const ImVec2& viewport_size, const ImVec2& focal_point, float next_zoom);
    void handle_canvas_input(const ImVec2& origin,
                             const ImVec2& viewport_size,
                             bool viewport_hovered,
                             bool viewport_active,
                             bool canvas_active);
    bool mouse_to_pixel(const ImVec2& origin, int& out_x, int& out_y) const;
    void start_straighten_tool();
    void cancel_straighten_tool();
    void apply_straighten_tool();
    bool handle_straighten_input(const ImVec2& origin, bool viewport_hovered, bool canvas_active);
    void finish_drag(int x, int y);
    void commit_stroke();
    void update_pixel_drag_preview();
    void commit_pixel_drag_preview();
    void cancel_pixel_drag_preview();
    bool delete_selection_contents();
    void draw_selection_overlay(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_floating_selection_overlay(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_mask_overlay(ImDrawList* draw_list, const ImVec2& origin, const ImVec2& size) const;
    void draw_text_preview_overlay(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_lasso_preview(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_selected_model_face_overlay(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_tool_drag_preview(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_straighten_overlay(ImDrawList* draw_list, const ImVec2& origin) const;
    void draw_grid_overlay(ImDrawList* draw_list, const ImVec2& origin, const ImVec2& size) const;
    void draw_histogram_plot();
    bool draw_curves_editor();
    void draw_uv_overlay(ImDrawList* draw_list, const ImVec2& origin, float scale) const;
    void handle_uv_input(const ImVec2& origin, float scale);
    void draw_model_preview();
    bool ensure_transform_icon_textures();
    bool handle_model_transform_toolbar_input(const ImVec2& origin, const ImVec2& size);
    void draw_model_transform_toolbar(ImDrawList* draw_list, const ImVec2& origin);
    bool ensure_skybox_texture();
    void draw_skybox_background(ImDrawList* draw_list, const ImVec2& origin, const ImVec2& size);
    void handle_model_transform_drag(const ImVec2& origin, const ImVec2& size, bool hovered);
    void start_effect_preview(EffectPreviewKind kind);
    GpuEffectRequest gpu_effect_request(EffectPreviewKind kind) const;
    bool try_mps_effect(EffectPreviewKind kind, std::vector<Pixel>& out_pixels);
    bool try_gpu_effect(EffectPreviewKind kind, std::vector<Pixel>& out_pixels);
    bool try_gpu_affine_transform(float angle_degrees,
                                  float zoom,
                                  int pan_x,
                                  int pan_y,
                                  ResamplingMode resampling,
                                  std::vector<Pixel>& out_pixels);
    bool apply_affine_transform_to_document(const char* undo_name,
                                            float angle_degrees,
                                            float zoom,
                                            int pan_x,
                                            int pan_y,
                                            ResamplingMode resampling);
    bool apply_effect_to_document(EffectPreviewKind kind);
    void rebuild_effect_preview();
    void rebuild_rotate_zoom_preview();
    void start_depth_map_extraction();
    void finish_depth_map_job_if_ready();
    void insert_depth_map_layer(const DepthExtractionResult& result);
    void start_image_document_import(const std::string& path);
    void finish_image_import_job_if_ready();
    int default_depth_of_field_layer() const;
    const std::vector<Pixel>* depth_of_field_pixels(const Document& document) const;
    bool valid_depth_of_field_layer(const Document& document) const;
    void apply_effect_to(Document& target) const;
    void apply_effect_preview_to_document();
    void close_effect_preview(bool apply);
    void draw_effect_preview_parameters();
    void commit_text_box();
    void cancel_text_box();
};

} // namespace px
