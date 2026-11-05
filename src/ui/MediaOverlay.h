#pragma once

#include <QImage>
#include <QString>

// The little player drawn over a media block. Both renderers consume the same
// QImage, so the controls look identical whether they are painted on the CPU or
// uploaded as a texture.
namespace overlay {

struct BarState {
    bool playing = false;
    bool muted = false;
    bool failed = false;
    double progress = 0.0;      // 0..1
    QString time;               // "0:03 / 0:06"
};

// Physical pixel size, drawn with the matching device pixel ratio.
QImage bar(int widthPx, int heightPx, qreal dpr, const BarState &st);
QImage closeButton(int sizePx, qreal dpr, bool hot);

}   // namespace overlay
