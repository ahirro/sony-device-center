#include "TrayManager.h"

#include "DeviceCenterController.h"

#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWindow>
#include <algorithm>

namespace sony::devicecenter {

namespace {
constexpr int kPopupMargin = 8;
constexpr int kReopenGuardMs = 300;  // the click that hides the popup must not reopen it
}

bool TrayManager::available() {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

TrayManager::TrayManager(QQmlApplicationEngine& engine, DeviceCenterController& controller,
                         QWindow* mainWindow, QObject* parent)
    : QObject(parent), _controller(controller), _main(mainWindow) {
    // Closing the main window must leave the program running in the tray.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    engine.load(QUrl(QStringLiteral("qrc:/qml/TrayPopup.qml")));
    for (auto* root : engine.rootObjects()) {
        if (root->objectName() == QLatin1String("trayPopup")) {
            _popup = qobject_cast<QWindow*>(root);
            break;
        }
    }
    if (_popup) {
        connect(_popup, SIGNAL(openMainRequested()), this, SLOT(showMainWindow()));
        connect(_popup, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible) _popupHidden.restart();
        });
    }

    _tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/resources/brand/app-icon.png")), this);

    _menu = new QMenu();  // owned by the tray icon's context-menu slot below
    auto* open = _menu->addAction(tr("Open Device Center"));
    connect(open, &QAction::triggered, this, &TrayManager::showMainWindow);
    _menu->addSeparator();
    connect(_menu->addAction(tr("Noise cancelling")), &QAction::triggered, this,
            [this] { _controller.setAnc(true); });
    connect(_menu->addAction(tr("Ambient sound")), &QAction::triggered, this, [this] {
        _controller.setAmbient(_controller.ambientLevel() > 0 ? _controller.ambientLevel() : 10,
                               _controller.focusOnVoice());
    });
    connect(_menu->addAction(tr("Noise control off")), &QAction::triggered, this,
            [this] { _controller.setNoiseControlOff(); });
    _menu->addSeparator();
    connect(_menu->addAction(tr("Quit")), &QAction::triggered, qApp, &QCoreApplication::quit);
    _tray->setContextMenu(_menu);

    connect(_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) togglePopup();
        else if (reason == QSystemTrayIcon::DoubleClick) {
            if (_popup) _popup->hide();
            showMainWindow();
        }
    });

    connect(&_controller, &DeviceCenterController::stateChanged, this, &TrayManager::updateToolTip);

    if (_main) {
        connect(_main, &QWindow::visibleChanged, this, [this](bool visible) {
            // Tell the user once that closing the window does not quit.
            if (visible || _hintShown) return;
            _hintShown = true;
            QSettings settings("SonyBridge", "SonyDeviceCenter");
            if (!settings.value("trayHintShown", false).toBool()) {
                settings.setValue("trayHintShown", true);
                _tray->showMessage(QStringLiteral("Sony Device Center"),
                                   tr("Still running in the tray. Right-click the icon to quit."),
                                   QSystemTrayIcon::Information, 5000);
            }
        });
    }

    updateToolTip();
    _tray->show();
}

TrayManager::~TrayManager() {
    if (_tray) _tray->hide();
    delete _menu;
}

void TrayManager::showMainWindow() {
    if (!_main) return;
    _main->show();
    _main->raise();
    _main->requestActivate();
}

void TrayManager::togglePopup() {
    if (!_popup) {
        showMainWindow();
        return;
    }
    if (_popup->isVisible()) {
        _popup->hide();
        return;
    }
    if (_popupHidden.isValid() && _popupHidden.elapsed() < kReopenGuardMs) return;
    positionPopup();
    _popup->show();
    _popup->raise();
    _popup->requestActivate();
}

void TrayManager::positionPopup() {
    const QRect icon = _tray->geometry();
    QScreen* screen = icon.isEmpty() ? QGuiApplication::primaryScreen()
                                     : QGuiApplication::screenAt(icon.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();
    const int w = _popup->width();
    const int h = _popup->height();

    int x, y;
    if (icon.isEmpty()) {
        // Some platforms do not report the icon position: use the bottom-right corner.
        x = area.right() - w - kPopupMargin;
        y = area.bottom() - h - kPopupMargin;
    } else {
        x = icon.center().x() - w / 2;
        // Taskbar at the bottom opens upwards, at the top opens downwards.
        y = icon.center().y() > area.center().y() ? icon.top() - h - kPopupMargin
                                                  : icon.bottom() + kPopupMargin;
    }
    x = std::clamp(x, area.left() + kPopupMargin, std::max(area.left() + kPopupMargin, area.right() - w - kPopupMargin));
    y = std::clamp(y, area.top() + kPopupMargin, std::max(area.top() + kPopupMargin, area.bottom() - h - kPopupMargin));
    _popup->setPosition(x, y);
}

void TrayManager::updateToolTip() {
    QString tip = _controller.deviceName().isEmpty() ? QStringLiteral("Sony Device Center")
                                                     : _controller.deviceName();
    if (_controller.batteryLevel() >= 0) tip += QStringLiteral(" - %1%").arg(_controller.batteryLevel());
    const QString mode = _controller.noiseControlMode();
    if (mode == QLatin1String("cancelling")) tip += QStringLiteral(" - Noise cancelling");
    else if (mode == QLatin1String("ambient")) tip += QStringLiteral(" - Ambient");
    else if (mode == QLatin1String("off")) tip += QStringLiteral(" - Noise control off");
    _tray->setToolTip(tip);
}

} // namespace sony::devicecenter
