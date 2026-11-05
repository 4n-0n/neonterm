#include "ui/TermView.h"

#include "core/Session.h"
#include "ui/MediaStore.h"
#include "ui/Theme.h"

#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

QRgb lightenRgb(QRgb c, int amount)
{
    QColor q(c);
    return q.lighter(100 + amount).rgb();
}

QRgb darkenRgb(QRgb c, int amount)
{
    QColor q(c);
    return q.darker(100 + amount).rgb();
}

int styleIndex(const Cell &c)
{
    int i = 0;
    if (c.flags & CF_Bold) i |= 1;
    if (c.flags & CF_Italic) i |= 2;
    if (c.flags & CF_Underline) i |= 4;
    if (c.flags & CF_Strike) i |= 8;
    return i;
}

} // namespace

TermView::TermView(QWidget *parent)
    : TermSurface(parent)
{
}

void TermView::paintEvent(QPaintEvent *)
{
    countFrame();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const theme::Look &lk = theme::look();

    // background: vertical ramp + a faint cold glow in the upper-left corner
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor(0x07, 0x0b, 0x12));
    bg.setColorAt(1.0, QColor(0x04, 0x06, 0x09));
    p.fillRect(rect(), bg);
    {
        QRadialGradient glow(QPointF(width() * 0.15, 0), width() * 1.1);
        QColor c = lk.accent;
        c.setAlpha(16);
        glow.setColorAt(0.0, c);
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(rect(), glow);
    }

    if (m_sess) {
        if (m_dirty || m_text.size() != size() * devicePixelRatioF() || m_text.isNull())
            rebuildLayers();

        drawCellBackgrounds(p);
        drawSelection(p);
        drawMedia(p);

        if (!m_text.isNull())
            p.drawImage(0, 0, m_text);
        drawCursor(p);
    }

    drawScrollbar(p);
    drawFrameFx(p);
}

void TermView::rebuildLayers()
{
    m_dirty = false;
    if (!m_sess || m_cols < 2 || m_rows < 2)
        return;

    const qreal dpr = devicePixelRatioF();
    const QSize pxSize(int(std::ceil(width() * dpr)), int(std::ceil(height() * dpr)));
    if (pxSize.width() < 2 || pxSize.height() < 2)
        return;
    if (m_text.size() != pxSize) {
        m_text = QImage(pxSize, QImage::Format_ARGB32_Premultiplied);
        m_text.setDevicePixelRatio(dpr);
    }
    m_text.fill(Qt::transparent);
    m_layerSize = pxSize;

    const Screen &scr = m_sess->screen();
    const Palette &pal = scr.palette();
    const int hist = scr.historySize();
    const int topAbs = hist - m_scrollOffset;

    QPainter p(&m_text);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::Antialiasing, false);

    auto effectiveFg = [&pal](const Cell &k) -> QRgb {
        QRgb fg = k.fg ? k.fg : pal.fg;
        if (k.flags & CF_Reverse)
            fg = k.bg ? k.bg : pal.bg;
        if (!(k.flags & CF_Reverse) && k.fg == 0 && (k.flags & CF_Bold))
            fg = lightenRgb(fg, 16);
        if (k.flags & CF_Reverse && k.fg == 0 && (k.flags & CF_Bold))
            fg = lightenRgb(fg, 12);
        if (k.flags & CF_Dim)
            fg = darkenRgb(fg, 45);
        return fg;
    };

    for (int r = 0; r < m_rows; ++r) {
        const int abs = topAbs + r;
        if (abs < 0 || abs >= hist + scr.rows())
            continue;
        const std::vector<Cell> &cells = scr.absLine(abs);
        const int limit = std::min<int>(m_cols, int(cells.size()));
        const qreal baseline = m_padY + r * m_cellH + m_ascent;
        int c = 0;
        while (c < limit) {
            const Cell &cell = cells[size_t(c)];
            if (cell.flags & CF_Cont) {
                ++c;
                continue;
            }
            const bool wide = (c + 1 < limit) && (cells[size_t(c + 1)].flags & CF_Cont);
            if (cell.blank() && !wide) {
                ++c;
                continue;
            }
            const int style = styleIndex(cell);
            const QRgb fg = effectiveFg(cell);

            if (wide) {
                p.setFont(m_fonts[size_t(style)]);
                p.setPen(QColor(fg));
                p.drawText(QPointF(m_padX + c * m_cellW, baseline),
                           QString::fromUcs4(&cell.ch, 1));
                c += 2;
                continue;
            }

            const int start = c;
            std::u32string buf;
            while (c < limit) {
                const Cell &k = cells[size_t(c)];
                if (k.flags & CF_Cont)
                    break;
                if (c + 1 < limit && (cells[size_t(c + 1)].flags & CF_Cont))
                    break;
                if (styleIndex(k) != style || effectiveFg(k) != fg)
                    break;
                buf.push_back(k.ch);
                ++c;
            }
            if (buf.empty()) {
                ++c;
                continue;
            }
            p.setFont(m_fonts[size_t(style)]);
            p.setPen(QColor(fg));
            const QString s = QString::fromUcs4(buf.data(), int(buf.size()));
            p.drawText(QPointF(m_padX + start * m_cellW, baseline), s);
        }
    }
    p.end();

}

