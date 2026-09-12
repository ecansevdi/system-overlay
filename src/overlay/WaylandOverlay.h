#pragma once

class OverlayWindow;
class QScreen;
class Config;

namespace LayerShellQt
{
class Window;
}

// Wayland backend: turns the overlay window into a real zwlr-layer-shell
// overlay surface via KDE's LayerShellQt, so KWin composites it above normal
// windows and fullscreen applications (layer "overlay").
//
// Must be called BEFORE the window is shown: LayerShellQt hooks the platform
// surface creation event to swap in the layer-shell integration.
class WaylandOverlay
{
public:
    // Returns true when the layer-shell integration was attached. False means
    // "not built with layershell" or "not a Wayland platform window" — caller
    // falls back to the X11/ordinary-window strategy.
    static bool configure(OverlayWindow *win, const Config &cfg, QScreen *screen);

    // Re-target an already created layer surface to another output (requires
    // a hide/show remap by the caller afterwards).
    static void applyScreen(OverlayWindow *win, QScreen *screen);
};
