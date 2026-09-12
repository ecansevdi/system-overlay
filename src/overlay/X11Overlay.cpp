#include "X11Overlay.h"

#include "OverlayWindow.h"
#include "config/Config.h"

#include <QScreen>

bool X11Overlay::configure(OverlayWindow *win, const Config &cfg, QScreen *screen)
{
    Q_UNUSED(cfg);

    // Qt::Tool => _NET_WM_WINDOW_TYPE_UTILITY on X11: no taskbar entry, no
    // Alt+Tab, never focused by the WM. StaysOnTop keeps it above normal
    // windows; TransparentForInput gives an empty input shape (click-through)
    // and WindowDoesNotAcceptFocus clears WM_HINTS input.
    win->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                  | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);

    if (screen)
        win->setScreen(screen);

    return true;
}

void X11Overlay::place(OverlayWindow *win, QScreen *screen, int offsetX, int offsetY)
{
    if (!screen)
        screen = win->screen();
    if (!screen)
        return;
    const QPoint topLeft = screen->geometry().topLeft();
    win->setGeometry(QRect(topLeft + QPoint(offsetX, offsetY), win->size()));
}
