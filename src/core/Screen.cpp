#include "core/Screen.h"

#include <QString>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace {

// Locale-independent cell width: wcwidth() answers -1 for anything outside the
// current locale's charset, which would silently drop Cyrillic/CJK on a bare
// "C" locale. Combining marks get 0, East Asian wide and emoji get 2.
int charWidth(char32_t c)
{
    if (c == 0 || c < 0x20 || (c >= 0x7f && c < 0xa0))
        return 0;
    if ((c >= 0x0300 && c <= 0x036f) || (c >= 0x0483 && c <= 0x0489)
        || (c >= 0x0591 && c <= 0x05c7) || (c >= 0x0610 && c <= 0x061a)
        || (c >= 0x064b && c <= 0x065f) || (c >= 0x0670 && c <= 0x0670)
        || (c >= 0x06d6 && c <= 0x06dc) || (c >= 0x0e31 && c <= 0x0e31)
        || (c >= 0x0e34 && c <= 0x0e3a) || (c >= 0x1ab0 && c <= 0x1aff)
        || (c >= 0x1dc0 && c <= 0x1dff) || (c >= 0x20d0 && c <= 0x20f0)
        || (c >= 0xfe00 && c <= 0xfe0f) || (c >= 0xfe20 && c <= 0xfe2f)
        || (c >= 0x200b && c <= 0x200f) || (c >= 0x2060 && c <= 0x2064)
        || (c >= 0xe0100 && c <= 0xe01ef))
        return 0;
    if (c >= 0x1100 && (c <= 0x115f || c == 0x2329 || c == 0x232a
        || (c >= 0x2e80 && c <= 0x303e) || (c >= 0x3041 && c <= 0x33ff)
        || (c >= 0x3400 && c <= 0x4dbf) || (c >= 0x4e00 && c <= 0x9fff)
        || (c >= 0xa000 && c <= 0xa4cf) || (c >= 0xa960 && c <= 0xa97f)
        || (c >= 0xac00 && c <= 0xd7a3) || (c >= 0xf900 && c <= 0xfaff)
        || (c >= 0xfe10 && c <= 0xfe19) || (c >= 0xfe30 && c <= 0xfe6f)
        || (c >= 0xff00 && c <= 0xff60) || (c >= 0xffe0 && c <= 0xffe6)
        || (c >= 0x1b000 && c <= 0x1b001) || (c >= 0x1f004 && c <= 0x1f004)
        || (c >= 0x1f0cf && c <= 0x1f0cf) || (c >= 0x1f18e && c <= 0x1f18e)
        || (c >= 0x1f191 && c <= 0x1f19a) || (c >= 0x1f200 && c <= 0x1f320)
        || (c >= 0x1f32d && c <= 0x1f335) || (c >= 0x1f337 && c <= 0x1f37c)
        || (c >= 0x1f37e && c <= 0x1f393) || (c >= 0x1f3a0 && c <= 0x1f3ca)
        || (c >= 0x1f3cf && c <= 0x1f3d3) || (c >= 0x1f3e0 && c <= 0x1f3f0)
        || (c >= 0x1f3f4 && c <= 0x1f3f4) || (c >= 0x1f3f8 && c <= 0x1f43e)
        || (c >= 0x1f440 && c <= 0x1f440) || (c >= 0x1f442 && c <= 0x1f4fc)
        || (c >= 0x1f4ff && c <= 0x1f53d) || (c >= 0x1f54b && c <= 0x1f54e)
        || (c >= 0x1f550 && c <= 0x1f567) || (c >= 0x1f57a && c <= 0x1f57a)
        || (c >= 0x1f595 && c <= 0x1f596) || (c >= 0x1f5a4 && c <= 0x1f5a4)
        || (c >= 0x1f5fb && c <= 0x1f64f) || (c >= 0x1f680 && c <= 0x1f6c5)
        || (c >= 0x1f6cc && c <= 0x1f6cc) || (c >= 0x1f6d0 && c <= 0x1f6d2)
        || (c >= 0x1f6eb && c <= 0x1f6ec) || (c >= 0x1f6f4 && c <= 0x1f6f9)
        || (c >= 0x1f910 && c <= 0x1f93e) || (c >= 0x1f940 && c <= 0x1f970)
        || (c >= 0x1f973 && c <= 0x1f976) || (c >= 0x1f97a && c <= 0x1f9a2)
        || (c >= 0x1f9b0 && c <= 0x1f9b9) || (c >= 0x1f9c0 && c <= 0x1f9c2)
        || (c >= 0x1f9d0 && c <= 0x1f9ff) || (c >= 0x20000 && c <= 0x3fffd)))
        return 2;
    return 1;
}

} // namespace

