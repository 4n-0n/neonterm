#include "ui/TermSurface.h"

#include "core/Session.h"
#include "ui/MediaOverlay.h"
#include "ui/MediaStore.h"
#include "ui/Theme.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QSet>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

TermSurface::TermSurface(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    m_padX = 16 * theme::uiScale();
    m_padY = 14 * theme::uiScale();
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::IBeamCursor);
    setMouseTracking(true);
    buildFonts();

    m_store = new MediaStore(this);
    connect(m_store, &MediaStore::changed, this, [this] { update(); });

    m_blink = new QTimer(this);
    m_blink->setInterval(530);
    connect(m_blink, &QTimer::timeout, this, [this] {
        m_cursorOn = !m_cursorOn;
        requestPaint();
    });
    m_blink->start();

    m_fpsTimer = new QTimer(this);
    m_fpsTimer->setInterval(1000);
    connect(m_fpsTimer, &QTimer::timeout, this, [this] {
        const qint64 ms = std::max<qint64>(1, m_fpsClock.restart());
        emit fpsChanged(double(m_frames) * 1000.0 / double(ms));
        m_frames = 0;
    });
    m_fpsTimer->start();
    m_fpsClock.start();
}

TermSurface::~TermSurface() = default;

void TermSurface::buildFonts()
{
    m_fonts.clear();
    m_fonts.reserve(16);
    const QFont base = theme::mono(m_fontPx);
    for (int i = 0; i < 16; ++i) {
        QFont f = base;
        if (i & 1)
            f.setWeight(QFont::DemiBold);
        f.setItalic(i & 2);
        f.setUnderline(i & 4);
        f.setStrikeOut(i & 8);
        m_fonts.push_back(f);
    }
}

void TermSurface::setSession(Session *s)
{
    if (m_sess == s)
        return;
    if (m_sess)
        disconnect(m_sess, nullptr, this, nullptr);
    m_sess = s;
    m_scrollOffset = 0;
    clearSelection();
    m_dirty = true;
    if (m_store)
        m_store->clear();
    if (m_sess) {
        connect(m_sess, &Session::updated, this, [this] {
            if (m_scrollOffset > 0)
                m_scrollOffset = std::min(m_scrollOffset, m_sess->screen().historySize());
            m_dirty = true;
            emitBellIfNeeded();
            if (m_store) {
                QSet<int> alive;
                for (const MediaItem &m : m_sess->screen().media())
                    alive.insert(m.id);
                m_store->retain(alive);
            }
            requestPaint();
        });
        connect(m_sess, &Session::titleChanged, this, &TermSurface::titleChanged);
        connect(m_sess, &Session::mediaAdded, this, [this](const MediaItem &item) {
            if (m_store)
                m_store->add(item);
            update();
        });
        connect(m_sess, &Session::mediaStopped, this, [this] {
            if (m_store)
                m_store->clear();
        });
        layoutGrid();
    }
    requestPaint();
}

void TermSurface::emitBellIfNeeded()
{
    if (m_sess && m_sess->screen().takeBell())
        emit bell();
}

void TermSurface::setFontPixelSize(int px)
{
    px = std::clamp(px, 6, 72);
    if (px == m_fontPx)
        return;
    m_fontPx = px;
    buildFonts();
    layoutGrid();
    requestPaint();
}

void TermSurface::setCrtEnabled(bool on)
{
    if (m_crt == on)
        return;
    m_crt = on;
    requestPaint();
}

