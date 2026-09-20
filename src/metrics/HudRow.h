#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <algorithm>
#include <vector>

// One rendered HUD row: the text plus an optional thin progress bar.
// fraction is 0..1 for the bar fill; a negative fraction means the row has
// no natural percentage and is rendered without a bar.
//
// text is split into "label" ("CPU", "VRAM", ...) and "value" ("12% 45°C")
// by the parser below, so the painter can pad every label to one common
// width: rows align regardless of which metrics are enabled at the moment.
struct HudRow
{
    QString text;      // full line as before, e.g. "CPU: 12% 45°C"
    QString label;     // part before the first ':' (empty -> no aligning)
    QString value;     // part after the first ':' (leading spaces stripped)
    double fraction = -1.0;
    QColor color;
};

// Row indices shared by MetricManager and the tray menu toggles.
enum HudRows {
    RowCpu = 0,
    RowGpu = 1,
    RowRam = 2,
    RowVram = 3,
    RowNetUp = 4,
    RowNetDown = 5,
    RowFps = 6,
    RowCount = 7
};

// Split "CPU: 12% 45°C" into label + value. A row without a colon (or with
// an empty label) is returned unchanged with an empty label, which the
// painter renders as-is (left aligned, no padding).
inline void splitHudText(const QString &text, QString *label, QString *value)
{
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon <= 0) {
        label->clear();
        *value = text;
        return;
    }
    *label = text.left(colon);
    QString rest = text.mid(colon + 1);
    while (rest.startsWith(QLatin1Char(' ')))
        rest.remove(0, 1);
    *value = rest;
}

// Dynamic alignment: pads every row's label field ("NAME:" plus one space)
// to the width of the widest label currently visible. Monospace font ->
// widening one label widens all of them equally, so the values (percentages,
// GiB, MB/s) start at the same x:
//
//   CPU:  10% 49°C
//   GPU:  14% 47°C
//   RAM:  5.4/31.3 GiB
//   VRAM: 0.5/8.0 GiB
//   up:   12.3 MB/s
//   down: 73.2 MB/s
//   FPS:  60
//
// Rows are produced by independent metric blocks; rows can appear/disappear
// at runtime (tray checkboxes, missing sensors, GPU without VRAM info), so
// the width is recomputed from the rows that are actually present. Adding a
// future metric needs nothing here: give its row the "NAME: value" shape and
// it aligns itself.
inline void alignHudRows(std::vector<HudRow> &rows)
{
    // Rows are built as plain text ("NAME: value"); split them into fields
    // here so callers never have to care about the label/value split.
    for (HudRow &row : rows)
        splitHudText(row.text, &row.label, &row.value);

    int maxLabelWidth = 0;
    for (const HudRow &row : rows)
        maxLabelWidth = std::max(maxLabelWidth, int(row.label.size()));

    if (maxLabelWidth <= 0) // nothing to align
        return;

    // "NAME:" + one trailing space forms the label field, padded to a common
    // character count so values line up ("up:   12 MB/s" vs "VRAM: 0.5/8.0").
    for (HudRow &row : rows) {
        if (row.label.isEmpty())
            continue;
        const QString field = row.label + QLatin1String(": ");
        row.text = field.leftJustified(maxLabelWidth + 2) + row.value;
    }
}

Q_DECLARE_METATYPE(HudRow)

inline bool operator==(const HudRow &a, const HudRow &b)
{
    return a.text == b.text && a.label == b.label && a.value == b.value
        && a.fraction == b.fraction && a.color == b.color;
}
inline bool operator!=(const HudRow &a, const HudRow &b)
{
    return !(a == b);
}
