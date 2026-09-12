#include "MetricManager.h"

#include <QElapsedTimer>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>

MetricManager::MetricManager(const Config &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_gpu(config.refreshInterval())
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        Q_EMIT rowsChanged(sampleAndFormat());
    });

    m_rowVisible[RowCpu] = config.showCpuUsage() || config.showCpuTemp();
    m_rowVisible[RowGpu] = config.showGpuUsage() || config.showGpuTemp();
    m_rowVisible[RowRam] = config.showRam();
    m_rowVisible[RowVram] = config.showVram();
    m_baseColor = QColor(config.textColor());

    if (const auto src = HwmonScanner::findCpuTemp())
        m_cpuTemp = *src;
    else
        qWarning("MetricManager: no plausible CPU temperature sensor found (will show \"--\")");

    if (!m_gpu.discover())
        qWarning("MetricManager: no supported GPU backend (will show \"--\" for GPU metrics)");
}

void MetricManager::prime()
{
    m_cpu.prime();
    m_gpu.prime();
}

void MetricManager::start()
{
    m_timer->start(m_config.refreshInterval());
    Q_EMIT rowsChanged(sampleAndFormat());
}

void MetricManager::setInterval(int ms)
{
    m_timer->start(ms);
}

void MetricManager::setRowVisible(int row, bool visible)
{
    if (row < 0 || row >= RowCount || m_rowVisible[row] == visible)
        return;
    m_rowVisible[row] = visible;
    // Refresh immediately so the HUD reacts without waiting for the next tick.
    Q_EMIT rowsChanged(sampleAndFormat());
}

void MetricManager::setBaseColor(const QColor &color)
{
    if (!color.isValid() || color == m_baseColor)
        return;
    m_baseColor = color;
    Q_EMIT rowsChanged(sampleAndFormat());
}

// The single colour function shared by every percentage-based value:
// the base colour below the warning threshold, yellow in the warning band
// and red at/above the critical threshold.
QColor MetricManager::colorForPercent(double pct) const
{
    if (pct >= m_config.criticalPct())
        return QColor(m_config.criticalColor());
    if (pct >= m_config.warningPct())
        return QColor(m_config.warningColor());
    return m_baseColor;
}

