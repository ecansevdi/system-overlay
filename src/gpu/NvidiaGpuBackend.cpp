#include "NvidiaGpuBackend.h"
#include "GpuDiscovery.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <dlfcn.h>

// Minimal NVML definitions (mirrors the public driver API, no headers needed).
namespace {

using NvmlUtilization = struct { unsigned int gpu; unsigned int memory; };
using NvmlMemory = struct {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};
constexpr int NVML_TEMPERATURE_GPU = 0;

} // namespace

struct NvidiaGpuBackend::Funcs
{
    // nvmlReturn_t is an int enum; all functions return 0 (NVML_SUCCESS) on success.
    int (*init_v2)();
    int (*shutdown)();
    int (*deviceGetHandleByPciBusId_v2)(const char *, void **);
    int (*deviceGetUtilizationRates)(void *, NvmlUtilization *);
    int (*deviceGetTemperature)(void *, int, unsigned int *);
    int (*deviceGetMemoryInfo)(void *, NvmlMemory *);

    template <typename Sym>
    static Sym resolve(void *lib, const char *name)
    {
        return reinterpret_cast<Sym>(dlsym(lib, name));
    }
};

NvidiaGpuBackend::NvidiaGpuBackend(const DrmCard &card, int intervalMs)
    : m_card(card)
    , m_fn(std::make_unique<Funcs>())
{
    // sysfs: 0000:07:00.0  ->  NVML: 00000000:07:00.0
    const QString addr = card.pciAddress;
    m_nvmlBusId = QStringLiteral("00000000:").arg(addr.section(QLatin1Char(':'), 0, 0).right(8))
                  + addr.mid(addr.indexOf(QLatin1Char(':')) + 1);
    m_smiBusId = card.pciAddress;

    if (!tryInitNvml())
        startNvidiaSmi(intervalMs);
}

NvidiaGpuBackend::~NvidiaGpuBackend()
{
    if (m_smiProcess) {
        m_smiProcess->kill();
        m_smiProcess->waitForFinished(1000);
        delete m_smiProcess;
    }
    if (m_nvmlOk && m_fn && m_fn->shutdown)
        m_fn->shutdown();
    if (m_nvmlLib)
        dlclose(m_nvmlLib);
}

QString NvidiaGpuBackend::name() const
{
    if (m_nvmlOk)
        return QStringLiteral("NVIDIA (NVML, runtime-loaded)");
    if (m_smiProcess)
        return QStringLiteral("NVIDIA (nvidia-smi streaming)");
    return QStringLiteral("NVIDIA (no data source)");
}

bool NvidiaGpuBackend::tryInitNvml()
{
    for (const char *soname : { "libnvidia-ml.so.1", "libnvidia-ml.so" }) {
        m_nvmlLib = dlopen(soname, RTLD_LAZY | RTLD_LOCAL);
        if (m_nvmlLib)
            break;
    }
    if (!m_nvmlLib)
        return false;

    m_fn->init_v2 = Funcs::resolve<int (*)()>(m_nvmlLib, "nvmlInit_v2");
    m_fn->shutdown = Funcs::resolve<int (*)()>(m_nvmlLib, "nvmlShutdown");
    m_fn->deviceGetHandleByPciBusId_v2 =
        Funcs::resolve<int (*)(const char *, void **)>(m_nvmlLib, "nvmlDeviceGetHandleByPciBusId_v2");
    m_fn->deviceGetUtilizationRates =
        Funcs::resolve<int (*)(void *, NvmlUtilization *)>(m_nvmlLib, "nvmlDeviceGetUtilizationRates");
    m_fn->deviceGetTemperature =
        Funcs::resolve<int (*)(void *, int, unsigned int *)>(m_nvmlLib, "nvmlDeviceGetTemperature");
    m_fn->deviceGetMemoryInfo =
        Funcs::resolve<int (*)(void *, NvmlMemory *)>(m_nvmlLib, "nvmlDeviceGetMemoryInfo");

    if (!m_fn->init_v2 || !m_fn->deviceGetHandleByPciBusId_v2 || !m_fn->deviceGetUtilizationRates
        || !m_fn->deviceGetTemperature || !m_fn->deviceGetMemoryInfo)
        return false;
    if (m_fn->init_v2() != 0)
        return false;

    const QByteArray busIdUtf8 = m_nvmlBusId.toUtf8();
    if (m_fn->deviceGetHandleByPciBusId_v2(busIdUtf8.constData(), &m_nvmlDevice) != 0)
        return false;

    m_nvmlOk = true;
    return true;
}