void TermSurface::layoutGrid()
{
    const QFontMetricsF fm(m_fonts.value(0));
    // snap the cell to a whole number of physical pixels: at 1.4x a fractional
    // pitch is what made every glyph look resampled
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    const qreal rawW = std::max<qreal>(4.0, fm.horizontalAdvance(QLatin1Char('M')));
    const qreal rawH = std::max<qreal>(6.0, std::ceil(fm.height() + 1.0));
    m_cellW = std::round(rawW * dpr) / dpr;
    m_cellH = std::round(rawH * dpr) / dpr;
    m_ascent = fm.ascent();

    const int cols = int(std::floor((double(width()) - 2.0 * m_padX) / double(m_cellW)));
    const int rows = int(std::floor((double(height()) - 2.0 * m_padY) / double(m_cellH)));
    const bool changed = (m_cols != std::max(2, cols)) || (m_rows != std::max(2, rows));
    m_cols = std::max(2, cols);
    m_rows = std::max(2, rows);
    m_dirty = true;
    if (m_sess)
        m_sess->resize(m_cols, m_rows);
    if (changed)
        emit gridChanged(QSize(m_cols, m_rows));
}

void TermSurface::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutGrid();
}

void TermSurface::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    setFocus(Qt::OtherFocusReason);
    layoutGrid();
}

void TermSurface::focusInEvent(QFocusEvent *e)
{
    QWidget::focusInEvent(e);
    m_cursorOn = true;
    requestPaint();
}

void TermSurface::focusOutEvent(QFocusEvent *e)
{
    QWidget::focusOutEvent(e);
    requestPaint();
}

TermSurface::PaintState TermSurface::paintState() const
{
    PaintState st;
    st.session = m_sess;
    st.cellW = m_cellW;
    st.cellH = m_cellH;
    st.ascent = m_ascent;
    st.padX = m_padX;
    st.padY = m_padY;
    st.cols = m_cols;
    st.rows = m_rows;
    st.scrollOffset = m_scrollOffset;
    st.fontPx = m_fontPx;
    st.hasSelection = m_hasSel;
    st.selAabs = m_selA.abs;
    st.selAcol = m_selA.col;
    st.selBabs = m_selB.abs;
    st.selBcol = m_selB.col;
    st.cursorOn = m_cursorOn;
    st.crt = m_crt;
    st.media = m_sess ? &m_sess->screen().media() : nullptr;
    st.store = m_store;
    if (m_sess)
        st.palette = &m_sess->screen().palette();
    return st;
}


int TermSurface::mediaTopAbs() const
{
    if (!m_sess)
        return 0;
    return m_sess->screen().historySize() - m_scrollOffset;
}

QRectF TermSurface::mediaRectPx(const MediaItem &item, int topAbs) const
{
    const QRectF cell(m_padX + item.col * m_cellW, m_padY + (item.abs - topAbs) * m_cellH,
                      item.cols * m_cellW, item.rows * m_cellH);
    QSize src = m_store ? m_store->sourceSize(item) : QSize();
    if (src.width() <= 0 || src.height() <= 0) {
        const QImage img = m_store ? m_store->frame(item) : QImage();
        src = img.size();
    }
    if (src.width() <= 0 || src.height() <= 0)
        src = QSize(16, 9);
    const double s = std::min(cell.width() / src.width(), cell.height() / src.height());
    return QRectF(cell.center().x() - src.width() * s * 0.5,
                  cell.center().y() - src.height() * s * 0.5,
                  src.width() * s, src.height() * s);
}

QVector<TermSurface::MediaUi> TermSurface::mediaUi() const
{
    QVector<MediaUi> out;
    if (!m_sess || !m_store)
        return out;
    const int topAbs = mediaTopAbs();
    const qreal S = theme::uiScale();
    for (const MediaItem &it : m_sess->screen().media()) {
        if (it.abs + it.rows <= topAbs || it.abs >= topAbs + m_rows)
            continue;
        MediaUi ui;
        ui.id = it.id;
        ui.media = mediaRectPx(it, topAbs);
        if (ui.media.width() < 40 || ui.media.height() < 24)
            continue;
        const qreal cs = std::min<qreal>(24 * S, ui.media.height() * 0.18);
        ui.close = QRectF(ui.media.right() - cs - 5 * S, ui.media.top() + 5 * S, cs, cs);
        if (qEnvironmentVariableIsSet("NEONTERM_DEBUG_MEDIA"))
            qInfo("media %d: cell=%d,%d %dx%d src=%dx%d rect=%.1f,%.1f %.1fx%.1f",
                  it.id, it.col, it.abs, it.cols, it.rows, m_store->sourceSize(it).width(),
                  m_store->sourceSize(it).height(), ui.media.x(), ui.media.y(),
                  ui.media.width(), ui.media.height());
        ui.hasPlayer = m_store->hasPlayer(it);
        if (ui.hasPlayer) {
            const qreal bh = std::min<qreal>(34 * S, ui.media.height() * 0.45);
            ui.bar = QRectF(ui.media.left(), ui.media.bottom() - bh, ui.media.width(), bh);
        }
        out.push_back(ui);
    }
    return out;
}