QList<HudRow> MetricManager::sampleAndFormat()
{
    static const bool perfTrace = qEnvironmentVariableIsSet("SYSTEM_OVERLAY_PERF");
    QElapsedTimer perfTimer;
    if (perfTrace)
        perfTimer.start();

    QList<HudRow> rows;

    const bool needGpuSample = m_config.showGpuUsage() || m_config.showGpuTemp() || m_config.showVram()
        || m_rowVisible[RowGpu] || m_rowVisible[RowVram];
    GpuSample gpu;
    if (needGpuSample)
        gpu = m_gpu.sample(); // exactly one backend sample per refresh

    if (m_rowVisible[RowCpu] && (m_config.showCpuUsage() || m_config.showCpuTemp())) {
        HudRow row;
        row.text = QStringLiteral("CPU:");
        bool haveTemp = false;
        if (m_config.showCpuUsage()) {
            if (const auto util = m_cpu.sample()) {
                row.text += QStringLiteral(" %1%").arg(*util);
                row.fraction = std::clamp(*util, 0, 100) / 100.0;
                row.color = colorForPercent(*util);
            } else {
                row.text += QStringLiteral(" --%");
            }
        }
        if (m_config.showCpuTemp() && !m_cpuTemp.inputPath.isEmpty()) {
            if (const auto milliC = HwmonScanner::readMilliDegrees(m_cpuTemp.inputPath)) {
                row.text += QStringLiteral(" %1°C").arg(std::round(milliC.value() / 1000.0), 0, 'f', 0);
                haveTemp = true;
            }
        }
        if (!haveTemp)
            row.text += QStringLiteral(" --°C");
        if (!row.color.isValid())
            row.color = colorForPercent(0);
        rows.append(row);
    }

    if (m_rowVisible[RowGpu] && (m_config.showGpuUsage() || m_config.showGpuTemp())) {
        HudRow row;
        row.text = QStringLiteral("GPU:");
        if (m_config.showGpuUsage()) {
            if (gpu.utilizationPct) {
                row.text += QStringLiteral(" %1%").arg(std::round(gpu.utilizationPct.value()), 0, 'f', 0);
                row.fraction = std::clamp(gpu.utilizationPct.value(), 0.0, 100.0) / 100.0;
                row.color = colorForPercent(gpu.utilizationPct.value());
            } else {
                row.text += QStringLiteral(" --%");
            }
        }
        if (m_config.showGpuTemp()) {
            if (gpu.temperatureC)
                row.text += QStringLiteral(" %1°C").arg(std::round(gpu.temperatureC.value()), 0, 'f', 0);
            else
                row.text += QStringLiteral(" --°C");
        }
        if (!row.color.isValid())
            row.color = colorForPercent(0);
        rows.append(row);
    }

    if (m_rowVisible[RowRam]) {
        HudRow row;
        if (const auto mem = m_memory.sample()) {
            row.text = QStringLiteral("RAM: %1/%2 GiB")
                           .arg(mem->usedGiB, 0, 'f', 1)
                           .arg(mem->totalGiB, 0, 'f', 1);
            if (mem->totalGiB > 0) {
                const double pct = 100.0 * mem->usedGiB / mem->totalGiB;
                row.fraction = std::clamp(mem->usedGiB / mem->totalGiB, 0.0, 1.0);
                row.color = colorForPercent(pct);
            }
        } else {
            row.text = QStringLiteral("RAM: -- GiB");
        }
        if (!row.color.isValid())
            row.color = colorForPercent(0);
        rows.append(row);
    }

    if (m_rowVisible[RowVram]) {
        HudRow row;
        if (gpu.vramUsedGiB) {
            if (gpu.vramTotalGiB && gpu.vramTotalGiB.value() > 0) {
                row.text = QStringLiteral("VRAM: %1/%2 GiB")
                               .arg(gpu.vramUsedGiB.value(), 0, 'f', 1)
                               .arg(gpu.vramTotalGiB.value(), 0, 'f', 1);
                const double pct = 100.0 * gpu.vramUsedGiB.value() / gpu.vramTotalGiB.value();
                row.fraction = std::clamp(gpu.vramUsedGiB.value() / gpu.vramTotalGiB.value(), 0.0, 1.0);
                row.color = colorForPercent(pct);
            } else {
                row.text = QStringLiteral("VRAM: %1 GiB").arg(gpu.vramUsedGiB.value(), 0, 'f', 1);
            }
        } else {
            row.text = QStringLiteral("VRAM: -- GiB");
        }
        if (!row.color.isValid())
            row.color = colorForPercent(0);
        rows.append(row);
    }

    m_currentRows = rows;
    if (perfTrace)
        fprintf(stderr, "sample cost: %lld us\n", perfTimer.nsecsElapsed() / 1000);
    return m_currentRows;
}

QString MetricManager::debugInfo() const
{
    QString out;
    QTextStream ts(&out);

    ts << "Detected CPU temperature source:\n";
    if (m_cpuTemp.inputPath.isEmpty()) {
        ts << "  none\n";
    } else {
        ts << "  " << m_cpuTemp.chipName << " / "
           << (m_cpuTemp.label.isEmpty() ? QStringLiteral("(unlabeled)") : m_cpuTemp.label)
           << " -> " << m_cpuTemp.inputPath << "\n";
    }

    ts << "Detected GPU:\n";
    ts << "  " << m_gpu.summary() << "\n";
    ts << m_gpu.debugInfo();
    ts << "Colour thresholds: warning >= " << m_config.warningPct() << "% ("
       << m_config.warningColor() << "), critical >= " << m_config.criticalPct() << "% ("
       << m_config.criticalColor() << ")\n";

    return out;
}
