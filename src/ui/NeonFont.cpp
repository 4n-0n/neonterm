#include "ui/NeonFont.h"

#include <QStringList>
#include <QString>

#include <cstring>

namespace neonfont {
namespace {

struct Raw {
    char32_t cp;
    const char *paths;   // "x,y x,y x,y|..." — one polyline per '|'
};

// Grid: x 0..6, y 0..10, baseline 8, cap height 1..8, x-height 4.6..8.
// Acute and grave accents above uppercase letters use negative y on purpose:
// the glyph box is centred in the cell, so there is room above it.
const Raw kGlyphs[] = {
    {U' ', ""},
    {U'!', "3,1.2 3,6|3,7.4 3,8"},
    {U'"', "2,1.2 2,3.4|4,1.2 4,3.4"},
    {U'#', "2,1.6 1.6,8|4,1.6 3.6,8|1,3.6 5,3.6|1,6 5,6"},
    {U'$', "5,2.2 4.2,1.2 2,1.2 1,2.2 1,3.6 2,4.6 4,4.6 5,5.6 5,7 4,8 1.8,8 1,7.2|3,0.4 3,8.8"},
    {U'%', "1.2,8.4 4.8,0.8|1,1.2 2.2,1.2 2.2,2.6 1,2.6 1,1.2|3.8,6.6 5,6.6 5,8 3.8,8 3.8,6.6"},
    {U'&', "5,8 2.4,8 1.2,6.8 1.2,2 2.4,1 3.4,1 4.4,2 4.4,3.4 1.2,5.4 1.2,7 2.2,8|4.4,6.4 4.4,7.4 5.2,8.4"},
    {U'\'', "3,1.2 3,3.2"},
    {U'(', "4.2,0.8 2.8,2.6 2.8,6.2 4.2,8"},
    {U')', "1.8,0.8 3.2,2.6 3.2,6.2 1.8,8"},
    {U'*', "3,1.8 3,5.4|1.6,2.8 4.4,4.4|4.4,2.8 1.6,4.4"},
    {U'+', "1,5.4 5,5.4|3,3.4 3,7.4"},
    {U',', "3,7.4 2.4,9.2"},
    {U'-', "1.4,5.2 4.6,5.2"},
    {U'.', "3,7.4 3,8.2"},
    {U'/', "5.2,0.8 0.8,8.8"},
    {U'0', "2,1 4,1 5,2 5,7 4,8 2,8 1,7 1,2 2,1|1.6,6.6 4.4,2.4"},
    {U'1', "2,2.6 3.2,1 3.2,8|2,8 4.4,8"},
    {U'2', "1,2 2,1 4,1 5,2 5,3.4 1,8 5,8"},
    {U'3', "1,1.4 2,1 4,1 5,2 5,3.4 4,4.4 2.6,4.4|4,4.4 5,5.4 5,7 4,8 2,8 1,7.6"},
    {U'4', "4.4,1 1,6 5.4,6|4.4,1 4.4,8"},
    {U'5', "5,1 1,1 1,4.2 3.6,4.2 5,5.2 5,7 4,8 2,8 1,7.6"},
    {U'6', "5,1.6 4,1 2,1 1,2 1,7 2,8 4,8 5,7 5,5.6 4,4.6 2,4.6 1,5.6"},
    {U'7', "1,1 5,1 2,8"},
    {U'8', "2,1 4,1 5,2 5,3.2 4,4.2 2,4.2 1,3.2 1,2 2,1|2,4.2 4,4.2 5,5.4 5,7 4,8 2,8 1,7 1,5.4 2,4.2"},
    {U'9', "1,7.4 2,8 4,8 5,7 5,2 4,1 2,1 1,2 1,3.4 2,4.4 4,4.4 5,3.4"},
    {U':', "3,4 3,4.6|3,7.2 3,8"},
    {U';', "3,4 3,4.6|3,7.4 2.4,9.2"},
    {U'<', "4.4,1.8 1.4,5 4.4,8.2"},
    {U'=', "1,3.8 5,3.8|1,6.4 5,6.4"},
    {U'>', "1.6,1.8 4.6,5 1.6,8.2"},
    {U'?', "1,2 2,1 4,1 5,2 5,3.2 3,5 3,6|3,7.4 3,8"},
    {U'@', "4.6,5.8 2.2,5.8 1.6,5 1.6,3.2 2.4,2.4 4,2.4 4.8,3.4 4.8,5.8|4.6,5.8 4.8,8 3.4,8.4 1.4,8.4 0.6,7.4 0.6,2.2 1.8,0.8 4.2,0.8 5.2,1.8"},
    {U'A', "1,8 1,2 2,1 4,1 5,2 5,8|1,4.6 5,4.6"},
    {U'B', "1,1 1,8|1,1 4,1 5,2 5,3.4 4,4.4 1,4.4|1,4.4 4,4.4 5,5.4 5,7 4,8 1,8"},
    {U'C', "5,2 4,1 2,1 1,2 1,7 2,8 4,8 5,7"},
    {U'D', "1,1 1,8|1,1 3.6,1 5,2.4 5,6.6 3.6,8 1,8"},
    {U'E', "5,1 1,1 1,8 5,8|1,4.4 4,4.4"},
    {U'F', "5,1 1,1 1,8|1,4.4 4,4.4"},
    {U'G', "5,2 4,1 2,1 1,2 1,7 2,8 4,8 5,7 5,4.8 3.2,4.8"},
    {U'H', "1,1 1,8|5,1 5,8|1,4.4 5,4.4"},
    {U'I', "2,1 4,1|3,1 3,8|2,8 4,8"},
    {U'J', "5,1 5,7 4,8 2,8 1,7"},
    {U'K', "1,1 1,8|5,1 1,5|2.2,3.9 5,8"},
    {U'L', "1,1 1,8 5,8"},
    {U'M', "1,8 1,1 3,4 5,1 5,8"},
    {U'N', "1,8 1,1 5,8 5,1"},
    {U'O', "2,1 4,1 5,2 5,7 4,8 2,8 1,7 1,2 2,1"},
    {U'P', "1,8 1,1 4,1 5,2 5,3.5 4,4.5 1,4.5"},
    {U'Q', "2,1 4,1 5,2 5,7 4,8 2,8 1,7 1,2 2,1|3.4,6.4 5.2,8.6"},
    {U'R', "1,8 1,1 4,1 5,2 5,3.5 4,4.5 1,4.5|2.8,4.5 5,8"},
    {U'S', "5,2 4,1 2,1 1,2 1,3.4 2,4.4 4,4.4 5,5.4 5,7 4,8 2,8 1,7"},
    {U'T', "1,1 5,1|3,1 3,8"},
    {U'U', "1,1 1,7 2,8 4,8 5,7 5,1"},
    {U'V', "1,1 3,8 5,1"},
    {U'W', "1,1 1.8,8 3,5 4.2,8 5,1"},
    {U'X', "1,1 5,8|5,1 1,8"},
    {U'Y', "1,1 3,4.4 5,1|3,4.4 3,8"},
    {U'Z', "1,1 5,1 1,8 5,8"},
    {U'[', "4,1 2,1 2,8 4,8"},
    {U'\\', "0.8,0.8 5.2,8.8"},
    {U']', "2,1 4,1 4,8 2,8"},
    {U'^', "1.6,3.4 3,1.6 4.4,3.4"},
    {U'_', "1,9 5,9"},
    {U'`', "2.4,1 3.4,2.4"},
    {U'a', "4.8,3.4 2.6,3.4 1.4,4.4|4.8,3.4 4.8,8|4.8,5.8 2.2,5.8 1.2,6.6 1.2,7.3 2,8 4.8,8"},
    {U'b', "1,1 1,8|1,4 4,4 5,4.8 5,7.2 4,8 1,8"},
    {U'c', "5,4.4 4.2,3.4 2,3.4 1.2,4.4 1.2,6.8 2,8 4.2,8 5,7.4"},
    {U'd', "4.8,1 4.8,8|4.8,4 4,4 2,4 1.2,4.8 1.2,7.2 2,8 4.8,8"},
    {U'e', "1.2,5.8 5,5.8|5,5.8 5,4.8 4.2,3.4 2.2,3.4 1.2,4.4 1.2,6.8 2,8 4.2,8 5,7.2"},
    {U'f', "4.6,1.2 3.6,1.2 2.8,2 2.8,8|1.4,3.4 4.2,3.4"},
    {U'g', "2,3.4 4.4,3.4 5,4.2 5,6.4 4.4,7.2 2,7.2 1.2,6.4 1.2,4.2 2,3.4|5,3.4 5,8.2 4.4,9.5 2.4,9.9 1.2,9.3"},
    {U'h', "1,1 1,8|1,4.2 2,3.4 4,3.4 5,4.6 5,8"},
    {U'i', "3,3.4 3,8|3,2.2 3,2.9"},
    {U'j', "4,3.4 4,8.8 3.2,9.6 2,9.6 1.4,9|4,2.2 4,2.9"},
    {U'k', "1,1 1,8|4.6,3.4 1,5.9|2.4,4.9 4.8,8"},
    {U'l', "3,1 3,8|2.6,8 3.4,8"},
    {U'm', "1,8 1,3.4|1,4.2 1.9,3.4 2.6,3.4 2.9,4 2.9,8|2.9,4.2 3.7,3.4 4.4,3.4 5,4.2 5,8"},
    {U'n', "1,8 1,3.4|1,4.2 2,3.4 4,3.4 5,4.6 5,8"},
    {U'o', "2,3.4 4,3.4 5,4.6 5,6.8 4,8 2,8 1,6.8 1,4.6 2,3.4"},
    {U'p', "1,3.4 1,10|1,4.2 2,3.4 4,3.4 5,4.6 5,6.8 4,8 1,8"},
    {U'q', "5,3.4 5,10|5,4.2 4,3.4 2,3.4 1,4.6 1,6.8 2,8 5,8"},
    {U'r', "1,8 1,3.4|1,4.6 2.2,3.4 4.4,3.4"},
    {U's', "5,4.3 4.2,3.4 2.2,3.4 1.2,4.2 1.2,5.1 2,5.9 4,5.9 5,6.7 5,7.4 4,8 2,8 1.2,7.3"},
    {U't', "2.8,1.4 2.8,7.2 3.6,8 4.6,8|1.4,3.4 4.2,3.4"},
    {U'u', "1,3.4 1,6.9 2,8 4,8 5,6.9 5,3.4"},
    {U'v', "1,3.4 3,8 5,3.4"},
    {U'w', "1,3.4 1.8,8 3,4.8 4.2,8 5,3.4"},
    {U'x', "1,3.4 5,8|5,3.4 1,8"},
    {U'y', "1,3.4 3.4,8|5,3.4 2.2,10"},
    {U'z', "1,3.4 5,3.4 1,8 5,8"},
    {U'{', "4.4,1 3.2,1.8 3.2,3.8 2.2,4.6 3.2,5.4 3.2,7.4 4.4,8"},
    {U'|', "3,0.6 3,9.4"},
    {U'}', "1.6,1 2.8,1.8 2.8,3.8 3.8,4.6 2.8,5.4 2.8,7.4 1.6,8"},
    {U'~', "0.8,5 2,4 4,6 5.2,5"},

    // accents and a few characters Italian text needs
    {0x00a0, ""},
    {0x00d2, "2,1 4,1 5,2 5,7 4,8 2,8 1,7 1,2 2,1|1.6,0.2 2.6,-0.9"},
    {0x00d9, "1,1 1,7 2,8 4,8 5,7 5,1|1.6,0.2 2.6,-0.9"},
    {0x20ac, "5,2 4,1 2,1 1,2 1,7 2,8 4,8 5,7|1.4,3.6 4.6,3.6|1.4,5.6 4.6,5.6"},
    {0x00b0, "2.4,1 3.6,1 3.6,2.2 2.4,2.2 2.4,1"},
    {0x00b7, "3,5.4 3,6.2"},
    {0x00ab, "4.4,4.6 2.6,6.2 4.4,7.8|3,4.6 1.2,6.2 3,7.8"},
    {0x00bb, "1.6,4.6 3.4,6.2 1.6,7.8|3,4.6 4.8,6.2 3,7.8"},
    {0x2192, "1,5.2 4.6,5.2|3.4,3.6 5,5.2 3.4,6.8"},
    {0x2190, "5,5.2 1.4,5.2|2.6,3.6 1,5.2 2.6,6.8"},
};

// An accented letter is the base glyph plus one mark: kept as composition so
// every accent follows the letter it sits on.
struct Composed {
    char32_t cp;
    char32_t base;
    char32_t mark;   // 0x300 grave, 0x301 acute, 0x303 tilde, 0x327 cedilla
};

const Composed kComposed[] = {
    {0x00e0, U'a', 0x300}, {0x00e8, U'e', 0x300}, {0x00ec, U'i', 0x300},
    {0x00f2, U'o', 0x300}, {0x00f9, U'u', 0x300},
    {0x00e9, U'e', 0x301}, {0x00ed, U'i', 0x301}, {0x00f3, U'o', 0x301},
    {0x00fa, U'u', 0x301},
    {0x00f1, U'n', 0x303},
    {0x00e7, U'c', 0x327},
    {0x00c0, U'A', 0x300}, {0x00c8, U'E', 0x300}, {0x00cc, U'I', 0x300},
    {0x00d2, U'O', 0x300}, {0x00d9, U'U', 0x300},
    {0x00c9, U'E', 0x301}, {0x00cd, U'I', 0x301}, {0x00d3, U'O', 0x301},
    {0x00da, U'U', 0x301},
};

QVector<Segment> parsePaths(const char *paths)
{
    QVector<Segment> segs;
    const QString all = QString::fromLatin1(paths);
    const QStringList polys = all.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString &poly : polys) {
        const QStringList pts = poly.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (pts.size() < 2)
            continue;
        float px = 0, py = 0;
        bool first = true;
        for (const QString &pt : pts) {
            const QStringList xy = pt.split(QLatin1Char(','));
            if (xy.size() != 2)
                continue;
            const float x = xy[0].toFloat();
            const float y = xy[1].toFloat();
            if (first) {
                px = x;
                py = y;
                first = false;
                continue;
            }
            segs.push_back(Segment{px, py, x, y});
            px = x;
            py = y;
        }
    }
    return segs;
}

QVector<Segment> markStrokes(char32_t mark, bool upper)
{
    switch (mark) {
    case 0x300:
        return upper ? parsePaths("1.6,-0.7 2.6,0.5")
                     : parsePaths("1.8,1.5 2.8,2.7");
    case 0x301:
        return upper ? parsePaths("2.8,-0.7 3.8,0.5")
                     : parsePaths("2.8,1.5 3.8,2.7");
    case 0x303:
        return parsePaths("1.7,2.6 2.3,2.0 3.1,2.8 3.7,2.2");
    case 0x327:
        return parsePaths("3.1,8.1 2.7,9.4 2.1,9.1");
    default: return {};
    }
}

QHash<char32_t, QVector<Segment>> build()
{
    QHash<char32_t, QVector<Segment>> map;
    map.reserve(int(sizeof(kGlyphs) / sizeof(kGlyphs[0])) + 24);
    for (const Raw &r : kGlyphs)
        map.insert(r.cp, parsePaths(r.paths));

    for (const Composed &c : kComposed) {
        QVector<Segment> segs = map.value(c.base);
        const bool upper = (c.base >= U'A' && c.base <= U'Z');
        segs += markStrokes(c.mark, upper);
        map.insert(c.cp, segs);
    }
    return map;
}

const QHash<char32_t, QVector<Segment>> &table()
{
    static const QHash<char32_t, QVector<Segment>> t = build();
    return t;
}

} // namespace

