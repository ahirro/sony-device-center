#include <QApplication>
#include <QCoreApplication>
#include <QWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>

#include "DeviceCenterController.h"
#include "TrayManager.h"

#include <memory>

int main(int argc, char *argv[]) {
    // QApplication (not QGuiApplication): the tray icon and its menu live in QtWidgets.
    QApplication app(argc, argv);

    // Main.qml customises background/handle/indicator on its controls. The
    // native "Windows" and "macOS" styles Qt picks by default there refuse
    // that (one warning per control, and native-looking widgets), so pin the
    // one style that honours customisation on every platform.
    QQuickStyle::setStyle("Basic");
    app.setApplicationName("Sony Device Center");
    app.setOrganizationName("SonyBridge");
    app.setApplicationVersion(SONY_DEVICE_CENTER_VERSION);

    // Wayland and the GNOME/KDE shells match a window to its .desktop entry by
    // this name; without it the taskbar falls back to a generic placeholder
    // even though the window icon below is set.
    QGuiApplication::setDesktopFileName("sony-device-center");

    // The raster form is used deliberately: QIcon can only read the SVG brand
    // asset when Qt's qsvg image plugin is deployed alongside the binary.
    app.setWindowIcon(QIcon(":/resources/brand/app-icon.png"));

    sony::devicecenter::DeviceCenterController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("controller", &controller);

    // Autostart passes --minimized: start hidden in the tray (only when a tray exists).
    const bool trayAvailable = sony::devicecenter::TrayManager::available();
    const bool startMinimized = trayAvailable && QCoreApplication::arguments().contains("--minimized");
    engine.rootContext()->setContextProperty("startMinimized", startMinimized);

    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.load(url);

    std::unique_ptr<sony::devicecenter::TrayManager> tray;
    if (trayAvailable && !engine.rootObjects().isEmpty()) {
        tray = std::make_unique<sony::devicecenter::TrayManager>(
            engine, controller, qobject_cast<QWindow*>(engine.rootObjects().first()));
    }

    const int result = app.exec();
    tray.reset();
    return result;
}
