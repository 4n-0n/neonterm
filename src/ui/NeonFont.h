#pragma once

#include <QHash>
#include <QPointF>
#include <QVector>

// NeonFont: a monoline vector font drawn for this project. Glyphs live on a
// 6 x 10 grid (baseline at y=8, cap height 1..8, x-height 4.6..8, descender to
// 10) as polylines. The GL renderer turns them into instanced quads with
// analytic antialiasing, so they stay crisp at any device pixel ratio — no
// font hinting, no texture resampling.
namespace neonfont {

struct Segment {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// units: the glyph box is 6 wide, 10 tall
constexpr float kBoxW = 6.0f;
constexpr float kBoxH = 10.0f;

// nullptr when the font has no design for that codepoint
const QVector<Segment> *glyph(char32_t cp);

// Box drawing, block elements, shades and braille are drawn as rectangles in
// cell units; returns false when the codepoint is not one of them.
struct Rect {
    float x = 0, y = 0, w = 1, h = 1;
};
bool boxRects(char32_t cp, QVector<Rect> &out);

bool covers(char32_t cp);

} // namespace neonfont
