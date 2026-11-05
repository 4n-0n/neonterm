#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QFont>
#include <QVector>
#include <QWidget>

class Session;
class QTimer;
class MediaStore;
struct Palette;
struct MediaItem;

// TermSurface: everything the two renderers share — the real PTY session, the
// cell metrics, the grid layout negotiated with the child, the scrollback view,
// the selection model and the keyboard/mouse mapping. Subclasses only draw.
class TermSurface : public QWidget
{
    Q_OBJECT
public:
    explicit TermSurface(QWidget *parent = nullptr);
    ~TermSurface() override;

    void setSession(Session *s);
    Session *session() const { return m_sess; }

    void setFontPixelSize(int px);
    int fontPixelSize() const { return m_fontPx; }

    void setCrtEnabled(bool on);
    bool crtEnabled() const { return m_crt; }

    // Where the media blocks are, in logical pixels, plus the little controls
    // drawn over them. Shared by both renderers and by the mouse hit test, so
    // what you click is exactly what you see.
    struct MediaUi {
        int id = 0;
        QRectF media;      // fitted image rect
        QRectF close;      // close button
        QRectF bar;        // control strip (empty when there is no player)
        bool hasPlayer = false;
    };
    QVector<MediaUi> mediaUi() const;
    QImage mediaBarImage(const MediaUi &ui);
    QImage mediaCloseImage(const MediaUi &ui);
    MediaStore *mediaStore() const { return m_store; }
    void closeMedia(int id);
    void closeAllMedia();

    bool hasSelection() const { return m_hasSel; }
    void copySelection();
    void pasteClipboard();
    void clearSelection();
    void scrollByLines(int lines);
    int scrollOffset() const { return m_scrollOffset; }
    QSize gridSize() const { return QSize(m_cols, m_rows); }

    // subclasses call this at the top of every paint
    void countFrame() { ++m_frames; }

    // immutable snapshot of what a renderer needs, so a renderer that is not a
    // TermSurface subclass (the GL canvas is a child QOpenGLWidget) can draw
    struct PaintState {
        Session *session = nullptr;
        const Palette *palette = nullptr;
        qreal cellW = 8, cellH = 18, ascent = 14, padX = 16, padY = 14;
        int cols = 80, rows = 24, scrollOffset = 0, fontPx = 14;
        bool hasSelection = false;
        int selAabs = 0, selAcol = 0, selBabs = 0, selBcol = 0;
        bool cursorOn = true;
        bool crt = true;
        const std::vector<MediaItem> *media = nullptr;
        MediaStore *store = nullptr;
    };
    PaintState paintState() const;
    bool needsRepaint() const { return m_dirty; }
    void markPainted() { m_dirty = false; }

signals:
    void fpsChanged(double fps);
    void titleChanged(const QString &title);
    void bell();
    void contextMenuRequested(const QPoint &globalPos);
    void gridChanged(const QSize &grid);

protected:
    struct Hit {
        int abs = 0;
        int col = 0;
        bool valid = false;
    };

    void layoutGrid();
    int mediaTopAbs() const;
    QRectF mediaRectPx(const MediaItem &item, int topAbs) const;
    bool handleMediaPress(const QPoint &pos);
    Hit hitTest(const QPoint &pos) const;
    QString selectionText() const;
    void sendMouse(QMouseEvent *e, const QString &kind);
    void sendWheelMouse(const QPoint &pos, int steps);
    void resetBlink();
    void setDirty() { m_dirty = true; requestPaint(); }
    virtual void requestPaint() { update(); }

    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void contextMenuEvent(QContextMenuEvent *e) override;

    // shared state, read by the renderers
    Session *m_sess = nullptr;
    int m_fontPx = 14;
    bool m_crt = true;
    QVector<QFont> m_fonts;
    qreal m_cellW = 8;
    qreal m_cellH = 18;
    qreal m_ascent = 14;
    qreal m_padX = 16;
    qreal m_padY = 14;
    int m_cols = 80;
    int m_rows = 24;
    int m_scrollOffset = 0;
    bool m_selecting = false;
    bool m_hasSel = false;
    Hit m_selA;
    Hit m_selB;
    bool m_cursorOn = true;
    bool m_dirty = true;
    MediaStore *m_store = nullptr;
    int m_seekId = -1;      // dragging inside a progress bar
    int m_hoverMedia = -1;  // block under the pointer
    QHash<int, QPair<quint64, QImage>> m_barCache;
    QHash<int, QPair<quint64, QImage>> m_closeCache;

private:
    void buildFonts();
    void emitBellIfNeeded();

    QTimer *m_blink = nullptr;
    QTimer *m_fpsTimer = nullptr;
    qint64 m_frames = 0;
    QElapsedTimer m_fpsClock;
    qreal m_wheelAcc = 0;
};
