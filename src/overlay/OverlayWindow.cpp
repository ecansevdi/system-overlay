#include "OverlayWindow.h"

#include <QExposeEvent>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <cmath>
#include <cstdio>

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

void OverlayWindow::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    updateGeometry();
    scheduleRender();
}

QSize OverlayWindow::computeSizeHint() const
{
    const QFontMetrics fm(m_cfg.font);
    const QStringList lines = m_text.isEmpty() ? QStringList{QStringLiteral(" ")} : m_text.split(QLatin1Char('\n'));

    int widest = 0;
    for (const QString &line : lines)
        widest = std::max(widest, fm.horizontalAdvance(line));

    const qreal pad = m_cfg.outlineWidth / 2.0 + m_cfg.shadowOffset + 2.0;
    return QSize(int(std::ceil(widest + 2 * pad)), int(std::ceil(lines.size() * fm.height() + 2 * pad)));
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

    // Re-rasterize the text only when its inputs changed.
    const QString cacheKey = m_text +QLatin1Char('|') + QString::number(size().width())
        + QLatin1Char('x') + QString::number(size().height()) + QLatin1Char('@')
        + QString::number(devicePixelRatio());
    if (m_textCache.isNull() || m_textCache.size() != physical || m_textCacheKey != cacheKey) {
        renderTextToImage(m_textCache);
        m_textCacheKey = cacheKey;
    }

    m_backingStore->beginPaint(rect);
    QPaintDevice *device = m_backingStore->paintDevice();
    QPainter p(device);
    p.drawImage(QPoint(0, 0), m_textCache);
    p.end();
    m_backingStore->endPaint();
    m_backingStore->flush(rect);

    if (perfTrace)
        fprintf(stderr, "render cost: %lld us | win dpr=%.2f screen dpr=%.2f geom=%dx%d\n",
            perfTimer.nsecsElapsed() / 1000, devicePixelRatio(),
            screen() ? screen()->devicePixelRatio() : -1.0, width(), height());
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

    const QStringList lines = m_text.isEmpty() ? QStringList{QStringLiteral(" ")} : m_text.split(QLatin1Char('\n'));
    const QFontMetrics fm(m_cfg.font);
    const qreal pad = m_cfg.outlineWidth / 2.0 + m_cfg.shadowOffset + 2.0;

    QPen outlinePen(m_cfg.outlineColor, m_cfg.outlineWidth);
    outlinePen.setJoinStyle(Qt::RoundJoin);
    outlinePen.setCapStyle(Qt::RoundCap);

    for (int i = 0; i < lines.size(); ++i) {
        const qreal baseline = pad + i * fm.height() + fm.ascent();

        QPainterPath path;
        path.addText(QPointF(pad, baseline), m_cfg.font, lines.at(i));

        // 1) offset shadow
        p.save();
        p.translate(m_cfg.shadowOffset, m_cfg.shadowOffset);
        p.setPen(Qt::NoPen);
        p.setBrush(m_cfg.shadowColor);
        p.drawPath(path);
        p.restore();

        // 2) dark outline
        p.strokePath(path, outlinePen);

        // 3) light text fill
        p.fillPath(path, m_cfg.textColor);
    }
    p.end();
}
