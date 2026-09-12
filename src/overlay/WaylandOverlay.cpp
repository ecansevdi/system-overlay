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

    // Real HUD: topmost layer, no keyboard, no focus. Anchors and margins
    // place it in the configured corner; offsets are measured from the
    // anchored edges.
    LSW::Anchors anchors;
    QMargins margins(0, 0, 0, 0);
    switch (cfg.position()) {
    case Config::Position::TopLeft:
        anchors = LSW::Anchors(LSW::AnchorTop) | LSW::AnchorLeft;
        margins = QMargins(cfg.offsetX(), cfg.offsetY(), 0, 0);
        break;
    case Config::Position::TopRight:
        anchors = LSW::Anchors(LSW::AnchorTop) | LSW::AnchorRight;
        margins = QMargins(0, cfg.offsetY(), cfg.offsetX(), 0);
        break;
    case Config::Position::BottomLeft:
        anchors = LSW::Anchors(LSW::AnchorBottom) | LSW::AnchorLeft;
        margins = QMargins(cfg.offsetX(), 0, 0, cfg.offsetY());
        break;
    case Config::Position::BottomRight:
        anchors = LSW::Anchors(LSW::AnchorBottom) | LSW::AnchorRight;
        margins = QMargins(0, 0, cfg.offsetX(), cfg.offsetY());
        break;
    }

    layer->setScope(QStringLiteral("system-overlay"));
    layer->setLayer(LSW::LayerOverlay);
    layer->setAnchors(anchors);
    // The HUD never reserves screen space. Top corners ignore panels
    // entirely (-1); bottom corners additionally stay clear of bottom
    // panels (0 = respect other surfaces' exclusive zones without
    // reserving any space itself).
    layer->setExclusiveZone(cfg.bottomAnchored() ? 0 : -1);
    layer->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
    layer->setMargins(margins);
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
