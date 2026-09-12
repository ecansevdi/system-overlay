#include "TrayIcon.h"

#include <QAction>
#include <QColorDialog>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSystemTrayIcon>

namespace {

// Preset palette offered in the tray menu (light tints that read well on
// dark backgrounds; the dark outline keeps them legible on light ones).
struct PaletteEntry
{
    const char *name;
    const char *hex;
};
constexpr PaletteEntry kPalette[] = {
    { "Yeşil",    "#a6f28f" },
    { "Sarı",     "#f2d24f" },
    { "Turuncu",  "#f2a25d" },
    { "Kırmızı",  "#f25d5d" },
    { "Pembe",    "#f25dc8" },
    { "Mor",      "#b05df2" },
    { "Mavi",     "#5da8f2" },
    { "Camgöbeği", "#5df2e1" },
    { "Beyaz",    "#f2f2f2" },
};

QPixmap swatchPixmap(const QColor &color)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(color);
    p.drawRoundedRect(1, 1, 13, 13, 4, 4);
    p.end();
    return pm;
}

// Tray icon painted at 48px so it stays crisp on HiDPI panels; no external
// asset needed.
QPixmap makeIcon()
{
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(24, 24, 24, 230));
    p.drawRoundedRect(2, 2, 44, 44, 10, 10);

    p.setBrush(QColor(0xa6, 0xf2, 0x8f)); // HUD text colour
    p.drawRect(11, 27, 7, 11);
    p.drawRect(21, 17, 7, 21);
    p.drawRect(31, 11, 7, 27);
    p.end();
    return pm;
}

// Accepts "#rrggbb", "rrggbb", "#rgb", named colours and "R,G,B" (0-255).
QColor parseColorInput(QString input)
{
    input = input.trimmed();
    if (input.isEmpty())
        return QColor();

    if (input.contains(QLatin1Char(','))) {
        const QStringList parts = input.split(QLatin1Char(','));
        if (parts.size() == 3) {
            bool okR = false, okG = false, okB = false;
            const int r = parts.at(0).toInt(&okR);
            const int g = parts.at(1).toInt(&okG);
            const int b = parts.at(2).toInt(&okB);
            if (okR && okG && okB && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0
                && b <= 255)
                return QColor(r, g, b);
        }
        return QColor();
    }

    // Bare hex (e.g. "a6f28f") is not a named colour: add the missing '#'.
    static const QRegularExpression bareHex(QStringLiteral("^[0-9a-fA-F]{6}$"));
    if (bareHex.match(input).hasMatch())
        input.prepend(QLatin1Char('#'));

    return QColor(input); // handles #rgb / #rrggbb / SVG names; invalid on garbage
}

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    m_tray = new QSystemTrayIcon(QIcon(makeIcon()), this);
    m_tray->setToolTip(QStringLiteral("System Overlay (CPU/GPU/RAM/VRAM)"));

    auto *menu = new QMenu;
    m_tray->setContextMenu(menu);

    m_visibilityAction = menu->addAction(QStringLiteral("Gizle"));
    m_pauseAction = menu->addAction(QStringLiteral("Duraklat"));
    menu->addSeparator();

    // Per-metric toggles: rows can be shown/hidden at runtime.
    static const char *kRowNames[4] = {"CPU", "GPU", "RAM", "VRAM"};
    for (int row = 0; row < 4; ++row) {
        QAction *action = menu->addAction(QLatin1String(kRowNames[row]));
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, row](bool visible) {
            Q_EMIT metricToggled(row, visible);
        });
        m_metricActions[row] = action;
    }
    menu->addSeparator();

    // Colour picker: preset palette, manual RGB entry and a full dialog.
    QMenu *colorMenu = menu->addMenu(QStringLiteral("Renk"));
    for (const PaletteEntry &entry : kPalette) {
        QAction *swatch = colorMenu->addAction(swatchPixmap(QColor(entry.hex)),
            QLatin1String(entry.name));
        connect(swatch, &QAction::triggered, this, [this, entry]() {
            Q_EMIT baseColorRequested(QColor(entry.hex));
        });
    }
    colorMenu->addSeparator();
    QAction *rgbAction = colorMenu->addAction(QStringLiteral("RGB gir…"));
    connect(rgbAction, &QAction::triggered, this, [this]() {
        const QString input = QInputDialog::getText(nullptr,
            QStringLiteral("RGB gir"),
            QStringLiteral("Renk kodu:  #RRGGBB  veya  R,G,B  (0-255)"));
        if (input.isEmpty())
            return;
        const QColor color = parseColorInput(input);
        if (color.isValid())
            Q_EMIT baseColorRequested(color);
        else
            m_tray->showMessage(QStringLiteral("Geçersiz renk"),
                QStringLiteral("Örnekler: #a6f28f  veya  166,242,143"),
                QSystemTrayIcon::Warning, 3000);
    });
    QAction *dialogAction = colorMenu->addAction(QStringLiteral("Renk penceresi…"));
    connect(dialogAction, &QAction::triggered, this, [this]() {
        const QColor color = QColorDialog::getColor(QColor(kPalette[0].hex), nullptr,
            QStringLiteral("HUD rengi seç"));
        if (color.isValid())
            Q_EMIT baseColorRequested(color);
    });

    menu->addSeparator();
    QAction *quitAction = menu->addAction(QStringLiteral("Çıkış"));
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray->show();
}

void TrayIcon::updateActions()
{
    m_visibilityAction->setText(m_visible ? QStringLiteral("Gizle") : QStringLiteral("Göster"));
    m_pauseAction->setText(m_paused ? QStringLiteral("Devam et") : QStringLiteral("Duraklat"));
}
