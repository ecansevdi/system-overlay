#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>

// One rendered HUD row: the text plus an optional thin progress bar.
// fraction is 0..1 for the bar fill; a negative fraction means the row has
// no natural percentage and is rendered without a bar.
struct HudRow
{
    QString text;
    double fraction = -1.0;
    QColor color;
};

// Row indices shared by MetricManager and the tray menu toggles.
enum HudRows { RowCpu = 0, RowGpu = 1, RowRam = 2, RowVram = 3, RowCount = 4 };

Q_DECLARE_METATYPE(HudRow)

inline bool operator==(const HudRow &a, const HudRow &b)
{
    return a.text == b.text && a.fraction == b.fraction && a.color == b.color;
}
inline bool operator!=(const HudRow &a, const HudRow &b)
{
    return !(a == b);
}
