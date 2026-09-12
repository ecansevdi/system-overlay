#pragma once

#include <QList>
#include <QString>
#include <optional>

// Enumerates /sys/class/hwmon/* and picks a sensible CPU package temperature.
// Nothing is hard-coded: chip names and labels are discovered at runtime.
class HwmonScanner
{
public:
    struct TempInput {
        QString inputPath;   // e.g. /sys/class/hwmon/hwmon3/temp1_input
        QString label;       // e.g. "Tctl", may be empty
        long milliC = -1;    // current value in milli-degrees C, -1 on error
    };

    struct Chip {
        QString path;        // e.g. /sys/class/hwmon/hwmon3
        QString name;        // e.g. "k10temp", "coretemp", "i915", ...
        QList<TempInput> temps;
    };

    // Scan all hwmon chips present right now (cheap enough for --debug use).
    static QList<Chip> scan();

    // Best-effort CPU package temperature source.
    // Preference:
    //   1. k10temp / zenpower  : Tctl, then Tdie
    //   2. coretemp            : "Package id N", then first Core
    //   3. any chip whose label looks like a CPU package temp
    // Returns std::nullopt when nothing plausible exists (caller shows "--").
    struct CpuTempSource
    {
        QString inputPath;
        QString chipName;
        QString label;
    };
    static std::optional<CpuTempSource> findCpuTemp();

    // Read a single temp*_input file; returns milli-degrees C or nullopt.
    static std::optional<long> readMilliDegrees(const QString &inputPath);

    // Human readable dump for --debug.
    static QString debugDump();
};
