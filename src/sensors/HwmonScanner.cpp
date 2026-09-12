#include "HwmonScanner.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

static std::optional<long> readLongFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    const QString content = QString::fromLatin1(f.readAll().trimmed());
    bool ok = false;
    const long v = content.toLong(&ok);
    if (!ok)
        return std::nullopt;
    return v;
}

std::optional<long> HwmonScanner::readMilliDegrees(const QString &inputPath)
{
    const std::optional<long> v = readLongFile(inputPath);
    if (!v)
        return std::nullopt;
    // hwmon temp inputs are in milli-degrees Celsius; sanity check the range.
    if (*v < -60000 || *v > 250000)
        return std::nullopt;
    return v;
}

QList<HwmonScanner::Chip> HwmonScanner::scan()
{
    QList<Chip> chips;

    QDir hwmonDir(QStringLiteral("/sys/class/hwmon"));
    const auto entries = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString chipPath = hwmonDir.filePath(entry);

        Chip chip;
        chip.path = chipPath;
        {
            QFile nameFile(chipPath + QStringLiteral("/name"));
            if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            chip.name = QString::fromLatin1(nameFile.readAll().trimmed());
        }

        const auto files = QDir(chipPath)
                               .entryList(QDir::Files, QDir::Name)
                               .filter(QRegularExpression(QStringLiteral("^temp\\d+_input$")));
        for (const QString &inputFile : files) {
            const QString base = inputFile.chopped(6); // strip "_input"
            TempInput t;
            t.inputPath = chipPath + QLatin1Char('/') + inputFile;
            const QString labelPath = chipPath + QLatin1Char('/') + base + QStringLiteral("_label");
            QFile lf(labelPath);
            if (lf.open(QIODevice::ReadOnly | QIODevice::Text))
                t.label = QString::fromLatin1(lf.readAll().trimmed());
            if (const auto v = readMilliDegrees(t.inputPath))
                t.milliC = *v;
            chip.temps.append(t);
        }

        if (!chip.temps.isEmpty())
            chips.append(chip);
    }
    return chips;
}

static bool labelMatchesCpu(const QString &label)
{
    const QString l = label.toLower();
    return l.contains(QLatin1String("tctl")) || l.contains(QLatin1String("tdie"))
        || l.contains(QLatin1String("package")) || l == QLatin1String("cpu")
        || l.startsWith(QLatin1String("cpu ")) || l.contains(QLatin1String("cpu temp"));
}

static int labelRank(const HwmonScanner::Chip &chip, const HwmonScanner::TempInput &t)
{
    const QString l = t.label.toLower();
    if (chip.name == QLatin1String("k10temp") || chip.name == QLatin1String("zenpower")) {
        if (l == QLatin1String("tctl"))
            return 0;
        if (l == QLatin1String("tdie"))
            return 1;
        return 5; // TccdN etc.
    }
    if (chip.name == QLatin1String("coretemp")) {
        if (l.startsWith(QLatin1String("package id")))
            return 0;
        return 2; // per-core temps
    }
    if (labelMatchesCpu(t.label))
        return 1;
    return 9; // unlabeled fallback inside a known-CPU chip
}

static bool isKnownCpuChipName(const QString &name)
{
    static const QStringList known = {
        QStringLiteral("k10temp"), QStringLiteral("zenpower"), QStringLiteral("coretemp"),
        QStringLiteral("cpu_thermal"), QStringLiteral("scpi_sensors"), QStringLiteral("cpu-thermal"),
    };
    return known.contains(name);
}

std::optional<HwmonScanner::CpuTempSource> HwmonScanner::findCpuTemp()
{
    const QList<Chip> chips = scan();

    // Pass 1: known CPU chip names.
    for (const Chip &chip : chips) {
        if (!isKnownCpuChipName(chip.name))
            continue;
        const auto best = std::min_element(chip.temps.cbegin(), chip.temps.cend(),
            [&chip](const TempInput &a, const TempInput &b) {
                const int ra = labelRank(chip, a);
                const int rb = labelRank(chip, b);
                if (ra != rb)
                    return ra < rb;
                return a.inputPath < b.inputPath;
            });
        if (best != chip.temps.cend() && best->milliC >= 0) {
            return CpuTempSource{best->inputPath, chip.name, best->label};
        }
    }

    // Pass 2: any chip with a CPU-ish label.
    for (const Chip &chip : chips) {
        for (const TempInput &t : chip.temps) {
            if (labelMatchesCpu(t.label) && t.milliC >= 0)
                return CpuTempSource{t.inputPath, chip.name, t.label};
        }
    }

    return std::nullopt;
}

QString HwmonScanner::debugDump()
{
    QString out;
    QTextStream ts(&out);
    const QList<Chip> chips = scan();
    for (const Chip &chip : chips) {
        ts << "  hwmon: " << chip.name << " (" << chip.path << ")\n";
        for (const TempInput &t : chip.temps) {
            ts << "    " << (t.label.isEmpty() ? QStringLiteral("(unlabeled)") : t.label)
               << " = " << (t.milliC >= 0 ? QString::number(t.milliC / 1000.0, 'f', 1)
                                           : QStringLiteral("<read error>"))
               << " C  [" << t.inputPath << "]\n";
        }
    }
    if (chips.isEmpty())
        ts << "  (no hwmon devices found)\n";

    if (const auto cpu = findCpuTemp()) {
        ts << "  selected CPU temp: " << cpu->chipName << " / " << cpu->label << " -> "
           << cpu->inputPath << "\n";
    } else {
        ts << "  selected CPU temp: none\n";
    }
    return out;
}