const QVector<Segment> *glyph(char32_t cp)
{
    const auto it = table().constFind(cp);
    if (it == table().constEnd())
        return nullptr;
    return &it.value();
}

bool covers(char32_t cp)
{
    if (glyph(cp))
        return true;
    return (cp >= 0x2500 && cp <= 0x257f) || (cp >= 0x2580 && cp <= 0x259f)
        || (cp >= 0x2800 && cp <= 0x28ff);
}

bool boxRects(char32_t cp, QVector<Rect> &out)
{
    out.clear();

    // ---- blocks and shades
    switch (cp) {
    case 0x2580: out.push_back({0, 0, 1, 0.5f}); return true;
    case 0x2584: out.push_back({0, 0.5f, 1, 0.5f}); return true;
    case 0x2588: out.push_back({0, 0, 1, 1}); return true;
    case 0x258c: out.push_back({0, 0, 0.5f, 1}); return true;
    case 0x2590: out.push_back({0.5f, 0, 0.5f, 1}); return true;
    case 0x2591: out.push_back({0, 0, 1, 1}); return true;   // shade: alpha below
    case 0x2592: out.push_back({0, 0, 1, 1}); return true;
    case 0x2593: out.push_back({0, 0, 1, 1}); return true;
    case 0x2594: out.push_back({0, 0, 1, 0.125f}); return true;
    case 0x2595: out.push_back({0.875f, 0, 0.125f, 1}); return true;
    case 0x2596: out.push_back({0, 0.5f, 0.5f, 0.5f}); return true;
    case 0x2597: out.push_back({0.5f, 0.5f, 0.5f, 0.5f}); return true;
    case 0x2598: out.push_back({0, 0, 0.5f, 0.5f}); return true;
    case 0x2599:
        out.push_back({0, 0, 0.5f, 1});
        out.push_back({0.5f, 0.5f, 0.5f, 0.5f});
        return true;
    case 0x259a:
        out.push_back({0, 0, 0.5f, 0.5f});
        out.push_back({0.5f, 0.5f, 0.5f, 0.5f});
        return true;
    case 0x259b:
        out.push_back({0, 0, 1, 0.5f});
        out.push_back({0, 0.5f, 0.5f, 0.5f});
        return true;
    case 0x259c:
        out.push_back({0, 0, 1, 0.5f});
        out.push_back({0.5f, 0.5f, 0.5f, 0.5f});
        return true;
    case 0x259d: out.push_back({0.5f, 0, 0.5f, 0.5f}); return true;
    case 0x259e:
        out.push_back({0.5f, 0, 0.5f, 0.5f});
        out.push_back({0, 0.5f, 0.5f, 0.5f});
        return true;
    case 0x259f:
        out.push_back({0, 0.5f, 1, 0.5f});
        out.push_back({0.5f, 0, 0.5f, 0.5f});
        return true;
    default: break;
    }

    // ---- braille: 2 x 4 dots, each one a small rectangle
    if (cp >= 0x2800 && cp <= 0x28ff) {
        const uint32_t mask = cp - 0x2800;
        if (mask == 0) {
            out.push_back({0, 0, 0, 0});
            return true;
        }
        const float cx[2] = {0.26f, 0.74f};
        const float cy[4] = {0.14f, 0.38f, 0.62f, 0.86f};
        // bits 0..5 = dots 1..6 (left top..right mid), bits 6..7 = dots 7,8
        for (int b = 0; b < 8; ++b) {
            if (!(mask & (1u << b)))
                continue;
            int col = 0, row = 0;
            if (b < 3) {
                col = 0;
                row = b;
            } else if (b < 6) {
                col = 1;
                row = b - 3;
            } else {
                col = b - 6;
                row = 3;
            }
            out.push_back({cx[col] - 0.09f, cy[row] - 0.07f, 0.18f, 0.14f});
        }
        return true;
    }

    // ---- box drawing: lines are thin rectangles in cell units
    const float t = 0.062f;          // line thickness
    const float a0 = 0.47f, a1 = 0.53f;   // single line band
    const float d0 = 0.36f, d1 = 0.42f, d2 = 0.58f, d3 = 0.64f;   // double lines
    switch (cp) {
    case 0x2500: out.push_back({0, a0, 1, t}); return true;
    case 0x2501: out.push_back({0, a0 - 0.02f, 1, 0.10f}); return true;
    case 0x2502: out.push_back({a0, 0, t, 1}); return true;
    case 0x2503: out.push_back({a0 - 0.02f, 0, 0.10f, 1}); return true;
    case 0x250c:
        out.push_back({a0, a0, 1 - a0, t});
        out.push_back({a0, a0, t, 1 - a0});
        return true;
    case 0x2510:
        out.push_back({0, a0, a1, t});
        out.push_back({a0, a0, t, 1 - a0});
        return true;
    case 0x2514:
        out.push_back({a0, a0, 1 - a0, t});
        out.push_back({a0, 0, t, a1});
        return true;
    case 0x2518:
        out.push_back({0, a0, a1, t});
        out.push_back({a0, 0, t, a1});
        return true;
    case 0x251c:
        out.push_back({a0, 0, t, 1});
        out.push_back({a0, a0, 1 - a0, t});
        return true;
    case 0x2524:
        out.push_back({a0, 0, t, 1});
        out.push_back({0, a0, a1, t});
        return true;
    case 0x252c:
        out.push_back({0, a0, 1, t});
        out.push_back({a0, a0, t, 1 - a0});
        return true;
    case 0x2534:
        out.push_back({0, a0, 1, t});
        out.push_back({a0, 0, t, a1});
        return true;
    case 0x253c:
        out.push_back({0, a0, 1, t});
        out.push_back({a0, 0, t, 1});
        return true;

    case 0x2550:
        out.push_back({0, d0, 1, 0.055f});
        out.push_back({0, d2, 1, 0.055f});
        return true;
    case 0x2551:
        out.push_back({d0, 0, 0.055f, 1});
        out.push_back({d2, 0, 0.055f, 1});
        return true;
    case 0x2554:
        out.push_back({d2, d0, 1 - d2, 0.055f});
        out.push_back({d0, d2, 1 - d0, 0.055f});
        out.push_back({d0, d2, 0.055f, 1 - d2});
        out.push_back({d2, d0, 0.055f, 1 - d0});
        return true;
    case 0x2557:
        out.push_back({0, d0, d2, 0.055f});
        out.push_back({0, d2, d2 + 0.055f, 0.055f});
        out.push_back({d2, d2, 0.055f, 1 - d2});
        out.push_back({d0, d0, 0.055f, 1 - d0});
        return true;
    case 0x255a:
        out.push_back({d2, d2 + 0.005f, 1 - d2, 0.055f});
        out.push_back({d0, d0, 1 - d0, 0.055f});
        out.push_back({d0, 0, 0.055f, d3});
        out.push_back({d2, 0, 0.055f, d2 + 0.055f});
        return true;
    case 0x255d:
        out.push_back({0, d2 + 0.005f, d2 + 0.055f, 0.055f});
        out.push_back({0, d0, d2 + 0.055f, 0.055f});
        out.push_back({d2, 0, 0.055f, d2 + 0.055f});
        out.push_back({d0, 0, 0.055f, d3});
        return true;
    case 0x2560:
        out.push_back({d0, 0, 0.055f, 1});
        out.push_back({d2, 0, 0.055f, 1});
        out.push_back({d2, d0, 1 - d2, 0.055f});
        out.push_back({d0, d2, 1 - d0, 0.055f});
        return true;
    case 0x2563:
        out.push_back({d0, 0, 0.055f, 1});
        out.push_back({d2, 0, 0.055f, 1});
        out.push_back({0, d0, d2 + 0.055f, 0.055f});
        out.push_back({0, d2, d0 + 0.055f, 0.055f});
        return true;
    case 0x2566:
        out.push_back({0, d0, 1, 0.055f});
        out.push_back({0, d2, 1, 0.055f});
        out.push_back({d0, d2, 0.055f, 1 - d2});
        out.push_back({d2, d0, 0.055f, 1 - d0});
        return true;
    case 0x2569:
        out.push_back({0, d0, 1, 0.055f});
        out.push_back({0, d2, 1, 0.055f});
        out.push_back({d0, 0, 0.055f, d3});
        out.push_back({d2, 0, 0.055f, d2 + 0.055f});
        return true;
    case 0x256c:
        out.push_back({0, d0, 1, 0.055f});
        out.push_back({0, d2, 1, 0.055f});
        out.push_back({d0, 0, 0.055f, 1});
        out.push_back({d2, 0, 0.055f, 1});
        return true;

    case 0x256d:
    case 0x256e:
    case 0x256f:
    case 0x2570: {
        // rounded corners rendered as the equivalent sharp ones
        const bool right = (cp == 0x256e || cp == 0x256f);
        const bool top = (cp == 0x256d || cp == 0x256e);
        out.push_back({right ? 0.0f : a0, a0, right ? a1 : 1 - a0, t});
        out.push_back({a0, top ? a0 : 0.0f, t, top ? 1 - a0 : a1});
        return true;
    }
    case 0x2574: out.push_back({0, a0, a1, t}); return true;
    case 0x2575: out.push_back({a0, a1, t, 1 - a1}); return true;
    case 0x2576: out.push_back({a0, a0, 1 - a0, t}); return true;
    case 0x2577: out.push_back({a0, 0, t, a1}); return true;
    default: break;
    }
    return false;
}

} // namespace neonfont
