#include "ui/MediaOverlay.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace overlay {
namespace {

const QColor kInk(4, 9, 14, 214);
const QColor kEdge(0x1b, 0x3a, 0x4c, 220);
const QColor kAccent(0x22, 0xe0, 0xff);
const QColor kDim(0x6f, 0x8f, 0xa5);
const QColor kText(0xc9, 0xe7, 0xf7);
const QColor kWarn(0xff, 0x6b, 0x6b);

void drawSpeaker(QPainter &p, const QRectF &box, bool muted)
{
    const qreal cx = box.center().x();
    const qreal cy = box.center().y();
    const qreal u = box.height() * 0.20;   // unit

    QPainterPath sp;
    sp.moveTo(cx - 1.6 * u, cy - 0.7 * u);
    sp.lineTo(cx - 0.7 * u, cy - 0.7 * u);
    sp.lineTo(cx + 0.4 * u, cy - 1.6 * u);
    sp.lineTo(cx + 0.4 * u, cy + 1.6 * u);
    sp.lineTo(cx - 0.7 * u, cy + 0.7 * u);
    sp.lineTo(cx - 1.6 * u, cy + 0.7 * u);
    sp.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(muted ? QColor(kDim) : QColor(kText));
    p.drawPath(sp);

    p.setBrush(Qt::NoBrush);
    if (muted) {
        p.setPen(QPen(kWarn, std::max(1.0, u * 0.42), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(cx + 0.9 * u, cy - 1.0 * u), QPointF(cx + 2.1 * u, cy + 1.0 * u));
    } else {
        p.setPen(QPen(QColor(kText), std::max(1.0, u * 0.34), Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(cx + 0.1 * u, cy - 1.0 * u, 1.5 * u, 2.0 * u), -70 * 16, 140 * 16);
        p.drawArc(QRectF(cx - 0.1 * u, cy - 1.5 * u, 2.4 * u, 3.0 * u), -62 * 16, 124 * 16);
    }
}

}   // namespace

QImage bar(int widthPx, int heightPx, qreal dpr, const BarState &st)
{
    QImage img(std::max(1, widthPx), std::max(1, heightPx), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal W = widthPx / dpr;
    const qreal H = heightPx / dpr;

    // a gradient instead of a flat strip: the image stays readable underneath,
    // the icons and the time stay readable on top
    QLinearGradient shade(0, 0, 0, H);
    shade.setColorAt(0.0, QColor(4, 9, 14, 90));
    shade.setColorAt(0.45, QColor(4, 9, 14, 205));
    shade.setColorAt(1.0, QColor(4, 9, 14, 242));
    p.fillRect(QRectF(0, 0, W, H), shade);
    p.setPen(QPen(kEdge, 1));
    p.drawLine(QPointF(0, 0.5), QPointF(W, 0.5));

    // play / pause
    const qreal btn = std::min<qreal>(H - 8, 26);
    const QRectF playBox(7, (H - btn) / 2, btn, btn);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(kAccent));
    if (st.failed) {
        p.setPen(QPen(kWarn, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawLine(playBox.topLeft() + QPointF(btn * 0.3, btn * 0.3),
                   playBox.bottomRight() - QPointF(btn * 0.3, btn * 0.3));
        p.drawLine(playBox.topRight() + QPointF(-btn * 0.3, btn * 0.3),
                   playBox.bottomLeft() + QPointF(btn * 0.3, -btn * 0.3));
    } else if (st.playing) {
        const qreal bw = btn * 0.18;
        const qreal bh = btn * 0.56;
        p.drawRoundedRect(QRectF(playBox.center().x() - bw * 1.6, playBox.center().y() - bh / 2,
                                 bw, bh), 1, 1);
        p.drawRoundedRect(QRectF(playBox.center().x() + bw * 0.6, playBox.center().y() - bh / 2,
                                 bw, bh), 1, 1);
    } else {
        QPainterPath tri;
        tri.moveTo(playBox.center().x() - btn * 0.18, playBox.center().y() - btn * 0.27);
        tri.lineTo(playBox.center().x() + btn * 0.30, playBox.center().y());
        tri.lineTo(playBox.center().x() - btn * 0.18, playBox.center().y() + btn * 0.27);
        tri.closeSubpath();
        p.drawPath(tri);
    }

    // mute
    const qreal muteBtn = std::min<qreal>(H - 10, 24);
    const QRectF muteBox(W - muteBtn - 8, (H - muteBtn) / 2, muteBtn, muteBtn);
    if (!st.failed)
        drawSpeaker(p, muteBox, st.muted);

    // progress
    const qreal x0 = playBox.right() + 10;
    const qreal x1 = muteBox.left() - 12;
    if (x1 > x0 + 30) {
        const qreal cy = H / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x18, 0x2b, 0x38));
        p.drawRoundedRect(QRectF(x0, cy - 2, x1 - x0, 4), 2, 2);
        const qreal pos = x0 + (x1 - x0) * std::clamp(st.progress, 0.0, 1.0);
        p.setBrush(QColor(kAccent));
        p.drawRoundedRect(QRectF(x0, cy - 2, pos - x0, 4), 2, 2);
        p.setBrush(QColor(0xe6, 0xf8, 0xff));
        p.drawEllipse(QPointF(pos, cy), 4.2, 4.2);
    }

    // elapsed / total, right aligned just before the mute button
    if (!st.time.isEmpty()) {
        const int fpx = std::max(8, int(H * 0.36));
        p.setFont(theme::mono(fpx));
        p.setPen(kText);
        const QRectF tr(muteBox.left() - 96, 0, 88, H);
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignRight, st.time);
    }

    p.end();
    return img;
}

QImage closeButton(int sizePx, qreal dpr, bool hot)
{
    QImage img(std::max(1, sizePx), std::max(1, sizePx), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal S = sizePx / dpr;
    const QRectF box(0.5, 0.5, S - 1, S - 1);

    p.setPen(QPen(hot ? QColor(kAccent) : QColor(kEdge), 1));
    p.setBrush(QColor(4, 9, 14, hot ? 235 : 205));
    p.drawRoundedRect(box, S * 0.28, S * 0.28);

    const qreal in = S * 0.30;
    p.setPen(QPen(hot ? QColor(kAccent) : QColor(0xb9, 0xd8, 0xea),
                  std::max(1.2, S * 0.075), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(in, in), QPointF(S - in, S - in));
    p.drawLine(QPointF(S - in, in), QPointF(in, S - in));

    p.end();
    return img;
}

}   // namespace overlay
