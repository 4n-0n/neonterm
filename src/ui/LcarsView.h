#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QVector>
#include <QWidget>

// LcarsView: the "computer of the Enterprise" face of the terminal. A tree of
// groups, every level a handful of very large targets, leaves are live screens.
// It is drawn entirely by hand (no widgets) in the LCARS palette: black field,
// orange/lilac/blue elbows and pills.
class LcarsView : public QWidget
{
    Q_OBJECT
public:
    explicit LcarsView(QWidget *parent = nullptr);

    // navigation, also used by the tests
    QStringList breadcrumb() const;
    QStringList children() const;       // titles of the visible buttons
    bool enterTitle(const QString &title);
    void back();
    void home();
    bool atLeaf() const;
    QString currentScreen() const;      // kind id of the leaf, empty for groups

    void restart();                     // resample and repaint now
    void debugState() const;

signals:
    void leaveRequested();              // "TERMINALE" / back from the root

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    struct Node {
        QString title;
        QString screen;          // empty => group
        QVector<Node> children;
    };

    struct Hit {
        QRectF rect;
        int child = -1;
        bool back = false;
    };

    struct Sys {
        QVector<double> cores;
        QVector<double> history;          // total cpu 0..1, last N samples
        double load1 = 0, load5 = 0, load15 = 0;
        qint64 memTotal = 0, memAvail = 0, swapTotal = 0, swapFree = 0;
        QVector<QPair<QString, double>> mounts;      // "used / total", ratio
        QVector<QPair<QString, QString>> mountText;
        QVector<QPair<QString, double>> temps;
        QVector<QPair<QString, QPair<double, double>>> net;   // iface, rx/tx B/s
        QVector<double> netHistory;
        QVector<QPair<QString, qint64>> procsMem;
        QVector<QPair<QString, double>> procsCpu;
        double uptime = 0;
        QString kernel;
        QString host;
        int battery = -1;
        bool charging = false;
        int failedUnits = -1;
        int threads = 0;
    };

    void buildTree();
    const Node *node() const;
    void sample();
    void paintFrame(QPainter &p);
    void paintGroup(QPainter &p, const QRectF &area);
    void paintScreen(QPainter &p, const QRectF &area);

    void screenLoad(QPainter &p, const QRectF &a);
    void screenMemory(QPainter &p, const QRectF &a);
    void screenDisks(QPainter &p, const QRectF &a);
    void screenThermal(QPainter &p, const QRectF &a);
    void screenPower(QPainter &p, const QRectF &a);
    void screenNet(QPainter &p, const QRectF &a);
    void screenTraffic(QPainter &p, const QRectF &a);
    void screenTopCpu(QPainter &p, const QRectF &a);
    void screenTopMem(QPainter &p, const QRectF &a);
    void screenDiag(QPainter &p, const QRectF &a);
    void statusStrip(QPainter &p, const QRectF &a);

    // lcars primitives
    void pill(QPainter &p, const QRectF &r, const QColor &c, const QString &text,
              const QColor &ink = Qt::black, bool right = true);
    void bar(QPainter &p, const QRectF &r, double ratio, const QColor &c, const QColor &bg);
    void label(QPainter &p, const QRectF &r, const QString &text, const QColor &c, int px,
               Qt::Alignment al = Qt::AlignLeft | Qt::AlignVCenter, bool bold = true);
    void spark(QPainter &p, const QRectF &r, const QVector<double> &v, const QColor &c);
    void elbow(QPainter &p, const QRectF &r, const QColor &c, bool rightSide);

    QVector<Node> m_root;
    QVector<int> m_path;
    QVector<Hit> m_hits;
    Sys m_sys;
    QTimer m_clock;
    QImage m_logo;
    QElapsedTimer m_beat;
    qint64 m_lastSample = 0;

    // previous counters, for the deltas
    QVector<qint64> m_prevCpu;
    QVector<qint64> m_prevNetRx;
    QVector<qint64> m_prevNetTx;
    QHash<int, qint64> m_prevProc;
    qint64 m_prevTotal = 0;
    qint64 m_prevIdle = 0;
    QElapsedTimer m_procClock;
};
