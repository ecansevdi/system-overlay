#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

#include "config/Config.h"
#include "gpu/GpuMetrics.h"
#include "metrics/CpuMetrics.h"
#include "metrics/MemoryMetrics.h"
#include "sensors/HwmonScanner.h"

#include <optional>

// Drives metric collection on a QTimer and renders the single overlay line.
// All reads happen in the main thread: every source used here is a tiny
// sysfs/procfs read (microseconds), which keeps the whole app idle cost
// negligible. No worker threads, no process spawning.
class MetricManager : public QObject
{
    Q_OBJECT
public:
    explicit MetricManager(const Config &config, QObject *parent = nullptr);

    // Take initial snapshots so the first timer tick has real deltas.
    void prime();

    void start();
    void setInterval(int ms);

    // Build the display string from a fresh sample (also used by --once).
    QString sampleAndFormat();

    // Last rendered string (so windows can initialize before the first tick).
    QString currentText() const { return m_currentText; }

    // Sensor/source discovery dump for --debug.
    QString debugInfo() const;

Q_SIGNALS:
    void textChanged(const QString &text);

private:
    const Config &m_config;
    QTimer *m_timer = nullptr;

    CpuMetrics m_cpu;
    MemoryMetrics m_memory;
    GpuMetrics m_gpu;
    HwmonScanner::CpuTempSource m_cpuTemp;
    QString m_currentText;
};
