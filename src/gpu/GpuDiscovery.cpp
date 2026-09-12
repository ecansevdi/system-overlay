#include "GpuDiscovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <optional>

namespace {

std::optional<QString> readFileTrimmed(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    return QString::fromLatin1(f.readAll().trimmed());
}

std::optional<uint32_t> readHexFile(const QString &path)
{
    const auto s = readFileTrimmed(path);
    if (!s)
        return std::nullopt;
    bool ok = false;
    const uint32_t v = s->toUInt(&ok, 16);
    if (!ok)
        return std::nullopt;
    return v;
}

} // namespace

QString DrmCard::vendorName() const
{
    switch (vendorId) {
    case 0x1002:
    case 0x1022:
        return QStringLiteral("AMD");
    case 0x8086:
        return QStringLiteral("Intel");
    case 0x10DE:
        return QStringLiteral("NVIDIA");
    default:
        return QStringLiteral("%1").arg(vendorId, 4, 16, QLatin1Char('0'));
    }
}

QList<DrmCard> GpuDiscovery::enumerate()
{
    QList<DrmCard> cards;

    static const QRegularExpression cardRe(QStringLiteral("^card(\\d+)$"));
    const auto entries = QDir(QStringLiteral("/sys/class/drm"))
                             .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString &entry : entries) {
        const auto m = cardRe.match(entry);
        if (!m.hasMatch())
            continue;

        DrmCard card;
        card.index = m.captured(1).toInt();
        card.sysfsPath = QStringLiteral("/sys/class/drm/") + entry;
        card.devicePath = card.sysfsPath + QStringLiteral("/device");

        // The canonical PCI address of the owning device.
        {
            QFileInfo devInfo(card.devicePath);
            const QString target = devInfo.canonicalFilePath();
            if (target.isEmpty())
                continue; // not a real PCI device (virtual card)
            card.pciAddress = target.section(QLatin1Char('/'), -1);
        }

        card.vendorId = readHexFile(card.devicePath + QStringLiteral("/vendor")).value_or(0);
        card.deviceId = readHexFile(card.devicePath + QStringLiteral("/device")).value_or(0);
        if (card.vendorId == 0)
            continue;

        card.bootVga = readFileTrimmed(card.devicePath + QStringLiteral("/boot_vga"))
                           .value_or(QStringLiteral("0"))
                           .compare(QLatin1String("1")) == 0;

        // Connected connectors: card1-DP-1/status etc. live next to card1.
        const QString drmDir = card.sysfsPath;
        const auto siblings = QDir(drmDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sib : siblings) {
            if (!sib.startsWith(entry + QLatin1Char('-')))
                continue;
            const auto status = readFileTrimmed(drmDir + QLatin1Char('/') + sib
                                                + QStringLiteral("/status"));
            if (status && *status == QLatin1String("connected")) {
                card.drivesDisplay = true;
                card.connectedConnectors.append(sib.section(QLatin1Char('-'), 1));
            }
        }

        cards.append(card);
    }

    std::sort(cards.begin(), cards.end(), [](const DrmCard &a, const DrmCard &b) {
        return a.index < b.index;
    });
    return cards;
}

std::optional<DrmCard> GpuDiscovery::pickPrimary()
{
    const QList<DrmCard> cards = enumerate();
    if (cards.isEmpty())
        return std::nullopt;

    const DrmCard *best = nullptr;
    int bestScore = -1;
    for (const DrmCard &card : cards) {
        // Display-carrying cards first, then boot_vga, then lowest index.
        int score = 0;
        if (card.drivesDisplay)
            score += 4;
        if (card.bootVga)
            score += 2;
        if (score > bestScore || (score == bestScore && best == nullptr)) {
            best = &card;
            bestScore = score;
        }
    }
    if (!best)
        return std::nullopt;
    return *best;
}
