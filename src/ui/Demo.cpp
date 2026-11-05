#include "ui/Demo.h"

#include "core/Screen.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>

namespace {

QString fg(int r, int g, int b)
{
    return QStringLiteral("\x1b[38;2;%1;%2;%3m").arg(r).arg(g).arg(b);
}

QString bgc(int r, int g, int b)
{
    return QStringLiteral("\x1b[48;2;%1;%2;%3m").arg(r).arg(g).arg(b);
}

const QString R = QStringLiteral("\x1b[0m");
const QString BOLD = QStringLiteral("\x1b[1m");
const QString DIM = QStringLiteral("\x1b[2m");

QString color256(int idx) { return QStringLiteral("\x1b[38;5;%1m").arg(idx); }

} // namespace

QByteArray demoScript()
{
    QString o;
    o += QStringLiteral("\x1b[?25l\x1b[2J\x1b[H");

    const QStringList logo = {
        QStringLiteral("███╗   ██╗███████╗ ██████╗ ███╗   ██╗████████╗███████╗██████╗ ███╗   ███╗"),
        QStringLiteral("████╗  ██║██╔════╝██╔═══██╗████╗  ██║╚══██╔══╝██╔════╝██╔══██╗████╗ ████║"),
        QStringLiteral("██╔██╗ ██║█████╗  ██║   ██║██╔██╗ ██║   ██║   █████╗  ██████╔╝██╔████╔██║"),
        QStringLiteral("██║╚██╗██║██╔══╝  ██║   ██║██║╚██╗██║   ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║"),
        QStringLiteral("██║ ╚████║███████╗╚██████╔╝██║ ╚████║   ██║   ███████╗██║  ██║██║ ╚═╝ ██║"),
        QStringLiteral("╚═╝  ╚═══╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝"),
    };
    o += QStringLiteral("\n");
    for (int i = 0; i < logo.size(); ++i) {
        const double t = double(i) / double(logo.size() - 1);
        const int r = int(34 + (176 - 34) * t);
        const int g = int(224 + (108 - 224) * t);
        const int b = 255;
        o += fg(r, g, b) + BOLD + QStringLiteral("  ") + logo[i] + R + QStringLiteral("\n");
    }
    o += QStringLiteral("\n  ") + fg(90, 130, 155)
         + QStringLiteral("a terminal that paints every glyph itself")
         + DIM + QStringLiteral("   |   v0.1.0") + R + QStringLiteral("\n");
    o += QStringLiteral("  ") + fg(28, 52, 68)
         + QString(96, QChar(0x2500)) + R + QStringLiteral("\n\n");

    struct KV {
        const char *k;
        const char *v;
    };
    const QVector<KV> left = {
        {"engine", "OpenGL 3.3 / instanced quads"},
        {"grid", "132 x 36 cells"},
        {"glyph", "monospace 14 px"},
        {"cell", "8.4 x 19 px"},
    };
    const QVector<KV> right = {
        {"pty", "/dev/pts/7 / 4242"},
        {"palette", "16 / 256 / 24-bit"},
        {"input", "keys / mouse / paste / ime"},
        {"effects", "scanline / vignette / no glow"},
    };
    for (int i = 0; i < left.size(); ++i) {
        o += QStringLiteral("  ") + fg(34, 224, 255) + QStringLiteral("> ") + R;
        o += fg(120, 150, 172) + QString::fromLatin1(left[i].k).leftJustified(9) + R;
        o += fg(214, 238, 255) + QString::fromLatin1(left[i].v).leftJustified(32) + R;
        o += fg(34, 224, 255) + QStringLiteral("> ") + R;
        o += fg(120, 150, 172) + QString::fromLatin1(right[i].k).leftJustified(9) + R;
        o += fg(170, 200, 220) + QString::fromLatin1(right[i].v) + R;
        o += QStringLiteral("\n");
    }

    o += QStringLiteral("\n");
    const Palette p;
    o += QStringLiteral("  ") + fg(90, 120, 140) + QStringLiteral("ANSI   ") + R;
    for (int i = 0; i < 16; ++i) {
        const QRgb c = p.color256(i);
        o += fg(qRed(c), qGreen(c), qBlue(c)) + QStringLiteral("██") + R;
        if (i == 7)
            o += QStringLiteral(" ");
    }
    o += QStringLiteral("\n  ") + fg(90, 120, 140) + QStringLiteral("256    ") + R;
    for (int i = 0; i < 48; ++i)
        o += color256(16 + i * 5) + QStringLiteral("█") + R;
    o += QStringLiteral("\n  ") + fg(90, 120, 140) + QStringLiteral("TRUE   ") + R;
    for (int i = 0; i < 48; ++i) {
        const double t = i / 47.0;
        o += fg(int(34 + 140 * t), int(140 - 40 * t), int(255 - 60 * t)) + QStringLiteral("█") + R;
    }
    o += QStringLiteral("\n\n");

    auto tag = [](const QString &text, int r, int g, int b) {
        return bgc(r, g, b) + fg(8, 12, 16) + BOLD + QStringLiteral(" %1 ").arg(text) + R;
    };
    auto stamp = [](const QString &t) {
        return fg(70, 96, 114) + t + QStringLiteral(" ") + R;
    };
    o += QStringLiteral("  ") + stamp(QStringLiteral("21:04:11"))
         + tag(QStringLiteral("OK"), 34, 224, 255) + QStringLiteral("  ")
         + fg(150, 178, 198) + QStringLiteral("pty").leftJustified(10) + R + QStringLiteral(" ")
         + fg(214, 238, 255) + QStringLiteral("forkpty 132x36  TERM=xterm-256color") + R
         + QStringLiteral("\n");
    o += QStringLiteral("  ") + stamp(QStringLiteral("21:04:11"))
         + tag(QStringLiteral("OK"), 34, 224, 255) + QStringLiteral("  ")
         + fg(150, 178, 198) + QStringLiteral("parser").leftJustified(10) + R + QStringLiteral(" ")
         + fg(214, 238, 255) + QStringLiteral("CSI / OSC / SGR / DEC private modes") + R
         + QStringLiteral("\n");
    o += QStringLiteral("  ") + stamp(QStringLiteral("21:04:12"))
         + tag(QStringLiteral("OK"), 34, 224, 255) + QStringLiteral("  ")
         + fg(150, 178, 198) + QStringLiteral("render").leftJustified(10) + R + QStringLiteral(" ")
         + fg(214, 238, 255) + QStringLiteral("stroke pass  0.2 ms/frame  1614 segments") + R
         + QStringLiteral("\n");
    o += QStringLiteral("  ") + stamp(QStringLiteral("21:04:12"))
         + tag(QStringLiteral("WARN"), 255, 209, 102) + QStringLiteral("  ")
         + fg(150, 178, 198) + QStringLiteral("clipboard").leftJustified(10) + R
         + fg(255, 224, 150) + QStringLiteral("selection owned by another client") + R
         + QStringLiteral("\n\n");

    o += QStringLiteral("  ") + fg(90, 120, 140) + QStringLiteral("boot   ") + R;
    o += fg(34, 224, 255) + QStringLiteral("▕") + R;
    const int cells = 40;
    const int lit = 33;
    for (int i = 0; i < cells; ++i) {
        if (i < lit) {
            const double t = double(i) / double(cells - 1);
            o += fg(int(34 + 120 * t), int(210 - 90 * t), 255)
                 + (i < lit - 3 ? QStringLiteral("█") : QStringLiteral("▓")) + R;
        } else {
            o += fg(22, 38, 48) + QStringLiteral("░") + R;
        }
    }
    o += fg(34, 224, 255) + QStringLiteral("▏") + R;
    o += QStringLiteral("  ") + BOLD + fg(214, 238, 255) + QStringLiteral("82%") + R;
    o += QStringLiteral("\n\n");

    o += QStringLiteral("  ") + fg(34, 224, 255) + BOLD + QStringLiteral("neonterm") + R;
    o += fg(70, 96, 114) + QStringLiteral(" ~ ") + R;
    o += fg(70, 96, 114) + QStringLiteral("$ ") + R;
    o += QStringLiteral("\x1b[?25h");

    // the demo is fed straight into the parser, with no tty to translate \n
    o.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    return o.toUtf8();
}
