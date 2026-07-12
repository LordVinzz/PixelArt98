// Copyright (c) 2026 DOMINGUEZ Vincent
// Licensed under the DOMINGUEZ Non-Commercial Software License v1.0.

#include "ui/AppSettings.hpp"
#include "ui/EmbeddedAssets.hpp"
#include "ui/QtMainWindow.hpp"

#include <QApplication>
#include <QByteArray>
#include <QPixmap>
#include <QScreen>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include <QTimer>

#include <string>

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PixelArt98"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("PixelArt98"));

    QString import_path;
    const QStringList arguments = application.arguments();
    for (qsizetype index = 1; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == QStringLiteral("--import-image")) import_path = arguments[++index];
    }

    const px::AppSettings settings = px::load_app_settings();
    QSplashScreen* splash = nullptr;
    if (settings.show_splash_screen) {
        const auto bytes = px::assets::splash_png();
        QPixmap pixmap;
        pixmap.loadFromData(QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())), "PNG");
        if (!pixmap.isNull()) {
            const QSize available = application.primaryScreen()->availableGeometry().size() / 2;
            pixmap = pixmap.scaled(available, Qt::KeepAspectRatio, Qt::FastTransformation);
            splash = new QSplashScreen(pixmap);
            splash->show();
            application.processEvents();
        }
    }

    px::QtMainWindow window(settings);
    window.show();
    if (!import_path.isEmpty()) window.import_image_document(import_path);
    if (splash != nullptr) {
        splash->finish(&window);
        delete splash;
    }
    return application.exec();
}
