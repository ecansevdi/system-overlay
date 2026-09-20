#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

#include "config/Config.h"
#include "gpu/GpuMetrics.h"
#include "metrics/CpuMetrics.h"
#include "metrics/FpsTracker.h"
#include "metrics/HudRow.h"
#include "metrics/MemoryMetrics.h"
#include "metrics/NetMetrics.h"
#include "sensors/HwmonScanner.h"

#include <vector>

// Drives metric collection on a QTimer and renders the HUD rows.
// All reads happen in the main thread: every source used here is a tiny
// sysfs/procfs read (microseconds), which keeps the whole app idle cost
// negligible. No worker threads, no process spawning.
//
// Each refresh produces a list of HudRows (text + bar fraction + colour).
// The colour comes from one shared function: at >= warningPct the row turns
// warningColour, at >= criticalPct criticalColour, otherwise the normal text
// colour (thresholds from [colors] in the config). The tray menu can toggle
// rows at runtime via setRowVisible().
class MetricManager : public QObject
{
    Q_OBJECT
public:
    explicit MetricManager(const Config &config, QObject *parent = nullptr);

    // Take initial snapshots so the first timer tick has real deltas.
    void prime();

    void start();
    void setInterval(int ms);

    // Pause/resume metric updates (values stay as of the last refresh).
    void setPaused(bool paused)
    {
        if (paused)
            m_timer->stop();
        else
            m_timer->start(m_config.refreshInterval());
    }

    // Runtime visibility of whole rows (tray menu checkboxes).
    void setRowVisible(int row, bool visible);
    bool rowVisible(int row) const { return m_rowVisible[row]; }

    // Base (normal-band) text colour, chosen live from the tray menu.
    // Warning/critical colours keep coming from the config.
    void setBaseColor(const QColor &color);

    // Build the display rows from a fresh sample.
    std::vector<HudRow> sampleAndFormat();

    // Last rendered rows (so windows can initialize before the first tick).
    const std::vector<HudRow> &currentRows() const { return m_currentRows; }

    // Sensor/source discovery dump for --debug.
    QString debugInfo() const;

Q_SIGNALS:
    void rowsChanged(const std::vector<HudRow> &rows);

private:
    QColor colorForPercent(double pct) const;
    HudRow formatNetRow(const QString &label, double mBps,
                        const std::optional<NetMetrics::NetSample> &net) const;

    const Config &m_config;
    QTimer *m_timer = nullptr;

    CpuMetrics m_cpu;
    MemoryMetrics m_memory;
    NetMetrics m_net;
    GpuMetrics m_gpu;
    FpsTracker m_fps;
    HwmonScanner::CpuTempSource m_cpuTemp;
    std::vector<HudRow> m_currentRows;
    bool m_rowVisible[RowCount] = {true, true, true, true, true, true, true};
    QColor m_baseColor;
};