void TermView::drawCellBackgrounds(QPainter &p)
{
    const Screen &scr = m_sess->screen();
    const Palette &pal = scr.palette();
    const int hist = scr.historySize();
    const int topAbs = hist - m_scrollOffset;

    for (int r = 0; r < m_rows; ++r) {
        const int abs = topAbs + r;
        if (abs < 0 || abs >= hist + scr.rows())
            continue;
        const std::vector<Cell> &cells = scr.absLine(abs);
        const int limit = std::min<int>(m_cols, int(cells.size()));
        int c = 0;
        while (c < limit) {
            const Cell &cell = cells[size_t(c)];
            const bool rev = (cell.flags & CF_Reverse) != 0;
            const QRgb bg = cell.bg;
            if (bg == 0 && !rev) {
                ++c;
                continue;
            }
            const QRgb col = rev ? (cell.fg ? cell.fg : pal.fg) : bg;
            const int start = c;
            while (c < limit) {
                const Cell &k = cells[size_t(c)];
                if (k.bg != bg || ((k.flags & CF_Reverse) != 0) != rev)
                    break;
                ++c;
            }
            p.fillRect(QRectF(m_padX + start * m_cellW, m_padY + r * m_cellH,
                              (c - start) * m_cellW, m_cellH),
                       QColor(col));
        }
    }
}

