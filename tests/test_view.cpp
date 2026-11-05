#include <QtTest>

#include "core/Session.h"
#include "ui/Demo.h"
#include "ui/GlTermView.h"
#include "ui/HeaderBar.h"
#include "ui/LcarsView.h"
#include "ui/Theme.h"
#include "ui/TermSurface.h"
#include "ui/MediaStore.h"
#include "ui/TermWindow.h"

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QUrl>

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

class TestView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void demoPaintsRealPixels();
    void typedKeysReachTheShell();
    void tabsOpenAndClose();
    void mouseSelectionFillsClipboard();
    void fontSizeChangesTheGrid();
    void paintIsFastEnough();
    void glRendererPaints();
    void reverseVideoPaintsItsBackground();
    void clickingTheCloseButtonRemovesTheImage();
    void playButtonTogglesTheAudioPlayer();
    void stuiSwitchTurnsTheConsoleIntoLcars();
    void lcarsTreeWalksDownAndBack();
    void lcarsPaintsTheLcarsPalette();
    void closeAllMediaShortcutClearsTheViewport();
};

void TestView::initTestCase()
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    // never read or write the developer's real ~/.config/neonnet/neonterm.conf
    const QString dir = QDir::tempPath()
                        + QStringLiteral("/neonterm-testview-%1")
                              .arg(QCoreApplication::applicationPid());
    QDir(dir).removeRecursively();
    qputenv("NEONTERM_CONFIG_DIR", dir.toUtf8());
}

void TestView::init()
{
    // every case starts from defaults: a saved geometry would override resize()
    QDir(QString::fromUtf8(qgetenv("NEONTERM_CONFIG_DIR"))).removeRecursively();
}

void TestView::demoPaintsRealPixels()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1100, 700);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));
    QTest::qWait(150);

    QCOMPARE(view->session()->screen().cols(), view->gridSize().width());
    QCOMPARE(view->session()->screen().rows(), view->gridSize().height());

    const QImage img = win.grab().toImage();
    QVERIFY(!img.isNull());
    int bright = 0;
    int cyan = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb p = img.pixel(x, y);
            if (qRed(p) > 150 || qGreen(p) > 150 || qBlue(p) > 170)
                ++bright;
            if (qBlue(p) > 120 && qGreen(p) > 100)
                ++cyan;
        }
    }
    // the demo banner, palette strips and log lines must leave a lot of ink
    QVERIFY2(bright > 1500, qPrintable(QStringLiteral("bright=%1").arg(bright)));
    QVERIFY2(cyan > 200, qPrintable(QStringLiteral("cyan=%1").arg(cyan)));
}

void TestView::typedKeysReachTheShell()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(900, 600);
    TermSurface *view = win.newTab(QStringLiteral("/bin/cat"));
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 40; }));
    QTest::keyClicks(view, QStringLiteral("abc-123"));
    QVERIFY2(waitFor([&] {
                 return view->session()->screen().dump().contains(QStringLiteral("abc-123"));
             }),
             qPrintable(view->session()->screen().dump()));
}

void TestView::tabsOpenAndClose()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(900, 600);
    win.newDemoTab(demoScript());
    win.newDemoTab(demoScript());
    QCOMPARE(win.tabCount(), 2);
    QCOMPARE(win.tabTitles().size(), 2);
    QCOMPARE(win.activeIndex(), 1);
    win.closeTab(0);
    QCOMPARE(win.tabCount(), 1);
    QCOMPARE(win.activeIndex(), 0);
    QVERIFY(win.headerBar() != nullptr);
}

void TestView::mouseSelectionFillsClipboard()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1100, 700);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));
    QTest::qWait(120);

    QGuiApplication::clipboard()->clear();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(20, 62));
    QTest::mouseMove(view, QPoint(360, 62), 20);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(360, 62));

    QVERIFY(view->hasSelection());
    const QString copied = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    QVERIFY2(!copied.isEmpty(), "la selezione non ha prodotto testo");
}

void TestView::fontSizeChangesTheGrid()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1000, 640);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));
    const int before = view->gridSize().width();
    win.applyFontSize(win.fontPixelSize() + 6);
    QVERIFY(waitFor([&] { return view->gridSize().width() < before; }));
    QCOMPARE(view->fontPixelSize(), win.fontPixelSize());
}

void TestView::paintIsFastEnough()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1400, 900);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));

    // fill the whole grid with coloured text, so the glyph layer is not sparse
    QByteArray filler;
    for (int r = 0; r < 60; ++r) {
        filler += "\x1b[38;2;" + QByteArray::number(60 + r * 3) + ";220;255m";
        for (int c = 0; c < 40; ++c)
            filler += "abcdefghij";
        filler += "\x1b[0m\r\n";
    }
    view->session()->feed(filler);
    QTest::qWait(80);

    const int frames = 30;
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < frames; ++i) {
        view->update();
        win.grab();
    }
    const double perFrame = double(t.elapsed()) / frames;
    qInfo("paint: %.1f ms/frame at %dx%d cells", perFrame, view->gridSize().width(),
          view->gridSize().height());
    QVERIFY2(perFrame < 60.0, qPrintable(QStringLiteral("paint too slow: %1 ms").arg(perFrame)));
}