QRgb Palette::color256(int idx) const
{
    if (idx < 0 || idx > 255)
        return fg;
    if (idx < 16)
        return ansi[size_t(idx)];
    if (idx < 232) {
        // 6x6x6 cube; the first step is not 0 but 95 on xterm
        static const int steps[6] = {0, 95, 135, 175, 215, 255};
        const int r = (idx - 16) / 36;
        const int g = ((idx - 16) % 36) / 6;
        const int b = (idx - 16) % 6;
        return qRgb(steps[r], steps[g], steps[b]);
    }
    const int v = 8 + (idx - 232) * 10;
    return qRgb(v, v, v);
}

Screen::Screen()
{
    m_pen.fg = 0;
    m_pen.bg = 0;
    m_pen.flags = 0;
    resize(80, 24);
    reset();
}

void Screen::reset()
{
    m_lines.assign(size_t(m_rows), blankLine());
    m_history.clear();
    m_media.clear();
    m_cursor = Pos{};
    m_saved = Pos{};
    m_modes = Modes{};
    m_pen = Cell{};
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
    m_title.clear();
    m_tabs.assign(size_t(m_cols), false);
    for (int c = 8; c < m_cols; c += 8)
        m_tabs[size_t(c)] = true;
    m_bell = false;
    notify();
}

std::vector<Cell> Screen::blankLine() const
{
    return std::vector<Cell>(size_t(m_cols));
}

void Screen::resize(int cols, int rows)
{
    cols = std::clamp(cols, 2, 500);
    rows = std::clamp(rows, 2, 300);
    const int oldCols = m_cols;
    const int oldRows = int(m_lines.size());
    if (cols == oldCols && rows == oldRows)
        return;

    auto reshape = [&](std::vector<std::vector<Cell>> &lines) {
        for (auto &l : lines)
            l.resize(size_t(cols));
        if (rows > int(lines.size()))
            lines.resize(size_t(rows), std::vector<Cell>(size_t(cols)));
        else
            lines.resize(size_t(rows));
    };

    m_cols = cols;
    m_rows = rows;
    reshape(m_lines);
    if (!m_primaryLines.empty())
        reshape(m_primaryLines);
    for (auto &l : m_history)
        l.resize(size_t(cols));

    m_tabs.resize(size_t(cols), false);

    if (m_top == 0 && m_bottom == oldRows - 1) {
        m_top = 0;
        m_bottom = rows - 1;
    } else {
        m_top = std::clamp(m_top, 0, rows - 1);
        m_bottom = std::clamp(m_bottom, m_top, rows - 1);
    }
    m_cursor.x = std::clamp(m_cursor.x, 0, cols - 1);
    m_cursor.y = std::clamp(m_cursor.y, 0, rows - 1);
    m_wrapPending = false;
    notify();
}

const std::vector<Cell> &Screen::absLine(int abs) const
{
    if (abs < 0)
        abs = 0;
    const int h = int(m_history.size());
    if (abs < h)
        return m_history[size_t(abs)];
    const int local = std::min(abs - h, int(m_lines.size()) - 1);
    return m_lines[size_t(local)];
}

QString Screen::absLineText(int abs, bool trimTrailing) const
{
    if (abs < 0 || abs >= int(m_history.size()) + m_rows)
        return QString();
    const auto &cells = absLine(abs);
    QString out;
    out.reserve(cells.size());
    for (const Cell &c : cells) {
        if (c.flags & CF_Cont)
            continue;
        out.append(QString::fromUcs4(&c.ch, 1));
    }
    if (trimTrailing) {
        int n = out.size();
        while (n > 0 && out.at(n - 1) == QLatin1Char(' '))
            --n;
        out.truncate(n);
    }
    return out;
}

QString Screen::lineText(int row) const
{
    return absLineText(int(m_history.size()) + row);
}

