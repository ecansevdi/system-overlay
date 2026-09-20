#pragma once

#include <QObject>
#include <QPointer>
#include <memory>
#include <vector>

#include "OverlayWindow.h"

#include "config/Config.h"

class MetricManager;
class QScreen;
class QTimer;
class TrayIcon;

// Owns the overlay window(s), the screen-following logic and the tray icon.
// First release: primary monitor. The screen mode ("primary" / "all" / output
// name) is already wired here; "all" creates one overlay per screen.
class OverlayController : public QObject
{
    Q_OBJECT
public:
    OverlayController(const Config &config, MetricManager *metrics, QObject *parent = nullptr);
    ~OverlayController() override;

    void start();

public Q_SLOTS:
    void holdHud();
    void releaseHud();
    void releaseHudSoon();

private:
    OverlayWindow::RenderConfig renderConfigFor(QScreen *screen) const;
    void addWindowForScreen(QScreen *screen);
    void removeWindowForScreen(QScreen *screen);
    void showWindow(OverlayWindow *win, QScreen *screen);
    bool applyLayerShell(OverlayWindow *win, QScreen *screen);
    void createTrayIcon();
    void hideAllWindows();
    void showAllWindows();
    void setHudSuppressed(bool suppressed);
    void armHudRestore();
    void holdHudForMenu();
    void rebuildWindows();
    void registerHudDbus();
    void loadMenuScript();
    void unloadMenuScript();
    void pollMenuCursor();

    const Config &m_config;
    MetricManager *m_metrics = nullptr;
    TrayIcon *m_tray = nullptr;
    QTimer *m_hudRestoreTimer = nullptr;
    QTimer *m_menuPollTimer = nullptr;
    bool m_userWantsVisible = true;
    bool m_hudParked = false;
    bool m_sawShellPopup = false;
    int m_cursorAwayTicks = 0;

    struct Entry
    {
        std::unique_ptr<OverlayWindow> window;
        QScreen *screen = nullptr;
        bool layerShell = false;
    };
    std::vector<Entry> m_windows;
};
