#pragma once

#include <QSize>
#include <QWidget>

// StatusLine: the instrument strip under the terminal.
class StatusLine : public QWidget
{
    Q_OBJECT
public:
    explicit StatusLine(QWidget *parent = nullptr);

    void setSessionInfo(const QString &program, int pid, const QSize &grid);
    void setFps(double fps);
    void setEffects(bool crt);
    void setState(const QString &text, bool ok);
    void setFontPixelSize(int px);
    void setRenderer(const QString &name);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QString m_program = QStringLiteral("—");
    int m_pid = 0;
    QSize m_grid;
    double m_fps = 0;
    bool m_crt = true;
    int m_fontPx = 14;
    QString m_renderer = QStringLiteral("CPU BLUR");
    QString m_state = QStringLiteral("READY");
    bool m_ok = true;
};
