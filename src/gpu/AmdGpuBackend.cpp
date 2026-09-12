#include "AmdGpuBackend.h"
#include "GpuDiscovery.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

namespace {

std::optional<QString> readFileTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    return QString::fromLatin1(f.readAll().trimmed());
}

// Prefer die/edge temperature, then junction, then whatever comes first.
int amdTempRank(const QString &label)
{
    const QString l = label.toLower();
    if (l == QLatin1String("edge"))
        return 0;
    if (l == QLatin1String("junction"))
        return 1;
    if (l.startsWith(QLatin1String("package")))
        return 2;
    return 3;
}

} // namespace

AmdGpuBackend::AmdGpuBackend(const DrmCard &card)
    : m_card(card)
{
    const QString dev = card.devicePath;
    m_busyPath = dev + QStringLiteral("/gpu_busy_percent");
    m_vramUsedPath = dev + QStringLiteral("/mem_info_vram_used");
    m_vramTotalPath = dev + QStringLiteral("/mem_info_vram_total");

    // Find the hwmon chip owned by this PCI device; never hard-code a number.
    const QString hwmonRoot = dev + QStringLiteral("/hwmon");
    const auto hwmons = QDir(hwmonRoot)
                            .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QString bestPath;
    int bestRank = 99;
    for (const QString &hw : hwmons) {
        const QDir chipDir(hwmonRoot + QLatin1Char('/') + hw);
        const auto inputs = chipDir.entryList(QDir::Files, QDir::Name)
                                .filter(QRegularExpression(QStringLiteral("^temp\\d+_input$")));
        for (const QString &input : inputs) {
            const QString base = input.chopped(6);
            QString label;
            {
                QFile lf(chipDir.filePath(base + QStringLiteral("_label")));
                if (lf.open(QIODevice::ReadOnly | QIODevice::Text))
                    label = QString::fromLatin1(lf.readAll().trimmed());
            }
            const int rank = amdTempRank(label);
            if (rank < bestRank) {
                bestRank = rank;
                bestPath = chipDir.filePath(input);
                m_tempLabel = label;
            }
        }
    }
    m_tempPath = bestPath;
}

QString AmdGpuBackend::name() const
{
    return QStringLiteral("AMD (amdgpu sysfs)");
}

std::optional<double> AmdGpuBackend::readTemperature()
{
    if (m_tempPath.isEmpty())
        return std::nullopt;
    QFile f(m_tempPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    bool ok = false;
    const long milliC = QString::fromLatin1(f.readAll().trimmed()).toLong(&ok);
    if (!ok || milliC < -60000 || milliC > 250000)
        return std::nullopt;
    return milliC / 1000.0;
}

GpuSample AmdGpuBackend::sample()
{
    GpuSample s;

    if (const auto v = readFileTrimmed(m_busyPath)) {
        bool ok = false;
        const int pct = v->toInt(&ok);
        if (ok && pct >= 0 && pct <= 100)
            s.utilizationPct = pct;
    }

    if (const auto used = readFileTrimmed(m_vramUsedPath)) {
        bool ok = false;
        const qulonglong bytes = used->toULongLong(&ok);
        if (ok)
            s.vramUsedGiB = bytes / (1024.0 * 1024.0 * 1024.0);
    }
    if (const auto total = readFileTrimmed(m_vramTotalPath)) {
        bool ok = false;
        const qulonglong bytes = total->toULongLong(&ok);
        if (ok)
            s.vramTotalGiB = bytes / (1024.0 * 1024.0 * 1024.0);
    }

    s.temperatureC = readTemperature();
    return s;
}

QString AmdGpuBackend::debugInfo() const
{
    QString out;
    QTextStream ts(&out);
    ts << "  GPU: " << m_card.vendorName() << " device " << m_card.deviceId << " ("
       << m_card.pciAddress << ", card" << m_card.index << ")\n";
    ts << "  utilization source: " << m_busyPath << "\n";
    ts << "  VRAM source: " << m_vramUsedPath << "\n";
    ts << "  temperature source: "
       << (m_tempPath.isEmpty() ? QStringLiteral("(not found)")
                                : m_tempPath + (m_tempLabel.isEmpty() ? QString() : QStringLiteral(" [") + m_tempLabel + QLatin1Char(']')))
       << "\n";
    return out;
}
