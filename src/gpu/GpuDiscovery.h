#pragma once

#include <QList>
#include <QString>

#include <cstdint>

// Discovers real DRM cards under /sys/class/drm. card0 is NOT assumed to be
// the gaming GPU: cards are ranked by whether they drive a display and by the
// boot_vga flag.
struct DrmCard
{
    int index = -1;              // N in cardN
    QString sysfsPath;           // /sys/class/drm/card1
    QString devicePath;          // /sys/class/drm/card1/device
    QString pciAddress;          // "0000:07:00.0"
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    bool bootVga = false;
    bool drivesDisplay = false;  // at least one connected connector
    QStringList connectedConnectors;
    QString hwmonName;           // drm driver name from hwmon (i915/amdgpu/...), may be empty

    QString vendorName() const;
};

class GpuDiscovery
{
public:
    static QList<DrmCard> enumerate();

    // The card we render stats for: display-carrying first, then boot_vga,
    // then lowest index. nullopt when no usable card exists.
    static std::optional<DrmCard> pickPrimary();
};
