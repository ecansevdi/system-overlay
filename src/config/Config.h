#pragma once

#include <QString>
#include <QStringList>

// Immutable (after startup) runtime configuration, assembled from the config
// file and command line overrides. Missing values always fall back to
// sensible defaults; a missing or broken config file must never be fatal.
class Config
{
public:
    enum class ScreenMode { Primary, All, Named };

    // Screen corner the HUD is pinned to. Bottom corners additionally avoid
    // panels (exclusive zone 0 instead of -1).
    enum class Position { TopLeft, TopRight, BottomLeft, BottomRight };

    void load(const QString &explicitPath = {});

    // Command line overrides (applied on top of the file values).
    void setRefreshInterval(int ms);
    void setScreen(const QString &spec);
    void setDebug(bool on);

    int refreshInterval() const { return m_refreshInterval; }
    ScreenMode screenMode() const { return m_screenMode; }
    const QString &screenName() const { return m_screenName; }
    bool debug() const { return m_debug; }

    Position position() const { return m_position; }
    bool bottomAnchored() const
    {
        return m_position == Position::BottomLeft || m_position == Position::BottomRight;
    }
    int offsetX() const { return m_offsetX; }
    int offsetY() const { return m_offsetY; }
    bool showTray() const { return m_showTray; }

    // Value colouring: one function for every percentage-based value.
    // >= criticalPct -> criticalColor, >= warningPct -> warningColor, else
    // the normal text colour. Thresholds are user-configurable.
    int warningPct() const { return m_warningPct; }
    int criticalPct() const { return m_criticalPct; }
    const QString &warningColor() const { return m_warningColor; }
    const QString &criticalColor() const { return m_criticalColor; }
    int fontSizePx() const { return m_fontSizePx; }
    const QString &fontFamily() const { return m_fontFamily; }
    bool showBackground() const { return m_showBackground; }
    const QString &textColor() const { return m_textColor; }
    const QString &outlineColor() const { return m_outlineColor; }

    bool showCpuUsage() const { return m_showCpuUsage; }
    bool showCpuTemp() const { return m_showCpuTemp; }
    bool showGpuUsage() const { return m_showGpuUsage; }
    bool showGpuTemp() const { return m_showGpuTemp; }
    bool showRam() const { return m_showRam; }
    bool showVram() const { return m_showVram; }

    static QString configFilePath();

private:
    void writeDefaultConfigFile(const QString &path) const;

    int m_refreshInterval = 1000;
    ScreenMode m_screenMode = ScreenMode::Primary;
    QString m_screenName;
    bool m_debug = false;

    Position m_position = Position::BottomRight;
    int m_offsetX = 10;
    int m_offsetY = 10;
    bool m_showTray = true;

    int m_warningPct = 75;
    int m_criticalPct = 90;
    QString m_warningColor = QStringLiteral("#f2d24f"); // yellow
    QString m_criticalColor = QStringLiteral("#f25d5d"); // red
    int m_fontSizePx = 14;
    QString m_fontFamily = QStringLiteral("monospace");
    bool m_showBackground = false;
    QString m_textColor = QStringLiteral("#a6f28f");
    QString m_outlineColor = QStringLiteral("#000000");

    bool m_showCpuUsage = true;
    bool m_showCpuTemp = true;
    bool m_showGpuUsage = true;
    bool m_showGpuTemp = true;
    bool m_showRam = true;
    bool m_showVram = true;
};
