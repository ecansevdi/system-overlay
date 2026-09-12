#include "GpuMetrics.h"
#include "GpuDiscovery.h"

#include "AmdGpuBackend.h"
#include "IntelGpuBackend.h"
#include "NvidiaGpuBackend.h"

#include <QTextStream>

std::unique_ptr<GpuBackend> createGpuBackendForVendor(uint32_t vendorId, const DrmCard &card,
                                                      int refreshIntervalMs)
{
    switch (vendorId) {
    case 0x1002:
    case 0x1022:
        return std::make_unique<AmdGpuBackend>(card);
    case 0x8086:
        return std::make_unique<IntelGpuBackend>(card);
    case 0x10DE:
        return std::make_unique<NvidiaGpuBackend>(card, refreshIntervalMs);
    default:
        return nullptr; // unknown vendor: metrics stay "--", never fatal
    }
}

GpuMetrics::GpuMetrics(int refreshIntervalMs)
    : m_intervalMs(refreshIntervalMs)
{
}

bool GpuMetrics::discover()
{
    const auto card = GpuDiscovery::pickPrimary();
    if (!card)
        return false;

    m_card = *card;
    m_hasCard = true;
    m_backend = createGpuBackendForVendor(m_card.vendorId, m_card, m_intervalMs);
    return m_backend != nullptr;
}

void GpuMetrics::prime()
{
    if (m_backend)
        m_backend->prime();
}

GpuSample GpuMetrics::sample()
{
    if (!m_backend)
        return GpuSample{};
    return m_backend->sample();
}

QString GpuMetrics::summary() const
{
    if (!m_hasCard)
        return QStringLiteral("no DRM GPU card found");
    if (!m_backend)
        return QStringLiteral("GPU %1 (card%2) has no supported backend")
            .arg(m_card.vendorName(), QString::number(m_card.index));
    return QStringLiteral("GPU %1 (card%2): %3")
        .arg(m_card.vendorName(), QString::number(m_card.index), m_backend->name());
}

QString GpuMetrics::debugInfo() const
{
    if (!m_backend)
        return summary() + QLatin1Char('\n');
    return m_backend->debugInfo();
}