void TestView::glRendererPaints()
{
    TermWindow win;
    win.setUseOpenGL(true);
    win.resize(980, 640);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 40; }));

    auto *gl = qobject_cast<GlTermView *>(view);
    QVERIFY2(gl != nullptr, "il renderer OpenGL non e' stato creato");
    if (!waitFor([&] { return gl->glReady(); }, 2000))
        QSKIP("nessun contesto OpenGL in questo ambiente");
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        QSKIP("la piattaforma offscreen non espone il framebuffer del QOpenGLWidget");
    QTest::qWait(300);

    qInfo("renderer: %s", qPrintable(gl->rendererInfo()));
    const QImage fb = gl->grabGlFrame();
    QVERIFY2(!fb.isNull(), "grabFramebuffer vuoto");
    QVERIFY(fb.width() >= view->width());
    QVERIFY(fb.height() >= view->height());

    int bright = 0;
    for (int y = 0; y < fb.height(); y += 3) {
        for (int x = 0; x < fb.width(); x += 3) {
            const QRgb p = fb.pixel(x, y);
            if (qRed(p) > 140 || qGreen(p) > 140 || qBlue(p) > 160)
                ++bright;
        }
    }
    QVERIFY2(bright > 400, qPrintable(QStringLiteral("pixels=%1").arg(bright)));
}

void TestView::playButtonTogglesTheAudioPlayer()
{
    const QString ff = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ff.isEmpty())
        QSKIP("ffmpeg non disponibile");

    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1100, 700);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/tone.wav");
    QProcess p;
    p.start(ff, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
                 QStringLiteral("lavfi"), QStringLiteral("-i"),
                 QStringLiteral("sine=frequency=440:duration=6"), QStringLiteral("-y"), path});
    QVERIFY(p.waitForFinished(15000));
    QVERIFY(QFileInfo::exists(path));

    view->session()->feed(QStringLiteral("\x1b]7770;show;path=%1;cols=50;rows=8;kind=audio;page=1\x07")
                              .arg(QString::fromUtf8(QUrl::toPercentEncoding(path)))
                              .toUtf8());
    QVERIFY2(waitFor([&] { return !view->mediaUi().isEmpty(); }), "audio block never appeared");
    if (!waitFor([&] { return view->mediaStore()->hasPlayer(view->session()->screen().media().front()); },
                 8000))
        QSKIP("backend multimediale non disponibile");

    const TermSurface::MediaUi ui = view->mediaUi().front();
    QVERIFY(ui.hasPlayer);
    const QPoint play(ui.bar.left() + 18, ui.bar.center().y());
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, play);
    QTest::qWait(300);
    bool playing = view->mediaStore()->state(view->session()->screen().media().front()).playing;

    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, play);
    QTest::qWait(300);
    const bool playing2 = view->mediaStore()->state(view->session()->screen().media().front()).playing;
    QVERIFY2(playing != playing2, "the play button did not change the player state");
    QVERIFY(!view->hasSelection());
}

void TestView::clickingTheCloseButtonRemovesTheImage()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1100, 700);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/block.png");
    QImage img(320, 180, QImage::Format_RGB32);
    img.fill(QColor(0x22, 0xe0, 0xff));
    QVERIFY(img.save(path));

    const QString osc = QStringLiteral("\x1b]7770;show;path=%1;cols=40;rows=12;kind=image;page=1\x07")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(path)));
    view->session()->feed(osc.toUtf8());
    QVERIFY2(waitFor([&] { return !view->mediaUi().isEmpty(); }), "media block never appeared");

    const TermSurface::MediaUi ui = view->mediaUi().front();
    QVERIFY(ui.close.isValid());
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, ui.close.center().toPoint());
    QVERIFY2(waitFor([&] { return view->mediaUi().isEmpty(); }), "image survived the close button");
    QVERIFY(view->session()->screen().media().empty());
    QVERIFY(!view->hasSelection());
}

void TestView::closeAllMediaShortcutClearsTheViewport()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1100, 700);
    TermSurface *view = win.newDemoTab(demoScript());
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 60; }));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/two.png");
    QImage img(240, 140, QImage::Format_RGB32);
    img.fill(QColor(0xb0, 0x6c, 0xff));
    QVERIFY(img.save(path));

    view->session()->feed(QStringLiteral("\x1b]7770;show;path=%1;cols=30;rows=9;kind=image;page=1\x07")
                              .arg(QString::fromUtf8(QUrl::toPercentEncoding(path)))
                              .toUtf8());
    QVERIFY(waitFor([&] { return !view->mediaUi().isEmpty(); }));

    view->closeAllMedia();
    QVERIFY(view->mediaUi().isEmpty());
    QVERIFY(view->session()->screen().media().empty());
}

