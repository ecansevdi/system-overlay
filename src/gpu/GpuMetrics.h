#pragma once

#include "GpuBackend.h"
#include "GpuDiscovery.h"

#include <memory>

// Owns backend discovery/selection for the primary GPU and produces samples.
// Falls back to an empty (all unset) sample when no supported GPU is found.
class GpuMetrics
{
public:
    explicit GpuMetrics(int refreshIntervalMs);

    // Discover cards, pick the primary one and build its backend.
    // Returns false when no usable GPU was found (never fatal).
    bool discover();

    void prime();

    GpuSample sample();

    // One-line summary for --debug ("no data yet" before the first sample).
    QString summary() const;

    // Full source description for --debug.
    QString debugInfo() const;

private:
    int m_intervalMs;
    std::unique_ptr<GpuBackend> m_backend;
    DrmCard m_card;
    bool m_hasCard = false;
};
