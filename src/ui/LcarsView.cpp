#include "ui/LcarsView.h"

#include "ui/Theme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>
#include <QtMath>

#include <sys/statvfs.h>

#include <algorithm>
#include <numeric>

namespace {

// LCARS, the palette of the Enterprise-D consoles
const QColor kOrange(0xff, 0x9c, 0x00);
const QColor kPeach(0xff, 0xcc, 0x99);
const QColor kLilac(0xcc, 0x99, 0xcc);
const QColor kBlue(0x99, 0x99, 0xff);
const QColor kRed(0xcc, 0x66, 0x66);
const QColor kTan(0xff, 0xcc, 0x66);
const QColor kIce(0x99, 0xcc, 0xff);
const QColor kGreen(0x99, 0xcc, 0x99);
const QColor kInk(0x00, 0x00, 0x00);

const QColor kSeries[6] = {kOrange, kLilac, kBlue, kTan, kRed, kIce};

QFont lcars(int px, bool bold = true)
{
    QFont f = theme::mono(px, bold ? QFont::DemiBold : QFont::Normal);
    f.setLetterSpacing(QFont::AbsoluteSpacing, std::max(0.6, px * 0.10));
    return f;
}

QString big(int n)
{
    return QLocale(QLocale::C).toString(n);
}

QString humanBytes(double b)
{
    const char *u[] = {"B", "K", "M", "G", "T"};
    int i = 0;
    while (b >= 1024 && i < 4) {
        b /= 1024;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(b, 0, 'f', b < 10 ? 1 : 0).arg(QLatin1String(u[i]));
}

QString clockText(double sec)
{
    const int s = int(sec);
    return QStringLiteral("%1G %2H %3M").arg(s / 86400).arg((s / 3600) % 24).arg((s / 60) % 60);
}

}   // namespace

LcarsView::LcarsView(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    buildTree();
    m_beat.start();

    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        sample();
        update();
    });
}

void LcarsView::buildTree()
{
    auto leaf = [](const QString &title, const QString &screen) {
        Node n;
        n.title = title;
        n.screen = screen;
        return n;
    };
    auto group = [](const QString &title, const QVector<Node> &kids) {
        Node n;
        n.title = title;
        n.children = kids;
        return n;
    };

    Node root;
    root.title = QStringLiteral("COMPUTER");
    root.children = {
        group(QStringLiteral("SISTEMI"),
              {leaf(QStringLiteral("CARICO"), QStringLiteral("load")),
               leaf(QStringLiteral("MEMORIA"), QStringLiteral("mem")),
               leaf(QStringLiteral("CAPACITA"), QStringLiteral("disks")),
               group(QStringLiteral("MONTAGGI"),
                     {leaf(QStringLiteral("TABELLA"), QStringLiteral("mounts"))}),
               leaf(QStringLiteral("TERMICO"), QStringLiteral("thermal")),
               leaf(QStringLiteral("ENERGIA"), QStringLiteral("power"))}),
        group(QStringLiteral("RETE"),
              {leaf(QStringLiteral("INTERFACCE"), QStringLiteral("net")),
               leaf(QStringLiteral("TRAFFICO"), QStringLiteral("traffic")),
               leaf(QStringLiteral("DIAGNOSTICA"), QStringLiteral("diag"))}),
        group(QStringLiteral("PROCESSI"),
              {leaf(QStringLiteral("CARICO CPU"), QStringLiteral("topcpu")),
               leaf(QStringLiteral("MEMORIA"), QStringLiteral("topmem"))}),
        group(QStringLiteral("ARCHIVIO"),
              {leaf(QStringLiteral("CAPACITA"), QStringLiteral("disks")),
               leaf(QStringLiteral("MONTAGGI"), QStringLiteral("mounts"))}),
        group(QStringLiteral("DIAGNOSTICA"),
              {leaf(QStringLiteral("PANORAMICA"), QStringLiteral("diag")),
               leaf(QStringLiteral("SERVIZI"), QStringLiteral("services")),
               leaf(QStringLiteral("TERMICO"), QStringLiteral("thermal"))}),
        group(QStringLiteral("USCITA"),
              {leaf(QStringLiteral("TERMINALE"), QStringLiteral("exit"))}),
    };
    m_root = {root};
}

const LcarsView::Node *LcarsView::node() const
{
    if (m_root.isEmpty())
        return nullptr;
    const Node *n = &m_root.first();
    const QVector<Node> *list = &n->children;
    for (int idx : m_path) {
        if (idx < 0 || idx >= list->size())
            return nullptr;
        n = &list->at(idx);
        list = &n->children;
    }
    return n;
}

QStringList LcarsView::breadcrumb() const
{
    QStringList out;
    if (m_root.isEmpty())
        return out;
    const QVector<Node> *list = &m_root.first().children;
    for (int idx : m_path) {
        if (idx < 0 || idx >= list->size())
            break;
        out << list->at(idx).title;
        list = &list->at(idx).children;
    }
    return out;
}

QStringList LcarsView::children() const
{
    QStringList out;
    if (const Node *n = node())
        for (const Node &c : n->children)
            out << c.title;
    return out;
}

bool LcarsView::enterTitle(const QString &title)
{
    const Node *n = node();
    if (!n)
        return false;
    for (int i = 0; i < n->children.size(); ++i) {
        if (n->children.at(i).title.compare(title, Qt::CaseInsensitive) == 0) {
            if (n->children.at(i).screen == QLatin1String("exit")) {
                emit leaveRequested();
                return true;
            }
            if (n->children.at(i).children.isEmpty() && n->children.at(i).screen.isEmpty())
                return false;
            m_path.push_back(i);
            sample();
            update();
            return true;
        }
    }
    return false;
}

void LcarsView::back()
{
    if (m_path.isEmpty()) {
        emit leaveRequested();
        return;
    }
    m_path.pop_back();
    update();
}

void LcarsView::home()
{
    m_path.clear();
    sample();
    update();
}

bool LcarsView::atLeaf() const
{
    const Node *n = node();
    return n && !n->screen.isEmpty();
}

QString LcarsView::currentScreen() const
{
    const Node *n = node();
    return n ? n->screen : QString();
}

