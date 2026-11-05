#pragma once

// Screen: the cell buffer of a terminal, plus every operation a VT parser
// needs. No widgets, no Qt event loop: tests drive it directly.

#include <QRgb>
#include <QString>
#include <array>
#include <deque>
#include <vector>

struct Palette {
    QRgb fg = 0xffd6f5ff;
    QRgb bg = 0xff060a0f;
    QRgb cursor = 0xff22e0ff;
    QRgb selection = 0xff123b52;
    std::array<QRgb, 16> ansi {{
        0xff0a0d12, 0xffff4d6d, 0xff22e39a, 0xffffd166,
        0xff00a8ff, 0xffb06cff, 0xff22d3ee, 0xffcfd8e3,
        0xff46586e, 0xffff7a8f, 0xff5cffc0, 0xffffe08a,
        0xff59ccff, 0xffd0a6ff, 0xff8ff3ff, 0xffffffff,
    }};
    QRgb color256(int idx) const;
};

enum CellFlag : quint8 {
    CF_Bold      = 1 << 0,
    CF_Dim       = 1 << 1,
    CF_Italic    = 1 << 2,
    CF_Underline = 1 << 3,
    CF_Blink     = 1 << 4,
    CF_Reverse   = 1 << 5,
    CF_Strike    = 1 << 6,
    CF_Cont      = 1 << 7,   // right half of a double-width glyph
};

struct Cell {
    char32_t ch = U' ';
    QRgb fg = 0;             // 0 == use the palette default
    QRgb bg = 0;
    quint8 flags = 0;

    bool blank() const
    {
        return ch == U' ' && bg == 0
            && (flags & (CF_Bold | CF_Dim | CF_Italic | CF_Underline
                         | CF_Blink | CF_Reverse | CF_Strike)) == 0;
    }
};

struct Pos {
    int x = 0;
    int y = 0;
};

// A piece of media placed on the grid: it scrolls with the text because it is
// anchored to an absolute line, and it disappears with the scrollback.
struct MediaItem {
    int id = 0;
    int abs = 0;      // absolute line (history + viewport)
    int col = 0;
    int cols = 0;
    int rows = 0;
    QString path;
    QString kind;     // image | video | audio | pdf
    int page = 1;
};

struct Modes {
    bool autoWrap = true;
    bool origin = false;
    bool insert = false;
    bool appCursor = false;
    bool cursorVisible = true;
    bool bracketedPaste = false;
    bool altScreen = false;
    bool newlineMode = false;    // LNM
    int  mouse = 0;              // 0 off, 1 normal, 2 button, 3 any
    bool mouseSgr = false;
    int  cursorStyle = 0;        // 0 block, 1 underline, 2 bar
};

class Screen
{
public:
    Screen();
    void reset();

    void setPalette(const Palette &p) { m_pal = p; }
    const Palette &palette() const { return m_pal; }

    void resize(int cols, int rows);
    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    const std::vector<Cell> &line(int row) const { return m_lines.at(size_t(row)); }
    const Cell &cellAt(int row, int col) const { return m_lines.at(size_t(row)).at(size_t(col)); }
    int historySize() const { return int(m_history.size()); }
    const std::vector<Cell> &historyLine(int i) const { return m_history.at(size_t(i)); }
    const std::vector<Cell> &absLine(int abs) const;
    QString absLineText(int abs, bool trimTrailing = true) const;
    QString textRange(int fromAbs, int fromCol, int toAbs, int toCol) const;
    QString dump() const;
    QString lineText(int row) const;

    // pen
    void setPenDefaults();
    const Cell &pen() const { return m_pen; }
    void applySgr(const std::vector<int> &params);

    // cursor
    Pos cursor() const { return m_cursor; }
    void moveTo(int col, int row);
    void moveRel(int dx, int dy);
    void setCol(int col);
    void setRow(int row);
    void saveCursor();
    void restoreCursor();
    bool cursorVisible() const { return m_modes.cursorVisible; }
    void setCursorVisible(bool on) { m_modes.cursorVisible = on; }
    int cursorStyle() const { return m_modes.cursorStyle; }
    void setCursorStyle(int s) { m_modes.cursorStyle = s; }

    // writing
    void put(char32_t ch);
    void carriageReturn();
    void newline();
    void nextLine();
    void reverseIndex();
    void backspace();
    void tab(int n = 1);
    void tabBack(int n = 1);
    void tabClear(int mode);
    void setTabStop(int col);
    void ringBell() { m_bell = true; notify(); }

    // erasing / editing
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertChars(int n);
    void deleteChars(int n);
    void eraseChars(int n);
    void insertLines(int n);
    void deleteLines(int n);
    void scrollUp(int n);
    void scrollDown(int n);
    void scrollUpHistory(int n);   // used by the view? kept out: view scrolls
    void fillScreen(char32_t ch);  // DECALN

    // regions / modes
    void setScrollRegion(int top, int bottom);
    void resetScrollRegion();
    int scrollTop() const { return m_top; }
    int scrollBottom() const { return m_bottom; }
    void setMode(int mode, bool on);
    void setPrivateMode(int mode, bool on);
    bool privateMode(int mode) const;
    const Modes &modes() const { return m_modes; }

    void addMedia(const MediaItem &m);
    void removeMedia(int id);
    void clearMedia();
    const std::vector<MediaItem> &media() const { return m_media; }
    int nextMediaId() { return ++m_mediaSeq; }

    QString title() const { return m_title; }
    void setTitle(const QString &t) { m_title = t; }

    // repaint bookkeeping
    quint64 revision() const { return m_rev; }
    void touch() { ++m_rev; }
    bool takeBell();

    void clearHistory();
    void setHistoryCap(int lines) { m_historyCap = size_t(std::max(0, lines)); }

private:
    std::vector<Cell> blankLine() const;
    void scrollRegionUp(int n);
    void scrollRegionDown(int n);
    void pushHistory(const std::vector<Cell> &l);
    void clampCursor();
    void putWide(char32_t ch, int width);
    void clearCells(std::vector<Cell> &line, int from, int to);
    void saveAlt();
    void restorePrimary();
    void enterAlt();
    void exitAlt();
    void notify() { ++m_rev; }

    int m_cols = 0;
    int m_rows = 0;
    std::vector<std::vector<Cell>> m_lines;
    std::deque<std::vector<Cell>> m_history;
    std::vector<MediaItem> m_media;
    int m_mediaSeq = 0;
    size_t m_historyCap = 5000;

    std::vector<std::vector<Cell>> m_primaryLines;
    Pos m_primaryCursor;

    Pos m_cursor;
    Cell m_pen;
    Modes m_modes;
    Pos m_saved;
    std::vector<bool> m_tabs;
    int m_top = 0;
    int m_bottom = 23;
    bool m_wrapPending = false;
    QString m_title;
    Palette m_pal;
    quint64 m_rev = 0;
    bool m_bell = false;
};
