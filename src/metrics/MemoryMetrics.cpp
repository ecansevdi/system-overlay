#include "MemoryMetrics.h"

#include <QFile>
#include <QString>

#include <cstddef>

std::optional<MemoryMetrics::MemSample> MemoryMetrics::sample()
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (!m_warned) {
            qWarning("MemoryMetrics: cannot open /proc/meminfo");
            m_warned = true;
        }
        return std::nullopt;
    }

    long totalKb = -1;
    long availableKb = -1;
    // NOTE: never gate the loop on atEnd()/canReadLine() here — procfs files
    // report size 0, which makes both unreliable. A plain readLine() performs
    // a real read and returns an empty QByteArray at EOF.
    while (true) {
        const QByteArray raw = f.readLine();
        if (raw.isEmpty())
            break;
        const QString line = QString::fromLatin1(raw);
        if (line.startsWith(QLatin1String("MemTotal:"))) {
            totalKb = line.mid(9).simplified().split(QLatin1Char(' ')).first().toLong();
        } else if (line.startsWith(QLatin1String("MemAvailable:"))) {
            availableKb = line.mid(13).simplified().split(QLatin1Char(' ')).first().toLong();
        }
        if (totalKb >= 0 && availableKb >= 0)
            break;
    }

    if (totalKb <= 0 || availableKb < 0 || availableKb > totalKb) {
        if (!m_warned) {
            qWarning("MemoryMetrics: unexpected /proc/meminfo layout");
            m_warned = true;
        }
        return std::nullopt;
    }

    MemSample s;
    s.totalGiB = totalKb / (1024.0 * 1024.0);
    s.usedGiB = (totalKb - availableKb) / (1024.0 * 1024.0); // kB -> GiB
    return s;
}