void LcarsView::debugState() const
{
    qInfo("lcars: mem=%lld avail=%lld cores=%d mounts=%zu temps=%zu net=%zu procs=%zu uptime=%.0f",
          (long long)m_sys.memTotal, (long long)m_sys.memAvail, int(m_sys.cores.size()),
          m_sys.mounts.size(), m_sys.temps.size(), m_sys.net.size(), m_sys.procsMem.size(),
          m_sys.uptime);
}

void LcarsView::restart()
{
    m_prevCpu.clear();
    m_prevNetRx.clear();
    m_prevNetTx.clear();
    m_prevProc.clear();
    m_prevTotal = 0;
    m_prevIdle = 0;
    m_procClock.restart();
    sample();
    if (qEnvironmentVariableIsSet("NEONTERM_DEBUG_LCARS"))
        debugState();
    update();
}

void LcarsView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_clock.start();
    restart();
}

void LcarsView::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    m_clock.stop();
}

void LcarsView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    update();
}

// ---------------------------------------------------------------- sampling

namespace {

QVector<qint64> readCpuLine(const QString &line)
{
    QVector<qint64> v;
    const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 1; i < f.size() && v.size() < 8; ++i)
        v << f.at(i).toLongLong();
    return v;
}

}   // namespace

void LcarsView::sample()
{
    const qint64 now = m_beat.elapsed();
    const double dt = m_lastSample > 0 ? std::max<qint64>(1, now - m_lastSample) / 1000.0 : 1.0;
    m_lastSample = now;

    // --- cpu, total and per core
    // procfs files report size 0: QTextStream sees "at end" at once, so read
    // the whole thing and split it
    QFile stat(QStringLiteral("/proc/stat"));
    if (stat.open(QIODevice::ReadOnly)) {
        const QStringList raw = QString::fromLatin1(stat.readAll()).split(QLatin1Char('\n'));
        QVector<QVector<qint64>> lines;
        for (const QString &l : raw) {
            if (!l.startsWith(QLatin1String("cpu")))
                break;
            lines << readCpuLine(l);
        }
        if (!lines.isEmpty()) {
            auto busy = [](const QVector<qint64> &v, qint64 &total, qint64 &idle) {
                total = 0;
                for (int i = 0; i < v.size(); ++i)
                    total += v.at(i);
                idle = v.size() > 3 ? v.at(3) : 0;
                if (v.size() > 4)
                    idle += v.at(4);
            };
            qint64 t = 0, i0 = 0;
            busy(lines.first(), t, i0);
            if (m_prevTotal > 0 && t > m_prevTotal) {
                const double dtc = double(t - m_prevTotal);
                const double load = 1.0 - double(i0 - m_prevIdle) / dtc;
                m_sys.history.push_back(std::clamp(load, 0.0, 1.0));
                while (m_sys.history.size() > 90)
                    m_sys.history.removeFirst();
            }
            m_prevTotal = t;
            m_prevIdle = i0;

            m_sys.cores.clear();
            const int nc = lines.size() - 1;
            m_prevCpu.resize(std::max(1, nc) * 2);
            for (int c = 1; c < lines.size(); ++c) {
                qint64 tc = 0, ic = 0;
                busy(lines.at(c), tc, ic);
                double load = 0;
                const qint64 pt = m_prevCpu.at((c - 1) * 2);
                const qint64 pi = m_prevCpu.at((c - 1) * 2 + 1);
                if (pt > 0 && tc > pt)
                    load = 1.0 - double(ic - pi) / double(tc - pt);
                m_sys.cores << std::clamp(load, 0.0, 1.0);
                if ((c - 1) * 2 + 1 < m_prevCpu.size()) {
                    m_prevCpu[(c - 1) * 2] = tc;
                    m_prevCpu[(c - 1) * 2 + 1] = ic;
                }
            }
        }
    }

    // --- load average
    QFile la(QStringLiteral("/proc/loadavg"));
    if (la.open(QIODevice::ReadOnly)) {
        const QStringList f = QString::fromLatin1(la.readAll()).split(QLatin1Char(' '));
        if (f.size() >= 3) {
            m_sys.load1 = f.at(0).toDouble();
            m_sys.load5 = f.at(1).toDouble();
            m_sys.load15 = f.at(2).toDouble();
        }
    }

    // --- memory
    QFile mi(QStringLiteral("/proc/meminfo"));
    if (mi.open(QIODevice::ReadOnly)) {
        for (const QString &line : QString::fromLatin1(mi.readAll()).split(QLatin1Char('\n'))) {
            const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() < 2)
                continue;
            const qint64 kb = f.at(1).toLongLong() * 1024;
            if (f.at(0) == QLatin1String("MemTotal:"))
                m_sys.memTotal = kb;
            else if (f.at(0) == QLatin1String("MemAvailable:"))
                m_sys.memAvail = kb;
            else if (f.at(0) == QLatin1String("SwapTotal:"))
                m_sys.swapTotal = kb;
            else if (f.at(0) == QLatin1String("SwapFree:"))
                m_sys.swapFree = kb;
        }
    }

    // --- mounts and usage
    m_sys.mounts.clear();
    m_sys.mountText.clear();
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (mounts.open(QIODevice::ReadOnly)) {
        for (const QString &line : QString::fromLatin1(mounts.readAll()).split(QLatin1Char('\n'))) {
            const QStringList f = line.split(QLatin1Char(' '));
            if (f.size() < 2 || !f.at(0).startsWith(QLatin1String("/dev/")))
                continue;
            const QString path = f.at(1);
            if (path.contains(QLatin1String("/snap/")) || path.contains(QLatin1String("/boot/")))
                continue;
            struct statvfs sv;
            if (::statvfs(path.toUtf8().constData(), &sv) != 0 || sv.f_blocks == 0)
                continue;
            const double total = double(sv.f_blocks) * double(sv.f_frsize);
            const double used = total - double(sv.f_bfree) * double(sv.f_frsize);
            m_sys.mounts.push_back({path, total > 0 ? used / total : 0.0});
            m_sys.mountText.push_back({path, QStringLiteral("%1 / %2").arg(humanBytes(used), humanBytes(total))});
        }
    }

    // --- temperatures
    m_sys.temps.clear();
    for (const QString &dir : QDir(QStringLiteral("/sys/class/hwmon")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = QStringLiteral("/sys/class/hwmon/") + dir;
        QFile nameF(base + QStringLiteral("/name"));
        QString name = QStringLiteral("SENSORE");
        if (nameF.open(QIODevice::ReadOnly))
            name = QString::fromLatin1(nameF.readAll()).trimmed().toUpper();
        for (int i = 1; i <= 4; ++i) {
            QFile tf(base + QStringLiteral("/temp%1_input").arg(i));
            if (!tf.open(QIODevice::ReadOnly))
                continue;
            const double c = QString::fromLatin1(tf.readAll()).trimmed().toDouble() / 1000.0;
            if (c > 0)
                m_sys.temps.push_back({QStringLiteral("%1 %2").arg(name).arg(i), c});
        }
    }

    // --- network rates
    QFile nd(QStringLiteral("/proc/net/dev"));
    QVector<qint64> rx, tx;
    QStringList ifaces;
    if (nd.open(QIODevice::ReadOnly)) {
        QTextStream in(&nd);
        in.readLine();
        in.readLine();
        while (!in.atEnd()) {
            const QString l = in.readLine().trimmed();
            const int colon = l.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QString name = l.left(colon).trimmed();
            if (name == QLatin1String("lo"))
                continue;
            const QStringList f = l.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() < 9)
                continue;
            ifaces << name;
            rx << f.at(0).toLongLong();
            tx << f.at(8).toLongLong();
        }
    }
    m_sys.net.clear();
    double totalRate = 0;
    for (int i = 0; i < ifaces.size(); ++i) {
        double r = 0, t = 0;
        if (i < m_prevNetRx.size() && m_prevNetRx.size() == rx.size()) {
            r = std::max<qint64>(0, rx.at(i) - m_prevNetRx.at(i)) / dt;
            t = std::max<qint64>(0, tx.at(i) - m_prevNetTx.at(i)) / dt;
        }
        totalRate += r + t;
        m_sys.net.push_back({ifaces.at(i), {r, t}});
    }
    m_prevNetRx = rx;
    m_prevNetTx = tx;
    m_sys.netHistory.push_back(totalRate);
    while (m_sys.netHistory.size() > 90)
        m_sys.netHistory.removeFirst();

    // --- processes (rss, and cpu from the utime deltas)
    m_sys.procsMem.clear();
    m_sys.procsCpu.clear();
    const double procDt = m_procClock.isValid() ? std::max<qint64>(1, m_procClock.restart()) / 1000.0 : 1.0;
    const int ncpu = std::max(1, QThread::idealThreadCount());
    QHash<int, qint64> ticks;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pidStr : pids) {
        bool ok = false;
        const int pid = pidStr.toInt(&ok);
        if (!ok)
            continue;
        QFile sf(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!sf.open(QIODevice::ReadOnly))
            continue;
        const QString st = QString::fromLatin1(sf.readAll());
        const int open = st.indexOf(QLatin1Char('('));
        const int close = st.lastIndexOf(QLatin1Char(')'));
        if (open < 0 || close < open)
            continue;
        const QString comm = st.mid(open + 1, close - open - 1);
        const QStringList rest = st.mid(close + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (rest.size() < 13)
            continue;
        const qint64 utime = rest.at(11).toLongLong();
        const qint64 stime = rest.at(12).toLongLong();
        const qint64 rss = rest.at(21).toLongLong() * 4096;
        ticks.insert(pid, utime + stime);
        m_sys.procsMem.push_back({comm, rss});
        if (m_prevProc.contains(pid)) {
            const double pct = double(utime + stime - m_prevProc.value(pid)) / 100.0 / procDt * 100.0 / ncpu;
            if (pct > 0.05)
                m_sys.procsCpu.push_back({QStringLiteral("%1 [%2]").arg(comm).arg(pid), pct});
        }
    }
    m_prevProc = ticks;
    std::sort(m_sys.procsMem.begin(), m_sys.procsMem.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    std::sort(m_sys.procsCpu.begin(), m_sys.procsCpu.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });
    if (m_sys.procsMem.size() > 40)
        m_sys.procsMem.resize(40);
    if (m_sys.procsCpu.size() > 40)
        m_sys.procsCpu.resize(40);

    // --- uptime, kernel, battery
    QFile up(QStringLiteral("/proc/uptime"));
    if (up.open(QIODevice::ReadOnly))
        m_sys.uptime = QString::fromLatin1(up.readAll()).split(QLatin1Char(' ')).value(0).toDouble();
    QFile kv(QStringLiteral("/proc/version"));
    if (kv.open(QIODevice::ReadOnly)) {
        const QString v = QString::fromLatin1(kv.readAll()).trimmed();
        m_sys.kernel = v.section(QLatin1Char(' '), 2, 2);
    }
    m_sys.host = QSysInfo::machineHostName();
    m_sys.threads = QThread::idealThreadCount();

    m_sys.battery = -1;
    const QStringList psus = QDir(QStringLiteral("/sys/class/power_supply")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &p : psus) {
        if (!p.startsWith(QLatin1String("BAT")))
            continue;
        QFile cap(QStringLiteral("/sys/class/power_supply/%1/capacity").arg(p));
        if (cap.open(QIODevice::ReadOnly))
            m_sys.battery = QString::fromLatin1(cap.readAll()).trimmed().toInt();
        QFile st(QStringLiteral("/sys/class/power_supply/%1/status").arg(p));
        if (st.open(QIODevice::ReadOnly))
            m_sys.charging = QString::fromLatin1(st.readAll()).trimmed() == QLatin1String("Charging");
        break;
    }
}

