#pragma once

#include <QObject>
#include <QPointer>
#include <memory>
#include <vector>

#include "OverlayWindow.h"

#include "config/Config.h"

class MetricManager;
class QScreen;

// Owns the overlay window(s) and the screen-following logic.
// First release: primary monitor. The screen mode ("primary" / "all" / output
// name) is already wired here; "all" creates one overlay per screen.
class OverlayController : public QObject
{
    Q_OBJECT
public:
    OverlayController(const Config &config, MetricManager *metrics, QObject *parent = nullptr);

    void start();

private:
    OverlayWindow::RenderConfig renderConfigFor(QScreen *screen) const;
    void addWindowForScreen(QScreen *screen);
    void removeWindowForScreen(QScreen *screen);
    void showWindow(OverlayWindow *win, QScreen *screen);
    bool applyLayerShell(OverlayWindow *win, QScreen *screen);

    const Config &m_config;
    MetricManager *m_metrics = nullptr;

    struct Entry
    {
        std::unique_ptr<OverlayWindow> window;
        QScreen *screen = nullptr;
        bool layerShell = false;
    };
    std::vector<Entry> m_windows;
};
