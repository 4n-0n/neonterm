#pragma once

#include "ui/TermSurface.h"

#include <QString>

// GlTermView: the GPU renderer. It is a plain TermSurface — so the real PTY, the
// selection model and the keys are shared with the CPU renderer — that hosts a
// QOpenGLWidget child. The child only draws: input and focus stay here.
//
// The canvas draws one instanced quad pass for backgrounds, one for glyphs
// sampled from a rasterised font atlas, then post passes for bloom, scanlines
// and vignette.
class GlTermView : public TermSurface
{
    Q_OBJECT
public:
    explicit GlTermView(QWidget *parent = nullptr);
    ~GlTermView() override;

    QString rendererInfo() const;
    bool glReady() const;
    QImage grabGlFrame() const;
    int lastFrameUs() const;
    void setStrokeFont(bool on);
    bool strokeFont() const;

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void requestPaint() override;

private:
    class Canvas;
    Canvas *m_canvas = nullptr;
};