// ---------------------------------------------------------------- painting

void LcarsView::pill(QPainter &p, const QRectF &r, const QColor &c, const QString &text,
                     const QColor &ink, bool right)
{
    QPainterPath path;
    const qreal rad = std::min(r.height() / 2.0, 26.0);
    path.addRoundedRect(r, rad, rad);
    // square off one end: the LCARS bars have one rounded end only
    QPainterPath square;
    if (right)
        square.addRect(QRectF(r.left(), r.top(), r.width() * 0.55, r.height()));
    else
        square.addRect(QRectF(r.right() - r.width() * 0.55, r.top(), r.width() * 0.55, r.height()));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(path.united(square));

    if (!text.isEmpty()) {
        p.setFont(lcars(std::clamp<int>(int(r.height() * 0.42), 9, 30)));
        p.setPen(ink);
        p.drawText(r.adjusted(16, 0, -16, 0), Qt::AlignVCenter | (right ? Qt::AlignRight : Qt::AlignLeft),
                   text.toUpper());
    }
}

void LcarsView::bar(QPainter &p, const QRectF &r, double ratio, const QColor &c, const QColor &bg)
{
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
    const double w = std::clamp(ratio, 0.0, 1.0) * r.width();
    if (w > 1) {
        p.setBrush(c);
        p.drawRoundedRect(QRectF(r.left(), r.top(), std::max(w, r.height()), r.height()),
                          r.height() / 2.0, r.height() / 2.0);
    }
}

