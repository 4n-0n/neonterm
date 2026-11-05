#include <QtTest>

#include "core/Screen.h"
#include "core/Session.h"
#include "ui/MediaStore.h"

#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QFileInfo>

#include <cmath>

namespace {

QString makePng(const QTemporaryDir &dir, const QSize &size = QSize(64, 32))
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor(0x22, 0xe0, 0xff));
    const QString path = dir.filePath(QStringLiteral("probe.png"));
    img.save(path);
    return path;
}

} // namespace

class TestMedia : public QObject
{
    Q_OBJECT

private slots:
    void protocolPlacesMediaAtTheCursor();
    void mediaScrollsWithTheBuffer();
    void storeDecodesAnImage();
    void showToolSpeaksTheProtocol();
};

void TestMedia::protocolPlacesMediaAtTheCursor()
{
    QTemporaryDir dir;
    const QString png = makePng(dir);

    Session s;
    s.screen().resize(60, 12);
    s.screen().reset();
    s.feed("two lines\r\nsecond\r\n");

    QSignalSpy added(&s, &Session::mediaAdded);
    const QByteArray seq = "\x1b]7770;show;path=" + QUrl::toPercentEncoding(png)
                         + ";cols=20;rows=6;kind=image;page=1\x07";
    s.feed(seq);

    QCOMPARE(added.count(), 1);
    QCOMPARE(s.screen().media().size(), size_t(1));
    const MediaItem &item = s.screen().media().front();
    QCOMPARE(item.path, png);
    QCOMPARE(item.kind, QStringLiteral("image"));
    QCOMPARE(item.cols, 20);
    QCOMPARE(item.rows, 6);
    QCOMPARE(item.page, 1);
    QVERIFY(item.id > 0);
    // the cursor was on the third line when the request arrived
    QCOMPARE(item.abs, 2);
    QCOMPARE(item.col, 0);

    // a stop request clears the grid
    s.feed("\x1b]7770;stop\x07");
    QVERIFY(s.screen().media().empty());
}

void TestMedia::mediaScrollsWithTheBuffer()
{
    Screen screen;
    screen.resize(20, 5);
    screen.reset();

    MediaItem item;
    item.id = 1;
    item.abs = 4;
    item.cols = 4;
    item.rows = 3;
    item.kind = QStringLiteral("image");
    screen.addMedia(item);

    // scrolling into the history does not renumber anything: abs 4 simply walks
    // up the viewport, which is what keeps an image glued to its text
    screen.moveTo(0, 4);
    for (int i = 0; i < 3; ++i) {
        screen.carriageReturn();
        screen.newline();
    }
    QCOMPARE(screen.media().size(), size_t(1));
    QCOMPARE(screen.media().front().abs, 4);
    QCOMPARE(screen.historySize(), 3);

    // when the history is trimmed the absolute numbering shifts down
    screen.setHistoryCap(2);
    for (int i = 0; i < 6; ++i) {
        screen.carriageReturn();
        screen.newline();
    }
    QVERIFY2(screen.media().empty() || screen.media().front().abs < 4,
             "l'ancoraggio non e' stato riallineato dopo il taglio della cronologia");
}

void TestMedia::storeDecodesAnImage()
{
    QTemporaryDir dir;
    const QString png = makePng(dir, QSize(80, 40));

    MediaStore store;
    MediaItem item;
    item.id = 7;
    item.path = png;
    item.kind = QStringLiteral("image");
    item.cols = 10;
    item.rows = 5;
    store.add(item);

    const QImage frame = store.frame(item);
    QVERIFY2(!frame.isNull(), "immagine non decodificata");
    QCOMPARE(frame.size(), QSize(80, 40));
    QCOMPARE(store.sourceSize(item), QSize(80, 40));
    QVERIFY(store.revision(item) > 0);
}

void TestMedia::showToolSpeaksTheProtocol()
{
    const QString show = qEnvironmentVariable("SHOW_BIN");
    if (show.isEmpty() || !QFileInfo::exists(show))
        QSKIP("SHOW_BIN non impostato");

    QTemporaryDir dir;
    const QString png = makePng(dir, QSize(200, 100));

    QProcess proc;
    proc.start(show, {QStringLiteral("--print"), png});
    QVERIFY(proc.waitForFinished(8000));
    QCOMPARE(proc.exitCode(), 0);

    const QByteArray out = proc.readAllStandardOutput();
    QVERIFY2(out.startsWith("\x1b]7770;show;path="), out.constData());
    QVERIFY(out.contains(";kind=image;"));
    QVERIFY(out.contains(";cols="));
    QVERIFY(out.contains(";rows="));

    // 200x100 at the default width: the tool must ask for a 2:1-ish block
    const int cols = out.mid(out.indexOf(";cols=") + 6).split(';').first().toInt();
    const int rows = out.mid(out.indexOf(";rows=") + 6).split(';').first().toInt();
    QVERIFY(cols >= 8 && cols <= 200);
    QVERIFY(rows >= 3 && rows <= 40);
    QVERIFY2(std::abs(double(cols) / double(rows) - 2.0 / 0.45) < 6.0,
             qPrintable(QStringLiteral("blocco %1x%2").arg(cols).arg(rows)));

    // text files are printed, not placed
    QProcess txt;
    QFile f(dir.filePath(QStringLiteral("note.txt")));
    f.open(QIODevice::WriteOnly);
    f.write("riga uno\nriga due\n");
    f.close();
    txt.start(show, {dir.filePath(QStringLiteral("note.txt"))});
    QVERIFY(txt.waitForFinished(8000));
    QVERIFY(txt.readAllStandardOutput().contains("riga due"));
}

QTEST_GUILESS_MAIN(TestMedia)
#include "test_media.moc"
