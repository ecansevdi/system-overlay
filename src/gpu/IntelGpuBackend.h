#pragma once

#include "GpuBackend.h"
#include "GpuDiscovery.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <chrono>
#include <optional>

// Intel GPUs (i915 / xe), entirely from kernel interfaces readable without root:
//
//   utilization : sum of per-client cumulative engine busy time from
//                 /proc/<pid>/fdinfo/<fd> (drm-engine-render/compute/copy,
//                 in nanoseconds), divided by wall-clock delta. Clients are
//                 deduplicated by drm-client-id. Processes of other users are
//                 unreadable and are skipped (documented limitation).
//   VRAM        : sum of drm-total-local0 (local memory = lmem/VRAM) across
//                 clients. Shared buffers may be counted once per client, so
//                 the number is an approximation; there is no userspace-wide
//                 sysfs counter on i915 (AMD's mem_info_vram_* is AMD-only).
//   temperature : <card>/device/hwmon/hwmonN/tempX_input (i915 package temp).
//
// The /proc scan happens once per refresh in the main thread and only reads
// small fdinfo files (a few KB); measured cost is ~1 ms per refresh.
class IntelGpuBackend final : public GpuBackend
{
public:
    explicit IntelGpuBackend(const DrmCard &card);

    QString name() const override;
    void prime() override;
    GpuSample sample() override;
    QString debugInfo() const override;

private:
    struct ClientStat
    {
        std::chrono::nanoseconds engineNs{0};
        quint64 localTotal = 0;  // drm-total-local0 (bytes)
        quint64 localShared = 0; // drm-shared-local0 (bytes, subset of total)
    };

    struct Snapshot
    {
        QHash<quint64, ClientStat> clients; // key: drm-client-id
        quint64 vramBytes = 0;
    };

    struct ScanResult
    {
        Snapshot snap;
        QList<QPair<int, QString>> aliveFds; // entries whose fdinfo was readable
    };

    ScanResult snapshotFromFdList(const QList<QPair<int, QString>> &fdList) const;
    QList<QPair<int, QString>> fullFdScan() const;
    Snapshot scanFdInfo();
    std::optional<double> readTemperature() const;
    void findHwmonTempPath();
    void findVramTotal();

    DrmCard m_card;
    QString m_pciAddressNormalized; // drm-pdev format
    QString m_tempPath;
    double m_vramTotalGiB = -1.0;   // largest PCI memory BAR (ReBAR), -1 = unknown

    // Full /proc/*/fd scans (thousands of readlink calls, ~20-30 ms) are
    // expensive; they run at most every kFullScanSeconds. Between full scans
    // we only re-read the fdinfo files of the cached (pid, fd) list, which
    // costs well under 1 ms and keeps the overlay's idle CPU usage negligible.
    QList<QPair<int, QString>> m_cachedFds;
    std::chrono::steady_clock::time_point m_lastFullScan;

    QHash<quint64, ClientStat> m_prevClients;
    bool m_hasPrev = false;
    std::optional<std::chrono::steady_clock::time_point> m_prevWall;
};
