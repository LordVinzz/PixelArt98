// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/AnimationExport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>

namespace px {

namespace {

void set_error(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

QString scaled_filter(int scale) {
    return QStringLiteral("scale=iw*%1:ih*%1:flags=neighbor").arg(std::clamp(scale, 1, 32));
}

QString gif_dither_name(GifDither dither) {
    switch (dither) {
        case GifDither::Sierra2_4A: return QStringLiteral("sierra2_4a");
        case GifDither::Bayer: return QStringLiteral("bayer");
        case GifDither::None: return QStringLiteral("none");
    }
    return QStringLiteral("sierra2_4a");
}

bool executable_file(const QString& path) {
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable();
}

QImage frame_image(const Document& document, int frame_index, const AnimationExportOptions& options) {
    const std::vector<Pixel> pixels = document.composite_frame(frame_index);
    const QImage view(reinterpret_cast<const uchar*>(pixels.data()), document.width, document.height,
                      document.width * static_cast<int>(sizeof(Pixel)), QImage::Format_RGBA8888);
    if (options.format != AnimationExportFormat::Mp4) return view.copy();

    QImage flattened(document.width, document.height, QImage::Format_RGBA8888);
    flattened.fill(options.video_background);
    QPainter painter(&flattened);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, view);
    painter.end();
    return flattened;
}

} // namespace

QString detect_ffmpeg_executable(const QString& preferred_path) {
    const QString preferred = preferred_path.trimmed();
    if (!preferred.isEmpty() && executable_file(preferred)) {
        return QFileInfo(preferred).absoluteFilePath();
    }
    const QString configured = qEnvironmentVariable("PIXELART_FFMPEG_PATH").trimmed();
    if (!configured.isEmpty() && executable_file(configured)) return QFileInfo(configured).absoluteFilePath();

    const QString from_path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!from_path.isEmpty()) return from_path;

    const std::array<QString, 5> common_locations = {
        QStringLiteral("/opt/homebrew/bin/ffmpeg"),
        QStringLiteral("/usr/local/bin/ffmpeg"),
        QStringLiteral("/usr/bin/ffmpeg"),
        QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe"),
        QStringLiteral("C:/Program Files/ffmpeg/bin/ffmpeg.exe")};
    const auto found = std::find_if(common_locations.begin(), common_locations.end(), executable_file);
    return found == common_locations.end() ? QString() : *found;
}

QString ffmpeg_install_command() {
#if defined(Q_OS_MACOS)
    const QString brew = QStandardPaths::findExecutable(QStringLiteral("brew"),
                                                         {QStringLiteral("/opt/homebrew/bin"),
                                                          QStringLiteral("/usr/local/bin")});
    return brew.isEmpty() ? QString() : QStringLiteral("brew install ffmpeg");
#elif defined(Q_OS_WIN)
    return QStandardPaths::findExecutable(QStringLiteral("winget.exe")).isEmpty()
               ? QString()
               : QStringLiteral("winget install --id Gyan.FFmpeg -e");
#else
    if (!QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty()) {
        return QStringLiteral("sudo apt-get update && sudo apt-get install ffmpeg");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty()) {
        return QStringLiteral("sudo dnf install ffmpeg");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty()) {
        return QStringLiteral("sudo pacman -S ffmpeg");
    }
    return QString();
#endif
}

QString ffconcat_quote_path(const QString& path) {
    QString normalized = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    normalized.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(normalized);
}

