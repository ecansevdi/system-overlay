#include "IntelGpuBackend.h"
#include "GpuDiscovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

#include <dirent.h>
#include <string.h>
#include <unistd.h>

namespace {

std::optional<long long> readLongFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    bool ok = false;
    const long long v = QString::fromLatin1(f.readAll().trimmed()).toLongLong(&ok);
    if (!ok)
        return std::nullopt;
    return v;
}

// fdinfo memory lines may be plain bytes or carry a suffix ("1234 KiB").
// Returns the value in bytes. Hand-rolled parser: this runs on every fdinfo
// line of every cached drm fd at every refresh, so QRegularExpression is
// deliberately avoided in this hot path.
std::optional<quint64> parseFdinfoSize(const QString &value)
{
    int i = 0;
    const int n = value.size();
    while (i < n && value.at(i).isSpace())
        ++i;
    if (i >= n || !value.at(i).isDigit())
        return std::nullopt;
    quint64 v = 0;
    while (i < n && value.at(i).isDigit()) {
        v = v * 10 + quint64(value.at(i).digitValue());
        ++i;
    }
    while (i < n && value.at(i).isSpace())
        ++i;
    if (i < n) {
        const QChar unit = value.at(i).toLower();
        if (unit == QLatin1Char('k'))
            v *= 1024ull;
        else if (unit == QLatin1Char('m'))
            v *= 1024ull * 1024ull;
        else if (unit == QLatin1Char('g'))
            v *= 1024ull * 1024ull * 1024ull;
    }
    return v;
}

// Compares PCI addresses in their various spellings ("0000:07:00.0" vs
// "00000000:07:00.0") by normalizing the domain to 4 hex digits.
QString normalizePciAddress(const QString &addr)
{
    const int firstColon = addr.indexOf(QLatin1Char(':'));
    if (firstColon < 0)
        return addr.toLower();
    QString domain = addr.left(firstColon);
    while (domain.size() > 4)
        domain.remove(0, 1);
    while (domain.size() < 4)
        domain.prepend(QLatin1Char('0'));
    return (domain + addr.mid(firstColon)).toLower();
}

} // namespace

IntelGpuBackend::IntelGpuBackend(const DrmCard &card)
    : m_card(card)
{
    m_pciAddressNormalized = normalizePciAddress(card.pciAddress);
    findHwmonTempPath();
}

void IntelGpuBackend::findHwmonTempPath()
{
    const QString hwmonRoot = m_card.devicePath + QStringLiteral("/hwmon");
    const auto hwmons = QDir(hwmonRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &hw : hwmons) {
        const QDir chipDir(hwmonRoot + QLatin1Char('/') + hw);
        const auto inputs = chipDir.entryList(QDir::Files, QDir::Name)
                                .filter(QRegularExpression(QStringLiteral("^temp\\d+_input$")));
        if (!inputs.isEmpty()) {
            m_tempPath = chipDir.filePath(inputs.first());
            return;
        }
    }
}

QString IntelGpuBackend::name() const
{
    return QStringLiteral("Intel (fdinfo + hwmon)");
}

std::optional<double> IntelGpuBackend::readTemperature() const
{
    if (m_tempPath.isEmpty())
        return std::nullopt;
    const auto milliC = readLongFile(m_tempPath);
    if (!milliC || *milliC < -60000 || *milliC > 250000)
        return std::nullopt;
    return *milliC / 1000.0;
}

// Full /proc/*/fd scan using POSIX calls directly; Qt's QDir/QFile layer
// allocates QStrings for every one of the thousands of directory entries,
// which made this scan cost ~30 ms. With readdir/readlinkat it stays around
// a third of that, and it only runs every few seconds.
QList<QPair<int, QString>> IntelGpuBackend::fullFdScan() const
{
    QList<QPair<int, QString>> list;

    DIR *proc = opendir("/proc");
    if (!proc)
        return list;

    auto isDriPath = [](const char *target) {
        return strncmp(target, "/dev/dri/", 9) == 0;
    };

    while (struct dirent *pidEntry = readdir(proc)) {
        char *end = nullptr;
        const long pid = strtol(pidEntry->d_name, &end, 10);
        if (!end || *end != '\0' || pid <= 0)
            continue;

        char fdDirPath[64];
        snprintf(fdDirPath, sizeof(fdDirPath), "/proc/%ld/fd", pid);
        DIR *fdDir = opendir(fdDirPath);
        if (!fdDir)
            continue;
        while (struct dirent *fdEntry = readdir(fdDir)) {
            if (fdEntry->d_name[0] == '.')
                continue;
            char linkPath[320];
            snprintf(linkPath, sizeof(linkPath), "/proc/%ld/fd/%s", pid, fdEntry->d_name);
            char target[256];
            const ssize_t len = readlink(linkPath, target, sizeof(target) - 1);
            if (len <= 0)
                continue;
            target[len] = '\0';
            if (isDriPath(target))
                list.append({int(pid), QString::fromLatin1(fdEntry->d_name)});
        }
        closedir(fdDir);
    }
    closedir(proc);
    return list;
}

