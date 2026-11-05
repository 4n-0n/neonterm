#include "ui/StatusLine.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>

StatusLine::StatusLine(QWidget *parent)
    : QWidget(parent)
{
    // drawn in design units, scaled by the painter: same proportions at any zoom
    setFixedHeight(theme::scaled(26));
}

void StatusLine::setSessionInfo(const QString &program, int pid, const QSize &grid)
{
    m_program = program;
    m_pid = pid;
    m_grid = grid;
    update();
}

void StatusLine::setFps(double fps)
{
    if (std::abs(fps - m_fps) < 0.4)
        return;
    m_fps = fps;
    update();
}

void StatusLine::setEffects(bool crt)
{
    m_crt = crt;
    update();
}

void StatusLine::setState(const QString &text, bool ok)
{
    m_state = text;
    m_ok = ok;
    update();
}

void StatusLine::setRenderer(const QString &name)
{
    if (m_renderer == name)
        return;
    m_renderer = name;
    update();
}

void StatusLine::setFontPixelSize(int px)
{
    m_fontPx = px;
    update();
}

void StatusLine::paintEvent(QPaintEvent *)
{
    const theme::Look &lk = theme::look();
    const qreal S = theme::uiScale();
    const int W = int(std::lround(width() / S));
    const int H = int(std::lround(height() / S));

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(S, S);
    p.fillRect(QRectF(0, 0, W, H), QColor(0x05, 0x08, 0x0c));
    p.fillRect(QRectF(0, 0, W, 1), QColor(lk.border));

    QFont f = theme::mono(10);
    QFont fb = theme::mono(9, QFont::DemiBold);
    const int y = H / 2;
    const QFontMetrics fm(f);

    // left: state pill
    p.setFont(fb);
    const QString state = m_state;
    const int pillW = QFontMetrics(fb).horizontalAdvance(state) + 24;
    QRectF pill(10, 4, pillW, H - 9);
    QColor pc = m_ok ? lk.ok : lk.warn;
    QColor fill = pc;
    fill.setAlpha(28);
    p.setBrush(fill);
    p.setPen(QPen(QColor(pc.red(), pc.green(), pc.blue(), 120), 1));
    p.drawRoundedRect(pill, 9, 9);
    p.setBrush(pc);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(pill.left() + 10, pill.center().y()), 2.6, 2.6);
    p.setPen(QColor(pc));
    p.drawText(pill.adjusted(18, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, state);

    // left text: pid + program
    p.setFont(f);
    p.setPen(QColor(lk.dim));
    QString left = QStringLiteral("pid %1  ·  %2").arg(m_pid > 0 ? QString::number(m_pid)
                                                                   : QStringLiteral("--"),
                                                       m_program);
    int x = int(pill.right()) + 12;
    p.drawText(QRect(x, 0, W / 2, H), Qt::AlignVCenter | Qt::AlignLeft, left);
    x += fm.horizontalAdvance(left) + 14;

    p.setPen(QColor(lk.accent));
    p.drawText(QRect(x, 0, 90, H), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("%1×%2").arg(m_grid.width()).arg(m_grid.height()));

    // right side badges
    struct Badge {
        QString text;
        bool on;
    };
    const QVector<Badge> badges = {
        {QStringLiteral("CRT"), m_crt},
        {QStringLiteral("%1px").arg(m_fontPx), true},
    };
    int right = W - 10;
    p.setFont(fb);
    for (int i = int(badges.size()) - 1; i >= 0; --i) {
        const Badge &b = badges[i];
        const int w = QFontMetrics(fb).horizontalAdvance(b.text) + 16;
        QRectF r(right - w, 4, w, H - 9);
        QColor c = b.on ? lk.accent : QColor(0x3a, 0x4a, 0x57);
        QColor bg = c;
        bg.setAlpha(b.on ? 26 : 12);
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, 4, 4);
        p.setPen(c);
        p.drawText(r, Qt::AlignCenter, b.text);
        right -= w + 6;
    }

    // fps meter
    p.setFont(f);
    const QString fpsText = QStringLiteral("%1 fps").arg(m_fps, 0, 'f', 1);
    const int fpsW = fm.horizontalAdvance(fpsText);
    right -= 8;
    p.setPen(QColor(lk.dim));
    p.drawText(QRect(right - fpsW, 0, fpsW, H), Qt::AlignVCenter | Qt::AlignRight, fpsText);
    right -= fpsW + 10;
    const int bars = 5;
    const int lit = std::clamp(int(m_fps / 12.0), 0, bars);
    for (int i = 0; i < bars; ++i) {
        const qreal h = 4 + i * 2.2;
        QRectF br(right - (bars - i) * 5, y - h / 2, 3, h);
        QColor c = (bars - i) <= lit ? lk.accent : QColor(0x22, 0x33, 0x3e);
        p.fillRect(br, c);
    }
    right -= bars * 5 + 12;

    // center: terminal capabilities
    const QString caps = QStringLiteral("xterm-256color · UTF-8 · TRUECOLOR · %1 · history 5000")
                             .arg(m_renderer);
    p.setPen(QColor(0x3c, 0x52, 0x62));
    const int capsW = fm.horizontalAdvance(caps);
    const int capsX = std::max(x + 100, (W - capsW) / 2);
    if (capsX + capsW < right)
        p.drawText(QRect(capsX, 0, capsW, H), Qt::AlignVCenter | Qt::AlignLeft, caps);

    p.setRenderHint(QPainter::Antialiasing, false);
}