bool prepare_animation_export(const Document& document,
                              const QString& directory,
                              const AnimationExportOptions& options,
                              PreparedAnimationExport& prepared,
                              QString* error) {
    prepared = {};
    if (document.width <= 0 || document.height <= 0 || document.frames.empty()) {
        set_error(error, QStringLiteral("The document has no animation frames to export."));
        return false;
    }
    if (!QDir().mkpath(directory)) {
        set_error(error, QStringLiteral("Could not create the temporary export directory."));
        return false;
    }

    const QDir output_directory(directory);
    prepared.concat_path = output_directory.filePath(QStringLiteral("frames.ffconcat"));
    prepared.palette_path = output_directory.filePath(QStringLiteral("palette.png"));

    for (std::size_t index = 0; index < document.frames.size(); ++index) {
        const QString frame_path = output_directory.filePath(
            QStringLiteral("frame-%1.png").arg(static_cast<qulonglong>(index), 8, 10, QLatin1Char('0')));
        const QImage image = frame_image(document, static_cast<int>(index), options);
        if (image.isNull() || !image.save(frame_path, "PNG")) {
            set_error(error, QStringLiteral("Could not render animation frame %1.").arg(index + 1U));
            return false;
        }
        prepared.frame_paths.push_back(frame_path);
    }

    QSaveFile concat_file(prepared.concat_path);
    if (!concat_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        set_error(error, QStringLiteral("Could not create the FFmpeg frame manifest."));
        return false;
    }
    QTextStream stream(&concat_file);
    stream << "ffconcat version 1.0\n";
    const double fixed_duration = 1.0 / std::clamp(options.frames_per_second, 1.0, 120.0);
    for (std::size_t index = 0; index < document.frames.size(); ++index) {
        const double duration = options.preserve_frame_timing
                                    ? static_cast<double>(std::max(1, document.frames[index].duration_ms)) / 1000.0
                                    : fixed_duration;
        prepared.duration_seconds += duration;
        if (index + 1U == document.frames.size()) {
            prepared.final_delay_centiseconds = std::max(1, static_cast<int>(std::lround(duration * 100.0)));
        }
        stream << "file " << ffconcat_quote_path(prepared.frame_paths[static_cast<qsizetype>(index)]) << '\n';
        stream << "option framerate 1000\n";
        stream << "duration " << QString::number(duration, 'f', 6) << '\n';
    }
    // The concat demuxer needs the final image repeated for its duration to be honored.
    stream << "file " << ffconcat_quote_path(prepared.frame_paths.back()) << '\n';
    stream << "option framerate 1000\n";
    if (!concat_file.commit()) {
        set_error(error, QStringLiteral("Could not save the FFmpeg frame manifest."));
        return false;
    }
    return true;
}

QStringList mp4_ffmpeg_arguments(const PreparedAnimationExport& prepared,
                                 const QString& output_path,
                                 const AnimationExportOptions& options) {
    const int bitrate = std::clamp(options.video_bitrate_kbps, 64, 100'000);
    const QString filter = scaled_filter(options.scale) +
                           QStringLiteral(",pad=ceil(iw/2)*2:ceil(ih/2)*2:color=black,format=yuv420p");
    return {QStringLiteral("-hide_banner"), QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("concat"),
            QStringLiteral("-safe"), QStringLiteral("0"),
            QStringLiteral("-i"), prepared.concat_path,
            QStringLiteral("-vf"), filter,
            QStringLiteral("-an"),
            QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-preset"), options.video_preset,
            QStringLiteral("-b:v"), QStringLiteral("%1k").arg(bitrate),
            QStringLiteral("-maxrate"), QStringLiteral("%1k").arg(bitrate),
            QStringLiteral("-bufsize"), QStringLiteral("%1k").arg(bitrate * 2),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"),
            QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
            output_path};
}

QStringList gif_palette_ffmpeg_arguments(const PreparedAnimationExport& prepared,
                                         const AnimationExportOptions& options) {
    const int colors = std::clamp(options.gif_max_colors, 2, 256);
    const QString filter = scaled_filter(options.scale) +
                           QStringLiteral(",palettegen=max_colors=%1:reserve_transparent=1:stats_mode=diff")
                               .arg(colors);
    return {QStringLiteral("-hide_banner"), QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("concat"),
            QStringLiteral("-safe"), QStringLiteral("0"),
            QStringLiteral("-i"), prepared.concat_path,
            QStringLiteral("-vf"), filter,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-update"), QStringLiteral("1"),
            prepared.palette_path};
}

QStringList gif_encode_ffmpeg_arguments(const PreparedAnimationExport& prepared,
                                        const QString& output_path,
                                        const AnimationExportOptions& options) {
    const QString filter = QStringLiteral("[0:v]%1[scaled];[scaled][1:v]paletteuse=dither=%2")
                               .arg(scaled_filter(options.scale), gif_dither_name(options.gif_dither));
    return {QStringLiteral("-hide_banner"), QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("concat"),
            QStringLiteral("-safe"), QStringLiteral("0"),
            QStringLiteral("-i"), prepared.concat_path,
            QStringLiteral("-i"), prepared.palette_path,
            QStringLiteral("-lavfi"), filter,
            QStringLiteral("-loop"), options.gif_loop_forever ? QStringLiteral("0") : QStringLiteral("-1"),
            QStringLiteral("-final_delay"), QString::number(prepared.final_delay_centiseconds),
            QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
            QStringLiteral("-t"), QString::number(prepared.duration_seconds, 'f', 6),
            output_path};
}

} // namespace px
