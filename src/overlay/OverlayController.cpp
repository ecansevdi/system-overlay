#include "OverlayController.h"

#include "TrayIcon.h"
#include "WaylandOverlay.h"
#include "X11Overlay.h"
#include "metrics/HudRow.h"
#include "metrics/MetricManager.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace {

constexpr auto kHudBusPath = "/Hud";
constexpr auto kMenuPlugin = "system-overlay-menu";

const char kMenuScript[] = R"JS(
var hadPopup = false;
function isPlasmaPopup(w) {
    if (!w)
        return false;
    try {
        var rc = String(w.resourceClass || "").toLowerCase();
        if (rc.indexOf("plasmashell") < 0)
            return false;
        return !!(w.popupWindow || w.dropdownMenu || w.comboBox);
    } catch (e) {
        return false;
    }
}
function recount() {
    var n = 0;
    var list = workspace.stackingOrder;
    for (var i = 0; i < list.length; ++i) {
        if (isPlasmaPopup(list[i]))
            n++;
    }
    try {
        if (n > 0) {
            hadPopup = true;
            callDBus("local.systemoverlay", "/Hud", "local.systemoverlay.Hud", "holdHud");
        } else if (hadPopup) {
            hadPopup = false;
            callDBus("local.systemoverlay", "/Hud", "local.systemoverlay.Hud", "releaseHudSoon");
        }
    } catch (e) {}
}
var timer = new QTimer();
timer.interval = 200;
timer.timeout.connect(recount);
timer.start();
)JS";

class HudDbusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.systemoverlay.Hud")
public:
    explicit HudDbusAdaptor(OverlayController *c)
        : QDBusAbstractAdaptor(c)
        , m(c)
    {
    }

public Q_SLOTS:
    void holdHud() { m->holdHud(); }
    void releaseHud() { m->releaseHud(); }
    void releaseHudSoon() { m->releaseHudSoon(); }

private:
    OverlayController *m = nullptr;
};

} // namespace