IntelGpuBackend::ScanResult IntelGpuBackend::snapshotFromFdList(
    const QList<QPair<int, QString>> &fdList) const
{
    ScanResult result;

    QSet<quint64> seenClients; // one fd per client is enough (dup()ed fds share a client id)

    for (const auto &entry : fdList) {
        const int pid = entry.first;
        const QString &fd = entry.second;

        QFile info(QStringLiteral("/proc/%1/fdinfo/%2").arg(pid).arg(fd));
        if (!info.open(QIODevice::ReadOnly | QIODevice::Text))
            continue; // e.g. process owned by another user, or fd closed

        quint64 clientId = 0;
        bool clientIdSeen = false;
        bool pdevMatches = true;
        quint64 localBytes = 0;
        bool totalLocalSeen = false;
        quint64 engineNs = 0;

        // Never gate the loop on atEnd(): procfs files report size 0, so
        // both QIODevice::atEnd() and QTextStream::atEnd() are true before
        // the first read. readLine() performs a real read and returns an
        // empty QByteArray at EOF.
        while (true) {
            const QByteArray raw = info.readLine();
            if (raw.isEmpty())
                break;
            // Manual parsing instead of QRegularExpression: this loop runs
            // over every line of every cached drm fd at every refresh.
            const QLatin1String line(raw.constData(), int(raw.size()));
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QLatin1String key(line.data(), colon);
            const QString value = QString::fromLatin1(line.data() + colon + 1,
                                                      line.size() - colon - 1);

            if (key == QLatin1String("drm-client-id")) {
                if (!clientIdSeen) {
                    bool ok = false;
                    clientId = value.trimmed().toULongLong(&ok);
                    clientIdSeen = ok;
                }
            } else if (key == QLatin1String("drm-pdev")) {
                if (normalizePciAddress(value.trimmed()) != m_pciAddressNormalized)
                    pdevMatches = false;
            } else if (key == QLatin1String("drm-total-local0")) {
                if (!totalLocalSeen) {
                    if (const auto bytes = parseFdinfoSize(value)) {
                        localBytes = *bytes;
                        totalLocalSeen = true;
                    }
                }
            } else if (key.startsWith(QLatin1String("drm-engine-"))) {
                // Only cumulative times carry an "ns" suffix; metadata keys
                // such as "drm-engine-capacity-video: 2" must not count.
                if (value.trimmed().endsWith(QLatin1String("ns"))) {
                    const QLatin1String engine(key.data() + 11, key.size() - 11);
                    if (engine == QLatin1String("render") || engine == QLatin1String("compute")
                        || engine == QLatin1String("copy")) {
                        // Value looks like "123456 ns"; toULongLong would
                        // reject the trailing unit, so take the leading digits.
                        const QString v = value.trimmed();
                        int digits = 0;
                        while (digits < v.size() && v.at(digits).isDigit())
                            ++digits;
                        if (digits > 0) {
                            bool ok = false;
                            const quint64 ns = v.left(digits).toULongLong(&ok);
                            if (ok)
                                engineNs += ns;
                        }
                    }
                }
            }
        }

        if (!clientIdSeen || !pdevMatches)
            continue;
        result.aliveFds.append(entry);
        if (seenClients.contains(clientId))
            continue;
        seenClients.insert(clientId);

        result.snap.clients.insert(clientId, EngineTime{std::chrono::nanoseconds(engineNs)});
        result.snap.vramBytes += localBytes;
    }
    return result;
}

IntelGpuBackend::Snapshot IntelGpuBackend::scanFdInfo()
{
    const auto now = std::chrono::steady_clock::now();
    // Full scan every 2 s so freshly started GPU clients (a game just
    // launched) show up quickly; incremental fdinfo reads cover the ticks
    // in between. POSIX scan costs ~10 ms, i.e. ~0.5 % of one core.
    constexpr std::chrono::milliseconds kFullScanInterval{2000};

    // Full scan periodically; also immediately when the cache went empty
    // (first GPU client appearing or everything closed).
    if (m_lastFullScan.time_since_epoch().count() == 0 || m_cachedFds.isEmpty()
        || now - m_lastFullScan >= kFullScanInterval) {
        m_cachedFds = fullFdScan();
        m_lastFullScan = now;
    }

    ScanResult result = snapshotFromFdList(m_cachedFds);
    m_cachedFds = result.aliveFds;
    return result.snap;
}

void IntelGpuBackend::prime()
{
    const Snapshot snap = scanFdInfo();
    m_prevClients = snap.clients;
    m_prevWall = std::chrono::steady_clock::now();
    m_hasPrev = true;
}

GpuSample IntelGpuBackend::sample()
{
    GpuSample s;
    s.temperatureC = readTemperature();

    const Snapshot snap = scanFdInfo();
    s.vramUsedGiB = snap.vramBytes / (1024.0 * 1024.0 * 1024.0);

    const auto now = std::chrono::steady_clock::now();
    if (m_hasPrev && m_prevWall) {
        const auto wallNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - *m_prevWall).count();
        if (wallNs > 0) {
            quint64 busyNs = 0;
            for (auto it = snap.clients.cbegin(); it != snap.clients.cend(); ++it) {
                const auto prev = m_prevClients.constFind(it.key());
                if (prev == m_prevClients.cend())
                    continue; // new client: skip to avoid counting startup burst
                const qint64 delta = it.value().total.count() - prev.value().total.count();
                if (delta > 0)
                    busyNs += static_cast<quint64>(delta);
            }
            double pct = 100.0 * double(busyNs) / double(wallNs);
            if (std::isfinite(pct))
                s.utilizationPct = std::clamp(pct, 0.0, 100.0);
        }
    }

    m_prevClients = snap.clients;
    m_prevWall = now;
    m_hasPrev = true;
    return s;
}

QString IntelGpuBackend::debugInfo() const
{
    QString out;
    QTextStream ts(&out);
    ts << "  GPU: " << m_card.vendorName() << " device " << m_card.deviceId << " ("
       << m_card.pciAddress << ", card" << m_card.index << ")\n";
    ts << "  utilization source: /proc/*/fdinfo drm-engine-{render,compute,copy}"
          " (sum of client deltas)\n";
    ts << "  VRAM source: /proc/*/fdinfo drm-total-local0 (sum over clients, approximate)\n";
    ts << "  temperature source: "
       << (m_tempPath.isEmpty() ? QStringLiteral("(not found)") : m_tempPath) << "\n";
    return out;
}