QString Screen::textRange(int fromAbs, int fromCol, int toAbs, int toCol) const
{
    if (fromAbs > toAbs || (fromAbs == toAbs && fromCol > toCol))
        return QString();
    QStringList lines;
    for (int a = fromAbs; a <= toAbs; ++a) {
        if (a < 0 || a >= int(m_history.size()) + m_rows)
            continue;
        const auto &cells = absLine(a);
        int first = 0;
        int last = int(cells.size()) - 1;
        if (a == fromAbs)
            first = std::clamp(fromCol, 0, int(cells.size()) - 1);
        if (a == toAbs)
            last = std::clamp(toCol, -1, int(cells.size()) - 1);
        QString s;
        for (int c = first; c <= last; ++c) {
            if (cells[size_t(c)].flags & CF_Cont)
                continue;
            s.append(QString::fromUcs4(&cells[size_t(c)].ch, 1));
        }
        lines << s;
    }
    return lines.join(QLatin1Char('\n'));
}

QString Screen::dump() const
{
    QString out;
    for (int a = 0; a < int(m_history.size()) + m_rows; ++a)
        out += absLineText(a, false) + QLatin1Char('\n');
    return out;
}

void Screen::setPenDefaults()
{
    m_pen = Cell{};
}

void Screen::applySgr(const std::vector<int> &p)
{
    if (p.empty()) {
        setPenDefaults();
        return;
    }
    for (size_t i = 0; i < p.size(); ++i) {
        const int v = p[i];
        switch (v) {
        case 0: setPenDefaults(); break;
        case 1: m_pen.flags |= CF_Bold; break;
        case 2: m_pen.flags |= CF_Dim; break;
        case 3: m_pen.flags |= CF_Italic; break;
        case 4: m_pen.flags |= CF_Underline; break;
        case 5: m_pen.flags |= CF_Blink; break;
        case 7: m_pen.flags |= CF_Reverse; break;
        case 9: m_pen.flags |= CF_Strike; break;
        case 21:
        case 22: m_pen.flags &= quint8(~(CF_Bold | CF_Dim)); break;
        case 23: m_pen.flags &= quint8(~CF_Italic); break;
        case 24: m_pen.flags &= quint8(~CF_Underline); break;
        case 25: m_pen.flags &= quint8(~CF_Blink); break;
        case 27: m_pen.flags &= quint8(~CF_Reverse); break;
        case 29: m_pen.flags &= quint8(~CF_Strike); break;
        case 39: m_pen.fg = 0; break;
        case 49: m_pen.bg = 0; break;
        case 38:
        case 48: {
            const bool fg = (v == 38);
            if (i + 1 >= p.size())
                break;
            if (p[i + 1] == 5 && i + 2 < p.size()) {
                const QRgb c = m_pal.color256(p[i + 2]);
                if (fg) m_pen.fg = c; else m_pen.bg = c;
                i += 2;
            } else if (p[i + 1] == 2 && i + 4 < p.size()) {
                const QRgb c = qRgb(p[i + 2], p[i + 3], p[i + 4]);
                if (fg) m_pen.fg = c; else m_pen.bg = c;
                i += 4;
            }
            break;
        }
        default:
            if (v >= 30 && v <= 37) m_pen.fg = m_pal.ansi[size_t(v - 30)];
            else if (v >= 40 && v <= 47) m_pen.bg = m_pal.ansi[size_t(v - 40)];
            else if (v >= 90 && v <= 97) m_pen.fg = m_pal.ansi[size_t(v - 90 + 8)];
            else if (v >= 100 && v <= 107) m_pen.bg = m_pal.ansi[size_t(v - 100 + 8)];
            break;
        }
    }
}

void Screen::moveTo(int col, int row)
{
    if (m_modes.origin) {
        row += m_top;
        row = std::clamp(row, m_top, m_bottom);
    }
    m_cursor.x = std::clamp(col, 0, m_cols - 1);
    m_cursor.y = std::clamp(row, 0, m_rows - 1);
    m_wrapPending = false;
}

void Screen::moveRel(int dx, int dy)
{
    m_cursor.y += dy;
    m_cursor.y = std::clamp(m_cursor.y, m_modes.origin ? m_top : 0,
                            m_modes.origin ? m_bottom : m_rows - 1);
    m_cursor.x = std::clamp(m_cursor.x + dx, 0, m_cols - 1);
    m_wrapPending = false;
}

void Screen::setCol(int col)
{
    m_cursor.x = std::clamp(col, 0, m_cols - 1);
    m_wrapPending = false;
}

void Screen::setRow(int row)
{
    m_cursor.y = std::clamp(row, m_modes.origin ? m_top : 0,
                            m_modes.origin ? m_bottom : m_rows - 1);
    m_wrapPending = false;
}

