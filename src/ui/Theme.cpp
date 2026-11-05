#include "ui/Theme.h"

#include <QFontDatabase>
#include <QStringList>

namespace theme {

const Look &look()
{
    static const Look l;
    return l;
}

const Palette &termPalette()
{
    static const Palette p = [] {
        Palette q;
        q.bg = 0xff060a0f;
        q.fg = 0xffd6f5ff;
        q.cursor = 0xff22e0ff;
        q.selection = 0xff123b52;
        return q;
    }();
    return p;
}

qreal uiScale()
{
    static const qreal s = [] {
        bool ok = false;
        const qreal env = qEnvironmentVariable("NEONTERM_UI_SCALE").toDouble(&ok);
        if (ok && env >= 0.6 && env <= 3.0)
            return env;
        return 1.2;   // 20% zoom over the original 14 px layout
    }();
    return s;
}

int scaled(int value)
{
    return int(std::lround(double(value) * double(uiScale())));
}

QString monoFamily()
{
    static const QString family = [] {
        const QStringList wanted = {
            QStringLiteral("JetBrains Mono"),
            QStringLiteral("DM Mono"),
            QStringLiteral("IBM Plex Mono"),
            QStringLiteral("Hack"),
            QStringLiteral("Ubuntu Mono"),
            QStringLiteral("DejaVu Sans Mono"),
        };
        const QStringList have = QFontDatabase::families();
        for (const QString &w : wanted) {
            if (have.contains(w))
                return w;
        }
        return QStringLiteral("monospace");
    }();
    return family;
}

QFont mono(int pixelSize, int weight)
{
    QFont f(monoFamily());
    f.setPixelSize(std::max(6, pixelSize));
    f.setWeight(QFont::Weight(weight));
    f.setStyleStrategy(QFont::PreferAntialias);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

} // namespace theme
