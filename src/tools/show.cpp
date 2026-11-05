// show — put a file on the neonterm grid.
//
// The terminal owns the pixels: this tool only measures the file, reserves the
// right number of cells and sends an OSC 7770 request. Nothing is transferred,
// so a 4K video costs the same as a 3 kB image. When the terminal is not
// neonterm, text files still print and everything else explains itself.

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>

#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

QStringList imageSuffixes()
{
    return {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
            QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("avif"),
            QStringLiteral("svg")};
}

QStringList videoSuffixes()
{
    return {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
            QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("m4v"),
            QStringLiteral("mpeg"), QStringLiteral("mpg"), QStringLiteral("ogv")};
}

QStringList audioSuffixes()
{
    return {QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("oga"),
            QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("m4a"),
            QStringLiteral("opus"), QStringLiteral("aac"), QStringLiteral("wma")};
}

QStringList textSuffixes()
{
    return {QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
            QStringLiteral("rst"), QStringLiteral("c"), QStringLiteral("h"),
            QStringLiteral("cpp"), QStringLiteral("hpp"), QStringLiteral("cc"),
            QStringLiteral("py"), QStringLiteral("sh"), QStringLiteral("bash"),
            QStringLiteral("json"), QStringLiteral("yaml"), QStringLiteral("yml"),
            QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("conf"),
            QStringLiteral("cfg"), QStringLiteral("log"), QStringLiteral("csv"),
            QStringLiteral("xml"), QStringLiteral("html"), QStringLiteral("css"),
            QStringLiteral("js"), QStringLiteral("ts"), QStringLiteral("go"),
            QStringLiteral("rs"), QStringLiteral("java"), QStringLiteral("sql"),
            QStringLiteral("php"), QStringLiteral("tex"), QStringLiteral("service")};
}

QString classify(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return QStringLiteral("pdf");
    if (imageSuffixes().contains(suffix))
        return QStringLiteral("image");
    if (videoSuffixes().contains(suffix))
        return QStringLiteral("video");
    if (audioSuffixes().contains(suffix))
        return QStringLiteral("audio");
    if (textSuffixes().contains(suffix))
        return QStringLiteral("text");
    if (QImageReader(path).canRead())
        return QStringLiteral("image");
    return QStringLiteral("unknown");
}

QSize imageSize(const QString &path)
{
    QImageReader r(path);
    r.setAutoTransform(true);
    return r.size();
}

QSize videoSize(const QString &path)
{
    QProcess ff;
    ff.start(QStringLiteral("ffprobe"),
             {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
              QStringLiteral("v:0"), QStringLiteral("-show_entries"),
              QStringLiteral("stream=width,height"), QStringLiteral("-of"),
              QStringLiteral("csv=p=0"), path});
    if (!ff.waitForFinished(4000))
        return {};
    const QString out = QString::fromLocal8Bit(ff.readAllStandardOutput()).trimmed();
    const QStringList wh = out.split(QLatin1Char(','));
    if (wh.size() < 2)
        return {};
    return QSize(wh[0].toInt(), wh[1].toInt());
}

QSize pdfSize(const QString &path, int *pages)
{
    QProcess pdf;
    pdf.start(QStringLiteral("pdfinfo"), {path});
    if (!pdf.waitForFinished(4000))
        return {};
    const QString out = QString::fromLocal8Bit(pdf.readAllStandardOutput());
    QRegularExpression pg(QStringLiteral("Pages:\\s+(\\d+)"));
    auto m = pg.match(out);
    if (m.hasMatch() && pages)
        *pages = m.captured(1).toInt();
    QRegularExpression sz(QStringLiteral("Page size:\\s+([\\d.]+)\\s+x\\s+([\\d.]+)"));
    auto ms = sz.match(out);
    if (!ms.hasMatch())
        return {};
    return QSize(int(ms.captured(1).toDouble()), int(ms.captured(2).toDouble()));
}

QSize terminalSize()
{
    struct winsize ws {};
    const int fd = ::open("/dev/tty", O_RDONLY);
    if (fd >= 0) {
        if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            ::close(fd);
            return QSize(ws.ws_col, ws.ws_row);
        }
        ::close(fd);
    }
    const QByteArray c = qgetenv("COLUMNS");
    const QByteArray l = qgetenv("LINES");
    return QSize(c.isEmpty() ? 80 : c.toInt(), l.isEmpty() ? 24 : l.toInt());
}

void writeTty(const QByteArray &bytes)
{
    FILE *tty = ::fopen("/dev/tty", "w");
    if (!tty)
        return;
    ::fwrite(bytes.constData(), 1, size_t(bytes.size()), tty);
    ::fflush(tty);
    ::fclose(tty);
}

