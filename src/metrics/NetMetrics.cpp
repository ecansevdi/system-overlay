#include "NetMetrics.h"

#include <QDateTime>
#include <QFile>

#include <cmath>

// Snapshot semantics: /proc/net/dev gives cumulative byte counters per
// interface, so a single snapshot means nothing by itself. prime() stores the
// first snapshot; sample() computes bytes/sec against it and stores the new
// snapshot for the next round — exactly like CpuMetrics handles /proc/stat.
//
// Loopback is excluded (local IPC is not internet traffic), and interfaces
// that were down since boot report all-zero counters, which would otherwise
// drag the total down every time they appear (e.g. VPN toggling) — those are
// skipped. Tap/bridge/veth devices follow the same rule via their name.

bool NetMetrics::takeSnapshot()
{
    QFile f(QStringLiteral("/proc/net/dev"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (!m_warned) {
            qWarning("NetMetrics: cannot open /proc/net/dev");
            m_warned = true;
        }
        return false;
    }

    long long total = 0;
    bool haveAny = false;

    // NOTE: never gate the loop on atEnd()/canReadLine() here — procfs files
    // report size 0, which makes both unreliable. A plain readLine() performs
    // a real read and returns an empty QByteArray at EOF.
    while (true) {
        const QByteArray raw = f.readLine();
        if (raw.isEmpty())
            break;

        // "eth0: 123456 1234 ..." — the header lines have no colon.
        const int colon = raw.indexOf(':');
        if (colon <= 0)
            continue;

        const QString iface = QString::fromLatin1(raw.left(colon)).trimmed();
        if (iface == QLatin1String("lo"))
            continue;

        // rx bytes is the first field after the colon, tx bytes the ninth.
        const QByteArray fields = raw.mid(colon + 1);
        const QList<QByteArray> parts = fields.simplified().split(' ');
        if (parts.size() < 9)
            continue;

        bool okRx = false, okTx = false;
        const long long rx = parts.at(0).toLongLong(&okRx);
        const long long tx = parts.at(8).toLongLong(&okTx);
        if (!okRx || !okTx)
            continue;

        // Interfaces that never carried traffic report 0/0 (e.g. a VPN tap
        // brought up after boot). Counting them would subtract their old
        // counters from the total; skipping them keeps the rate sane.
        if (rx == 0 && tx == 0)
            continue;

        total += rx + tx;
        haveAny = true;
    }

    if (!haveAny)
        return false;

    m_prevBytes = total;
    m_prevTimeMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

void NetMetrics::prime()
{
    m_hasPrev = takeSnapshot();
}

std::optional<NetMetrics::NetSample> NetMetrics::sample()
{
    if (!m_hasPrev) {
        // First run: take a snapshot now; the next tick produces the rate.
        prime();
        return std::nullopt;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const long long prevBytes = m_prevBytes;
    const qint64 prevTimeMs = m_prevTimeMs;

    if (!takeSnapshot())
        return std::nullopt;

    const double elapsedSec = double(nowMs - prevTimeMs) / 1000.0;
    if (elapsedSec <= 0.0)
        return std::nullopt;

    // Counter wrap/reset (module reload, interface recreate) would produce a
    // huge bogus spike; discard the interval instead of showing it.
    const double deltaBytes = double(m_prevBytes - prevBytes);
    if (deltaBytes < 0.0) {
        return std::nullopt;
    }

    NetSample s;
    s.megaBytesPerSecond = (deltaBytes / elapsedSec) / (1024.0 * 1024.0);
    return s;
}
