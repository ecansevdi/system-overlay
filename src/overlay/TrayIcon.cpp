#include "TrayIcon.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace {

// Painted at 48px so it stays crisp on HiDPI panels; no external asset needed.
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

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
{
    auto *menu = new QMenu;

    m_visibilityAction = menu->addAction(QStringLiteral("Gizle"));
    m_pauseAction = menu->addAction(QStringLiteral("Duraklat"));
    menu->addSeparator();
    QAction *quitAction = menu->addAction(QStringLiteral("Çıkış"));

    connect(m_visibilityAction, &QAction::triggered, this, [this]() {
        m_visible = !m_visible;
        updateActions();
        if (m_visible)
            Q_EMIT showRequested();
        else
            Q_EMIT hideRequested();
    });

    connect(m_pauseAction, &QAction::triggered, this, [this]() {
        m_paused = !m_paused;
        updateActions();
        Q_EMIT pauseRequested(m_paused);
    });

    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray = new QSystemTrayIcon(QIcon(makeIcon()), this);
    m_tray->setToolTip(QStringLiteral("System Overlay (CPU/GPU/RAM/VRAM)"));
    m_tray->setContextMenu(menu);
    m_tray->show();
}

void TrayIcon::updateActions()
{
    m_visibilityAction->setText(m_visible ? QStringLiteral("Gizle") : QStringLiteral("Göster"));
    m_pauseAction->setText(m_paused ? QStringLiteral("Devam et") : QStringLiteral("Duraklat"));
}
