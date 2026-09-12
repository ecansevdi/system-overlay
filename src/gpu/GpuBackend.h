#pragma once

#include <QString>
#include <memory>
#include <optional>

struct GpuSample
{
    std::optional<double> utilizationPct; // 0..100
    std::optional<double> temperatureC;
    std::optional<double> vramUsedGiB;
    std::optional<double> vramTotalGiB;   // only where the kernel exposes it
};

// One GPU backend (AMD / NVIDIA / Intel). Implementations must never spawn
// processes per refresh and must degrade gracefully: a metric that cannot be
// read simply stays unset (the UI renders "--").
class GpuBackend
{
public:
    virtual ~GpuBackend() = default;

    virtual QString name() const = 0;

    // Take an initial snapshot so the first real sample has usable deltas.
    virtual void prime() {}

    virtual GpuSample sample() = 0;

    // Multi-line description of the exact data sources, for --debug.
    virtual QString debugInfo() const = 0;
};

// Selects and instantiates a backend for the given DRM card.
// vendorId: PCI vendor of the card device (0x1002 AMD, 0x8086 Intel, 0x10de NVIDIA).
std::unique_ptr<GpuBackend> createGpuBackendForVendor(uint32_t vendorId, const class DrmCard &card,
                                                      int refreshIntervalMs);
