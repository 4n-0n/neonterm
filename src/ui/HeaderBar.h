#pragma once

#include <QStringList>
#include <QWidget>

class QTimer;

// HeaderBar: title bar of the app, painted by hand — logo mark, tab strip,
// per-tab close buttons, "+" and a live status readout on the right.
class HeaderBar : public QWidget
{
    Q_OBJECT
public:
    explicit HeaderBar(QWidget *parent = nullptr);

    void setTabs(const QStringList &titles, int active);
    void setInfo(const QString &info);
    int activeTab() const { return m_active; }

    void setStui(bool on);
    bool stui() const { return m_stuiOn; }
    QRectF stuiSwitchRect() const { return m_stuiRect; }

signals:
    void stuiToggled(bool on);
    void tabActivated(int index);
    void tabCloseRequested(int index);
    void newTabRequested();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    QSize sizeHint() const override;

private:
    struct TabRect {
        QRect rect;
        QRect closeRect;
        int index = 0;
    };
    void relayout();
    int tabAt(const QPoint &p, bool *onClose) const;

    QStringList m_titles;
    QVector<TabRect> m_rects;
    QRect m_plusRect;
    QString m_info;
    bool m_stuiOn = false;
    QRectF m_stuiRect;
    int m_active = 0;
    int m_hoverTab = -1;
    int m_hoverClose = -1;
    bool m_hoverPlus = false;
    bool m_pressed = false;
    QString m_clock;
    QTimer *m_clockTimer = nullptr;
};