void LcarsView::label(QPainter &p, const QRectF &r, const QString &text, const QColor &c, int px,
                      Qt::Alignment al, bool bold)
{
    p.setFont(lcars(px, bold));
    p.setPen(c);
    p.drawText(r, al, text.toUpper());
}

void LcarsView::spark(QPainter &p, const QRectF &r, const QVector<double> &v, const QColor &c)
{
    if (v.size() < 3)
        return;   // two points fill the whole box and look like a bug
    double maxV = 0.0001;
    for (double x : v)
        maxV = std::max(maxV, x);
    QPainterPath path;
    const double step = r.width() / std::max<qreal>(1, v.size() - 1);
    for (int i = 0; i < v.size(); ++i) {
        const double x = r.left() + i * step;
        const double y = r.bottom() - (v.at(i) / maxV) * r.height();
        if (i == 0)
            path.moveTo(x, y);
        else
            path.lineTo(x, y);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);

    QPainterPath fill = path;
    fill.lineTo(r.right(), r.bottom());
    fill.lineTo(r.left(), r.bottom());
    fill.closeSubpath();
    QColor f = c;
    f.setAlpha(48);
    p.setPen(Qt::NoPen);
    p.setBrush(f);
    p.drawPath(fill);
}

void LcarsView::elbow(QPainter &p, const QRectF &r, const QColor &c, bool rightSide)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const qreal rad = std::min<qreal>(28, r.height() * 0.42);
    QPainterPath path;
    path.addRoundedRect(r, rad, rad);
    QPainterPath keep;
    keep.addRect(QRectF(r.left(), r.top(), r.width() * 0.6, r.height()));
    p.drawPath(path.united(keep));
    Q_UNUSED(rightSide);
}

void LcarsView::paintFrame(QPainter &p)
{
    const qreal W = width();
    const qreal H = height();
    const qreal S = theme::uiScale();
    const qreal rail = std::clamp<qreal>(W * 0.21, 128 * S, 260 * S);

    p.fillRect(rect(), Qt::black);

    // left rail: the LCARS spine
    const qreal topH = 62 * S;
    QPainterPath spine;
    spine.addRoundedRect(QRectF(0, 0, rail - 18 * S, topH), 16, 16);
    QPainterPath sq;
    sq.addRect(QRectF(0, 0, (rail - 18 * S) * 0.7, topH));
    p.setPen(Qt::NoPen);
    p.setBrush(kOrange);
    p.drawPath(spine.united(sq));

    p.setPen(kInk);
    p.setFont(lcars(int(15 * S)));
    p.drawText(QRectF(20 * S, 0, rail - 40 * S, topH * 0.58), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("LCARS"));
    p.setFont(lcars(int(10 * S)));
    p.drawText(QRectF(20 * S, topH * 0.55, rail - 40 * S, topH * 0.45), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("47 · NCC-1701-D"));

    // vertical connector down to the footer
    QPainterPath stem;
    stem.addRoundedRect(QRectF(0, topH - 2, 26 * S, H - topH - 34 * S), 10, 10);
    p.setBrush(kLilac);
    p.drawPath(stem);

    // navigation: back, home, and the path in the rail
    m_hits.clear();
    qreal y = topH + 18 * S;
    const qreal bw = rail - 34 * S;
    if (!m_path.isEmpty()) {
        const QRectF r(10 * S, y, bw, 34 * S);
        pill(p, r, kOrange, QStringLiteral("INDIETRO"), Qt::black, true);
        m_hits.push_back({r, -1, true});
        y += 42 * S;
    }
    {
        const QRectF r(10 * S, y, bw, 34 * S);
        pill(p, r, kLilac, QStringLiteral("PANORAMICA"), Qt::black, true);
        Hit h{r, -1, false};
        h.child = -2;   // home
        m_hits.push_back(h);
        y += 52 * S;
    }

    // breadcrumb down the rail
    const QStringList path = breadcrumb();
    for (int i = 0; i < path.size(); ++i) {
        const QRectF r(10 * S, y, bw * (0.55 + 0.12 * std::min(i, 3)), 24 * S);
        p.setPen(Qt::NoPen);
        p.setBrush(i == path.size() - 1 ? kPeach : QColor(0x33, 0x33, 0x33));
        p.drawRect(r);
        label(p, r.adjusted(10 * S, 0, -6, 0), path.at(i), i == path.size() - 1 ? kInk : kPeach,
              int(10 * S));
        y += 28 * S;
    }

    // top band with the title
    const QRectF band(rail + 6 * S, 0, W - rail - 12 * S, 52 * S);
    p.setPen(Qt::NoPen);
    p.setBrush(kOrange);
    QPainterPath bandPath;
    bandPath.addRoundedRect(band, 14, 14);
    p.drawPath(bandPath);

    const Node *n = node();
    const QString title = n ? n->title : QStringLiteral("COMPUTER");
    p.setFont(lcars(std::clamp<int>(int(26 * S), 14, 40)));
    p.setPen(kInk);
    p.drawText(band.adjusted(18 * S, 0, -18 * S, 0), Qt::AlignVCenter | Qt::AlignLeft, title.toUpper());

    // clock on the right of the band
    p.setFont(lcars(int(13 * S)));
    p.drawText(band.adjusted(18 * S, 0, -20 * S, 0), Qt::AlignVCenter | Qt::AlignRight,
               QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss  ddd dd MMM"))
                   .toUpper());

    // footer
    const QRectF foot(rail + 6 * S, H - 26 * S, W - rail - 12 * S, 20 * S);
    p.setPen(Qt::NoPen);
    p.setBrush(kBlue);
    p.drawRect(foot);
    p.setFont(lcars(int(10 * S)));
    p.setPen(kInk);
    p.drawText(foot.adjusted(10 * S, 0, -10 * S, 0), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("LCARS 47 · %1 · %2").arg(m_sys.host.toUpper(),
                                                        m_sys.kernel.isEmpty() ? QStringLiteral("—")
                                                                               : m_sys.kernel));
    p.drawText(foot.adjusted(10 * S, 0, -10 * S, 0), Qt::AlignVCenter | Qt::AlignRight,
               QStringLiteral("%1 · %2 CORES").arg(clockText(m_sys.uptime)).arg(m_sys.threads));
}

