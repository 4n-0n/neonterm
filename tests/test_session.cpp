#include <QtTest>

#include "core/Session.h"

#include <QElapsedTimer>
#include <QSignalSpy>

#include <functional>

namespace {

bool waitFor(const std::function<bool()> &pred, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (pred())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    return pred();
}

} // namespace

class TestSession : public QObject
{
    Q_OBJECT

private slots:
    void capturesOutputAndExitCode();
    void interactiveShellReportsTheGrid();
    void oscTitleBecomesASignal();
    void writesReachTheChild();
};

void TestSession::capturesOutputAndExitCode()
{
    Session s;
    QSignalSpy spy(&s, &Session::exited);
    QVERIFY(s.start(QStringLiteral("/bin/sh"),
                    {QStringLiteral("-c"), QStringLiteral("printf 'hello-pty\\n'; exit 3")},
                    40, 10));
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 3);
    QVERIFY2(s.screen().dump().contains(QStringLiteral("hello-pty")),
             qPrintable(s.screen().dump()));
    QVERIFY(!s.running());
}

void TestSession::interactiveShellReportsTheGrid()
{
    Session s;
    QVERIFY(s.start(QStringLiteral("/bin/sh"), {}, 40, 10));
    s.resize(100, 30);
    QCOMPARE(s.screen().cols(), 100);
    QCOMPARE(s.screen().rows(), 30);
    s.write("stty size\r");
    QVERIFY2(waitFor([&] { return s.screen().dump().contains(QStringLiteral("30 100")); }),
             qPrintable(s.screen().dump()));
}

void TestSession::oscTitleBecomesASignal()
{
    Session s;
    QSignalSpy spy(&s, &Session::titleChanged);
    QVERIFY(s.start(QStringLiteral("/bin/sh"),
                    {QStringLiteral("-c"), QStringLiteral("printf '\\033]0;hello-title\\007'")},
                    60, 12));
    QVERIFY(waitFor([&] { return spy.count() > 0; }));
    QCOMPARE(spy.last().at(0).toString(), QStringLiteral("hello-title"));
    QCOMPARE(s.screen().title(), QStringLiteral("hello-title"));
}

void TestSession::writesReachTheChild()
{
    Session s;
    QVERIFY(s.start(QStringLiteral("/bin/cat"), {}, 60, 12));
    s.write("round-trip\n");
    QVERIFY2(waitFor([&] { return s.screen().dump().contains(QStringLiteral("round-trip")); }),
             qPrintable(s.screen().dump()));
}

QTEST_GUILESS_MAIN(TestSession)
#include "test_session.moc"
