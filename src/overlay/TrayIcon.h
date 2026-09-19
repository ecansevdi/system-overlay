#pragma once

#include <QColor>
#include <QObject>

class QSystemTrayIcon;
class QAction;
class QMenu;

// System tray presence for the HUD (StatusNotifierItem on Plasma): right-click
// menu with Show/Hide, Pause/Resume, per-row visibility, a colour picker
// (preset palette, RGB entry, full colour dialog) and Quit. Keeps the HUD
// itself completely input-transparent — controlling it never requires
// clicking on the overlay.
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

Q_SIGNALS:
    void showRequested();
    void hideRequested();
    void pauseRequested(bool paused);
    void quitRequested();

    // One of the HUD rows (HudRows indices) was toggled in the menu.
    void metricToggled(int row, bool visible);

    // A new base colour (normal text colour) was picked in the menu.
    void baseColorRequested(const QColor &color);

private:
    void updateActions();

    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_visibilityAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_metricActions[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    bool m_visible = true;
    bool m_paused = false;
};
