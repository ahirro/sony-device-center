#pragma once

#include <QElapsedTimer>
#include <QObject>

class QMenu;
class QQmlApplicationEngine;
class QSystemTrayIcon;
class QWindow;

namespace sony::devicecenter {

class DeviceCenterController;

// System tray icon with a right-click menu and a small quick-control window
// (qml/TrayPopup.qml) that opens on a left click.
class TrayManager : public QObject {
    Q_OBJECT
public:
    TrayManager(QQmlApplicationEngine& engine, DeviceCenterController& controller,
                QWindow* mainWindow, QObject* parent = nullptr);
    ~TrayManager() override;

    // False on desktops without a tray (some Wayland/GNOME setups).
    static bool available();

public slots:
    void showMainWindow();
    void togglePopup();

private:
    void updateToolTip();
    void positionPopup();

    DeviceCenterController& _controller;
    QSystemTrayIcon* _tray{nullptr};
    QMenu* _menu{nullptr};
    QWindow* _main{nullptr};
    QWindow* _popup{nullptr};
    QElapsedTimer _popupHidden;
    bool _hintShown{false};
};

} // namespace sony::devicecenter
