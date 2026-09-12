#pragma once

#include <optional>

// "Used" memory from /proc/meminfo, computed as MemTotal - MemAvailable
// (the kernel's estimate of memory a user-space process cannot reclaim),
// NOT the misleading MemTotal - MemFree. The total is reported as well so
// the HUD can render a used/total ratio.
class MemoryMetrics
{
public:
    struct MemSample
    {
        double usedGiB = 0.0;
        double totalGiB = 0.0;
    };

    std::optional<MemSample> sample(); // nullopt on read failure

private:
    bool m_warned = false;
};
