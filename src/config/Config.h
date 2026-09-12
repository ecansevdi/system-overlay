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

    void load(const QString &explicitPath = {});

    // Command line overrides (applied on top of the file values).
    void setRefreshInterval(int ms);
    void setScreen(const QString &spec);
    void setDebug(bool on);

    int refreshInterval() const { return m_refreshInterval; }
    ScreenMode screenMode() const { return m_screenMode; }
    const QString &screenName() const { return m_screenName; }
    bool debug() const { return m_debug; }

    int offsetX() const { return m_offsetX; }
    int offsetY() const { return m_offsetY; }
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

    int m_offsetX = 10;
    int m_offsetY = 10;
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