void LcarsView::paintGroup(QPainter &p, const QRectF &area)
{
    const Node *n = node();
    if (!n)
        return;
    const qreal S = theme::uiScale();
    const int count = n->children.size();
    if (count == 0)
        return;

    const int cols = count <= 3 ? 1 : (area.width() > 720 * S ? 2 : 1);
    const int rows = (count + cols - 1) / cols;
    const qreal gap = 14 * S;
    const qreal bw = (area.width() - gap * (cols - 1)) / cols;
    const qreal bh = std::min<qreal>(72 * S, (area.height() - gap * (rows - 1)) / std::max(1, rows));

    for (int i = 0; i < count; ++i) {
        const int cx = i % cols;
        const int cy = i / cols;
        const QRectF r(area.left() + cx * (bw + gap), area.top() + cy * (bh + gap), bw, bh);
        const QColor c = kSeries[i % 6];
        pill(p, r, c, n->children.at(i).title, Qt::black, true);

        // index on the far left, LCARS style
        p.setFont(lcars(int(12 * S)));
        p.setPen(kInk);
        p.drawText(r.adjusted(14 * S, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')));

        m_hits.push_back({r, i, false});
    }

    // live readout under the buttons: the screen must not end in a black hole
    const qreal used = gap + rows * (bh + gap);
    if (area.height() - used > 120 * S)
        statusStrip(p, QRectF(area.left(), area.top() + used, area.width(),
                              std::min<qreal>(96 * S, area.height() - used - 4 * S)));
}

void LcarsView::paintScreen(QPainter &p, const QRectF &area)
{
    const Node *n = node();
    if (!n)
        return;
    const QString s = n->screen;
    if (s == QLatin1String("load"))
        screenLoad(p, area);
    else if (s == QLatin1String("mem"))
        screenMemory(p, area);
    else if (s == QLatin1String("disks"))
        screenDisks(p, area);
    else if (s == QLatin1String("mounts")) {
        // a different shape for the same data: the table
        const qreal S = theme::uiScale();
        label(p, QRectF(area.left(), area.top(), area.width(), 24 * S), QStringLiteral("PUNTO DI MONTAGGIO"),
              kPeach, int(12 * S));
        label(p, QRectF(area.left(), area.top(), area.width() - 8 * S, 24 * S), QStringLiteral("SPAZIO"),
              kPeach, int(12 * S), Qt::AlignRight | Qt::AlignVCenter);
        qreal y = area.top() + 30 * S;
        for (int i = 0; i < m_sys.mountText.size() && y + 26 * S < area.bottom(); ++i) {
            const QRectF row(area.left(), y, area.width(), 22 * S);
            p.setPen(Qt::NoPen);
            p.setBrush(i % 2 ? QColor(0x14, 0x14, 0x14) : QColor(0x0a, 0x0a, 0x0a));
            p.drawRect(row);
            label(p, row.adjusted(8 * S, 0, 0, 0), m_sys.mountText.at(i).first, kIce, int(11 * S),
                  Qt::AlignLeft | Qt::AlignVCenter, false);
            label(p, row.adjusted(0, 0, -10 * S, 0), m_sys.mountText.at(i).second, kPeach,
                  int(11 * S), Qt::AlignRight | Qt::AlignVCenter);
            y += 26 * S;
        }
        if (area.bottom() - y > 120 * S)
            statusStrip(p, QRectF(area.left(), area.bottom() - 96 * S, area.width(), 96 * S));
    } else if (s == QLatin1String("thermal"))
        screenThermal(p, area);
    else if (s == QLatin1String("power"))
        screenPower(p, area);
    else if (s == QLatin1String("net"))
        screenNet(p, area);
    else if (s == QLatin1String("traffic"))
        screenTraffic(p, area);
    else if (s == QLatin1String("topcpu"))
        screenTopCpu(p, area);
    else if (s == QLatin1String("topmem"))
        screenTopMem(p, area);
    else if (s == QLatin1String("diag") || s == QLatin1String("services"))
        screenDiag(p, area);
}

// ---------------------------------------------------------------- screens

void LcarsView::screenLoad(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    const int cores = m_sys.cores.size();
    const qreal colW = a.width() / 2.0 - 8 * S;

    label(p, QRectF(a.left(), a.top(), colW, 22 * S), QStringLiteral("CARICO PROCESSORE"), kPeach,
          int(11 * S));
    qreal y = a.top() + 28 * S;
    const qreal rowH = std::min<qreal>(26 * S, (a.height() * 0.55) / std::max(1, cores));
    for (int i = 0; i < cores && y + rowH < a.top() + a.height() * 0.62; ++i) {
        const double v = m_sys.cores.at(i);
        p.setFont(lcars(int(11 * S)));
        p.setPen(kIce);
        p.drawText(QRectF(a.left(), y, 62 * S, rowH), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("C%1").arg(i, 2, 10, QLatin1Char('0')));
        bar(p, QRectF(a.left() + 64 * S, y + rowH * 0.28, colW - 150 * S, rowH * 0.44), v,
            v > 0.85 ? kRed : (v > 0.6 ? kTan : kOrange), QColor(0x1a, 0x1a, 0x1a));
        p.setPen(kPeach);
        p.drawText(QRectF(a.left() + colW - 82 * S, y, 80 * S, rowH),
                   Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("%1%").arg(int(v * 100)));
        y += rowH;
    }

    // right column: load averages in big type
    const qreal rx = a.left() + a.width() / 2.0 + 8 * S;
    struct { const char *k; double v; } stats[3] = {{"1 MIN", m_sys.load1},
                                                    {"5 MIN", m_sys.load5},
                                                    {"15 MIN", m_sys.load15}};
    for (int i = 0; i < 3; ++i) {
        const QRectF box(rx, a.top() + i * 70 * S, a.width() / 2.0 - 8 * S, 62 * S);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x11, 0x11, 0x11));
        p.drawRect(box);
        p.setBrush(kSeries[(i + 1) % 6]);
        p.drawRect(QRectF(box.left(), box.top(), 8 * S, box.height()));
        label(p, box.adjusted(30 * S, 6 * S, -14 * S, 0), QLatin1String(stats[i].k), kLilac,
              int(11 * S));
        p.setFont(lcars(int(32 * S)));
        p.setPen(kPeach);
        p.drawText(box.adjusted(30 * S, 24 * S, -14 * S, -4 * S), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(stats[i].v, 'f', 2));
    }

    // history at the bottom
    const QRectF hist(a.left(), a.bottom() - a.height() * 0.26, a.width(), a.height() * 0.22);
    label(p, hist.adjusted(0, -20 * S, 0, -hist.height() + 18 * S),
          QStringLiteral("STORICO 90 S"), kLilac, int(10 * S));
    spark(p, hist.adjusted(0, 4 * S, 0, 0), m_sys.history, kOrange);
}

