#pragma once

#include <QObject>

class QSystemTrayIcon;
class QAction;

// System tray presence for the HUD (StatusNotifierItem on Plasma): right-click
// menu with Show/Hide, Pause/Resume and Quit. Keeps the HUD itself completely
// input-transparent — closing never requires clicking on the overlay.
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

private:
    void updateActions();

    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_visibilityAction = nullptr;
    QAction *m_pauseAction = nullptr;
    bool m_visible = true;
    bool m_paused = false;
};
