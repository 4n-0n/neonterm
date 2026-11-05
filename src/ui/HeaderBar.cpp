#include "ui/HeaderBar.h"

#include "ui/Theme.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace {
constexpr int kPadLeft = 14;
constexpr int kLogoWidth = 172;
constexpr int kTabGap = 6;
constexpr int kTabMin = 96;
constexpr int kTabMax = 214;
constexpr int kPlusWidth = 30;
} // namespace

HeaderBar::HeaderBar(QWidget *parent)
    : QWidget(parent)
{
    // everything below is drawn in "design" units and scaled by the painter, so
    // the whole bar zooms without touching a single coordinate
    setFixedHeight(theme::scaled(44));
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    m_clock = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, [this] {
        m_clock = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        update();
    });
    m_clockTimer->start();
}

void HeaderBar::setStui(bool on)
{
    if (m_stuiOn == on)
        return;
    m_stuiOn = on;
    update();
}

QSize HeaderBar::sizeHint() const
{
    return QSize(theme::scaled(900), theme::scaled(44));
}

void HeaderBar::setTabs(const QStringList &titles, int active)
{
    m_titles = titles;
    m_active = titles.isEmpty() ? -1 : std::clamp(active, 0, int(titles.size()) - 1);
    relayout();
    update();
}

void HeaderBar::setInfo(const QString &info)
{
    if (m_info == info)
        return;
    m_info = info;
    update();
}

void HeaderBar::relayout()
{
    m_rects.clear();
    const int W = int(std::lround(width() / theme::uiScale()));
    const int H = int(std::lround(height() / theme::uiScale()));
    const int rightLimit = W - kPadLeft - 190;   // room for the info readout
    int x = kPadLeft + kLogoWidth;
    int avail = std::max(0, rightLimit - x - kPlusWidth - kTabGap);

    int tabW = kTabMax;
    if (!m_titles.isEmpty()) {
        tabW = avail / int(m_titles.size()) - kTabGap;
        tabW = std::clamp(tabW, kTabMin, kTabMax);
    }

    for (int i = 0; i < m_titles.size(); ++i) {
        const int w = tabW;
        TabRect t;
        t.index = i;
        t.rect = QRect(x, 8, w, H - 16);
        t.closeRect = QRect(t.rect.right() - 20, t.rect.top() + 5, 15, 15);
        m_rects.push_back(t);
        x += w + kTabGap;
    }
    m_plusRect = QRect(x, 10, kPlusWidth, H - 20);
}