void LcarsView::screenMemory(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    const double used = double(m_sys.memTotal - m_sys.memAvail);
    const double ramRatio = m_sys.memTotal > 0 ? used / double(m_sys.memTotal) : 0;
    const double swapUsed = double(m_sys.swapTotal - m_sys.swapFree);
    const double swapRatio = m_sys.swapTotal > 0 ? swapUsed / double(m_sys.swapTotal) : 0;

    label(p, QRectF(a.left(), a.top(), a.width(), 24 * S), QStringLiteral("MEMORIA CENTRALE"), kPeach,
          int(12 * S));
    p.setFont(lcars(int(56 * S)));
    p.setPen(kOrange);
    p.drawText(QRectF(a.left(), a.top() + 26 * S, a.width(), 64 * S),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("%1%").arg(int(ramRatio * 100)));
    p.setFont(lcars(int(13 * S)));
    p.setPen(kIce);
    p.drawText(QRectF(a.left(), a.top() + 86 * S, a.width(), 22 * S), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 OCCUPATI DI %2").arg(humanBytes(used), humanBytes(m_sys.memTotal)));

    bar(p, QRectF(a.left(), a.top() + 122 * S, a.width(), 26 * S), ramRatio, kOrange,
        QColor(0x1a, 0x1a, 0x1a));
    label(p, QRectF(a.left(), a.top() + 154 * S, a.width(), 20 * S),
          QStringLiteral("RAM · %1 DISPONIBILI").arg(humanBytes(m_sys.memAvail)), kLilac, int(10 * S));

    label(p, QRectF(a.left(), a.top() + 196 * S, a.width(), 20 * S), QStringLiteral("SWAP"), kPeach,
          int(10 * S));
    if (m_sys.swapTotal > 0) {
        bar(p, QRectF(a.left(), a.top() + 220 * S, a.width(), 22 * S), swapRatio, kLilac,
            QColor(0x1a, 0x1a, 0x1a));
        label(p, QRectF(a.left(), a.top() + 248 * S, a.width(), 20 * S),
              QStringLiteral("%1 DI %2 · %3%").arg(humanBytes(swapUsed),
                                                   humanBytes(m_sys.swapTotal),
                                                   QString::number(int(swapRatio * 100))),
              kIce, int(10 * S), Qt::AlignLeft | Qt::AlignVCenter, false);
    } else {
        label(p, QRectF(a.left(), a.top() + 220 * S, a.width(), 20 * S), QStringLiteral("NON CONFIGURATO"),
              kRed, int(11 * S));
    }
}

void LcarsView::screenDisks(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("CAPACITA ARCHIVIO"),
          kPeach, int(11 * S));
    qreal y = a.top() + 30 * S;
    const qreal rowH = std::min<qreal>(54 * S, (a.height() - 30 * S) / std::max<size_t>(1, m_sys.mounts.size()));
    for (size_t i = 0; i < m_sys.mounts.size() && y + rowH < a.bottom(); ++i) {
        const double v = m_sys.mounts.at(i).second;
        p.setFont(lcars(int(11 * S)));
        p.setPen(kIce);
        p.drawText(QRectF(a.left(), y, a.width() * 0.42, rowH * 0.5), Qt::AlignBottom | Qt::AlignLeft,
                   m_sys.mounts.at(i).first.toUpper());
        p.setPen(kPeach);
        p.drawText(QRectF(a.left() + a.width() * 0.42, y, a.width() * 0.58, rowH * 0.5),
                   Qt::AlignBottom | Qt::AlignRight, m_sys.mountText.at(i).second);
        bar(p, QRectF(a.left(), y + rowH * 0.58, a.width(), rowH * 0.26), v,
            v > 0.9 ? kRed : (v > 0.75 ? kTan : kBlue), QColor(0x1a, 0x1a, 0x1a));
        y += rowH;
    }
}

void LcarsView::screenThermal(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("SONDE TERMICHE"), kPeach,
          int(11 * S));
    if (m_sys.temps.isEmpty()) {
        label(p, QRectF(a.left(), a.top() + 30 * S, a.width(), 24 * S), QStringLiteral("NESSUNA SONDA"),
              kRed, int(12 * S));
        return;
    }
    const int n = m_sys.temps.size();
    const qreal gap = 12 * S;
    const qreal gw = (a.width() - gap * (n - 1)) / std::max(1, n);
    const qreal top = a.top() + 30 * S;
    const qreal headH = 74 * S;
    for (int i = 0; i < n; ++i) {
        const double c = m_sys.temps.at(i).second;
        const double ratio = std::clamp(c / 100.0, 0.0, 1.0);
        const QRectF col(a.left() + i * (gw + gap), top, gw, a.height() - 30 * S);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x11, 0x11, 0x11));
        p.drawRect(col);

        // value and sensor name stay in the dark header, the gauge fills below
        p.setFont(lcars(int(24 * S)));
        p.setPen(c > 75 ? kRed : (c > 60 ? kTan : kPeach));
        p.drawText(QRectF(col.left(), col.top() + 6 * S, col.width(), 32 * S),
                   Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("%1°").arg(int(c)));
        p.setFont(lcars(int(9 * S)));
        p.setPen(kIce);
        p.drawText(QRectF(col.left() + 4 * S, col.top() + 40 * S, col.width() - 8 * S, 26 * S),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetricsF(lcars(int(9 * S))).elidedText(m_sys.temps.at(i).first.toUpper(),
                                                               Qt::ElideRight,
                                                               col.width() - 8 * S));

        const QRectF track(col.left() + 8 * S, col.top() + headH, col.width() - 16 * S,
                           col.height() - headH - 10 * S);
        p.setBrush(QColor(0x0a, 0x0a, 0x0a));
        p.drawRect(track);
        const qreal bh = track.height() * ratio;
        p.setBrush(c > 75 ? kRed : (c > 60 ? kTan : kBlue));
        p.drawRect(QRectF(track.left(), track.bottom() - bh, track.width(), bh));
    }
}

