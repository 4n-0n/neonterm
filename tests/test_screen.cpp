#include <QtTest>

#include "core/Screen.h"
#include "core/VtParser.h"

#include <cstring>

namespace {

struct Fixture {
    Screen screen;
    VtParser parser{&screen};

    Fixture(int cols = 20, int rows = 6)
    {
        screen.resize(cols, rows);
        screen.reset();
    }

    void feed(const QByteArray &bytes) { parser.feed(bytes.constData(), int(bytes.size())); }
    QString text(int row) const { return screen.lineText(row); }
};

} // namespace

class TestScreen : public QObject
{
    Q_OBJECT

private slots:
    void writesText();
    void wrapsAtTheRightEdge();
    void autoWrapCanBeDisabled();
    void sgrBasicAndExtended();
    void cursorMoves();
    void eraseInLineAndDisplay();
    void scrollingFeedsHistory();
    void wideGlyphsTakeTwoCells();
    void altScreenRestoresPrimary();
    void scrollRegionLimitsScrolling();
    void oscSetsTitle();
    void resizeKeepsContent();
    void insertAndDelete();
    void deviceStatusReportReplies();
};

void TestScreen::writesText()
{
    Fixture f;
    f.feed("hello\r\nworld");
    QCOMPARE(f.text(0), QStringLiteral("hello"));
    QCOMPARE(f.text(1), QStringLiteral("world"));
    QCOMPARE(f.screen.cursor().x, 5);
    QCOMPARE(f.screen.cursor().y, 1);
}

void TestScreen::wrapsAtTheRightEdge()
{
    Fixture f(10, 4);
    f.feed("0123456789");
    QCOMPARE(f.text(0), QStringLiteral("0123456789"));
    QCOMPARE(f.screen.cursor().x, 9);
    f.feed("X");
    QCOMPARE(f.text(0), QStringLiteral("0123456789"));
    QCOMPARE(f.text(1), QStringLiteral("X"));
}

void TestScreen::autoWrapCanBeDisabled()
{
    Fixture f(10, 4);
    f.feed("\x1b[?7l");
    f.feed("0123456789ABC");
    QCOMPARE(f.text(0), QStringLiteral("012345678C"));
    QCOMPARE(f.text(1), QString());
}

void TestScreen::sgrBasicAndExtended()
{
    Fixture f(20, 4);
    f.feed("\x1b[1;31mA");
    const Cell c0 = f.screen.cellAt(0, 0);
    QCOMPARE(quint32(c0.ch), quint32('A'));
    QCOMPARE(c0.fg, f.screen.palette().ansi[1]);
    QVERIFY(c0.flags & CF_Bold);

    f.feed("\x1b[0mB");
    QCOMPARE(f.screen.cellAt(0, 1).fg, QRgb(0));

    f.feed("\x1b[38;5;196mC");
    QCOMPARE(f.screen.cellAt(0, 2).fg, f.screen.palette().color256(196));

    f.feed("\x1b[48;2;10;20;30mD");
    QCOMPARE(f.screen.cellAt(0, 3).bg, qRgb(10, 20, 30));
    QCOMPARE(f.screen.cellAt(0, 3).fg, f.screen.palette().color256(196));

    f.feed("\x1b[0;3;4;7;9mE");
    const Cell e = f.screen.cellAt(0, 4);
    QVERIFY(e.flags & CF_Italic);
    QVERIFY(e.flags & CF_Underline);
    QVERIFY(e.flags & CF_Reverse);
    QVERIFY(e.flags & CF_Strike);
}

void TestScreen::cursorMoves()
{
    Fixture f(20, 6);
    f.feed("\x1b[3;5H");
    QCOMPARE(f.screen.cursor().x, 4);
    QCOMPARE(f.screen.cursor().y, 2);
    f.feed("\x1b[2A");
    QCOMPARE(f.screen.cursor().y, 0);
    f.feed("\x1b[3C\x1b[1G");
    QCOMPARE(f.screen.cursor().x, 0);
    f.feed("\x1b[2;3f");
    QCOMPARE(f.screen.cursor().x, 2);
    QCOMPARE(f.screen.cursor().y, 1);
    f.feed("\x1b[s\x1b[6;1H\x1b[u");
    QCOMPARE(f.screen.cursor().y, 1);
    QCOMPARE(f.screen.cursor().x, 2);
}

void TestScreen::eraseInLineAndDisplay()
{
    Fixture f(10, 4);
    f.feed("abcdef\x1b[1;3H\x1b[K");
    QCOMPARE(f.text(0), QStringLiteral("ab"));
    f.feed("\x1b[2;1Hxyz\x1b[2J");
    QCOMPARE(f.text(0), QString());
    QCOMPARE(f.text(1), QString());

    f.feed("12345\x1b[1;3H\x1b[1K");
    QCOMPARE(f.text(0), QString());
}