void HeaderBar::paintEvent(QPaintEvent *)
{
    const theme::Look &lk = theme::look();
    const qreal S = theme::uiScale();
    const int W = int(std::lround(width() / S));
    const int H = int(std::lround(height() / S));

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(S, S);

    QLinearGradient g(0, 0, 0, H);
    g.setColorAt(0.0, QColor(0x09, 0x11, 0x19));
    g.setColorAt(1.0, QColor(0x06, 0x0a, 0x10));
    p.fillRect(QRectF(0, 0, W, H), g);

    // neon hairline under the bar + a brighter accent segment on the left
    p.fillRect(QRectF(0, H - 1, W, 1), QColor(lk.border));
    QColor hot = lk.accent;
    hot.setAlpha(170);
    p.fillRect(QRectF(0, H - 1, 220, 1), hot);

    // ---- logo
    const QRect logoBox(kPadLeft, 11, 24, 22);
    p.setPen(QPen(QColor(lk.accent), 1.2));
    p.setBrush(QColor(0x0a, 0x18, 0x22));
    p.drawRoundedRect(QRectF(logoBox).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setBrush(Qt::NoBrush);
    QPainterPath chev;
    chev.moveTo(logoBox.left() + 7, logoBox.top() + 6);
    chev.lineTo(logoBox.left() + 13, logoBox.center().y());
    chev.lineTo(logoBox.left() + 7, logoBox.bottom() - 6);
    p.setPen(QPen(QColor(lk.accent), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(chev);
    p.fillRect(QRectF(logoBox.left() + 15, logoBox.bottom() - 8, 5, 2), QColor(lk.accent2));

    QFont brand = theme::mono(12, QFont::DemiBold);
    brand.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    p.setFont(brand);
    p.setPen(QColor(lk.text));
    p.drawText(QRect(logoBox.right() + 10, 6, 130, 18), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("NEONTERM"));
    p.setPen(QColor(lk.dim));
    QFont tiny = theme::mono(8);
    tiny.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    p.setFont(tiny);
    p.drawText(QRect(logoBox.right() + 11, 23, 130, 13), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("VT · EMBEDDED"));

    // ---- tabs
    QFont tabFont = theme::mono(11, QFont::DemiBold);
    QFont idxFont = theme::mono(8, QFont::Normal);
    for (const TabRect &t : m_rects) {
        const bool active = t.index == m_active;
        const bool hover = t.index == m_hoverTab;
        QRectF r = QRectF(t.rect).adjusted(0, 0, 0, -1);

        if (active) {
            QLinearGradient tg(0, r.top(), 0, r.bottom());
            tg.setColorAt(0.0, QColor(0x11, 0x24, 0x33));
            tg.setColorAt(1.0, QColor(0x0a, 0x14, 0x1d));
            p.setBrush(tg);
            p.setPen(QPen(QColor(0x1d, 0x3c, 0x52), 1));
            p.drawRoundedRect(r, 5, 5);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(lk.accent));
            p.drawRoundedRect(QRectF(r.left() + 8, r.top(), r.width() - 16, 2), 1, 1);
        } else if (hover) {
            p.setBrush(QColor(0x0c, 0x16, 0x1f));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, 5, 5);
        }

        p.setFont(idxFont);
        p.setPen(QColor(active ? lk.accent : lk.dim));
        p.drawText(QRect(t.rect.left() + 10, t.rect.top(), 26, t.rect.height()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1").arg(t.index + 1, 2, 10, QLatin1Char('0')));

        p.setFont(tabFont);
        p.setPen(QColor(active ? 0xe8, 0xf7, 0xff : 0x8f, 0xa9, 0xbd));
        const QRect textRect(t.rect.left() + 30, t.rect.top(), t.rect.width() - 54, t.rect.height());
        const QString title = QFontMetrics(tabFont).elidedText(
            m_titles.value(t.index, QStringLiteral("shell")), Qt::ElideRight, textRect.width());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, title);

        // close cross
        QColor cross = (t.index == m_hoverClose && hover) ? QColor(lk.accent) : QColor(0x4c, 0x63, 0x77);
        p.setPen(QPen(cross, 1.3));
        const QRectF cr = QRectF(t.closeRect).adjusted(4, 4, -4, -4);
        p.drawLine(cr.topLeft(), cr.bottomRight());
        p.drawLine(cr.topRight(), cr.bottomLeft());
    }

    // ---- new tab
    QRectF pr(m_plusRect);
    p.setPen(QPen(m_hoverPlus ? QColor(lk.accent) : QColor(0x25, 0x40, 0x52), 1.2));
    p.setBrush(m_hoverPlus ? QColor(0x0e, 0x1d, 0x28) : Qt::NoBrush);
    p.drawRoundedRect(pr.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setPen(QPen(m_hoverPlus ? QColor(lk.accent) : QColor(0x6c, 0x8a, 0x9e), 1.6));
    const QPointF pc = pr.center();
    p.drawLine(QPointF(pc.x() - 5, pc.y()), QPointF(pc.x() + 5, pc.y()));
    p.drawLine(QPointF(pc.x(), pc.y() - 5), QPointF(pc.x(), pc.y() + 5));

    // ---- right readout
    QFont infoFont = theme::mono(10);
    p.setFont(infoFont);
    const QString right = m_info.isEmpty() ? m_clock : m_info;
    const int rightW = QFontMetrics(infoFont).horizontalAdvance(right);

    // STUI switch, immediately left of the readout: a pill with a lit dot
    const int swW = 74;
    const int swH = 24;
    const QRectF sw(W - rightW - 44 - swW, (H - swH) / 2.0, swW, swH);
    m_stuiRect = sw;
    p.setPen(QPen(m_stuiOn ? QColor(lk.accent) : QColor(lk.border), 1));
    p.setBrush(m_stuiOn ? QColor(0x0e, 0x2a, 0x33) : QColor(0x08, 0x0d, 0x12));
    p.drawRoundedRect(sw, swH / 2.0, swH / 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(m_stuiOn ? QColor(lk.accent) : QColor(0x2a, 0x3a, 0x46));
    p.drawEllipse(QPointF(sw.left() + 13, sw.center().y()), 3.4, 3.4);
    QFont swFont = theme::mono(10, QFont::DemiBold);
    p.setFont(swFont);
    p.setPen(m_stuiOn ? QColor(lk.accent) : QColor(lk.dim));
    p.drawText(sw.adjusted(24, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
               m_stuiOn ? QStringLiteral("STUI") : QStringLiteral("STUI"));
    p.setPen(QColor(lk.dim));
    p.drawText(QRect(W - rightW - 16, 0, rightW + 4, H),
               Qt::AlignVCenter | Qt::AlignRight, right);
    QColor dot = lk.ok;
    p.setPen(Qt::NoPen);
    p.setBrush(dot);
    p.drawEllipse(QPointF(W - rightW - 26, H / 2.0), 3, 3);

    p.setRenderHint(QPainter::Antialiasing, false);
}

int HeaderBar::tabAt(const QPoint &p, bool *onClose) const
{
    for (const TabRect &t : m_rects) {
        if (t.closeRect.contains(p)) {
            if (onClose)
                *onClose = true;
            return t.index;
        }
        if (t.rect.contains(p)) {
            if (onClose)
                *onClose = false;
            return t.index;
        }
    }
    return -1;
}

void HeaderBar::mousePressEvent(QMouseEvent *e)
{
    m_pressed = true;
    bool onClose = false;
    const QPoint pos = (e->position() / theme::uiScale()).toPoint();
    const int idx = tabAt(pos, &onClose);
    if (idx >= 0) {
        if (onClose)
            emit tabCloseRequested(idx);
        else
            emit tabActivated(idx);
        return;
    }
    if (m_stuiRect.contains(pos)) {
        emit stuiToggled(!m_stuiOn);
        return;
    }
    if (m_plusRect.contains(pos))
        emit newTabRequested();
}

void HeaderBar::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = (e->position() / theme::uiScale()).toPoint();
    bool onClose = false;
    const int idx = tabAt(pos, &onClose);
    const int hoverClose = (idx >= 0 && onClose) ? idx : -1;
    const int hoverTab = idx;
    const bool hoverPlus = m_plusRect.contains(pos);
    if (hoverClose != m_hoverClose || hoverTab != m_hoverTab || hoverPlus != m_hoverPlus) {
        m_hoverClose = hoverClose;
        m_hoverTab = hoverTab;
        m_hoverPlus = hoverPlus;
        setCursor(hoverPlus || idx >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void HeaderBar::mouseReleaseEvent(QMouseEvent *)
{
    m_pressed = false;
}

void HeaderBar::leaveEvent(QEvent *e)
{
    QWidget::leaveEvent(e);
    m_hoverTab = -1;
    m_hoverClose = -1;
    m_hoverPlus = false;
    update();
}

void HeaderBar::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayout();
}