QImage TermSurface::mediaBarImage(const MediaUi &ui)
{
    if (!ui.hasPlayer || ui.bar.isEmpty())
        return QImage();
    const MediaItem *item = nullptr;
    if (m_sess) {
        for (const MediaItem &m : m_sess->screen().media())
            if (m.id == ui.id) {
                item = &m;
                break;
            }
    }
    if (!item || !m_store)
        return QImage();

    const MediaStore::State st = m_store->state(*item);
    const double progress = st.duration > 0 ? double(st.position) / double(st.duration) : 0.0;
    auto clock = [](qint64 ms) {
        const int total = int(ms / 1000);
        return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
    };

    const qreal dpr = devicePixelRatioF();
    const int wPx = std::max(1, int(std::lround(ui.bar.width() * dpr)));
    const int hPx = std::max(1, int(std::lround(ui.bar.height() * dpr)));
    // a bar is rebuilt only when what it shows changes
    const quint64 key = (quint64(st.playing) << 40) | (quint64(st.muted) << 39)
                        | (quint64(st.failed) << 38)
                        | (quint64(std::llround(progress * 200)) << 20)
                        | (quint64(wPx) << 10) | quint64(hPx);
    const auto it = m_barCache.constFind(ui.id);
    if (it != m_barCache.constEnd() && it->first == key && !it->second.isNull())
        return it->second;

    overlay::BarState bs;
    bs.playing = st.playing;
    bs.muted = st.muted;
    bs.failed = st.failed;
    bs.progress = progress;
    bs.time = QStringLiteral("%1 / %2").arg(clock(st.position), clock(st.duration));
    QImage img = overlay::bar(wPx, hPx, dpr, bs);
    m_barCache.insert(ui.id, {key, img});
    return img;
}

QImage TermSurface::mediaCloseImage(const MediaUi &ui)
{
    if (ui.close.isEmpty())
        return QImage();
    const qreal dpr = devicePixelRatioF();
    const int px = std::max(8, int(std::lround(ui.close.width() * dpr)));
    const quint64 key = quint64(px) | (quint64(m_hoverMedia == ui.id) << 40);
    const auto it = m_closeCache.constFind(ui.id);
    if (it != m_closeCache.constEnd() && it->first == key && !it->second.isNull())
        return it->second;
    QImage img = overlay::closeButton(px, dpr, m_hoverMedia == ui.id);
    m_closeCache.insert(ui.id, {key, img});
    return img;
}

void TermSurface::closeMedia(int id)
{
    if (!m_sess)
        return;
    m_sess->screen().removeMedia(id);
    if (m_store)
        m_store->forget(id);
    m_barCache.remove(id);
    m_closeCache.remove(id);
    if (m_hoverMedia == id)
        m_hoverMedia = -1;
    setDirty();
}

void TermSurface::closeAllMedia()
{
    if (!m_sess)
        return;
    const int hovered = m_hoverMedia;
    for (const MediaItem &it : m_sess->screen().media())
        closeMedia(it.id);
    m_hoverMedia = hovered;
}

