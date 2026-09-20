#include "FpsTracker.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

namespace {

constexpr auto kBusService = "local.systemoverlay";
constexpr auto kBusPath = "/Fps";
constexpr auto kBusInterface = "local.systemoverlay.Fps";
constexpr auto kPluginName = "system-overlay-fps";

const char kKwinScript[] = R"JS(
var counts = {};
var timer = new QTimer();

function keyOf(w) {
    try { return String(w.internalId); } catch (e) { return ""; }
}

function isTracked(w) {
    if (!w)
        return false;
    try {
        if (w.deleted)
            return false;
        if (w.desktopWindow || w.dock || w.splash || w.specialWindow)
            return false;
        if (w.tooltip || w.notification || w.criticalNotification)
            return false;
        if (w.popupWindow || w.dropdownMenu || w.comboBox)
            return false;
        var rc = String(w.resourceClass || "");
        if (rc.indexOf("system-overlay") !== -1)
            return false;
    } catch (e) {
        return false;
    }
    return true;
}

function attach(w) {
    if (!isTracked(w))
        return;
    var k = keyOf(w);
    if (!k)
        return;
    w.damaged.connect(function() {
        counts[k] = (counts[k] || 0) + 1;
    });
}

var list = workspace.stackingOrder;
for (var i = 0; i < list.length; ++i)
    attach(list[i]);
workspace.windowAdded.connect(attach);

timer.interval = 1000;
timer.timeout.connect(function() {
    var max = 0;
    for (var k in counts) {
        if (counts[k] > max)
            max = counts[k];
        counts[k] = 0;
    }
    try {
        callDBus("local.systemoverlay", "/Fps", "local.systemoverlay.Fps", "report", max);
    } catch (e) {}
});
timer.start();
)JS";

} // namespace

class FpsDbusAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.systemoverlay.Fps")
public:
    explicit FpsDbusAdaptor(FpsTracker *tracker)
        : QDBusAbstractAdaptor(tracker)
        , m_tracker(tracker)
    {
    }

public Q_SLOTS:
    void report(int fps) { m_tracker->report(fps); }

private:
    FpsTracker *m_tracker = nullptr;
};

FpsTracker::FpsTracker(QObject *parent)
    : QObject(parent)
{
    registerDbus();
}

FpsTracker::~FpsTracker()
{
    stop();
}

void FpsTracker::registerDbus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_source = QStringLiteral("session bus unavailable");
        return;
    }
    if (!bus.registerService(QString::fromUtf8(kBusService))) {
        m_source = QStringLiteral("DBus name already taken");
        return;
    }
    m_adaptor = new FpsDbusAdaptor(this);
    if (!bus.registerObject(QString::fromUtf8(kBusPath), this)) {
        m_source = QStringLiteral("DBus object register failed");
        return;
    }
    m_dbusReady = true;
}

void FpsTracker::report(int fps)
{
    m_latest = std::max(0, fps);
    m_haveSample = true;
}

std::optional<int> FpsTracker::sample() const
{
    if (!m_running || !m_haveSample)
        return std::nullopt;
    return m_latest;
}

QString FpsTracker::debugInfo() const
{
    QString line = QStringLiteral("  FPS: ");
    if (!m_running)
        return line + QStringLiteral("tracker stopped\n");
    if (!m_source.isEmpty())
        line += m_source;
    else
        line += QStringLiteral("KWin Window.damaged (fastest client)");
    if (m_haveSample)
        line += QStringLiteral(", last=%1").arg(m_latest);
    return line + QLatin1Char('\n');
}

bool FpsTracker::writeScriptFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        return false;
    QDir().mkpath(dir);
    m_scriptPath = dir + QStringLiteral("/system-overlay-kwin-fps.js");
    QFile f(m_scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream out(&f);
    out << QString::fromUtf8(kKwinScript);
    return true;
}

void FpsTracker::unloadKwinScript()
{
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QDBusConnection::sessionBus());
    if (!kwin.isValid())
        return;
    kwin.call(QStringLiteral("unloadScript"), QString::fromUtf8(kPluginName));
}

bool FpsTracker::loadKwinScript()
{
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QDBusConnection::sessionBus());
    if (!kwin.isValid()) {
        m_source = QStringLiteral("org.kde.KWin Scripting not available");
        return false;
    }
    if (!writeScriptFile()) {
        m_source = QStringLiteral("could not write KWin fps script");
        return false;
    }
    kwin.call(QStringLiteral("unloadScript"), QString::fromUtf8(kPluginName));
    const QDBusReply<int> id = kwin.call(QStringLiteral("loadScript"), m_scriptPath,
        QString::fromUtf8(kPluginName));
    if (!id.isValid() || id.value() < 0) {
        m_source = QStringLiteral("KWin loadScript failed");
        return false;
    }
    kwin.call(QStringLiteral("start"));
    m_source = QStringLiteral("KWin Window.damaged (fastest client)");
    return true;
}

void FpsTracker::start()
{
    if (m_running)
        return;
    if (!m_dbusReady)
        registerDbus();
    if (!loadKwinScript()) {
        m_running = true; // session is "on" so the tray toggle still has a lifetime
        return;
    }
    m_running = true;
    m_haveSample = false;
}

void FpsTracker::stop()
{
    if (!m_running)
        return;
    unloadKwinScript();
    m_running = false;
    m_haveSample = false;
    m_latest = 0;
}

#include "FpsTracker.moc"