void TestScreen::scrollingFeedsHistory()
{
    Fixture f(20, 5);
    for (int i = 0; i < 9; ++i)
        f.feed("line" + QByteArray::number(i) + "\r\n");
    QVERIFY(f.screen.historySize() >= 3);
    QVERIFY(f.screen.absLineText(0).startsWith(QStringLiteral("line0")));
    QVERIFY(f.screen.dump().contains(QStringLiteral("line8")));
}

void TestScreen::wideGlyphsTakeTwoCells()
{
    Fixture f(10, 4);
    f.feed(QStringLiteral("あ").toUtf8());
    QCOMPARE(quint32(f.screen.cellAt(0, 0).ch), quint32(0x3042));
    QVERIFY(f.screen.cellAt(0, 1).flags & CF_Cont);
    QCOMPARE(f.screen.cursor().x, 2);
    QCOMPARE(f.text(0), QStringLiteral("あ"));

    f.feed(QStringLiteral("😀").toUtf8());
    QCOMPARE(quint32(f.screen.cellAt(0, 2).ch), quint32(0x1F600));
    QVERIFY(f.screen.cellAt(0, 3).flags & CF_Cont);
}

void TestScreen::altScreenRestoresPrimary()
{
    Fixture f(20, 5);
    f.feed("primary");
    f.feed("\x1b[?1049h");
    QVERIFY(f.screen.modes().altScreen);
    QCOMPARE(f.text(0), QString());
    f.feed("\x1b[2;1Hzzz");
    QCOMPARE(f.text(1), QStringLiteral("zzz"));
    f.feed("\x1b[?1049l");
    QVERIFY(!f.screen.modes().altScreen);
    QCOMPARE(f.text(0), QStringLiteral("primary"));
}

void TestScreen::scrollRegionLimitsScrolling()
{
    Fixture f(10, 6);
    f.feed("a\r\nb\r\nc\r\nd\r\ne\r\nf");
    f.feed("\x1b[2;4r");
    QCOMPARE(f.screen.scrollTop(), 1);
    QCOMPARE(f.screen.scrollBottom(), 3);
    f.feed("\x1b[4;1H\n");
    // the region scrolled: "b" left the region, the top line is untouched and
    // nothing went into the scrollback, because this was not the whole screen
    QCOMPARE(f.text(0), QStringLiteral("a"));
    QCOMPARE(f.text(1), QStringLiteral("c"));
    QCOMPARE(f.text(4), QStringLiteral("e"));
    QCOMPARE(f.screen.historySize(), 0);
    f.feed("\x1b[r");
    QCOMPARE(f.screen.scrollTop(), 0);
    QCOMPARE(f.screen.scrollBottom(), 5);
}

void TestScreen::oscSetsTitle()
{
    Fixture f;
    f.feed("\x1b]0;window title\x07");
    QCOMPARE(f.screen.title(), QStringLiteral("window title"));
    f.feed("\x1b]2;second\x1b\\");
    QCOMPARE(f.screen.title(), QStringLiteral("second"));
}

void TestScreen::resizeKeepsContent()
{
    Fixture f(20, 4);
    f.feed("hello\r\nworld");
    f.screen.resize(30, 8);
    QCOMPARE(f.screen.cols(), 30);
    QCOMPARE(f.screen.rows(), 8);
    QCOMPARE(f.text(0), QStringLiteral("hello"));
    QCOMPARE(f.text(1), QStringLiteral("world"));
    f.screen.resize(3, 2);
    QCOMPARE(f.text(0), QStringLiteral("hel"));
}

void TestScreen::insertAndDelete()
{
    Fixture f(12, 4);
    f.feed("abcdef\x1b[1;3H\x1b[2@");
    QCOMPARE(f.text(0), QStringLiteral("ab  cdef"));
    f.feed("\x1b[1;3H\x1b[2P");
    QCOMPARE(f.text(0), QStringLiteral("abcdef"));

    f.feed("\x1b[1;1H\x1b[2Jone\r\ntwo\r\nthree");
    f.feed("\x1b[2;1H\x1b[L");
    QCOMPARE(f.text(1), QString());
    QCOMPARE(f.text(2), QStringLiteral("two"));
    f.feed("\x1b[2;1H\x1b[M");
    QCOMPARE(f.text(1), QStringLiteral("two"));
}

void TestScreen::deviceStatusReportReplies()
{
    Fixture f(20, 6);
    QByteArray reply;
    f.parser.setResponder([&reply](const QByteArray &r) { reply = r; });
    f.feed("\x1b[2;5H\x1b[6n");
    QCOMPARE(reply, QByteArray("\x1b[2;5R"));
    f.feed("\x1b[c");
    QVERIFY(!reply.isEmpty());
    QVERIFY(reply.startsWith("\x1b[?"));
}

QTEST_GUILESS_MAIN(TestScreen)
#include "test_screen.moc"