bool TermSurface::handleMediaPress(const QPoint &pos)
{
    if (!m_sess || !m_store)
        return false;
    const QVector<MediaUi> list = mediaUi();
    // last drawn is on top, so search backwards
    for (int i = list.size() - 1; i >= 0; --i) {
        const MediaUi &ui = list.at(i);
        if (!ui.media.contains(pos))
            continue;
        if (ui.close.adjusted(-3, -3, 3, 3).contains(pos)) {
            closeMedia(ui.id);
            return true;
        }
        if (ui.hasPlayer && ui.bar.contains(pos)) {
            const MediaItem *item = nullptr;
            for (const MediaItem &m : m_sess->screen().media())
                if (m.id == ui.id) {
                    item = &m;
                    break;
                }
            if (!item)
                return true;
            const qreal x = pos.x() - ui.bar.left();
            const qreal muteZone = ui.bar.width() - 40;
            if (x < 38)
                m_store->togglePlay(*item);
            else if (x > muteZone)
                m_store->toggleMute(*item);
            else {
                m_seekId = ui.id;
                const double ratio = std::clamp((x - 44) / std::max<qreal>(1, ui.bar.width() - 104),
                                                0.0, 1.0);
                m_store->seek(*item, ratio);
            }
            setDirty();
            return true;
        }
        return true;   // a click on the image must not start a text selection
    }
    return false;
}

void TermSurface::resetBlink()
{
    m_cursorOn = true;
    m_blink->start();
}

TermSurface::Hit TermSurface::hitTest(const QPoint &pos) const
{
    Hit h;
    if (!m_sess)
        return h;
    int col = int(std::floor((pos.x() - m_padX) / m_cellW));
    int row = int(std::floor((pos.y() - m_padY) / m_cellH));
    col = std::clamp(col, 0, m_cols - 1);
    row = std::clamp(row, 0, m_rows - 1);
    h.col = col;
    h.abs = m_sess->screen().historySize() - m_scrollOffset + row;
    h.valid = true;
    return h;
}

QString TermSurface::selectionText() const
{
    if (!m_hasSel || !m_sess)
        return QString();
    Hit a = m_selA;
    Hit b = m_selB;
    if (a.abs > b.abs || (a.abs == b.abs && a.col > b.col))
        std::swap(a, b);
    return m_sess->screen().textRange(a.abs, a.col, b.abs, b.col);
}

void TermSurface::copySelection()
{
    const QString s = selectionText();
    if (s.isEmpty())
        return;
    QClipboard *cb = QGuiApplication::clipboard();
    cb->setText(s, QClipboard::Clipboard);
    if (cb->supportsSelection())
        cb->setText(s, QClipboard::Selection);
}

void TermSurface::pasteClipboard()
{
    if (!m_sess)
        return;
    const QString s = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    if (s.isEmpty())
        return;
    QByteArray bytes = s.toUtf8();
    if (m_sess->screen().modes().bracketedPaste)
        bytes = "\x1b[200~" + bytes + "\x1b[201~";
    m_sess->write(bytes);
    m_scrollOffset = 0;
    setDirty();
}

void TermSurface::clearSelection()
{
    m_hasSel = false;
    m_selecting = false;
    m_selA.valid = m_selB.valid = false;
    requestPaint();
}

void TermSurface::scrollByLines(int lines)
{
    if (!m_sess)
        return;
    const int hist = m_sess->screen().historySize();
    const int next = std::clamp(m_scrollOffset + lines, 0, hist);
    if (next == m_scrollOffset)
        return;
    m_scrollOffset = next;
    setDirty();
}

