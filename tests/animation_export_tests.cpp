// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/AnimationExport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>

#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

using namespace px;

namespace {

Document animation_document() {
    Document document = Document::create(2, 1);
    document.frames[0].duration_ms = 120;
    document.active_cel().pixels[0] = rgba(255, 0, 0, 128);
    document.active_cel().pixels[1] = rgba(0, 255, 0, 255);
    document.add_frame(true);
    assert(document.frames.size() == 2);
    document.frames[1].duration_ms = 240;
    document.active_cel().pixels[0] = rgba(0, 0, 255, 255);
    return document;
}

QString read_text(const QString& path) {
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly | QIODevice::Text));
    return QString::fromUtf8(file.readAll());
}

void test_ffmpeg_detection_override() {
    QTemporaryDir directory;
    assert(directory.isValid());
#if defined(Q_OS_WIN)
    const QString executable = directory.filePath(QStringLiteral("fake-ffmpeg.exe"));
#else
    const QString executable = directory.filePath(QStringLiteral("fake-ffmpeg"));
#endif
    QFile file(executable);
    assert(file.open(QIODevice::WriteOnly));
    file.write("fake");
    file.close();
    assert(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                               QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                               QFileDevice::ExeGroup));

    const bool had_override = qEnvironmentVariableIsSet("PIXELART_FFMPEG_PATH");
    const QByteArray previous = qgetenv("PIXELART_FFMPEG_PATH");
    assert(qputenv("PIXELART_FFMPEG_PATH", executable.toUtf8()));
    assert(detect_ffmpeg_executable() == QFileInfo(executable).absoluteFilePath());
    assert(detect_ffmpeg_executable(executable) == QFileInfo(executable).absoluteFilePath());
    if (had_override) assert(qputenv("PIXELART_FFMPEG_PATH", previous));
    else assert(qunsetenv("PIXELART_FFMPEG_PATH"));
}

void test_variable_timing_manifest_and_gif_frames() {
    QTemporaryDir directory;
    assert(directory.isValid());
    AnimationExportOptions options;
    options.format = AnimationExportFormat::Gif;
    options.preserve_frame_timing = true;

    PreparedAnimationExport prepared;
    QString error;
    const Document document = animation_document();
    assert(prepare_animation_export(document, directory.path(), options, prepared, &error));
    assert(error.isEmpty());
    assert(prepared.frame_paths.size() == 2);
    assert(QFileInfo::exists(prepared.frame_paths[0]));
    assert(QFileInfo::exists(prepared.frame_paths[1]));

    const QString manifest = read_text(prepared.concat_path);
    assert(manifest.startsWith(QStringLiteral("ffconcat version 1.0\n")));
    assert(manifest.contains(QStringLiteral("duration 0.120000")));
    assert(manifest.contains(QStringLiteral("duration 0.240000")));
    assert(manifest.count(QStringLiteral("file ")) == 3);
    assert(manifest.count(QStringLiteral("option framerate 1000")) == 3);
    assert(std::abs(prepared.duration_seconds - 0.36) < 0.0001);
    assert(prepared.final_delay_centiseconds == 24);

    const QImage rendered(prepared.frame_paths[0]);
    assert(!rendered.isNull());
    const QColor first = rendered.pixelColor(0, 0);
    assert(first.red() == 255);
    assert(first.green() == 0);
    assert(first.alpha() == 128);
}

void test_fixed_fps_and_mp4_background_flattening() {
    QTemporaryDir directory;
    assert(directory.isValid());
    AnimationExportOptions options;
    options.format = AnimationExportFormat::Mp4;
    options.preserve_frame_timing = false;
    options.frames_per_second = 20.0;
    options.video_background = Qt::white;

    PreparedAnimationExport prepared;
    QString error;
    assert(prepare_animation_export(animation_document(), directory.path(), options, prepared, &error));
    const QString manifest = read_text(prepared.concat_path);
    assert(manifest.count(QStringLiteral("duration 0.050000")) == 2);
    assert(std::abs(prepared.duration_seconds - 0.1) < 0.0001);
    assert(prepared.final_delay_centiseconds == 5);

    const QImage rendered(prepared.frame_paths[0]);
    assert(!rendered.isNull());
    const QColor first = rendered.pixelColor(0, 0);
    assert(first.alpha() == 255);
    assert(first.red() == 255);
    assert(std::abs(first.green() - 127) <= 1);
    assert(std::abs(first.blue() - 127) <= 1);
}

void test_mp4_arguments_include_quality_and_compatibility_controls() {
    PreparedAnimationExport prepared;
    prepared.concat_path = QStringLiteral("/tmp/frames.ffconcat");
    prepared.duration_seconds = 1.25;
    AnimationExportOptions options;
    options.format = AnimationExportFormat::Mp4;
    options.scale = 4;
    options.video_bitrate_kbps = 3'456;
    options.video_preset = QStringLiteral("slow");

    const QString joined = mp4_ffmpeg_arguments(prepared, QStringLiteral("movie.mp4"), options)
                               .join(QLatin1Char('|'));
    assert(joined.contains(QStringLiteral("-c:v|libx264")));
    assert(joined.contains(QStringLiteral("-b:v|3456k")));
    assert(joined.contains(QStringLiteral("-maxrate|3456k")));
    assert(joined.contains(QStringLiteral("-bufsize|6912k")));
    assert(joined.contains(QStringLiteral("-preset|slow")));
    assert(joined.contains(QStringLiteral("scale=iw*4:ih*4:flags=neighbor")));
    assert(joined.contains(QStringLiteral("format=yuv420p")));
    assert(joined.contains(QStringLiteral("-movflags|+faststart")));
    assert(joined.endsWith(QStringLiteral("movie.mp4")));
}