OverlayController::OverlayController(const Config &config, MetricManager *metrics, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_metrics(metrics)
{
    connect(m_metrics, &MetricManager::rowsChanged, this, [this](const std::vector<HudRow> &rows) {
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
    registerHudDbus();
}

OverlayController::~OverlayController()
{
    unloadMenuScript();
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

void OverlayController::hideAllWindows()
{
    for (const Entry &e : m_windows)
        e.window->hide();
}

void OverlayController::showAllWindows()
{
    const bool wayland = QGuiApplication::platformName() == QLatin1String("wayland");
    for (const Entry &e : m_windows) {
        if (e.layerShell) {
            WaylandOverlay::configure(e.window.get(), m_config, e.screen);
            WaylandOverlay::applyScreen(e.window.get(), e.screen);
        }
        e.window->setOpacity(1);
        e.window->show();
        if (!wayland)
            X11Overlay::place(e.window.get(), e.screen, m_config);
    }
}

void OverlayController::rebuildWindows()
{
    std::vector<QScreen *> screens;
    screens.reserve(m_windows.size());
    for (const Entry &e : m_windows)
        screens.push_back(e.screen);
    m_windows.clear();
    for (QScreen *screen : screens) {
        addWindowForScreen(screen);
        showWindow(m_windows.back().window.get(), screen);
    }
}

void OverlayController::setHudSuppressed(bool suppressed)
{
    if (!suppressed) {
        // Parking works; unparking the same layer-shell surface often does
        // not. Recreate the overlay so it maps again in the corner.
        rebuildWindows();
        return;
    }
    for (const Entry &e : m_windows) {
        if (e.layerShell)
            WaylandOverlay::park(e.window.get(), e.screen);
        else
            e.window->hide();
    }
}

void OverlayController::holdHudForMenu()
{
    if (m_hudRestoreTimer)
        m_hudRestoreTimer->stop();
    m_hudParked = true;
    m_cursorAwayTicks = 0;
    if (m_menuPollTimer)
        m_menuPollTimer->start();
    setHudSuppressed(true);
}

void OverlayController::armHudRestore()
{
    if (!m_userWantsVisible || !m_hudParked)
        return;
    if (m_hudRestoreTimer)
        m_hudRestoreTimer->start();
}

void OverlayController::holdHud()
{
    m_sawShellPopup = true;
    holdHudForMenu();
}

void OverlayController::releaseHudSoon()
{
    if (!m_sawShellPopup)
        return;
    m_sawShellPopup = false;
    armHudRestore();
}

void OverlayController::releaseHud()
{
    if (m_hudRestoreTimer)
        m_hudRestoreTimer->stop();
    if (m_menuPollTimer)
        m_menuPollTimer->stop();
    m_cursorAwayTicks = 0;
    if (!m_userWantsVisible) {
        m_hudParked = false;
        return;
    }
    if (!m_hudParked)
        return;
    m_hudParked = false;
    m_sawShellPopup = false;
    setHudSuppressed(false);
}

void OverlayController::pollMenuCursor()
{
    if (!m_hudParked || !m_userWantsVisible)
        return;
    QScreen *screen = nullptr;
    if (!m_windows.empty())
        screen = m_windows.front().screen;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect g = screen->availableGeometry();
    const QPoint c = QCursor::pos();
    // Tray menus open from the panel. Keep the HUD parked while the pointer
    // stays in that half of the screen so a tall "Renk" submenu is covered.
    const bool inMenuZone = m_config.bottomAnchored()
        ? (c.y() >= g.top() + g.height() * 45 / 100)
        : (c.y() <= g.top() + g.height() * 55 / 100);
    if (inMenuZone) {
        m_cursorAwayTicks = 0;
        return;
    }
    ++m_cursorAwayTicks;
    if (m_cursorAwayTicks >= 4)
        releaseHud();
}

void OverlayController::createTrayIcon()
{
    if (!m_config.showTray() || m_tray)
        return;

    m_tray = new TrayIcon(this);

    m_hudRestoreTimer = new QTimer(this);
    m_hudRestoreTimer->setSingleShot(true);
    m_hudRestoreTimer->setInterval(400);
    connect(m_hudRestoreTimer, &QTimer::timeout, this, &OverlayController::releaseHud);

    m_menuPollTimer = new QTimer(this);
    m_menuPollTimer->setInterval(150);
    connect(m_menuPollTimer, &QTimer::timeout, this, &OverlayController::pollMenuCursor);

    connect(m_tray, &TrayIcon::hideRequested, this, [this]() {
        m_userWantsVisible = false;
        if (m_hudRestoreTimer)
            m_hudRestoreTimer->stop();
        if (m_menuPollTimer)
            m_menuPollTimer->stop();
        m_hudParked = false;
        hideAllWindows();
    });
    connect(m_tray, &TrayIcon::showRequested, this, [this]() {
        m_userWantsVisible = true;
        showAllWindows();
    });
    connect(m_tray, &TrayIcon::menuAboutToShow, this, &OverlayController::holdHudForMenu);
    connect(m_tray, &TrayIcon::menuAboutToHide, this, &OverlayController::armHudRestore);
    connect(m_tray, &TrayIcon::menuActionTriggered, this, &OverlayController::releaseHud);
    connect(m_tray, &TrayIcon::pauseRequested, m_metrics, &MetricManager::setPaused);
    connect(m_tray, &TrayIcon::metricToggled, m_metrics, &MetricManager::setRowVisible);
    connect(m_tray, &TrayIcon::baseColorRequested, this, [this](const QColor &color) {
        m_metrics->setBaseColor(color);
        Config::saveTextColor(color); // survives restarts
    });
    connect(m_tray, &TrayIcon::quitRequested, qGuiApp, &QCoreApplication::quit);

    for (int row = 0; row < RowCount; ++row)
        m_tray->setRowChecked(row, m_metrics->rowVisible(row));
}

void OverlayController::registerHudDbus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    new HudDbusAdaptor(this);
    bus.registerObject(QString::fromUtf8(kHudBusPath), this);
}

void OverlayController::loadMenuScript()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/system-overlay-kwin-menu.js");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    f.write(kMenuScript);
    f.close();

    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QDBusConnection::sessionBus());
    if (!kwin.isValid())
        return;
    kwin.call(QStringLiteral("unloadScript"), QString::fromUtf8(kMenuPlugin));
    kwin.call(QStringLiteral("loadScript"), path, QString::fromUtf8(kMenuPlugin));
    kwin.call(QStringLiteral("start"));
}

void OverlayController::unloadMenuScript()
{
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QDBusConnection::sessionBus());
    if (!kwin.isValid())
        return;
    kwin.call(QStringLiteral("unloadScript"), QString::fromUtf8(kMenuPlugin));
}

void OverlayController::start()
{
    createTrayIcon();
    loadMenuScript();

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

#include "OverlayController.moc"
