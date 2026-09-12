#include "CpuMetrics.h"

#include <QFile>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

std::optional<std::array<uint64_t, 8>> readProcStat()
{
    QFile f(QStringLiteral("/proc/stat"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    // First line: "cpu  user nice system idle iowait irq softirq steal ..."
    const QString line = QString::fromLatin1(f.readLine());
    const QStringList fields = line.simplified().split(QLatin1Char(' '));
    if (fields.isEmpty() || fields.first() != QLatin1String("cpu") || fields.size() < 9)
        return std::nullopt;

    std::array<uint64_t, 8> counts{};
    for (int i = 0; i < 8; ++i) {
        bool ok = false;
        const uint64_t v = fields.at(i + 1).toULongLong(&ok);
        if (!ok)
            return std::nullopt;
        counts[i] = v;
    }
    return counts;
}

} // namespace

void CpuMetrics::prime()
{
    if (const auto counts = readProcStat()) {
        m_prev = *counts;
        m_hasPrev = true;
    }
}

std::optional<int> CpuMetrics::sample()
{
    const auto counts = readProcStat();
    if (!counts)
        return std::nullopt;

    const auto &cur = *counts;
    if (!m_hasPrev) {
        m_prev = cur;
        m_hasPrev = true;
        return std::nullopt; // no delta yet
    }

    uint64_t total = 0;
    for (int i = 0; i < 8; ++i) {
        if (cur[i] < m_prev[i])
            return std::nullopt; // counters reset (e.g. system boot); skip this round
        total += cur[i] - m_prev[i];
    }
    const uint64_t idle = (cur[3] - m_prev[3]) + (cur[4] - m_prev[4]); // idle + iowait
    m_prev = cur;

    if (total == 0)
        return std::nullopt;

    const uint64_t busy = total - idle;
    return static_cast<int>((busy * 100 + total / 2) / total); // rounded
}