void TermSurface::keyPressEvent(QKeyEvent *e)
{
    if (!m_sess) {
        QWidget::keyPressEvent(e);
        return;
    }
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();

    if (key == Qt::Key_C && mods.testFlag(Qt::ControlModifier)
        && mods.testFlag(Qt::ShiftModifier)) {
        copySelection();
        return;
    }
    if (key == Qt::Key_V && mods.testFlag(Qt::ControlModifier)
        && mods.testFlag(Qt::ShiftModifier)) {
        pasteClipboard();
        return;
    }

    if (m_scrollOffset != 0) {
        m_scrollOffset = 0;
        m_dirty = true;
    }
    resetBlink();
    clearSelection();

    QByteArray out;
    const Modes &m = m_sess->screen().modes();
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);

    auto cursorSeq = [&](char final) -> QByteArray {
        if (m.appCursor)
            return QByteArray("\x1bO") + final;
        return QByteArray("\x1b[") + final;
    };
    auto tildeSeq = [&](int n) -> QByteArray {
        return "\x1b[" + QByteArray::number(n) + "~";
    };
    auto modSeq = [&](char final) -> QByteArray {
        const int mod = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
        return "\x1b[1;" + QByteArray::number(mod) + final;
    };

    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Right:
    case Qt::Key_Left: {
        const char final = key == Qt::Key_Up ? 'A' : key == Qt::Key_Down ? 'B'
                         : key == Qt::Key_Right ? 'C' : 'D';
        out = (ctrl || alt || shift) ? modSeq(final) : cursorSeq(final);
        break;
    }
    case Qt::Key_Home:
    case Qt::Key_End: {
        const char final = key == Qt::Key_Home ? 'H' : 'F';
        out = (ctrl || alt || shift) ? modSeq(final) : cursorSeq(final);
        break;
    }
    case Qt::Key_Insert: out = tildeSeq(2); break;
    case Qt::Key_Delete: out = tildeSeq(3); break;
    case Qt::Key_PageUp: out = tildeSeq(5); break;
    case Qt::Key_PageDown: out = tildeSeq(6); break;
    case Qt::Key_Backspace: out = "\x7f"; break;
    case Qt::Key_Tab: out = shift ? QByteArray("\x1b[Z") : QByteArray("\t"); break;
    case Qt::Key_Backtab: out = "\x1b[Z"; break;
    case Qt::Key_Return:
    case Qt::Key_Enter: out = "\r"; break;
    case Qt::Key_Escape: out = "\x1b"; break;
    case Qt::Key_Space: out = ctrl ? QByteArray(1, '\0') : QByteArray(" "); break;
    case Qt::Key_F1: out = "\x1bOP"; break;
    case Qt::Key_F2: out = "\x1bOQ"; break;
    case Qt::Key_F3: out = "\x1bOR"; break;
    case Qt::Key_F4: out = "\x1bOS"; break;
    case Qt::Key_F5: out = tildeSeq(15); break;
    case Qt::Key_F6: out = tildeSeq(17); break;
    case Qt::Key_F7: out = tildeSeq(18); break;
    case Qt::Key_F8: out = tildeSeq(19); break;
    case Qt::Key_F9: out = tildeSeq(20); break;
    case Qt::Key_F10: out = tildeSeq(21); break;
    case Qt::Key_F11: out = tildeSeq(23); break;
    case Qt::Key_F12: out = tildeSeq(24); break;
    default: {
        if (ctrl && key >= Qt::Key_A && key <= Qt::Key_Z) {
            out = QByteArray(1, char(key - Qt::Key_A + 1));
        } else if (ctrl && key == Qt::Key_BracketLeft) {
            out = "\x1b";
        } else if (ctrl && key == Qt::Key_Backslash) {
            out = "\x1c";
        } else if (ctrl && key == Qt::Key_BracketRight) {
            out = "\x1d";
        } else if (ctrl && key == Qt::Key_Slash) {
            out = "\x1f";
        } else if (ctrl && (key == Qt::Key_2 || key == Qt::Key_At)) {
            out = QByteArray(1, '\0');
        } else {
            const QString text = e->text();
            if (text.isEmpty())
                return;
            out = text.toUtf8();
        }
        break;
    }
    }

    if (out.isEmpty())
        return;
    if (alt && !out.startsWith('\x1b'))
        out.prepend('\x1b');
    m_sess->write(out);
    requestPaint();
}

