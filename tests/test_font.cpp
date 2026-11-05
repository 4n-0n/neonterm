#include <QtTest>

#include "ui/NeonFont.h"

class TestFont : public QObject
{
    Q_OBJECT

private slots:
    void coversPrintableAscii();
    void drawsBoxAndBlocks();
    void composesAccents();
    void leavesUnknownToTheAtlas();
};

void TestFont::coversPrintableAscii()
{
    for (char32_t cp = 0x20; cp <= 0x7e; ++cp) {
        const QVector<neonfont::Segment> *g = neonfont::glyph(cp);
        QVERIFY2(g != nullptr,
                 qPrintable(QStringLiteral("missing U+%1").arg(uint(cp), 4, 16, QLatin1Char('0'))));
        if (cp != U' ')
            QVERIFY2(!g->isEmpty(), qPrintable(QStringLiteral("empty U+%1").arg(uint(cp))));
        QVERIFY(neonfont::covers(cp));
    }
}

void TestFont::drawsBoxAndBlocks()
{
    QVector<neonfont::Rect> rects;
    for (char32_t cp : {0x2500, 0x2502, 0x250c, 0x2518, 0x253c, 0x2550, 0x2551, 0x2554, 0x256c,
                        0x2588, 0x2580, 0x2584, 0x2591, 0x2593}) {
        QVERIFY2(neonfont::boxRects(cp, rects),
                 qPrintable(QStringLiteral("no rects for U+%1").arg(uint(cp), 4, 16, QLatin1Char('0'))));
        QVERIFY(!rects.isEmpty());
    }

    // braille: one rectangle per raised dot (0x284a = bits 1,3,6 = dots 2,4,7)
    QVERIFY(neonfont::boxRects(0x284a, rects));
    QCOMPARE(rects.size(), 3);
    QVERIFY(neonfont::boxRects(0x2800, rects));   // blank braille
}

void TestFont::composesAccents()
{
    const QVector<neonfont::Segment> *a = neonfont::glyph(U'a');
    const QVector<neonfont::Segment> *agrave = neonfont::glyph(0x00e0);
    QVERIFY(a && agrave);
    QVERIFY(agrave->size() > a->size());

    bool markAboveXHeight = false;
    for (const neonfont::Segment &s : *agrave) {
        if (s.y0 <= 3.0f && s.y1 <= 3.0f)
            markAboveXHeight = true;
    }
    QVERIFY2(markAboveXHeight, "il segno dell'accento non sta sopra la lettera");

    // uppercase accents have to climb above the cap line: negative y on purpose
    const QVector<neonfont::Segment> *Agrave = neonfont::glyph(0x00c0);
    QVERIFY(Agrave);
    bool aboveCap = false;
    for (const neonfont::Segment &s : *Agrave) {
        if (s.y0 < 0.0f || s.y1 < 0.0f)
            aboveCap = true;
    }
    QVERIFY2(aboveCap, "l'accento maiuscolo non supera la linea delle maiuscole");

    QVERIFY(neonfont::glyph(0x00e7));   // cedilla
    QVERIFY(neonfont::glyph(0x00f1));   // tilde
    QVERIFY(neonfont::glyph(0x20ac));   // euro
}

void TestFont::leavesUnknownToTheAtlas()
{
    QVERIFY(neonfont::glyph(0x4e00) == nullptr);    // CJK: atlas fallback
    QVERIFY(!neonfont::covers(0x4e00));
    QVERIFY(neonfont::glyph(0x1f600) == nullptr);   // emoji: atlas fallback
}

QTEST_GUILESS_MAIN(TestFont)
#include "test_font.moc"
