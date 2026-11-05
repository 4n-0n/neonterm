#include "core/Pty.h"

#include <QByteArray>
#include <QDir>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

Pty::~Pty()
{
    if (m_master >= 0)
        ::close(m_master);
    if (m_pid > 0 && !m_reaped) {
        // do not leave orphan shells behind when a tab is closed
        ::kill(-m_pid, SIGHUP);
        int st = 0;
        waitpid(m_pid, &st, WNOHANG);
    }
}

bool Pty::start(const QString &program, const QStringList &args, int cols, int rows,
                const QString &cwd, QString *error)
{
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        if (error)
            *error = QStringLiteral("forkpty: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    if (pid == 0) {
        // child
        QByteArray prog = program.toLocal8Bit();
        std::vector<QByteArray> argStore;
        argStore.push_back(prog);
        for (const QString &a : args)
            argStore.push_back(a.toLocal8Bit());
        std::vector<char *> argv;
        argv.reserve(argStore.size() + 1);
        for (auto &a : argStore)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        ::setenv("TERM_PROGRAM", "neonterm", 1);
        ::setenv("TERM_PROGRAM_VERSION", "0.1.0", 1);
        if (!cwd.isEmpty())
            (void)::chdir(QFile::encodeName(cwd).constData());
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    m_pid = pid;
    m_master = master;
    const int flags = ::fcntl(m_master, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(m_master, F_SETFL, flags | O_NONBLOCK);
    return true;
}

qint64 Pty::write(const char *data, qint64 len)
{
    if (m_master < 0 || len <= 0)
        return 0;
    ssize_t n;
    do {
        n = ::write(m_master, data, size_t(len));
    } while (n < 0 && errno == EINTR);
    return n < 0 ? -1 : qint64(n);
}

void Pty::resize(int cols, int rows)
{
    if (m_master < 0)
        return;
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ::ioctl(m_master, TIOCSWINSZ, &ws);
}

bool Pty::reap(int *code)
{
    if (m_pid <= 0 || m_reaped)
        return m_reaped;
    int st = 0;
    const pid_t r = ::waitpid(m_pid, &st, WNOHANG);
    if (r == m_pid) {
        m_reaped = true;
        if (code)
            *code = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? -WTERMSIG(st) : -1);
    }
    return m_reaped;
}

void Pty::closeFd()
{
    if (m_master >= 0) {
        ::close(m_master);
        m_master = -1;
    }
}

void Pty::hangup()
{
    if (m_pid > 0 && !m_reaped)
        ::kill(-m_pid, SIGHUP);
}
