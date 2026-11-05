#include "core/Session.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <cerrno>
#include <unistd.h>

Session::Session(QObject *parent)
    : QObject(parent)
    , m_parser(&m_screen)
{
    m_parser.setResponder([this](const QByteArray &reply) { write(reply); });
    m_parser.setMediaHandler([this](const QByteArray &payload) { handleOscMedia(payload); });
}

Session::~Session()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    m_pty.hangup();
    m_pty.closeFd();
}

bool Session::start(const QString &program, const QStringList &args, int cols, int rows,
                    const QString &cwd)
{
    m_screen.resize(cols, rows);
    m_screen.reset();
    m_parser.reset();
    m_demoBytes.clear();
    m_program = program;
    m_lastTitle.clear();

    QString err;
    if (!m_pty.start(program, args, cols, rows, cwd, &err)) {
        m_running = false;
        feed("\x1b[1;31m" + err.toUtf8() + "\x1b[0m\r\n");
        return false;
    }
    m_running = true;
    m_notifier = new QSocketNotifier(m_pty.fd(), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { drain(); });
    return true;
}

bool Session::startShell(int cols, int rows, const QString &cwd)
{
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFile::exists(shell))
        shell = QStringLiteral("/bin/bash");
    return start(shell, {}, cols, rows, cwd);
}

void Session::startDemo(const QByteArray &bytes)
{
    m_program = QStringLiteral("demo");
    m_running = false;
    m_demoBytes = bytes;
    m_screen.reset();
    feed(bytes);
}

void Session::handleOscMedia(const QByteArray &payload)
{
    // show;path=<pct>;cols=N;rows=N;kind=image;page=1   |   stop
    const QList<QByteArray> parts = payload.split(';');
    if (parts.isEmpty())
        return;

    const QByteArray verb = parts.value(0).trimmed();
    if (verb == "stop") {
        m_screen.clearMedia();
        emit mediaStopped();
        emit updated();
        return;
    }
    if (verb != "show")
        return;

    QHash<QByteArray, QByteArray> kv;
    for (int i = 1; i < parts.size(); ++i) {
        const int eq = parts[i].indexOf('=');
        if (eq > 0)
            kv.insert(parts[i].left(eq), parts[i].mid(eq + 1));
    }

    const QString path = QString::fromUtf8(QByteArray::fromPercentEncoding(kv.value("path")));
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    const Pos cur = m_screen.cursor();
    MediaItem item;
    item.path = path;
    item.kind = QString::fromUtf8(kv.value("kind", "image"));
    item.page = std::max(1, kv.value("page", "1").toInt());
    item.col = std::clamp(cur.x, 0, m_screen.cols() - 4);
    item.cols = std::clamp(kv.value("cols", "40").toInt(), 4, m_screen.cols() - item.col);
    item.rows = std::clamp(kv.value("rows", "10").toInt(), 2, 60);
    item.abs = m_screen.historySize() + cur.y;
    item.id = m_screen.nextMediaId();

    m_screen.addMedia(item);
    emit mediaAdded(item);
    emit updated();
}

void Session::feed(const QByteArray &data)
{
    m_parser.feed(data.constData(), int(data.size()));
    publish();
}

void Session::publish()
{
    emit updated();
    const QString t = m_screen.title();
    if (t != m_lastTitle) {
        m_lastTitle = t;
        emit titleChanged(t);
    }
}

void Session::readChunk()
{
    char buf[65536];
    bool eof = false;
    qint64 budget = 64 * 1024;    // never starve the event loop: a flooding
                                  // producer would keep us in this loop forever
    while (budget > 0) {
        const ssize_t n = ::read(m_pty.fd(), buf, sizeof(buf));
        if (n > 0) {
            m_parser.feed(buf, int(n));
            budget -= n;
            continue;
        }
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        // 0 or EIO: the child side is gone
        eof = true;
        break;
    }
    publish();
    flushPending();

    if (!eof && budget <= 0 && m_running) {
        // yield: a flooding child would otherwise keep the notifier firing and
        // the paint events would only be served once the pipe runs dry
        if (m_notifier)
            m_notifier->setEnabled(false);
        QTimer::singleShot(4, this, [this] {
            if (!m_running)
                return;
            if (m_notifier)
                m_notifier->setEnabled(true);
            readChunk();
        });
        return;
    }

    if (eof && m_running) {
        int code = 0;
        bool done = m_pty.reap(&code);
        for (int i = 0; i < 40 && !done; ++i) {
            QThread::msleep(5);
            done = m_pty.reap(&code);
        }
        if (done)
            finishChild(code);
    }
}

void Session::drain()
{
    if (m_pty.fd() < 0)
        return;
    readChunk();
}

void Session::finishChild(int code)
{
    if (!m_running)
        return;
    m_running = false;
    m_exitCode = code;
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    m_pty.closeFd();
    publish();
    QTimer::singleShot(0, this, [this] {
        if (m_running)
            return;
        emit exited(m_exitCode);
    });
}

void Session::write(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    if (!m_outQueue.isEmpty()) {
        m_outQueue += data;
        flushPending();
        return;
    }
    if (m_pty.fd() < 0)
        return;
    qint64 off = 0;
    while (off < data.size()) {
        const qint64 n = m_pty.write(data.constData() + off, data.size() - off);
        if (n > 0) {
            off += n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            m_outQueue = data.mid(int(off));
            return;
        }
        break;
    }
}

void Session::flushPending()
{
    if (m_outQueue.isEmpty() || m_pty.fd() < 0)
        return;
    const qint64 n = m_pty.write(m_outQueue.constData(), m_outQueue.size());
    if (n > 0)
        m_outQueue.remove(0, int(n));
}

void Session::resize(int cols, int rows)
{
    if (cols == m_screen.cols() && rows == m_screen.rows())
        return;
    m_screen.resize(cols, rows);
    m_pty.resize(cols, rows);
    if (!m_demoBytes.isEmpty()) {
        // the demo reflows to the new grid instead of being clipped
        m_screen.reset();
        m_parser.reset();
        feed(m_demoBytes);
    }
    emit updated();
}