void Screen::saveCursor()
{
    m_saved = m_cursor;
    notify();
}

void Screen::restoreCursor()
{
    m_cursor = m_saved;
    clampCursor();
    notify();
}

void Screen::clampCursor()
{
    m_cursor.x = std::clamp(m_cursor.x, 0, m_cols - 1);
    m_cursor.y = std::clamp(m_cursor.y, 0, m_rows - 1);
    m_wrapPending = false;
}

void Screen::putWide(char32_t ch, int width)
{
    if (width <= 0)
        return;
    if (width == 2 && m_cursor.x == m_cols - 1) {
        if (m_modes.autoWrap) {
            m_lines[size_t(m_cursor.y)][size_t(m_cursor.x)] = Cell{m_pen.ch, 0, 0, 0};
            carriageReturn();
            newline();
        } else {
            return;
        }
    }
    auto &line = m_lines[size_t(m_cursor.y)];
    if (m_modes.insert)
        insertChars(width);
    Cell c = m_pen;
    c.ch = ch;
    c.flags = quint8(c.flags & ~CF_Cont);
    line[size_t(m_cursor.x)] = c;
    if (width == 2 && m_cursor.x + 1 < m_cols) {
        Cell cont{};
        cont.flags = CF_Cont;
        cont.fg = m_pen.fg;
        cont.bg = m_pen.bg;
        line[size_t(m_cursor.x + 1)] = cont;
    }
    if (m_cursor.x + width >= m_cols) {
        m_cursor.x = m_cols - 1;
        if (m_modes.autoWrap)
            m_wrapPending = true;
    } else {
        m_cursor.x += width;
    }
    notify();
}

void Screen::put(char32_t ch)
{
    int width = charWidth(ch);
    if (width < 0)
        return;
    if (width == 0) {
        // combining marks / zero width: keep the base glyph, drop the mark
        return;
    }
    if (m_wrapPending) {
        m_wrapPending = false;
        carriageReturn();
        newline();
    }
    putWide(ch, width);
}

void Screen::carriageReturn()
{
    m_cursor.x = 0;
    m_wrapPending = false;
    notify();
}

void Screen::newline()
{
    m_wrapPending = false;
    if (m_cursor.y == m_bottom) {
        scrollRegionUp(1);
    } else if (m_cursor.y < m_rows - 1) {
        ++m_cursor.y;
    }
    if (m_modes.newlineMode)
        m_cursor.x = 0;
    notify();
}

void Screen::nextLine()
{
    carriageReturn();
    newline();
}

void Screen::reverseIndex()
{
    if (m_cursor.y == m_top)
        scrollRegionDown(1);
    else if (m_cursor.y > 0)
        --m_cursor.y;
    m_wrapPending = false;
    notify();
}

void Screen::backspace()
{
    if (m_cursor.x > 0)
        --m_cursor.x;
    m_wrapPending = false;
    notify();
}

void Screen::tab(int n)
{
    for (int i = 0; i < n; ++i) {
        int x = m_cursor.x + 1;
        while (x < m_cols && !m_tabs[size_t(x)])
            ++x;
        m_cursor.x = std::min(x, m_cols - 1);
    }
    m_wrapPending = false;
    notify();
}

void Screen::tabBack(int n)
{
    for (int i = 0; i < n; ++i) {
        int x = m_cursor.x - 1;
        while (x > 0 && !m_tabs[size_t(x)])
            --x;
        m_cursor.x = std::max(x, 0);
    }
    m_wrapPending = false;
    notify();
}

void Screen::tabClear(int mode)
{
    if (mode == 0) {
        if (m_cursor.x >= 0 && m_cursor.x < m_cols)
            m_tabs[size_t(m_cursor.x)] = false;
    } else if (mode == 3) {
        std::fill(m_tabs.begin(), m_tabs.end(), false);
    }
    notify();
}

void Screen::setTabStop(int col)
{
    if (col >= 0 && col < m_cols)
        m_tabs[size_t(col)] = true;
    notify();
}

void Screen::clearCells(std::vector<Cell> &line, int from, int to)
{
    from = std::clamp(from, 0, m_cols);
    to = std::clamp(to, 0, m_cols);
    for (int c = from; c < to; ++c)
        line[size_t(c)] = Cell{};
}

