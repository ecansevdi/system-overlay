#pragma once

#include <QtGlobal>

#include <optional>

// Per-direction network throughput (all non-loopback interfaces) from
// /proc/net/dev, computed as the delta between two samples. Upload (tx) and
// download (rx) are kept separate so the HUD can render two rows from one
// snapshot — sampling twice would split the delta window and halve the rates.
//
// Loopback is excluded; VPN/tun/tap devices that carry real traffic are
// included. The link capacity (Mbit/s) is provided by the caller so each
// bar is scaled against the user's line speed (1000 Mbit/s -> 125 MB/s).
class NetMetrics
{
public:
    struct NetSample
    {
        double upMBps = 0.0;   // tx, megabytes per second
        double downMBps = 0.0; // rx, megabytes per second
    };

    void prime();                      // first snapshot so the next sample has a delta
    std::optional<NetSample> sample(); // nullopt until 2 samples exist

    // Full link speed as MB/s (1000 Mbit/s -> 125.0 MB/s), for bar scaling.
    static double maxMBpsFromMbit(double mbit)
    {
        return mbit * 1000000.0 / 8.0 / (1024.0 * 1024.0);
    }

private:
    bool takeSnapshot(); // sum rx/tx byte counters into m_prevRx / m_prevTx

    long long m_prevRx = 0;
    long long m_prevTx = 0;
    qint64 m_prevTimeMs = 0;
    bool m_hasPrev = false;
    bool m_warned = false;
};
