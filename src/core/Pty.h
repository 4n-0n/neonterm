#pragma once

#include <QString>
#include <QStringList>
#include <sys/types.h>

// Pty: a forked child on a pseudo terminal, non-blocking master side.
class Pty
{
public:
    Pty() = default;
    ~Pty();

    bool start(const QString &program, const QStringList &args, int cols, int rows,
               const QString &cwd, QString *error);
    int fd() const { return m_master; }
    qint64 write(const char *data, qint64 len);
    void resize(int cols, int rows);
    bool reap(int *code);
    void closeFd();
    void hangup();

    pid_t pid() const { return m_pid; }
    bool reaped() const { return m_reaped; }

private:
    int m_master = -1;
    pid_t m_pid = -1;
    bool m_reaped = false;
};