void Screen::eraseInDisplay(int mode)
{
    auto &cur = m_lines[size_t(m_cursor.y)];
    switch (mode) {
    case 0:
        clearCells(cur, m_cursor.x, m_cols);
        for (int r = m_cursor.y + 1; r < m_rows; ++r)
            clearCells(m_lines[size_t(r)], 0, m_cols);
        break;
    case 1:
        clearCells(cur, 0, m_cursor.x + 1);
        for (int r = 0; r < m_cursor.y; ++r)
            clearCells(m_lines[size_t(r)], 0, m_cols);
        break;
    case 2:
        for (auto &l : m_lines)
            clearCells(l, 0, m_cols);
        break;
    case 3:
        m_history.clear();
        break;
    default:
        break;
    }
    m_wrapPending = false;
    notify();
}

void Screen::eraseInLine(int mode)
{
    auto &cur = m_lines[size_t(m_cursor.y)];
    switch (mode) {
    case 0: clearCells(cur, m_cursor.x, m_cols); break;
    case 1: clearCells(cur, 0, m_cursor.x + 1); break;
    case 2: clearCells(cur, 0, m_cols); break;
    default: break;
    }
    notify();
}

void Screen::insertChars(int n)
{
    if (n <= 0)
        return;
    n = std::min(n, m_cols - m_cursor.x);
    auto &l = m_lines[size_t(m_cursor.y)];
    std::vector<Cell> blank;
    blank.resize(size_t(n));
    l.insert(l.begin() + m_cursor.x, blank.begin(), blank.end());
    l.resize(size_t(m_cols));
    notify();
}

void Screen::deleteChars(int n)
{
    if (n <= 0)
        return;
    n = std::min(n, m_cols - m_cursor.x);
    auto &l = m_lines[size_t(m_cursor.y)];
    l.erase(l.begin() + m_cursor.x, l.begin() + m_cursor.x + n);
    l.resize(size_t(m_cols));
    notify();
}

void Screen::eraseChars(int n)
{
    if (n <= 0)
        return;
    clearCells(m_lines[size_t(m_cursor.y)], m_cursor.x,
               std::min(m_cols, m_cursor.x + n));
    notify();
}

void Screen::insertLines(int n)
{
    if (n <= 0)
        return;
    const int start = std::max(m_cursor.y, m_top);
    if (start > m_bottom)
        return;
    n = std::min(n, m_bottom - start + 1);
    for (int i = 0; i < n; ++i) {
        m_lines.erase(m_lines.begin() + m_bottom);
        m_lines.insert(m_lines.begin() + start, blankLine());
    }
    m_lines.resize(size_t(m_rows));
    notify();
}

void Screen::deleteLines(int n)
{
    if (n <= 0)
        return;
    const int start = std::max(m_cursor.y, m_top);
    if (start > m_bottom)
        return;
    n = std::min(n, m_bottom - start + 1);
    for (int i = 0; i < n; ++i) {
        m_lines.erase(m_lines.begin() + start);
        m_lines.insert(m_lines.begin() + m_bottom, blankLine());
    }
    m_lines.resize(size_t(m_rows));
    notify();
}

void Screen::scrollRegionUp(int n)
{
    if (n <= 0)
        return;
    n = std::min(n, m_bottom - m_top + 1);
    for (int i = 0; i < n; ++i) {
        std::vector<Cell> removed = std::move(m_lines[size_t(m_top)]);
        m_lines.erase(m_lines.begin() + m_top);
        m_lines.insert(m_lines.begin() + m_bottom, blankLine());
        if (!m_modes.altScreen && m_top == 0 && m_bottom == m_rows - 1)
            pushHistory(removed);
    }
    m_lines.resize(size_t(m_rows));
    notify();
}

void Screen::scrollRegionDown(int n)
{
    if (n <= 0)
        return;
    n = std::min(n, m_bottom - m_top + 1);
    for (int i = 0; i < n; ++i) {
        m_lines.erase(m_lines.begin() + m_bottom);
        m_lines.insert(m_lines.begin() + m_top, blankLine());
    }
    m_lines.resize(size_t(m_rows));
    notify();
}

void Screen::scrollUp(int n) { scrollRegionUp(n); }

void Screen::scrollDown(int n) { scrollRegionDown(n); }

void Screen::fillScreen(char32_t ch)
{
    for (auto &l : m_lines) {
        for (auto &c : l) {
            c = Cell{};
            c.ch = ch;
        }
    }
    notify();
}

