#pragma once

#include <array>
#include <cstdint>
#include <optional>

// Total CPU utilization from /proc/stat, computed as the delta between two
// samples. Uses user, nice, system, idle, iowait, irq, softirq and steal.
class CpuMetrics
{
public:
    void prime();                 // take the first snapshot so the next sample has a delta
    std::optional<int> sample();  // utilisation percent 0..100, nullopt until 2 samples exist

private:
    std::array<uint64_t, 8> m_prev{};
    bool m_hasPrev = false;
};
