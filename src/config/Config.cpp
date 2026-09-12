#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

QString Config::configFilePath()
{
    // Deterministic XDG path: $XDG_CONFIG_HOME/system-overlay/config.ini
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/system-overlay/config.ini");
}

void Config::setRefreshInterval(int ms)
{
    m_refreshInterval = std::clamp(ms, 100, 60000);
}

void Config::setScreen(const QString &spec)
{
    const QString s = spec.trimmed().toLower();
    if (s == QLatin1String("primary")) {
        m_screenMode = ScreenMode::Primary;
        m_screenName.clear();
    } else if (s == QLatin1String("all")) {
        m_screenMode = ScreenMode::All;
        m_screenName.clear();
    } else if (!s.isEmpty()) {
        m_screenMode = ScreenMode::Named;
        m_screenName = spec.trimmed();
    }
}

static Config::Position parsePosition(const QString &value)
{
    const QString s = value.trimmed().toLower();
    if (s == QLatin1String("top-left"))
        return Config::Position::TopLeft;
    if (s == QLatin1String("top-right"))
        return Config::Position::TopRight;
    if (s == QLatin1String("bottom-left"))
        return Config::Position::BottomLeft;
    // "bottom-right" is the default; unknown values fall back to it too.
    return Config::Position::BottomRight;
}

void Config::setDebug(bool on)
{
    m_debug = on;
}

void Config::load(const QString &explicitPath)
{
    const QString path = explicitPath.isEmpty() ? configFilePath() : explicitPath;

    if (!QFileInfo::exists(path)) {
        // First run: leave a commented template behind, then use defaults.
        writeDefaultConfigFile(path);
        return;
    }

    QSettings settings(path, QSettings::IniFormat);

    setRefreshInterval(settings.value(QStringLiteral("general/refresh_interval"), 1000).toInt());

    setScreen(settings.value(QStringLiteral("display/screen"), QStringLiteral("primary")).toString());

    m_position = parsePosition(
        settings.value(QStringLiteral("display/position"), QStringLiteral("bottom-right")).toString());

    m_offsetX = settings.value(QStringLiteral("display/offset_x"), 10).toInt();
    m_offsetY = settings.value(QStringLiteral("display/offset_y"), 10).toInt();
    m_fontSizePx = std::clamp(
        settings.value(QStringLiteral("display/font_size"), 14).toInt(), 8, 72);
    m_fontFamily = settings.value(QStringLiteral("display/font_family"), QStringLiteral("monospace"))
                       .toString();
    m_showBackground = settings.value(QStringLiteral("display/show_background"), false).toBool();
    m_showTray = settings.value(QStringLiteral("display/show_tray"), true).toBool();
    m_textColor = settings.value(QStringLiteral("display/text_color"), m_textColor).toString();
    m_outlineColor = settings.value(QStringLiteral("display/outline_color"), m_outlineColor).toString();

    m_showCpuUsage = settings.value(QStringLiteral("metrics/show_cpu_usage"), true).toBool();
    m_showCpuTemp = settings.value(QStringLiteral("metrics/show_cpu_temp"), true).toBool();
    m_showGpuUsage = settings.value(QStringLiteral("metrics/show_gpu_usage"), true).toBool();
    m_showGpuTemp = settings.value(QStringLiteral("metrics/show_gpu_temp"), true).toBool();
    m_showRam = settings.value(QStringLiteral("metrics/show_ram"), true).toBool();
    m_showVram = settings.value(QStringLiteral("metrics/show_vram"), true).toBool();
}

void Config::writeDefaultConfigFile(const QString &path) const
{
    const QFileInfo fi(path);
    if (!QDir().mkpath(fi.absolutePath()))
        return;
    if (QFileInfo::exists(path))
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&f);
    out << QStringLiteral(
        "# system-overlay configuration\n"
        "# Missing keys fall back to the defaults shown below.\n"
        "\n"
        "[general]\n"
        "refresh_interval=1000\n"
        "\n"
        "[display]\n"
        "# screen: primary | all | output name (e.g. DP-1, HDMI-A-1)\n"
        "screen=primary\n"
        "# position: top-left | top-right | bottom-left | bottom-right\n"
        "position=bottom-right\n"
        "# offsets are measured from the anchored edges (right/bottom for\n"
        "# bottom-right). Bottom positions also avoid panels.\n"
        "offset_x=10\n"
        "offset_y=10\n"
        "font_size=14\n"
        "font_family=monospace\n"
        "show_background=false\n"
        "show_tray=true\n"
        "text_color=#a6f28f\n"
        "outline_color=#000000\n"
        "\n"
        "[metrics]\n"
        "show_cpu_usage=true\n"
        "show_cpu_temp=true\n"
        "show_gpu_usage=true\n"
        "show_gpu_temp=true\n"
        "show_ram=true\n"
        "show_vram=true\n");
}