void LcarsView::screenPower(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("ENERGIA"), kPeach, int(11 * S));
    if (m_sys.battery < 0) {
        label(p, QRectF(a.left(), a.top() + 30 * S, a.width(), 24 * S),
              QStringLiteral("ALIMENTAZIONE DA RETE"), kGreen, int(14 * S));
        return;
    }
    const double ratio = m_sys.battery / 100.0;
    p.setFont(lcars(int(64 * S)));
    p.setPen(ratio < 0.2 ? kRed : kOrange);
    p.drawText(QRectF(a.left(), a.top() + 30 * S, a.width(), 74 * S), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1%").arg(m_sys.battery));
    bar(p, QRectF(a.left(), a.top() + 118 * S, a.width(), 30 * S), ratio,
        ratio < 0.2 ? kRed : kGreen, QColor(0x1a, 0x1a, 0x1a));
    label(p, QRectF(a.left(), a.top() + 158 * S, a.width(), 24 * S),
          m_sys.charging ? QStringLiteral("IN CARICA") : QStringLiteral("A BATTERIA"), kLilac,
          int(13 * S));
}

void LcarsView::screenNet(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("INTERFACCE DI RETE"),
          kPeach, int(11 * S));
    qreal y = a.top() + 30 * S;
    const qreal rowH = std::min<qreal>(56 * S, (a.height() * 0.6) / std::max<size_t>(1, m_sys.net.size()));
    for (size_t i = 0; i < m_sys.net.size() && y + rowH < a.bottom(); ++i) {
        const auto &n = m_sys.net.at(i);
        p.setFont(lcars(int(12 * S)));
        p.setPen(kIce);
        p.drawText(QRectF(a.left(), y, a.width() * 0.4, rowH * 0.5), Qt::AlignBottom | Qt::AlignLeft,
                   n.first.toUpper());
        p.setFont(lcars(int(13 * S)));
        p.setPen(kOrange);
        p.drawText(QRectF(a.left() + a.width() * 0.4, y, a.width() * 0.3, rowH * 0.5),
                   Qt::AlignBottom | Qt::AlignRight, QStringLiteral("↓ %1/s").arg(humanBytes(n.second.first)));
        p.setPen(kLilac);
        p.drawText(QRectF(a.left() + a.width() * 0.7, y, a.width() * 0.3, rowH * 0.5),
                   Qt::AlignBottom | Qt::AlignRight, QStringLiteral("↑ %1/s").arg(humanBytes(n.second.second)));
        y += rowH;
    }
    const QRectF hist(a.left(), a.bottom() - a.height() * 0.26, a.width(), a.height() * 0.2);
    spark(p, hist, m_sys.netHistory, kLilac);
}

void LcarsView::screenTraffic(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    double peak = 0, sum = 0;
    for (double v : m_sys.netHistory) {
        peak = std::max(peak, v);
        sum += v;
    }
    const double avg = m_sys.netHistory.isEmpty() ? 0 : sum / m_sys.netHistory.size();

    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("TRAFFICO TOTALE"),
          kPeach, int(11 * S));
    p.setFont(lcars(int(40 * S)));
    p.setPen(kOrange);
    p.drawText(QRectF(a.left(), a.top() + 26 * S, a.width() / 2, 54 * S),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("%1/s").arg(humanBytes(avg)));
    p.setFont(lcars(int(12 * S)));
    p.setPen(kIce);
    p.drawText(QRectF(a.left(), a.top() + 80 * S, a.width(), 22 * S),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("MEDIA · PICCO %1/s").arg(humanBytes(peak)));
    spark(p, QRectF(a.left(), a.top() + 110 * S, a.width(), a.height() - 130 * S), m_sys.netHistory,
          kTan);
}

void LcarsView::screenTopCpu(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("PROCESSI PER CPU"), kPeach,
          int(11 * S));
    qreal y = a.top() + 28 * S;
    const qreal rowH = 24 * S;
    for (int i = 0; i < m_sys.procsCpu.size() && i < 24 && y + rowH < a.bottom(); ++i) {
        const auto &pr = m_sys.procsCpu.at(i);
        p.setPen(Qt::NoPen);
        p.setBrush(i % 2 ? QColor(0x12, 0x12, 0x12) : QColor(0x08, 0x08, 0x08));
        p.drawRect(QRectF(a.left(), y, a.width(), rowH));
        p.setFont(lcars(int(11 * S), false));
        p.setPen(kIce);
        p.drawText(QRectF(a.left() + 8 * S, y, a.width() * 0.68, rowH),
                   Qt::AlignVCenter | Qt::AlignLeft, pr.first.toUpper().left(48));
        p.setPen(kOrange);
        p.drawText(QRectF(a.left() + a.width() * 0.68, y, a.width() * 0.16, rowH),
                   Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("%1%").arg(pr.second, 0, 'f', 1));
        bar(p, QRectF(a.left() + a.width() * 0.86, y + rowH * 0.3, a.width() * 0.12, rowH * 0.4),
            std::clamp(pr.second / 100.0 * 4.0, 0.0, 1.0), kTan, QColor(0x1a, 0x1a, 0x1a));
        y += rowH;
    }
}