void test_gif_arguments_use_palette_dithering_and_loop_settings() {
    PreparedAnimationExport prepared;
    prepared.concat_path = QStringLiteral("/tmp/frames.ffconcat");
    prepared.palette_path = QStringLiteral("/tmp/palette.png");
    prepared.duration_seconds = 0.75;
    prepared.final_delay_centiseconds = 23;
    AnimationExportOptions options;
    options.format = AnimationExportFormat::Gif;
    options.scale = 3;
    options.gif_max_colors = 64;
    options.gif_dither = GifDither::None;
    options.gif_loop_forever = false;

    const QString palette = gif_palette_ffmpeg_arguments(prepared, options).join(QLatin1Char('|'));
    assert(palette.contains(QStringLiteral("scale=iw*3:ih*3:flags=neighbor")));
    assert(palette.contains(QStringLiteral("palettegen=max_colors=64")));
    assert(palette.contains(QStringLiteral("-update|1")));
    assert(palette.endsWith(QStringLiteral("/tmp/palette.png")));

    const QString encode =
        gif_encode_ffmpeg_arguments(prepared, QStringLiteral("animation.gif"), options)
            .join(QLatin1Char('|'));
    assert(encode.contains(QStringLiteral("paletteuse=dither=none")));
    assert(encode.contains(QStringLiteral("-loop|-1")));
    assert(encode.contains(QStringLiteral("-final_delay|23")));
    assert(encode.contains(QStringLiteral("-t|0.750000")));
    assert(encode.endsWith(QStringLiteral("animation.gif")));
}

void test_invalid_document_and_quoted_paths() {
    QTemporaryDir directory;
    assert(directory.isValid());
    Document document = Document::create(1, 1);
    document.frames.clear();
    PreparedAnimationExport prepared;
    QString error;
    assert(!prepare_animation_export(document, directory.path(), {}, prepared, &error));
    assert(!error.isEmpty());

    const QString quoted = ffconcat_quote_path(directory.filePath(QStringLiteral("it's a frame.png")));
    assert(quoted.startsWith(QLatin1Char('\'')));
    assert(quoted.endsWith(QLatin1Char('\'')));
    assert(quoted.contains(QStringLiteral("'\\''")));
}

void test_real_ffmpeg_encoding_when_requested() {
    if (!qEnvironmentVariableIsSet("PIXELART_RUN_FFMPEG_INTEGRATION")) return;
    const QString ffmpeg = detect_ffmpeg_executable();
    assert(!ffmpeg.isEmpty());

    QTemporaryDir directory;
    assert(directory.isValid());
    const Document document = animation_document();
    QString error;

    AnimationExportOptions gif_options;
    gif_options.format = AnimationExportFormat::Gif;
    gif_options.scale = 2;
    PreparedAnimationExport gif_prepared;
    const QString gif_directory = directory.filePath(QStringLiteral("gif export's frames"));
    assert(prepare_animation_export(document, gif_directory, gif_options, gif_prepared, &error));
    assert(QProcess::execute(ffmpeg, gif_palette_ffmpeg_arguments(gif_prepared, gif_options)) == 0);
    const QString gif_path = directory.filePath(QStringLiteral("animation.gif"));
    assert(QProcess::execute(
               ffmpeg, gif_encode_ffmpeg_arguments(gif_prepared, gif_path, gif_options)) == 0);
    assert(QFileInfo(gif_path).size() > 0);

    AnimationExportOptions mp4_options;
    mp4_options.format = AnimationExportFormat::Mp4;
    mp4_options.scale = 2;
    mp4_options.video_bitrate_kbps = 512;
    PreparedAnimationExport mp4_prepared;
    const QString mp4_directory = directory.filePath(QStringLiteral("mp4 export frames"));
    assert(prepare_animation_export(document, mp4_directory, mp4_options, mp4_prepared, &error));
    const QString mp4_path = directory.filePath(QStringLiteral("animation.mp4"));
    assert(QProcess::execute(
               ffmpeg, mp4_ffmpeg_arguments(mp4_prepared, mp4_path, mp4_options)) == 0);
    assert(QFileInfo(mp4_path).size() > 0);

#if defined(Q_OS_WIN)
    const QString ffprobe = QFileInfo(ffmpeg).dir().filePath(QStringLiteral("ffprobe.exe"));
#else
    const QString ffprobe = QFileInfo(ffmpeg).dir().filePath(QStringLiteral("ffprobe"));
#endif
    if (QFileInfo(ffprobe).isExecutable()) {
        for (const QString& path : {gif_path, mp4_path}) {
            QProcess probe;
            probe.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                                  QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                  QStringLiteral("-of"),
                                  QStringLiteral("default=noprint_wrappers=1:nokey=1"), path});
            assert(probe.waitForFinished(10'000));
            bool parsed = false;
            const double duration = QString::fromUtf8(probe.readAllStandardOutput()).trimmed().toDouble(&parsed);
            assert(parsed);
            assert(duration >= 0.35 && duration <= 0.40);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    test_ffmpeg_detection_override();
    test_variable_timing_manifest_and_gif_frames();
    test_fixed_fps_and_mp4_background_flattening();
    test_mp4_arguments_include_quality_and_compatibility_controls();
    test_gif_arguments_use_palette_dithering_and_loop_settings();
    test_invalid_document_and_quoted_paths();
    test_real_ffmpeg_encoding_when_requested();
    return 0;
}
