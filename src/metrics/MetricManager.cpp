#include "MetricManager.h"

#include <QElapsedTimer>
#include <QTextStream>

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
        Q_EMIT textChanged(sampleAndFormat());
    });

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
    Q_EMIT textChanged(sampleAndFormat());
}

void MetricManager::setInterval(int ms)
{
    m_timer->start(ms);
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

    return out;
}

QString MetricManager::sampleAndFormat()
{
    static const bool perfTrace = qEnvironmentVariableIsSet("SYSTEM_OVERLAY_PERF");
    QElapsedTimer perfTimer;
    if (perfTrace)
        perfTimer.start();

    QStringList parts;

    const bool needGpuSample = m_config.showGpuUsage() || m_config.showGpuTemp() || m_config.showVram();
    GpuSample gpu;
    if (needGpuSample)
        gpu = m_gpu.sample(); // exactly one backend sample per refresh

    if (m_config.showCpuUsage() || m_config.showCpuTemp()) {
        QString cpu = QStringLiteral("CPU:");
        if (m_config.showCpuUsage()) {
            if (const auto util = m_cpu.sample())
                cpu += QStringLiteral(" %1%").arg(*util);
            else
                cpu += QStringLiteral(" --%");
        }
        if (m_config.showCpuTemp()) {
            bool haveTemp = false;
            if (!m_cpuTemp.inputPath.isEmpty()) {
                if (const auto milliC = HwmonScanner::readMilliDegrees(m_cpuTemp.inputPath)) {
                    cpu += QStringLiteral(" %1°C").arg(std::round(milliC.value() / 1000.0), 0, 'f', 0);
                    haveTemp = true;
                }
            }
            if (!haveTemp)
                cpu += QStringLiteral(" --°C");
        }
        parts.append(cpu);
    }

    if (m_config.showGpuUsage() || m_config.showGpuTemp()) {
        QString gpuStr = QStringLiteral("GPU:");
        if (m_config.showGpuUsage()) {
            if (gpu.utilizationPct)
                gpuStr += QStringLiteral(" %1%").arg(std::round(gpu.utilizationPct.value()), 0, 'f', 0);
            else
                gpuStr += QStringLiteral(" --%");
        }
        if (m_config.showGpuTemp()) {
            if (gpu.temperatureC)
                gpuStr += QStringLiteral(" %1°C").arg(std::round(gpu.temperatureC.value()), 0, 'f', 0);
            else
                gpuStr += QStringLiteral(" --°C");
        }
        parts.append(gpuStr);
    }

    if (m_config.showRam()) {
        if (const auto used = m_memory.usedGiB())
            parts.append(QStringLiteral("RAM: %1 GiB").arg(used.value(), 0, 'f', 2));
        else
            parts.append(QStringLiteral("RAM: -- GiB"));
    }

    if (m_config.showVram()) {
        if (gpu.vramUsedGiB)
            parts.append(QStringLiteral("VRAM: %1 GiB").arg(gpu.vramUsedGiB.value(), 0, 'f', 2));
        else
            parts.append(QStringLiteral("VRAM: -- GiB"));
    }

    // One metric per line, rendered as a vertical stack.
    m_currentText = parts.join(QLatin1Char('\n'));
    if (perfTrace)
        fprintf(stderr, "sample cost: %lld us\n", perfTimer.nsecsElapsed() / 1000);
    return m_currentText;
}
