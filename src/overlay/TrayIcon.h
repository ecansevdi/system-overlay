#pragma once

#include <QColor>
#include <QObject>

#include "metrics/HudRow.h"

class QSystemTrayIcon;
class QAction;
class QMenu;

// System tray presence for the HUD (StatusNotifierItem on Plasma): right-click
// menu with Show/Hide, Pause/Resume, per-row visibility, a colour picker
// (preset palette, RGB entry, full colour dialog), a credit line and Quit.
// The menu is exported as a DBus menu so Plasma can show it (Wayland popups
// cannot grab without a parent that received input).
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

    QMenu *menu() const;
    void setRowChecked(int row, bool checked);

Q_SIGNALS:
    void showRequested();
    void hideRequested();
    void pauseRequested(bool paused);
    void quitRequested();

    void metricToggled(int row, bool visible);
    void baseColorRequested(const QColor &color);

    void menuAboutToShow();
    void menuAboutToHide();
    void menuActionTriggered();

private:
    void updateActions();

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_visibilityAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_metricActions[RowCount] = {};
    bool m_visible = true;
    bool m_paused = false;
};
