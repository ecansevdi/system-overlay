#include "OverlayController.h"

#include "TrayIcon.h"
#include "WaylandOverlay.h"
#include "X11Overlay.h"
#include "metrics/MetricManager.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

OverlayController::OverlayController(const Config &config, MetricManager *metrics, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_metrics(metrics)
{
    connect(m_metrics, &MetricManager::rowsChanged, this, [this](const QList<HudRow> &rows) {
        const bool xcb = QGuiApplication::platformName() != QLatin1String("wayland");
        for (const Entry &e : m_windows) {
            e.window->setRows(rows);
            // On X11 the window is self-positioned: recompute whenever the
            // content (and therefore the window size) changes so a
            // bottom/right-anchored HUD stays pinned to its corner.
            if (xcb)
                X11Overlay::place(e.window.get(), e.screen, m_config);
        }
    });
}

OverlayWindow::RenderConfig OverlayController::renderConfigFor(QScreen *screen) const
{
    OverlayWindow::RenderConfig rc;

    const QString family = m_config.fontFamily();
    if (family.compare(QLatin1String("monospace"), Qt::CaseInsensitive) == 0) {
        rc.font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        rc.font.setStyleHint(QFont::TypeWriter);
    } else {
        rc.font = QFont(family);
        rc.font.setStyleHint(QFont::TypeWriter);
    }
    rc.font.setPixelSize(m_config.fontSizePx());

    rc.textColor = QColor(m_config.textColor());
    rc.outlineColor = QColor(m_config.outlineColor());
    rc.showBackground = m_config.showBackground();

    Q_UNUSED(screen);
    return rc;
}

bool OverlayController::applyLayerShell(OverlayWindow *win, QScreen *screen)
{
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        return false;
    return WaylandOverlay::configure(win, m_config, screen);
}

void OverlayController::addWindowForScreen(QScreen *screen)
{
    Entry entry;
    entry.screen = screen;
    entry.window = std::make_unique<OverlayWindow>(renderConfigFor(screen));
    entry.layerShell = applyLayerShell(entry.window.get(), screen);
    if (!entry.layerShell)
        X11Overlay::configure(entry.window.get(), m_config, screen);

    // Paint the last known rows as soon as the window is exposed.
    entry.window->setRows(m_metrics->currentRows());

    m_windows.push_back(std::move(entry));
}

void OverlayController::showWindow(OverlayWindow *win, QScreen *screen)
{
    win->setScreen(screen);
    win->show();

    // On X11 (and any non-layer-shell platform) the WM decides the initial
    // position; pin the window to the configured screen corner.
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        X11Overlay::place(win, screen, m_config);
}

void OverlayController::removeWindowForScreen(QScreen *screen)
{
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(),
                        [screen](const Entry &e) { return e.screen == screen; }),
        m_windows.end());
}

void OverlayController::createTrayIcon()
{
    if (!m_config.showTray() || m_tray)
        return;

    m_tray = new TrayIcon(this);

    connect(m_tray, &TrayIcon::hideRequested, this, [this]() {
        for (const Entry &e : m_windows)
            e.window->hide();
    });
    connect(m_tray, &TrayIcon::showRequested, this, [this]() {
        const bool xcb = QGuiApplication::platformName() != QLatin1String("wayland");
        for (const Entry &e : m_windows) {
            e.window->show();
            if (xcb)
                X11Overlay::place(e.window.get(), e.screen, m_config);
        }
    });
    connect(m_tray, &TrayIcon::pauseRequested, m_metrics, &MetricManager::setPaused);
    connect(m_tray, &TrayIcon::metricToggled, m_metrics, &MetricManager::setRowVisible);
    connect(m_tray, &TrayIcon::baseColorRequested, this, [this](const QColor &color) {
        m_metrics->setBaseColor(color);
        Config::saveTextColor(color); // survives restarts
    });
    connect(m_tray, &TrayIcon::quitRequested, qGuiApp, &QCoreApplication::quit);
}

void OverlayController::start()
{
    createTrayIcon();

    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *screen) {
        if (m_config.screenMode() != Config::ScreenMode::Primary || m_windows.empty())
            return;
        Entry &entry = m_windows.front();
        entry.screen = screen;
        if (entry.layerShell) {
            // Layer surfaces are bound to an output: remap to follow.
            entry.window->hide();
            entry.window->setScreen(screen);
            WaylandOverlay::applyScreen(entry.window.get(), screen);
            entry.window->show();
        } else {
            entry.window->setScreen(screen);
            X11Overlay::place(entry.window.get(), screen, m_config);
        }
    });

    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        if (m_config.screenMode() == Config::ScreenMode::All)
            addWindowForScreen(screen);
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        removeWindowForScreen(screen);
    });

    switch (m_config.screenMode()) {
    case Config::ScreenMode::All:
        for (QScreen *screen : QGuiApplication::screens())
            addWindowForScreen(screen);
        break;
    case Config::ScreenMode::Named: {
        QScreen *target = nullptr;
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == m_config.screenName()) {
                target = screen;
                break;
            }
        }
        if (!target) {
            qWarning("OverlayController: screen \"%s\" not found, falling back to primary",
                qUtf8Printable(m_config.screenName()));
            target = QGuiApplication::primaryScreen();
        }
        if (target)
            addWindowForScreen(target);
        break;
    }
    case Config::ScreenMode::Primary:
        if (QGuiApplication::primaryScreen())
            addWindowForScreen(QGuiApplication::primaryScreen());
        break;
    }

    for (Entry &entry : m_windows)
        showWindow(entry.window.get(), entry.screen);

    m_metrics->start();
}
