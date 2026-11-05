#pragma once

#include "core/Pty.h"
#include "core/Screen.h"
#include "core/VtParser.h"

#include <QObject>

class QSocketNotifier;

// Session: one terminal — Screen + parser + pty + lifecycle signals.
class Session : public QObject
{
    Q_OBJECT
public:
    explicit Session(QObject *parent = nullptr);
    ~Session() override;

    bool start(const QString &program, const QStringList &args, int cols, int rows,
               const QString &cwd = QString());
    bool startShell(int cols, int rows, const QString &cwd = QString());
    void startDemo(const QByteArray &bytes);

    Screen &screen() { return m_screen; }
    const Screen &screen() const { return m_screen; }

    void write(const QByteArray &data);
    void resize(int cols, int rows);
    bool running() const { return m_running; }
    pid_t pid() const { return m_pty.pid(); }
    QString program() const { return m_program; }
    int exitCode() const { return m_exitCode; }

    // bytes straight into the parser (demo, tests, --exec replay)
    void feed(const QByteArray &data);

signals:
    void updated();
    void titleChanged(const QString &title);
    void exited(int code);
    // a `show` request was accepted and placed on the grid
    void mediaAdded(const MediaItem &item);
    void mediaStopped();

private:
    void handleOscMedia(const QByteArray &payload);
    void drain();
    void readChunk();
    void finishChild(int code);
    void flushPending();
    void publish();

    Screen m_screen;
    VtParser m_parser;
    Pty m_pty;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_outQueue;
    QString m_program;
    QString m_lastTitle;
    bool m_running = false;
    int m_exitCode = 0;
    QByteArray m_demoBytes;
};
