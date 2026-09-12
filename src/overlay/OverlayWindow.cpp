#include "OverlayWindow.h"

#include <QExposeEvent>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr int kBarHeight = 3; // thin bar under each row
constexpr int kBarGap = 2;    // gap between text baseline block and bar
} // namespace

OverlayWindow::OverlayWindow(const RenderConfig &config, QWindow *parent)
    : QWindow(parent)
    , m_cfg(config)
{
    // Frameless, never in taskbar/Alt+Tab (Tool), no decorations.
    // Qt::WindowTransparentForInput is the important one: the Qt Wayland
    // platform then commits an EMPTY wl_surface input region, and the Qt xcb
    // platform sets an empty X11 input shape (via XFixes). Both happen at the
    // windowing-system level, so clicks pass through even onto applications
    // below the surface. Qt::WindowDoesNotAcceptFocus additionally clears the
    // X11 WM_HINTS input flag.
    setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput
             | Qt::WindowDoesNotAcceptFocus);

    setSurfaceType(QSurface::RasterSurface);
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8); // translucent background
    setFormat(fmt);

    m_backingStore = new QBackingStore(this);

    // Never expose a 0x0 layer surface: with a top+left-only anchor the
    // layer-shell spec requires a committed size > 0. Give the window its
    // placeholder size immediately, before the platform window exists.
    updateGeometry();
}

void OverlayWindow::setRenderConfig(const RenderConfig &config)
{
    m_cfg = config;
    updateGeometry();
    scheduleRender();
}

void OverlayWindow::setRows(const QList<HudRow> &rows)
{
    if (m_rows == rows)
        return;
    m_rows = rows;
    updateGeometry();
    scheduleRender();
}

QSize OverlayWindow::computeSizeHint() const
{
    const QFontMetrics fm(m_cfg.font);

    int widest = 0;
    for (const HudRow &row : m_rows)
        widest = std::max(widest, fm.horizontalAdvance(row.text));
    if (widest == 0)
        widest = fm.horizontalAdvance(QStringLiteral(" "));

    const qreal pad = m_cfg.outlineWidth / 2.0 + m_cfg.shadowOffset + 2.0;
    const int rowH = fm.height() + kBarHeight + kBarGap;
    const int count = m_rows.isEmpty() ? 1 : m_rows.size();
    return QSize(int(std::ceil(widest + 2 * pad)), int(std::ceil(count * rowH + 2 * pad)));
}

void OverlayWindow::updateGeometry()
{
    const QSize hint = computeSizeHint();
    if (size() != hint)
        resize(hint);
}

bool OverlayWindow::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::UpdateRequest:
        renderNow();
        return true;
    case QEvent::Resize:
        if (m_backingStore)
            m_backingStore->resize(size() * devicePixelRatio());
        scheduleRender();
        return true;
    default:
        return QWindow::event(event);
    }
}

void OverlayWindow::exposeEvent(QExposeEvent *event)
{
    Q_UNUSED(event);
    if (isExposed())
        renderNow();
}

void OverlayWindow::scheduleRender()
{
    if (m_renderPending)
        return;
    m_renderPending = true;
    requestUpdate();
}

void OverlayWindow::renderNow()
{
    if (!isExposed() || !m_backingStore)
        return;
    m_renderPending = false;

    static const bool perfTrace = qEnvironmentVariableIsSet("SYSTEM_OVERLAY_PERF");
    QElapsedTimer perfTimer;
    if (perfTrace)
        perfTimer.start();

    const QRect rect(QPoint(0, 0), size());
    const QSize physical = size() * devicePixelRatio();
    m_backingStore->resize(physical);

    // Re-rasterize only when the rendered content actually changed.
    QString cacheKey = QString::number(size().width()) + QLatin1Char('x')
        + QString::number(size().height()) + QLatin1Char('@')
        + QString::number(devicePixelRatio()) + QLatin1Char('#')
        + QString::number(m_cfg.showBackground);
    for (const HudRow &row : m_rows) {
        cacheKey += QLatin1Char('|') + row.text + QString::number(row.fraction, 'f', 3)
            + row.color.name();
    }

    if (m_textCache.isNull() || m_textCache.size() != physical || m_cacheKey != cacheKey) {
        renderTextToImage(m_textCache);
        m_cacheKey = cacheKey;
    }

    m_backingStore->beginPaint(rect);
    QPaintDevice *device = m_backingStore->paintDevice();
    QPainter p(device);
    p.drawImage(QPoint(0, 0), m_textCache);
    p.end();
    m_backingStore->endPaint();
    m_backingStore->flush(rect);

    if (perfTrace)
        fprintf(stderr, "render cost: %lld us\n", perfTimer.nsecsElapsed() / 1000);
}

void OverlayWindow::renderTextToImage(QImage &image)
{
    const qreal dpr = devicePixelRatio();
    image = QImage(size() * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter p(&image);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    if (m_cfg.showBackground) {
        const QRectF r(QPointF(0.5, 0.5), QSizeF(width() - 1.0, height() - 1.0));
        QPainterPath bg;
        bg.addRoundedRect(r, 5.0, 5.0);
        p.fillPath(bg, m_cfg.backgroundColor);
    }

    const QFontMetrics fm(m_cfg.font);
    const qreal pad = m_cfg.outlineWidth / 2.0 + m_cfg.shadowOffset + 2.0;
    const int rowH = fm.height() + kBarHeight + kBarGap;

    int widest = 0;
    for (const HudRow &row : m_rows)
        widest = std::max(widest, fm.horizontalAdvance(row.text));
    if (widest == 0)
        widest = fm.horizontalAdvance(QStringLiteral(" "));

    QPen outlinePen(m_cfg.outlineColor, m_cfg.outlineWidth);
    outlinePen.setJoinStyle(Qt::RoundJoin);
    outlinePen.setCapStyle(Qt::RoundCap);

    for (int i = 0; i < m_rows.size(); ++i) {
        const HudRow &row = m_rows.at(i);
        const qreal rowTop = pad + i * rowH;
        const qreal baseline = rowTop + fm.ascent();

        QPainterPath path;
        path.addText(QPointF(pad, baseline), m_cfg.font, row.text);

        // 1) offset shadow
        p.save();
        p.translate(m_cfg.shadowOffset, m_cfg.shadowOffset);
        p.setPen(Qt::NoPen);
        p.setBrush(m_cfg.shadowColor);
        p.drawPath(path);
        p.restore();

        // 2) dark outline
        p.strokePath(path, outlinePen);

        // 3) value colour fill (normal / warning / critical)
        p.fillPath(path, row.color.isValid() ? row.color : m_cfg.textColor);

        // 4) thin progress bar under the text (empty track when the metric
        //    has no percentage right now)
        const qreal barY = rowTop + fm.height() + kBarGap;
        p.fillRect(QRectF(pad, barY, widest, kBarHeight), m_cfg.shadowColor);
        if (row.fraction >= 0) {
            const double f = std::clamp(row.fraction, 0.0, 1.0);
            p.fillRect(QRectF(pad, barY, widest * f, kBarHeight),
                row.color.isValid() ? row.color : m_cfg.textColor);
        }
    }
    p.end();
}
