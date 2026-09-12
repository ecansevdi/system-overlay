#pragma once

#include <QBackingStore>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QWindow>

// The overlay surface itself: a frameless, input-transparent top-level
// QWindow painted with QBackingStore. Deliberately not a QWidget — a raw
// QWindow is lighter and, crucially, lets LayerShellQt attach its shell
// integration at platform-surface creation time (before any shell surface is
// committed), which is required for a real layer-shell overlay.
//
// Text is drawn with a dark outline plus a soft offset shadow so it stays
// readable on light and dark backgrounds alike.
class OverlayWindow : public QWindow
{
    Q_OBJECT
public:
    struct RenderConfig
    {
        QFont font;
        QColor textColor = QColor(0xa6, 0xf2, 0x8f); // light green
        QColor outlineColor = QColor(0, 0, 0, 230);
        QColor shadowColor = QColor(0, 0, 0, 110);
        qreal outlineWidth = 2.2;
        qreal shadowOffset = 1.4;
        bool showBackground = false;
        QColor backgroundColor = QColor(0, 0, 0, 115);
    };

    explicit OverlayWindow(const RenderConfig &config, QWindow *parent = nullptr);

    void setText(const QString &text);
    void setRenderConfig(const RenderConfig &config);

protected:
    bool event(QEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;

private:
    void renderNow();
    void renderTextToImage(QImage &image);
    void scheduleRender();
    QSize computeSizeHint() const;
    void updateGeometry();

    QBackingStore *m_backingStore = nullptr;
    QString m_text;
    RenderConfig m_cfg;

    // Cached rasterization of the current text: text rendering with an
    // outlined QPainterPath costs a few ms; redrawing happens at most once
    // per refresh, so we rasterize once and only blit on frame updates.
    QImage m_textCache;
    QString m_textCacheKey;
    bool m_renderPending = false;
};