void Screen::addMedia(const MediaItem &m)
{
    m_media.push_back(m);
    if (m_media.size() > 24)
        m_media.erase(m_media.begin());
    notify();
}

void Screen::removeMedia(int id)
{
    const auto it = std::find_if(m_media.begin(), m_media.end(),
                                 [id](const MediaItem &m) { return m.id == id; });
    if (it != m_media.end()) {
        m_media.erase(it);
        notify();
    }
}

void Screen::clearMedia()
{
    if (m_media.empty())
        return;
    m_media.clear();
    notify();
}

void Screen::pushHistory(const std::vector<Cell> &l)
{
    if (m_historyCap == 0)
        return;
    m_history.push_back(l);
    while (m_history.size() > m_historyCap) {
        m_history.pop_front();
        // anchors are absolute lines: trimming history moves them up, and a
        // media block that is completely gone is dropped
        for (MediaItem &m : m_media)
            m.abs -= 1;
    }
    m_media.erase(std::remove_if(m_media.begin(), m_media.end(),
                                 [](const MediaItem &m) { return m.abs + m.rows <= 0; }),
                  m_media.end());
}

void Screen::clearHistory()
{
    m_history.clear();
    notify();
}

void Screen::setScrollRegion(int top, int bottom)
{
    top = std::clamp(top, 0, m_rows - 1);
    bottom = std::clamp(bottom, 0, m_rows - 1);
    if (bottom <= top) {
        m_top = 0;
        m_bottom = m_rows - 1;
    } else {
        m_top = top;
        m_bottom = bottom;
    }
    moveTo(0, 0);
    notify();
}

void Screen::resetScrollRegion()
{
    m_top = 0;
    m_bottom = m_rows - 1;
    notify();
}

void Screen::setMode(int mode, bool on)
{
    switch (mode) {
    case 4: m_modes.insert = on; break;
    case 20: m_modes.newlineMode = on; break;
    default: break;
    }
    notify();
}

void Screen::setPrivateMode(int mode, bool on)
{
    switch (mode) {
    case 1: m_modes.appCursor = on; break;
    case 6: m_modes.origin = on; moveTo(0, 0); break;
    case 7: m_modes.autoWrap = on; break;
    case 25: m_modes.cursorVisible = on; break;
    case 47:
    case 1047:
    case 1049:
        if (on) enterAlt();
        else exitAlt();
        break;
    case 1000:
    case 1002:
    case 1003:
        if (on)
            m_modes.mouse = (mode == 1000 ? 1 : mode == 1002 ? 2 : 3);
        else if (m_modes.mouse == (mode == 1000 ? 1 : mode == 1002 ? 2 : 3))
            m_modes.mouse = 0;
        break;
    case 1006: m_modes.mouseSgr = on; break;
    case 2004: m_modes.bracketedPaste = on; break;
    default: break;
    }
    notify();
}

bool Screen::privateMode(int mode) const
{
    switch (mode) {
    case 1: return m_modes.appCursor;
    case 6: return m_modes.origin;
    case 7: return m_modes.autoWrap;
    case 25: return m_modes.cursorVisible;
    case 47:
    case 1047:
    case 1049: return m_modes.altScreen;
    case 2004: return m_modes.bracketedPaste;
    default: return false;
    }
}

void Screen::enterAlt()
{
    if (m_modes.altScreen)
        return;
    saveAlt();
    m_modes.altScreen = true;
    m_lines.assign(size_t(m_rows), blankLine());
    m_cursor = Pos{};
    m_top = 0;
    m_bottom = m_rows - 1;
    m_wrapPending = false;
    notify();
}

void Screen::saveAlt()
{
    m_primaryLines = m_lines;
    m_primaryCursor = m_cursor;
}

void Screen::exitAlt()
{
    if (!m_modes.altScreen)
        return;
    m_modes.altScreen = false;
    if (!m_primaryLines.empty()) {
        m_lines = std::move(m_primaryLines);
        m_primaryLines.clear();
        m_lines.resize(size_t(m_rows));
        for (auto &l : m_lines)
            l.resize(size_t(m_cols));
        m_cursor = m_primaryCursor;
    } else {
        m_lines.assign(size_t(m_rows), blankLine());
        m_cursor = Pos{};
    }
    m_top = 0;
    m_bottom = m_rows - 1;
    clampCursor();
    notify();
}

bool Screen::takeBell()
{
    const bool b = m_bell;
    m_bell = false;
    return b;
}