void TermSurface::wheelEvent(QWheelEvent *e)
{
    if (!m_sess)
        return;
    const int dy = e->angleDelta().y();
    if (dy == 0)
        return;
    m_wheelAcc += dy / 120.0;
    const int steps = int(m_wheelAcc);
    if (steps == 0)
        return;
    m_wheelAcc -= steps;

    if (m_sess->screen().modes().mouse > 0 && !e->modifiers().testFlag(Qt::ShiftModifier)) {
        sendWheelMouse(e->position().toPoint(), steps);
        return;
    }
    scrollByLines(-steps * 3);
}

void TermSurface::sendWheelMouse(const QPoint &pos, int steps)
{
    const Modes &m = m_sess->screen().modes();
    const Hit h = hitTest(pos);
    const int x = h.col + 1;
    const int y = (h.abs - (m_sess->screen().historySize() - m_scrollOffset)) + 1;
    const int btn = steps > 0 ? 64 : 65;
    for (int i = 0; i < std::abs(steps); ++i) {
        if (m.mouseSgr) {
            m_sess->write("\x1b[<" + QByteArray::number(btn) + ";"
                          + QByteArray::number(x) + ";" + QByteArray::number(y) + "M");
        } else {
            QByteArray s("\x1b[M");
            s.append(char(32 + btn));
            s.append(char(32 + std::clamp(x, 1, 223)));
            s.append(char(32 + std::clamp(y, 1, 223)));
            m_sess->write(s);
        }
    }
}

void TermSurface::mousePressEvent(QMouseEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    if (!m_sess)
        return;
    const Modes &m = m_sess->screen().modes();
    if (m.mouse > 0 && !e->modifiers().testFlag(Qt::ShiftModifier)) {
        sendMouse(e, QStringLiteral("press"));
        return;
    }
    if (e->button() == Qt::MiddleButton) {
        const QString s = QGuiApplication::clipboard()->text(QClipboard::Selection);
        if (!s.isEmpty()) {
            QString t = s;
            t.replace(QLatin1Char('\n'), QLatin1Char('\r'));
            m_sess->write(t.toUtf8());
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (handleMediaPress(e->position().toPoint()))
        return;
    m_selA = hitTest(e->position().toPoint());
    m_selB = m_selA;
    m_selecting = true;
    m_hasSel = false;
    m_scrollOffset = 0;
    setDirty();
}

void TermSurface::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_sess)
        return;
    const Modes &m = m_sess->screen().modes();
    if (m.mouse >= 2 && (e->buttons() & Qt::LeftButton) && !m_selecting
        && !e->modifiers().testFlag(Qt::ShiftModifier)) {
        sendMouse(e, QStringLiteral("move"));
        return;
    }
    if (m_seekId >= 0 && m_store && m_sess) {
        for (const MediaItem &it : m_sess->screen().media()) {
            if (it.id != m_seekId)
                continue;
            const QRectF rect = mediaRectPx(it, mediaTopAbs());
            if (rect.width() > 0) {
                const double ratio = std::clamp((e->position().x() - rect.left() - 44)
                                                    / std::max<qreal>(1, rect.width() - 104),
                                                0.0, 1.0);
                m_store->seek(it, ratio);
            }
            return;
        }
        m_seekId = -1;
    }
    if (!m_selecting) {
        // keep the close button lit while the pointer is on a block
        int hover = -1;
        for (const MediaUi &ui : mediaUi())
            if (ui.media.contains(e->position()))
                hover = ui.id;
        if (hover != m_hoverMedia) {
            m_hoverMedia = hover;
            requestPaint();
        }
        return;
    }
    const Hit h = hitTest(e->position().toPoint());
    if (h.abs == m_selB.abs && h.col == m_selB.col)
        return;
    m_selB = h;
    m_hasSel = m_selA.valid && m_selB.valid
               && (m_selA.abs != m_selB.abs || m_selA.col != m_selB.col);
    requestPaint();
}

