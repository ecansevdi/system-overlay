#include "MetricManager.h"

#include <QElapsedTimer>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

MetricManager::MetricManager(const Config &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_gpu(config.refreshInterval())
    , m_fps(this)
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
    m_rowVisible[RowNetUp] = config.showNet();
    m_rowVisible[RowNetDown] = config.showNet();
    m_rowVisible[RowFps] = config.showFps();
    m_baseColor = QColor(config.textColor());

    if (const auto src = HwmonScanner::findCpuTemp())
        m_cpuTemp = *src;
    else
        qWarning("MetricManager: no plausible CPU temperature sensor found (will show \"--\")");

    if (!m_gpu.discover())
        qWarning("MetricManager: no supported GPU backend (will show \"--\" for GPU metrics)");

    if (m_rowVisible[RowNetUp] || m_rowVisible[RowNetDown])
        m_net.prime();
    if (m_rowVisible[RowFps])
        m_fps.start();
}

void MetricManager::prime()
{
    m_cpu.prime();
    m_gpu.prime();
    m_net.prime();
}

void MetricManager::start()
{
    m_timer->start(m_config.refreshInterval());
    Q_EMIT rowsChanged(sampleAndFormat());

    if (m_config.debug()) {
        fprintf(stderr, "HUD rows:\n");
        if (m_currentRows.empty()) {
            fprintf(stderr, "  (none)\n");
        } else {
            for (const HudRow &row : m_currentRows)
                fprintf(stderr, "  %s\n", qUtf8Printable(row.text));
        }
        fflush(stderr);
    }
}

void MetricManager::setInterval(int ms)
{
    m_timer->start(ms);
}

void MetricManager::setRowVisible(int row, bool visible)
{
    if (row < 0 || row >= RowCount || m_rowVisible[row] == visible)
        return;
    const bool netWasLive = m_rowVisible[RowNetUp] || m_rowVisible[RowNetDown];
    m_rowVisible[row] = visible;
    if ((row == RowNetUp || row == RowNetDown) && visible && !netWasLive)
        m_net.prime(); // fresh delta base so the row does not show a stale spike
    if (row == RowFps) {
        if (visible)
            m_fps.start();
        else
            m_fps.stop();
    }
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

HudRow MetricManager::formatNetRow(const QString &label, double mBps,
                                   const std::optional<NetMetrics::NetSample> &net) const
{
    HudRow row;
    row.text = label;
    if (net) {
        const double maxMbps = NetMetrics::maxMBpsFromMbit(m_config.netLinkMbit());
        row.text += QStringLiteral(" %1 MB/s").arg(mBps, 0, 'f', 1);
        if (maxMbps > 0.0) {
            row.fraction = std::clamp(mBps / maxMbps, 0.0, 1.0);
            row.color = colorForPercent(100.0 * mBps / maxMbps);
        } else {
            row.fraction = -1.0; // 0 Mbit config: text only, no bar
        }
    } else {
        row.text += QStringLiteral(" -- MB/s");
    }
    if (!row.color.isValid())
        row.color = colorForPercent(0);
    return row;
}

std::vector<HudRow> MetricManager::sampleAndFormat()
{
    static const bool perfTrace = qEnvironmentVariableIsSet("SYSTEM_OVERLAY_PERF");
    QElapsedTimer perfTimer;
    if (perfTrace)
        perfTimer.start();

    std::vector<HudRow> rows;

    const bool needGpuSample = m_rowVisible[RowGpu] || m_rowVisible[RowVram];
    GpuSample gpu;
    if (needGpuSample)
        gpu = m_gpu.sample(); // exactly one backend sample per refresh

    // One /proc/net/dev snapshot feeds both up and down. Sampling twice
    // would split the delta window and report half the real rate.
    std::optional<NetMetrics::NetSample> net;
    if (m_rowVisible[RowNetUp] || m_rowVisible[RowNetDown])
        net = m_net.sample();

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
        if (m_config.showCpuTemp() && !haveTemp)
            row.text += QStringLiteral(" --°C");
        if (!row.color.isValid())
            row.color = colorForPercent(0);
        rows.push_back(std::move(row));
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
        rows.push_back(std::move(row));
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
        rows.push_back(std::move(row));
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
        rows.push_back(std::move(row));
    }

    if (m_rowVisible[RowNetUp])
        rows.push_back(formatNetRow(QStringLiteral("up:"), net ? net->upMBps : 0.0, net));
    if (m_rowVisible[RowNetDown])
        rows.push_back(formatNetRow(QStringLiteral("down:"), net ? net->downMBps : 0.0, net));

    if (m_rowVisible[RowFps]) {
        HudRow row;
        row.fraction = -1.0; // frame cap is variable; a full-scale bar is meaningless
        if (const auto fps = m_fps.sample(); fps && *fps > 0) {
            row.text = QStringLiteral("FPS: %1").arg(*fps);
            row.color = (*fps < 55) ? QColor(m_config.warningColor()) : m_baseColor;
        } else {
            row.text = QStringLiteral("FPS: --");
            row.color = m_baseColor;
        }
        rows.push_back(std::move(row));
    }

    // Dynamic label alignment: pad every "LABEL:" prefix to the width of the
    // widest visible label (monospace font -> equal advance). Done here, once,
    // so any future metric with the "NAME: value" shape aligns automatically.
    alignHudRows(rows);

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
    ts << m_fps.debugInfo();
    ts << "Colour thresholds: warning >= " << m_config.warningPct() << "% ("
       << m_config.warningColor() << "), critical >= " << m_config.criticalPct() << "% ("
       << m_config.criticalColor() << ")\n";

    static const char *kNames[RowCount] = {"CPU", "GPU", "RAM", "VRAM", "up", "down", "FPS"};
    ts << "Row visibility:";
    for (int i = 0; i < RowCount; ++i)
        ts << ' ' << kNames[i] << '=' << (m_rowVisible[i] ? "on" : "off");
    ts << '\n';

    return out;
}
