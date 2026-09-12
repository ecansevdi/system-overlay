#pragma once

class OverlayWindow;
class QScreen;
class Config;

#include "config/Config.h"

// X11 backend (fallback when layer-shell is unavailable): an unmanaged-looking
// frameless tool window that stays on top, skips taskbar/Alt+Tab and never
// takes focus or input. Click-through is done by the Qt xcb platform itself
// via Qt::WindowTransparentForInput (empty X11 input shape through XFixes).
class X11Overlay
{
public:
    static bool configure(OverlayWindow *win, const Config &cfg, QScreen *screen);
    static void place(OverlayWindow *win, QScreen *screen, const Config &cfg);
};
