#pragma once

#include "core/Screen.h"

#include <QColor>
#include <QFont>

namespace theme {

struct Look {
    QColor bg0 {0x05, 0x07, 0x0b};
    QColor bg1 {0x0b, 0x12, 0x1a};
    QColor panel {0x07, 0x0b, 0x11};
    QColor panelHi {0x0d, 0x17, 0x22};
    QColor border {0x15, 0x2a, 0x3a};
    QColor accent {0x22, 0xe0, 0xff};
    QColor accent2 {0xb0, 0x6c, 0xff};
    QColor text {0xcf, 0xe9, 0xff};
    QColor dim {0x55, 0x6f, 0x84};
    QColor warn {0xff, 0xd1, 0x66};
    QColor ok {0x22, 0xe3, 0x9a};
};

const Look &look();
const Palette &termPalette();

// One factor for the whole interface: the terminal font, the padding and the
// chrome are all scaled by it, so the proportions never change.
qreal uiScale();
int scaled(int value);

QString monoFamily();
QFont mono(int pixelSize, int weight = QFont::Normal);

} // namespace theme
