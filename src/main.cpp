#include "ui/Demo.h"
#include "ui/LcarsView.h"
#include "ui/TermWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QSurfaceFormat>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>

#include <clocale>

namespace {

void configureParser(QCommandLineParser &p)
{
    p.setApplicationDescription(QStringLiteral(
        "neonterm — terminale con grafica disegnata a mano (PTY + VT100/ANSI)"));
    p.addHelpOption();
    p.addVersionOption();
    p.addOption({QStringLiteral("exec"), QStringLiteral("esegue un comando invece della shell"),
                 QStringLiteral("cmd")});
    p.addOption({QStringLiteral("cwd"), QStringLiteral("directory di partenza"),
                 QStringLiteral("dir")});
    p.addOption({QStringLiteral("demo"), QStringLiteral("apre la schermata dimostrativa")});
    p.addOption({QStringLiteral("capture"), QStringLiteral("salva la finestra in un PNG ed esce"),
                 QStringLiteral("file")});
    p.addOption({QStringLiteral("size"), QStringLiteral("dimensione finestra, es. 1240x780"),
                 QStringLiteral("WxH")});
    p.addOption({QStringLiteral("font-size"), QStringLiteral("dimensione carattere in px"),
                 QStringLiteral("px")});
    p.addOption({QStringLiteral("no-crt"), QStringLiteral("disattiva scanline e vignettatura")});
    p.addOption({QStringLiteral("paint"), QStringLiteral("usa il renderer CPU invece di OpenGL")});
    p.addOption({QStringLiteral("stui"), QStringLiteral("apre l'interfaccia LCARS (STUI)")});
    p.addOption({QStringLiteral("stui-at"), QStringLiteral("percorso LCARS, es. \"SISTEMI/CARICO\""),
                 QStringLiteral("path")});
    p.addOption({QStringLiteral("stroke-font"), QStringLiteral("usa il font monolineare disegnato qui invece del monospace di sistema")});
    p.addOption({QStringLiteral("write-icon"), QStringLiteral("scrive le icone PNG nella cartella ed esce"),
                 QStringLiteral("dir")});
}

QImage renderIcon(int s)
{
    {
        QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath box;
        box.addRoundedRect(QRectF(0.5, 0.5, s - 1.0, s - 1.0), s * 0.22, s * 0.22);
        p.fillPath(box, QColor(0x08, 0x0d, 0x13));
        p.setPen(QPen(QColor(0x22, 0xe0, 0xff), std::max(1.0, s * 0.055)));
        p.setBrush(Qt::NoBrush);
        p.drawPath(box);

        QPainterPath chev;
        chev.moveTo(s * 0.31, s * 0.28);
        chev.lineTo(s * 0.63, s * 0.50);
        chev.lineTo(s * 0.31, s * 0.72);
        p.setPen(QPen(QColor(0x22, 0xe0, 0xff), std::max(1.4, s * 0.10),
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(chev);
        p.fillRect(QRectF(s * 0.56, s * 0.68, s * 0.18, s * 0.09),
                   QColor(0xb0, 0x6c, 0xff));
        return img;
    }
}

QIcon makeAppIcon()
{
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512})
        icon.addPixmap(QPixmap::fromImage(renderIcon(s)));
    return icon;
}

} // namespace

int main(int argc, char **argv)
{
    setlocale(LC_CTYPE, "");

    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--help") || a == QLatin1String("-h")
            || a == QLatin1String("--version") || a == QLatin1String("-v"))
            headless = true;
    }
    if (headless) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("neonterm"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
        QCommandLineParser p;
        configureParser(p);
        p.process(app);   // prints and exits on --help/--version
        return 0;
    }

    // one shared context for every tab, and a real 3.3 core surface: the glyph
    // atlas and the shaders are the same objects across QOpenGLWidgets
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("neonterm"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("neonnet"));
    QApplication::setWindowIcon(makeAppIcon());
    QGuiApplication::setDesktopFileName(QStringLiteral("neonterm"));

    QCommandLineParser p;
    configureParser(p);
    p.process(app);

    if (p.isSet(QStringLiteral("write-icon"))) {
        const QString dir = p.value(QStringLiteral("write-icon"));
        QDir().mkpath(dir);
        for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) {
            const QString file = QStringLiteral("%1/neonterm-%2.png").arg(dir).arg(s);
            if (!renderIcon(s).save(file))
                qWarning("icon write failed: %s", qPrintable(file));
        }
        return 0;
    }

    const QString execCmd = p.value(QStringLiteral("exec"));
    const QString cwd = p.value(QStringLiteral("cwd"));
    const bool demo = p.isSet(QStringLiteral("demo"));
    const QString capture = p.value(QStringLiteral("capture"));

    TermWindow win;
    win.setUseOpenGL(!p.isSet(QStringLiteral("paint")));
    win.setStrokeFont(p.isSet(QStringLiteral("stroke-font")));
    if (p.isSet(QStringLiteral("font-size")))
        win.applyFontSize(p.value(QStringLiteral("font-size")).toInt());
    if (p.isSet(QStringLiteral("no-crt")))
        win.setCrt(false);

    if (p.isSet(QStringLiteral("size"))) {
        const QStringList wh = p.value(QStringLiteral("size")).split(QLatin1Char('x'));
        if (wh.size() == 2)
            win.resize(wh[0].toInt(), wh[1].toInt());
    }

    win.setKeepOnExit(!execCmd.isEmpty() || !capture.isEmpty());
    if (!cwd.isEmpty())
        win.setStartDirectory(cwd);

    if (demo) {
        win.newDemoTab(demoScript());
    } else if (!execCmd.isEmpty()) {
        win.newTab(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), execCmd});
    } else {
        win.newTab();
    }


    win.show();

    if (p.isSet(QStringLiteral("stui")) || p.isSet(QStringLiteral("stui-at")))
        win.setStui(true);
    if (p.isSet(QStringLiteral("stui-at"))) {
        for (const QString &step : p.value(QStringLiteral("stui-at")).split(QLatin1Char('/'),
                                                                            Qt::SkipEmptyParts))
            win.lcars()->enterTitle(step.trimmed());
    }
    win.raise();
    win.activateWindow();

    if (!qEnvironmentVariableIsEmpty("NEONTERM_DEBUG"))
        qInfo("neonterm renderer: %s", qPrintable(win.rendererInfo()));

    if (!capture.isEmpty()) {
        const QString path = capture;
        QTimer::singleShot(900, &app, [&win, &app, path] {
            const QPixmap shot = win.grabComposite();
            QDir().mkpath(QFileInfo(path).absolutePath());
            const bool ok = shot.save(path);
            qInfo("capture %s: %s (%dx%d)", ok ? "ok" : "failed", qPrintable(path),
                  shot.width(), shot.height());
            app.quit();
        });
    }

    return app.exec();
}