void TermSurface::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_sess || e->button() != Qt::LeftButton)
        return;
    const Modes &m = m_sess->screen().modes();
    if (m.mouse > 0 && !e->modifiers().testFlag(Qt::ShiftModifier)) {
        sendMouse(e, QStringLiteral("release"));
        return;
    }
    if (m_seekId >= 0) {
        m_seekId = -1;
        return;
    }
    if (!m_selecting)
        return;
    m_selecting = false;
    m_selB = hitTest(e->position().toPoint());
    m_hasSel = m_selA.valid && m_selB.valid
               && (m_selA.abs != m_selB.abs || m_selA.col != m_selB.col);
    if (m_hasSel)
        copySelection();
    requestPaint();
}

void TermSurface::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!m_sess || e->button() != Qt::LeftButton)
        return;
    const Hit h = hitTest(e->position().toPoint());
    if (!h.valid)
        return;
    const QString text = m_sess->screen().absLineText(h.abs, false);
    if (text.isEmpty())
        return;
    auto isWord = [](QChar c) {
        return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-')
            || c == QLatin1Char('.') || c == QLatin1Char('/') || c == QLatin1Char('~');
    };
    int a = h.col;
    int b = h.col;
    while (a > 0 && isWord(text.at(std::min(a, int(text.size()) - 1))))
        --a;
    while (b + 1 < text.size() && isWord(text.at(std::min(b + 1, int(text.size()) - 1))))
        ++b;
    m_selA = h;
    m_selA.col = a;
    m_selB = h;
    m_selB.col = b;
    m_hasSel = b >= a;
    copySelection();
    requestPaint();
}

void TermSurface::sendMouse(QMouseEvent *e, const QString &kind)
{
    const Modes &m = m_sess->screen().modes();
    const Hit h = hitTest(e->position().toPoint());
    const int x = h.col + 1;
    const int y = h.abs - (m_sess->screen().historySize() - m_scrollOffset) + 1;
    int btn = e->button() == Qt::LeftButton ? 0
            : e->button() == Qt::MiddleButton ? 1
            : e->button() == Qt::RightButton ? 2 : 3;
    if (kind == QLatin1String("move"))
        btn = 3 + 32;
    const Qt::KeyboardModifiers mods = e->modifiers();
    const int mod = (mods.testFlag(Qt::ShiftModifier) ? 4 : 0)
                  | (mods.testFlag(Qt::AltModifier) ? 8 : 0)
                  | (mods.testFlag(Qt::ControlModifier) ? 16 : 0);
    btn += mod;

    if (m.mouseSgr) {
        const char fin = kind == QLatin1String("release") ? 'm' : 'M';
        m_sess->write("\x1b[<" + QByteArray::number(btn) + ";" + QByteArray::number(x) + ";"
                      + QByteArray::number(y) + fin);
    } else {
        QByteArray s("\x1b[M");
        if (kind == QLatin1String("release"))
            btn = 3;
        s.append(char(32 + btn));
        s.append(char(32 + std::clamp(x, 1, 223)));
        s.append(char(32 + std::clamp(y, 1, 223)));
        m_sess->write(s);
    }
}

void TermSurface::inputMethodEvent(QInputMethodEvent *e)
{
    if (!m_sess)
        return;
    if (!e->commitString().isEmpty())
        m_sess->write(e->commitString().toUtf8());
}

QVariant TermSurface::inputMethodQuery(Qt::InputMethodQuery q) const
{
    if (q == Qt::ImEnabled)
        return true;
    return QWidget::inputMethodQuery(q);
}

void TermSurface::contextMenuEvent(QContextMenuEvent *e)
{
    emit contextMenuRequested(e->globalPos());
    e->accept();
}
