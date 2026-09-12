#pragma once

#include <optional>

// "Used" memory from /proc/meminfo, computed as MemTotal - MemAvailable
// (the kernel's estimate of memory a user-space process cannot reclaim),
// NOT the misleading MemTotal - MemFree.
class MemoryMetrics
{
public:
    std::optional<double> usedGiB(); // nullopt on read failure

private:
    bool m_warned = false;
};
