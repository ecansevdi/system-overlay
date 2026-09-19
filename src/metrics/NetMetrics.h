#pragma once

#include <QtGlobal>

#include <optional>

#include <QString>

// Total network throughput (all non-loopback interfaces) from /proc/net/dev,
// computed as the delta between two samples, like CpuMetrics. Download and
// upload are summed into one rate so the HUD needs a single NET row.
//
// The link capacity (Mbit/s) is provided by the caller so the bar fraction is
// scaled against the user's actual internet speed, not against the highest
// observed rate. The user enters their speed in the config, e.g. 1000 Mbit/s
// line -> shows "NET: 73.4 MB/s" with a bar spanning 0..125 MB/s.
class NetMetrics
{
public:
    struct NetSample
    {
        double megaBytesPerSecond = 0.0; // down+up combined
    };

    void prime();                          // first snapshot so the next sample has a delta
    std::optional<NetSample> sample();     // nullopt until 2 samples exist

    // Full link speed as MB/s (1000 Mbit/s -> 125.0 MB/s), for bar scaling.
    static double maxMBpsFromMbit(double mbit) { return mbit * 1000000.0 / 8.0 / (1024.0 * 1024.0); }

private:
    bool takeSnapshot();                   // sum rx+tx byte counters into m_prevBytes

    long long m_prevBytes = 0;
    qint64 m_prevTimeMs = 0;
    bool m_hasPrev = false;
    bool m_warned = false;
};
