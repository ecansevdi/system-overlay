#include "WaylandOverlay.h"

#include "OverlayWindow.h"
#include "config/Config.h"

#include <QMargins>
#include <QScreen>

#ifdef SYSTEM_OVERLAY_WITH_LAYERSHELL
#include <LayerShellQt/Window>
#endif

bool WaylandOverlay::configure(OverlayWindow *win, const Config &cfg, QScreen *screen)
{
#ifdef SYSTEM_OVERLAY_WITH_LAYERSHELL
    auto *layer = LayerShellQt::Window::get(win);
    if (!layer)
        return false;

    using LSW = LayerShellQt::Window;

    // Real HUD: topmost layer, no reserved screen space, no keyboard, no focus.
    layer->setScope(QStringLiteral("system-overlay"));
    layer->setLayer(LSW::LayerOverlay);
    layer->setAnchors(LSW::Anchors(LSW::AnchorTop) | LSW::AnchorLeft);
    // -1 = ignore other surfaces' exclusive zones AND reserve no space, so the
    // overlay never pushes panels/windows around like a panel would.
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
    layer->setMargins(QMargins(cfg.offsetX(), cfg.offsetY(), 0, 0));
    layer->setCloseOnDismissed(false);
    layer->setActivateOnShow(false);

    if (screen)
        layer->setScreen(screen);

    return true;
#else
    Q_UNUSED(win);
    Q_UNUSED(cfg);
    Q_UNUSED(screen);
    return false;
#endif
}

void WaylandOverlay::applyScreen(OverlayWindow *win, QScreen *screen)
{
#ifdef SYSTEM_OVERLAY_WITH_LAYERSHELL
    if (auto *layer = LayerShellQt::Window::get(win)) {
        if (screen)
            layer->setScreen(screen);
    }
#else
    Q_UNUSED(win);
    Q_UNUSED(screen);
#endif
}