void TermView::drawMedia(QPainter &p)
{
    const QVector<MediaUi> list = mediaUi();
    if (list.isEmpty() || !m_sess || !m_store)
        return;

    p.save();
    p.setClipRect(QRectF(m_padX, m_padY, m_cols * m_cellW, m_rows * m_cellH));
    for (const MediaUi &ui : list) {
        const MediaItem *item = nullptr;
        for (const MediaItem &m : m_sess->screen().media())
            if (m.id == ui.id) {
                item = &m;
                break;
            }
        if (!item)
            continue;

        const QImage img = m_store->frame(*item);
        if (!img.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(ui.media, img);
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.setPen(QPen(QColor(0x1b, 0x3a, 0x4c), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(ui.media.adjusted(-0.5, -0.5, 0.5, 0.5));
        }

        const QImage bar = mediaBarImage(ui);
        if (!bar.isNull())
            p.drawImage(ui.bar.topLeft(), bar);
        const QImage close = mediaCloseImage(ui);
        if (!close.isNull())
            p.drawImage(ui.close.topLeft(), close);
    }
    p.restore();
}

void TermView::drawSelection(QPainter &p)
{
    if (!m_hasSel || !m_sess)
        return;
    Hit a = m_selA;
    Hit b = m_selB;
    if (a.abs > b.abs || (a.abs == b.abs && a.col > b.col))
        std::swap(a, b);
    const int hist = m_sess->screen().historySize();
    const int topAbs = hist - m_scrollOffset;

    QColor sel(theme::look().accent);
    sel.setAlpha(60);
    const int pad = int(std::ceil(m_cellH * 0.12));

    for (int abs = std::max(a.abs, topAbs); abs <= std::min(b.abs, topAbs + m_rows - 1); ++abs) {
        const int r = abs - topAbs;
        int c0 = (abs == a.abs) ? a.col : 0;
        int c1 = (abs == b.abs) ? b.col : m_cols - 1;
        if (c1 < c0)
            continue;
        p.fillRect(QRectF(m_padX + c0 * m_cellW, m_padY + r * m_cellH + 1,
                          (c1 - c0 + 1) * m_cellW, m_cellH - pad),
                   sel);
    }
}

void TermView::drawCursor(QPainter &p)
{
    if (!m_sess || m_scrollOffset > 0)
        return;
    const Screen &scr = m_sess->screen();
    if (!scr.cursorVisible())
        return;
    const Pos cur = scr.cursor();
    if (cur.y < 0 || cur.y >= m_rows || cur.x < 0 || cur.x >= m_cols)
        return;

    const bool focused = hasFocus();
    QColor c = theme::look().accent;
    c.setAlpha(focused ? (m_cursorOn ? 235 : 90) : 70);
    const qreal x = m_padX + cur.x * m_cellW;
    const qreal y = m_padY + cur.y * m_cellH;
    const int style = scr.cursorStyle();

    p.setRenderHint(QPainter::Antialiasing, true);
    if (style == 1) {
        p.fillRect(QRectF(x, y + m_cellH - 3, m_cellW, 3), c);
    } else if (style == 2) {
        p.fillRect(QRectF(x, y, 2.5, m_cellH), c);
    } else {
        QColor fill = c;
        fill.setAlpha(focused ? (m_cursorOn ? 55 : 24) : 18);
        p.fillRect(QRectF(x, y + 1, m_cellW, m_cellH - 2), fill);
        QPen pen(c);
        pen.setWidthF(1.2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(x + 0.5, y + 1.5, m_cellW - 1, m_cellH - 3));
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void TermView::drawScrollbar(QPainter &p)
{
    if (!m_sess)
        return;
    const int hist = m_sess->screen().historySize();
    if (hist <= 0)
        return;
    const qreal trackX = width() - 7;
    const qreal trackY = m_padY;
    const qreal trackH = m_rows * m_cellH;
    p.fillRect(QRectF(trackX, trackY, 2, trackH), QColor(0x10, 0x1c, 0x26));
    const qreal total = hist + m_rows;
    const qreal thumbH = std::max<qreal>(18, trackH * m_rows / total);
    const qreal maxTop = trackH - thumbH;
    const qreal progress = hist > 0 ? qreal(hist - m_scrollOffset) / qreal(hist) : 1.0;
    p.fillRect(QRectF(trackX, trackY + maxTop * progress, 2, thumbH),
               QColor(theme::look().accent.red(), theme::look().accent.green(),
                      theme::look().accent.blue(), 150));
}

void TermView::drawFrameFx(QPainter &p)
{
    const theme::Look &lk = theme::look();
    if (m_crt) {
        // scanlines: one dark line every three physical pixels
        const qreal step = 3.0 / devicePixelRatioF();
        QColor line(0, 0, 0, 26);
        for (qreal y = m_padY; y < height(); y += step)
            p.fillRect(QRectF(0, y, width(), std::max<qreal>(1.0, 1.0 / devicePixelRatioF())), line);

        // vignette
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0.0, QColor(0, 0, 0, 40));
        g.setColorAt(0.18, QColor(0, 0, 0, 0));
        g.setColorAt(0.82, QColor(0, 0, 0, 0));
        g.setColorAt(1.0, QColor(0, 0, 0, 60));
        p.fillRect(rect(), g);
    }

    // frame: 1px low-alpha border + corner ticks
    QColor b = lk.border;
    p.setPen(QPen(b, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, width() - 1, height() - 1));

    QColor a = lk.accent;
    a.setAlpha(150);
    p.setPen(QPen(a, 1.5));
    const qreal t = 14;
    p.drawLine(QPointF(0.5, 0.5), QPointF(t, 0.5));
    p.drawLine(QPointF(0.5, 0.5), QPointF(0.5, t));
    p.drawLine(QPointF(width() - t, 0.5), QPointF(width() - 0.5, 0.5));
    p.drawLine(QPointF(width() - 0.5, 0.5), QPointF(width() - 0.5, t));
    p.drawLine(QPointF(0.5, height() - t), QPointF(0.5, height() - 0.5));
    p.drawLine(QPointF(0.5, height() - 0.5), QPointF(t, height() - 0.5));
    p.drawLine(QPointF(width() - 0.5, height() - t), QPointF(width() - 0.5, height() - 0.5));
    p.drawLine(QPointF(width() - t, height() - 0.5), QPointF(width() - 0.5, height() - 0.5));
}
