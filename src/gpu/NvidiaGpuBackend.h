#pragma once

#include "GpuBackend.h"
#include "GpuDiscovery.h"

#include <QProcess>

#include <memory>

// NVIDIA GPUs. Priority:
//   1. NVML loaded at runtime via dlopen("libnvidia-ml.so.1") — no build-time
//      dependency, the binary still builds and runs on machines without the
//      driver or its headers.
//   2. A single long-running `nvidia-smi --query-gpu=... -lms <interval>`
//      process, so we never spawn per refresh.
// If neither works, the backend is simply unavailable (no fake values).
class NvidiaGpuBackend final : public GpuBackend
{
public:
    explicit NvidiaGpuBackend(const DrmCard &card, int intervalMs);
    ~NvidiaGpuBackend() override;

    QString name() const override;
    GpuSample sample() override;
    QString debugInfo() const override;

private:
    bool tryInitNvml();
    bool startNvidiaSmi(int intervalMs);
    GpuSample sampleNvml();
    std::optional<GpuSample> pollNvidiaSmi();

    DrmCard m_card;
    QString m_nvmlBusId;        // NVML format: 8-digit domain
    QString m_smiBusId;         // --id filter argument

    // NVML runtime handles
    void *m_nvmlLib = nullptr;
    bool m_nvmlOk = false;
    void *m_nvmlDevice = nullptr;

    struct Funcs;
    std::unique_ptr<Funcs> m_fn;

    QProcess *m_smiProcess = nullptr;
    QString m_smiName = QStringLiteral("nvidia-smi (streaming)");
};