QString quoteForTty(const QString &path)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(path));
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("show"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral(
        "Mostra un file dentro neonterm: immagini, video, audio, pdf e testo,\n"
        "con il blocco di celle giusto per il suo formato."));
    p.addHelpOption();
    p.addVersionOption();
    p.addOption({QStringLiteral("cols"), QStringLiteral("larghezza in celle"), QStringLiteral("n")});
    p.addOption({QStringLiteral("page"), QStringLiteral("pagina del PDF"), QStringLiteral("n"),
                 QStringLiteral("1")});
    p.addOption({QStringLiteral("stop"), QStringLiteral("ferma i media in riproduzione")});
    p.addOption({QStringLiteral("print"), QStringLiteral("scrive la sequenza su stdout")});
    p.addPositionalArgument(QStringLiteral("file"), QStringLiteral("file da mostrare"));

    p.process(app);

    if (p.isSet(QStringLiteral("stop"))) {
        const QByteArray seq = QByteArray("\x1b]7770;stop\x07");
        if (p.isSet(QStringLiteral("print")))
            fwrite(seq.constData(), 1, size_t(seq.size()), stdout);
        else
            writeTty(seq);
        return 0;
    }

    const QStringList files = p.positionalArguments();
    if (files.isEmpty()) {
        fprintf(stderr, "show: manca il file (show --help)\n");
        return 1;
    }

    const QString path = QFileInfo(files.first()).absoluteFilePath();
    if (!QFileInfo::exists(path)) {
        fprintf(stderr, "show: %s non esiste\n", qPrintable(files.first()));
        return 1;
    }

    const QString kind = classify(path);

    if (kind == QLatin1String("text")) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            fprintf(stderr, "show: %s non leggibile\n", qPrintable(path));
            return 1;
        }
        QTextStream out(stdout);
        out << "\x1b[2m── " << QFileInfo(path).fileName() << " ──\x1b[0m\n";
        QByteArray data = f.read(256 * 1024);
        QString text = QString::fromUtf8(data);
        text.replace(QLatin1Char('\r'), QString());
        text.replace(QChar(0x00), QLatin1Char(' '));
        out << text;
        if (!text.endsWith(QLatin1Char('\n')))
            out << '\n';
        out.flush();
        return 0;
    }

    if (kind == QLatin1String("unknown")) {
        fprintf(stderr, "show: formato non riconosciuto per %s\n", qPrintable(files.first()));
        return 1;
    }

    // ---- measure and reserve cells
    int page = std::max(1, p.value(QStringLiteral("page")).toInt());
    int pages = 0;
    QSize src;
    if (kind == QLatin1String("image"))
        src = imageSize(path);
    else if (kind == QLatin1String("video"))
        src = videoSize(path);
    else if (kind == QLatin1String("pdf"))
        src = pdfSize(path, &pages);
    if (src.isEmpty())
        src = QSize(16, 9);

    const QSize term = terminalSize();
    int cols = p.value(QStringLiteral("cols")).toInt();
    if (cols <= 0)
        cols = std::min(term.width() - 2, 120);
    cols = std::max(8, cols);

    // the terminal cell is roughly 0.45 as wide as it is tall in the default font
    const double aspect = 0.45;
    double rowsF = 0;
    if (kind == QLatin1String("audio"))
        rowsF = 9;
    else
        rowsF = cols * aspect * (double(src.height()) / double(src.width()));
    // never reserve more rows than the viewport has: the block would start
    // above the visible area and the image would be cut
    const int maxRows = std::max(3, std::min(40, term.height() - 4));
    rowsF = std::clamp(rowsF, 3.0, double(maxRows));
    int rows = int(std::lround(rowsF));

    // when the height was the limit, narrow the block instead of leaving the
    // image floating in a wide empty band
    if (!p.isSet(QStringLiteral("cols")) && kind != QLatin1String("audio")) {
        const int fit = int(std::lround(rows * (double(src.width()) / double(src.height())) / aspect));
        cols = std::clamp(fit, 8, cols);
    }

    QByteArray seq = "\x1b]7770;show;path=" + quoteForTty(path).toUtf8() + ";cols="
                   + QByteArray::number(cols) + ";rows=" + QByteArray::number(rows) + ";kind="
                   + kind.toUtf8() + ";page=" + QByteArray::number(page) + "\x07";

    if (p.isSet(QStringLiteral("print"))) {
        fwrite(seq.constData(), 1, size_t(seq.size()), stdout);
    } else {
        // flush what the shell already wrote, then ask the terminal to place the
        // media on the line after the command
        fflush(stdout);
        fputc('\n', stdout);
        fflush(stdout);
        writeTty(seq);
    }

    // reserve the block: the text continues under the media
    for (int i = 0; i < rows; ++i)
        fputc('\n', stdout);

    QString info = QStringLiteral("%1 · %2×%3 · %4 · %5×%6")
                       .arg(QFileInfo(path).fileName())
                       .arg(src.width())
                       .arg(src.height())
                       .arg(kind)
                       .arg(cols)
                       .arg(rows);
    if (pages > 0)
        info += QStringLiteral(" · pagina %1/%2").arg(page).arg(pages);
    fprintf(stdout, "\x1b[2m%s\x1b[0m\n", qPrintable(info));
    fflush(stdout);
    return 0;
}