void TestView::stuiSwitchTurnsTheConsoleIntoLcars()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1200, 760);
    TermSurface *view = win.newTab(QStringLiteral("/bin/cat"));
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 40; }));

    QVERIFY(!win.stui());
    win.headerBar()->grab();   // the switch rect is laid out while painting
    QVERIFY(waitFor([&] { return win.headerBar()->stuiSwitchRect().isValid(); }));
    const QRectF sw = win.headerBar()->stuiSwitchRect();
    // the rect is in design units, the event arrives in widget pixels
    const QPoint clickAt = (sw.center() * theme::uiScale()).toPoint();
    QTest::mouseClick(win.headerBar(), Qt::LeftButton, Qt::NoModifier, clickAt);
    QVERIFY2(waitFor([&] { return win.stui(); }), "the switch did not turn STUI on");
    QVERIFY(win.lcars()->isVisible());
    QVERIFY(!view->isVisible());

    // and back to the terminal with the same switch
    QTest::mouseClick(win.headerBar(), Qt::LeftButton, Qt::NoModifier, clickAt);
    QVERIFY2(waitFor([&] { return !win.stui(); }), "the switch did not turn STUI off");
    QVERIFY(view->isVisible());
}

void TestView::lcarsTreeWalksDownAndBack()
{
    LcarsView cl;
    cl.resize(1000, 700);
    QVERIFY(cl.breadcrumb().isEmpty());
    QVERIFY(cl.children().contains(QStringLiteral("SISTEMI")));

    QVERIFY(cl.enterTitle(QStringLiteral("SISTEMI")));
    QCOMPARE(cl.breadcrumb(), QStringList({QStringLiteral("SISTEMI")}));
    QVERIFY(cl.children().contains(QStringLiteral("CARICO")));

    // third level, so the tree really is a tree
    QVERIFY(cl.enterTitle(QStringLiteral("MONTAGGI")));
    QCOMPARE(cl.breadcrumb(), QStringList({QStringLiteral("SISTEMI"), QStringLiteral("MONTAGGI")}));
    QVERIFY(cl.enterTitle(QStringLiteral("TABELLA")));
    QVERIFY(cl.atLeaf());
    QCOMPARE(cl.currentScreen(), QStringLiteral("mounts"));

    cl.back();
    QCOMPARE(cl.breadcrumb().size(), 2);
    cl.back();
    QCOMPARE(cl.breadcrumb().size(), 1);
    cl.home();
    QVERIFY(cl.breadcrumb().isEmpty());
    QVERIFY(!cl.atLeaf());
}

void TestView::lcarsPaintsTheLcarsPalette()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(1200, 760);
    TermSurface *view = win.newTab(QStringLiteral("/bin/cat"));
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 40; }));
    win.setStui(true);
    QVERIFY(waitFor([&] { return win.lcars()->isVisible(); }));
    QTest::qWait(200);

    const QImage img = win.lcars()->grab().toImage();
    QVERIFY(!img.isNull());
    int orange = 0, black = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb p = img.pixel(x, y);
            if (qRed(p) > 220 && qGreen(p) > 120 && qGreen(p) < 190 && qBlue(p) < 40)
                ++orange;
            if (qRed(p) < 20 && qGreen(p) < 20 && qBlue(p) < 20)
                ++black;
        }
    }
    QVERIFY2(orange > 800, qPrintable(QStringLiteral("orange=%1").arg(orange)));
    QVERIFY2(black > 20000, qPrintable(QStringLiteral("black=%1").arg(black)));
    win.setStui(false);
}

void TestView::reverseVideoPaintsItsBackground()
{
    TermWindow win;
    win.setUseOpenGL(false);
    win.resize(700, 420);
    TermSurface *view = win.newDemoTab(QByteArray("\x1b[7mREVERSE\x1b[0m plain"));
    win.show();
    QVERIFY(waitFor([&] { return view->gridSize().width() > 20; }));
    QTest::qWait(120);

    const QImage img = win.grab().toImage();
    QVERIFY(!img.isNull());

    // the reverse block must be a light rectangle in the top-left corner of the
    // viewport: without it the dark glyphs vanish on the dark background
    int light = 0;
    for (int y = 0; y < std::min(90, img.height()); ++y) {
        for (int x = 0; x < std::min(260, img.width()); ++x) {
            if (qGray(img.pixel(x, y)) > 150)
                ++light;
        }
    }
    QVERIFY2(light > 200, qPrintable(QStringLiteral("pixel chiari: %1").arg(light)));
}

QTEST_MAIN(TestView)
#include "test_view.moc"