bool NvidiaGpuBackend::startNvidiaSmi(int intervalMs)
{
    m_smiProcess = new QProcess;
    m_smiProcess->setProgram(QStringLiteral("nvidia-smi"));
    m_smiProcess->setArguments({
        QStringLiteral("--query-gpu=utilization.gpu,temperature.gpu,memory.used"),
        QStringLiteral("--format=csv,noheader,nounits"),
        QStringLiteral("--id=") + m_smiBusId,
        QStringLiteral("-lms"),
        QString::number(std::clamp(intervalMs, 100, 60000)),
    });
    m_smiProcess->start(QIODevice::ReadOnly | QIODevice::Text);
    return m_smiProcess->waitForStarted(3000);
}

GpuSample NvidiaGpuBackend::sampleNvml()
{
    GpuSample s;
    NvmlUtilization util{};
    if (m_fn->deviceGetUtilizationRates(m_nvmlDevice, &util) == 0)
        s.utilizationPct = util.gpu;

    unsigned int temp = 0;
    if (m_fn->deviceGetTemperature(m_nvmlDevice, NVML_TEMPERATURE_GPU, &temp) == 0)
        s.temperatureC = temp;

    NvmlMemory mem{};
    if (m_fn->deviceGetMemoryInfo(m_nvmlDevice, &mem) == 0) {
        s.vramUsedGiB = mem.used / (1024.0 * 1024.0 * 1024.0);
        s.vramTotalGiB = mem.total / (1024.0 * 1024.0 * 1024.0);
    }
    return s;
}

std::optional<GpuSample> NvidiaGpuBackend::pollNvidiaSmi()
{
    if (!m_smiProcess || m_smiProcess->state() != QProcess::Running)
        return std::nullopt;
    if (!m_smiProcess->canReadLine())
        return std::nullopt;

    while (m_smiProcess->canReadLine()) {
        const QString line = QString::fromLatin1(m_smiProcess->readLine()).trimmed();
        if (line.isEmpty())
            continue;
        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() < 3)
            continue;
        GpuSample s;
        bool okU = false, okT = false, okM = false;
        const double util = parts.at(0).trimmed().toDouble(&okU);
        const double temp = parts.at(1).trimmed().toDouble(&okT);
        const double memMb = parts.at(2).trimmed().toDouble(&okM);
        if (okU)
            s.utilizationPct = util;
        if (okT)
            s.temperatureC = temp;
        if (okM)
            s.vramUsedGiB = memMb / 1024.0;
        return s;
    }
    return std::nullopt;
}

GpuSample NvidiaGpuBackend::sample()
{
    if (m_nvmlOk)
        return sampleNvml();
    if (m_smiProcess)
        return pollNvidiaSmi().value_or(GpuSample{});
    return GpuSample{};
}

QString NvidiaGpuBackend::debugInfo() const
{
    QString out;
    QTextStream ts(&out);
    ts << "  GPU: " << m_card.vendorName() << " device " << m_card.deviceId << " ("
       << m_card.pciAddress << ", card" << m_card.index << ")\n";
    if (m_nvmlOk)
        ts << "  source: NVML (runtime dlopen), device " << m_nvmlBusId << "\n";
    else if (m_smiProcess)
        ts << "  source: streaming nvidia-smi -lms\n";
    else
        ts << "  source: NONE (libnvidia-ml not found and nvidia-smi not usable)\n";
    return out;
}
