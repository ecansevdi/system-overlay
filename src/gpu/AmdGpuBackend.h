#pragma once

#include "GpuBackend.h"
#include "GpuDiscovery.h"

// AMD GPUs via sysfs (amdgpu):
//   utilization : <card>/device/gpu_busy_percent
//   VRAM        : <card>/device/mem_info_vram_used / mem_info_vram_total
//   temperature : <card>/device/hwmon/hwmonN (label edge > junction > first)
class AmdGpuBackend final : public GpuBackend
{
public:
    explicit AmdGpuBackend(const DrmCard &card);

    QString name() const override;
    GpuSample sample() override;
    QString debugInfo() const override;

private:
    std::optional<double> readTemperature();

    DrmCard m_card;
    QString m_busyPath;
    QString m_vramUsedPath;
    QString m_vramTotalPath;
    QString m_tempPath;      // chosen at construction; empty if none
    QString m_tempLabel;
};
