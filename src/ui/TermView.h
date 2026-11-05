#pragma once

#include "ui/TermSurface.h"

#include <QImage>

// TermView: the CPU renderer. Glyphs go into an offscreen layer that is blurred
// once per change and composited additively — this is the fallback when OpenGL
// is not available, and the reference for how the GL output should look.
class TermView : public TermSurface
{
    Q_OBJECT
public:
    explicit TermView(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    void rebuildLayers();
    void drawCellBackgrounds(QPainter &p);
    void drawSelection(QPainter &p);
    void drawMedia(QPainter &p);
    void drawCursor(QPainter &p);
    void drawScrollbar(QPainter &p);
    void drawFrameFx(QPainter &p);

    QImage m_text;
    QImage m_bloomImg;
    QSize m_layerSize;
};