void LcarsView::screenTopMem(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    label(p, QRectF(a.left(), a.top(), a.width(), 22 * S), QStringLiteral("PROCESSI PER MEMORIA"),
          kPeach, int(11 * S));
    const double maxRss = m_sys.procsMem.isEmpty() ? 1 : double(m_sys.procsMem.front().second);
    qreal y = a.top() + 28 * S;
    const qreal rowH = 24 * S;
    for (int i = 0; i < m_sys.procsMem.size() && i < 24 && y + rowH < a.bottom(); ++i) {
        const auto &pr = m_sys.procsMem.at(i);
        p.setPen(Qt::NoPen);
        p.setBrush(i % 2 ? QColor(0x12, 0x12, 0x12) : QColor(0x08, 0x08, 0x08));
        p.drawRect(QRectF(a.left(), y, a.width(), rowH));
        p.setFont(lcars(int(11 * S), false));
        p.setPen(kIce);
        p.drawText(QRectF(a.left() + 8 * S, y, a.width() * 0.6, rowH), Qt::AlignVCenter | Qt::AlignLeft,
                   pr.first.toUpper().left(40));
        p.setPen(kPeach);
        p.drawText(QRectF(a.left() + a.width() * 0.6, y, a.width() * 0.16, rowH),
                   Qt::AlignVCenter | Qt::AlignRight, humanBytes(double(pr.second)));
        bar(p, QRectF(a.left() + a.width() * 0.78, y + rowH * 0.3, a.width() * 0.2, rowH * 0.4),
            maxRss > 0 ? double(pr.second) / maxRss : 0, kBlue, QColor(0x1a, 0x1a, 0x1a));
        y += rowH;
    }
}

void LcarsView::statusStrip(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    const double cpu = m_sys.cores.isEmpty()
                           ? 0.0
                           : std::accumulate(m_sys.cores.begin(), m_sys.cores.end(), 0.0)
                                 / m_sys.cores.size();
    const double mem = m_sys.memTotal > 0 ? double(m_sys.memTotal - m_sys.memAvail) / m_sys.memTotal : 0;
    double disk = 0;
    for (const auto &m : m_sys.mounts)
        disk = std::max(disk, m.second);
    double rate = 0;
    for (const auto &n : m_sys.net)
        rate += n.second.first + n.second.second;

    struct Kpi { QString k; QString v; double ratio; QColor c; };
    const QVector<Kpi> kpis = {
        {QStringLiteral("CARICO CPU"), QStringLiteral("%1%").arg(int(cpu * 100)), cpu, kOrange},
        {QStringLiteral("MEMORIA"), QStringLiteral("%1%").arg(int(mem * 100)), mem, kLilac},
        {QStringLiteral("ARCHIVIO"), QStringLiteral("%1%").arg(int(disk * 100)), disk, kBlue},
        {QStringLiteral("RETE"), QStringLiteral("%1/s").arg(humanBytes(rate)),
         std::clamp(rate / (10.0 * 1024 * 1024), 0.0, 1.0), kTan},
    };

    const qreal gap = 12 * S;
    const qreal bw = (a.width() - gap * (kpis.size() - 1)) / kpis.size();
    const qreal bh = std::min<qreal>(96 * S, a.height());
    for (int i = 0; i < kpis.size(); ++i) {
        const QRectF box(a.left() + i * (bw + gap), a.top(), bw, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x10, 0x10, 0x10));
        p.drawRect(box);
        p.setBrush(kpis.at(i).c);
        p.drawRect(QRectF(box.left(), box.top(), box.width(), 5 * S));
        label(p, QRectF(box.left() + 10 * S, box.top() + 12 * S, box.width() - 20 * S, 14 * S),
              kpis.at(i).k, kPeach, int(10 * S), Qt::AlignLeft | Qt::AlignTop);
        QFont vf = lcars(int(25 * S));
        p.setFont(vf);
        p.setPen(kpis.at(i).c);
        const QRectF vr(box.left() + 10 * S, box.top() + 30 * S, box.width() - 20 * S, 34 * S);
        p.drawText(vr, Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(vf).elidedText(kpis.at(i).v, Qt::ElideRight, vr.width()));
        bar(p, QRectF(box.left() + 10 * S, box.bottom() - 16 * S, box.width() - 20 * S, 6 * S),
            kpis.at(i).ratio, kpis.at(i).c, QColor(0x1c, 0x1c, 0x1c));
    }
}

void LcarsView::screenDiag(QPainter &p, const QRectF &a)
{
    const qreal S = theme::uiScale();
    struct Row { QString k, v; };
    const QVector<Row> rows = {
        {QStringLiteral("NODO"), m_sys.host.toUpper()},
        {QStringLiteral("KERNEL"), m_sys.kernel},
        {QStringLiteral("ATTIVITA"), clockText(m_sys.uptime)},
        {QStringLiteral("PROCESSORI"), QStringLiteral("%1").arg(m_sys.threads)},
        {QStringLiteral("MEMORIA"), humanBytes(double(m_sys.memTotal))},
        {QStringLiteral("RENDERER"), QStringLiteral("OPENGL 3.3 CORE · NEONTERM GL")},
        {QStringLiteral("SESSIONE"), QStringLiteral("PTY REAL · VT100/ANSI")},
    };
    qreal y = a.top();
    for (int i = 0; i < rows.size(); ++i) {
        const QRectF r(a.left(), y, a.width(), 40 * S);
        p.setPen(Qt::NoPen);
        p.setBrush(i % 2 ? QColor(0x12, 0x12, 0x12) : QColor(0x08, 0x08, 0x08));
        p.drawRect(r);
        p.setBrush(kSeries[i % 6]);
        p.drawRect(QRectF(r.left(), r.top(), 6 * S, r.height()));
        label(p, r.adjusted(18 * S, 0, 0, 0), rows.at(i).k, kLilac, int(11 * S));
        p.setFont(lcars(int(12 * S)));
        p.setPen(kPeach);
        p.drawText(r.adjusted(0, 0, -14 * S, 0), Qt::AlignVCenter | Qt::AlignRight,
                   rows.at(i).v.toUpper());
        y += 44 * S;
    }
}

void LcarsView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    paintFrame(p);

    const qreal S = theme::uiScale();
    const qreal rail = std::clamp<qreal>(width() * 0.21, 128 * S, 260 * S);
    const QRectF area(rail + 22 * S, 70 * S, width() - rail - 44 * S, height() - 112 * S);

    // repaint hits from scratch: paintFrame already added the rail ones
    const auto railHits = m_hits;

    if (atLeaf())
        paintScreen(p, area);
    else
        paintGroup(p, area);

    m_hits = railHits + m_hits.mid(railHits.size());
}

void LcarsView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF pos = e->position();
    for (int i = m_hits.size() - 1; i >= 0; --i) {
        const Hit &h = m_hits.at(i);
        if (!h.rect.contains(pos))
            continue;
        if (h.back) {
            back();
            return;
        }
        if (h.child == -2) {
            home();
            return;
        }
        if (h.child >= 0 && enterTitle(children().value(h.child)))
            return;
    }
}
