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

void X11Overlay::place(OverlayWindow *win, QScreen *screen, const Config &cfg)
{
    if (!screen)
        screen = win->screen();
    if (!screen)
        return;

    // availableGeometry() excludes panels that set _NET_WM_STRUT, so the HUD
    // never overlaps them; the offsets are measured from the anchored edges
    // (mirroring the Wayland margins).
    const QRect g = screen->availableGeometry();
    const int x = cfg.offsetX();
    const int y = cfg.offsetY();

    QPoint topLeft;
    switch (cfg.position()) {
    case Config::Position::TopLeft:
        topLeft = g.topLeft() + QPoint(x, y);
        break;
    case Config::Position::TopRight:
        topLeft = QPoint(g.right() - win->width() - x, g.top() + y);
        break;
    case Config::Position::BottomLeft:
        topLeft = QPoint(g.left() + x, g.bottom() - win->height() - y);
        break;
    case Config::Position::BottomRight:
        topLeft = QPoint(g.right() - win->width() - x, g.bottom() - win->height() - y);
        break;
    }
    win->setGeometry(QRect(topLeft, win->size()));
}
