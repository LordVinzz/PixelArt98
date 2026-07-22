// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#pragma once

#include "core/Document.hpp"

#include <QColor>
#include <QString>
#include <QStringList>

namespace px {

enum class AnimationExportFormat {
    Gif,
    Mp4
};

enum class GifDither {
    Sierra2_4A,
    Bayer,
    None
};

struct AnimationExportOptions {
    AnimationExportFormat format = AnimationExportFormat::Gif;
    int scale = 1;
    bool preserve_frame_timing = true;
    double frames_per_second = 12.0;

    int video_bitrate_kbps = 8'000;
    QString video_preset = QStringLiteral("medium");
    QColor video_background = Qt::black;

    int gif_max_colors = 256;
    GifDither gif_dither = GifDither::Sierra2_4A;
    bool gif_loop_forever = true;
};

struct PreparedAnimationExport {
    QString concat_path;
    QString palette_path;
    QStringList frame_paths;
    double duration_seconds = 0.0;
    int final_delay_centiseconds = 1;
};

[[nodiscard]] QString detect_ffmpeg_executable(const QString& preferred_path = {});
[[nodiscard]] QString ffmpeg_install_command();
[[nodiscard]] QString ffconcat_quote_path(const QString& path);

bool prepare_animation_export(const Document& document,
                              const QString& directory,
                              const AnimationExportOptions& options,
                              PreparedAnimationExport& prepared,
                              QString* error = nullptr);

[[nodiscard]] QStringList mp4_ffmpeg_arguments(const PreparedAnimationExport& prepared,
                                               const QString& output_path,
                                               const AnimationExportOptions& options);
[[nodiscard]] QStringList gif_palette_ffmpeg_arguments(const PreparedAnimationExport& prepared,
                                                       const AnimationExportOptions& options);
[[nodiscard]] QStringList gif_encode_ffmpeg_arguments(const PreparedAnimationExport& prepared,
                                                      const QString& output_path,
                                                      const AnimationExportOptions& options);

} // namespace px
