#include "ui/MediaStore.h"

#include "ui/Theme.h"

#include <QAudioOutput>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QProcess>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxSourceWidth = 1920;

QString humanTime(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("--:--");
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QImage scaledDown(const QImage &img)
{
    if (img.isNull() || img.width() <= kMaxSourceWidth)
        return img;
    return img.scaledToWidth(kMaxSourceWidth, Qt::SmoothTransformation);
}

// 256 peak values in [0,1] from the first minutes of the file
QVector<float> waveform(const QString &path)
{
    QVector<float> peaks;
    QProcess ff;
    ff.start(QStringLiteral("ffmpeg"),
             {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
              QStringLiteral("-ac"), QStringLiteral("1"), QStringLiteral("-ar"),
              QStringLiteral("8000"), QStringLiteral("-t"), QStringLiteral("900"),
              QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-")});
    if (!ff.waitForFinished(4000)) {
        ff.kill();
        return peaks;
    }
    const QByteArray raw = ff.readAllStandardOutput();
    if (raw.size() < 512)
        return peaks;

    const int samples = raw.size() / 2;
    const int buckets = 256;
    peaks.resize(buckets);
    const auto *data = reinterpret_cast<const qint16 *>(raw.constData());
    const int per = std::max(1, samples / buckets);
    for (int b = 0; b < buckets; ++b) {
        float peak = 0;
        const int from = b * per;
        const int to = std::min(samples, from + per);
        for (int i = from; i < to; ++i)
            peak = std::max(peak, std::abs(float(data[i]) / 32768.0f));
        peaks[b] = peak;
    }
    // normalise: a quiet master would otherwise draw a flat line
    float top = 0;
    for (float v : peaks)
        top = std::max(top, v);
    if (top > 0.001f) {
        for (float &v : peaks)
            v = std::min(1.0f, v / top);
    }
    return peaks;
}

} // namespace

MediaStore::MediaStore(QObject *parent)
    : QObject(parent)
{
}

MediaStore::~MediaStore()
{
    clear();
}

void MediaStore::clear()
{
    const auto keys = m_entries.keys();
    for (int id : keys)
        drop(m_entries.value(id));
    m_entries.clear();
}

void MediaStore::drop(Entry *e)
{
    if (!e)
        return;
    if (e->ticker) {
        e->ticker->stop();
        e->ticker->deleteLater();
        e->ticker = nullptr;
    }
    if (e->movie) {
        e->movie->stop();
        e->movie->deleteLater();
        e->movie = nullptr;
    }
    if (e->player) {
        e->player->stop();
        e->player->deleteLater();
        e->player = nullptr;
    }
    if (e->sink) {
        e->sink->deleteLater();
        e->sink = nullptr;
    }
    if (e->audio) {
        e->audio->deleteLater();
        e->audio = nullptr;
    }
    delete e;
}

void MediaStore::retain(const QSet<int> &ids)
{
    const auto keys = m_entries.keys();
    for (int id : keys) {
        if (ids.contains(id))
            continue;
        drop(m_entries.take(id));
    }
}

void MediaStore::add(const MediaItem &item)
{
    if (m_entries.contains(item.id))
        return;
    auto *e = new Entry;
    e->item = item;
    m_entries.insert(item.id, e);

    const QString kind = item.kind;
    if (kind == QLatin1String("video"))
        loadVideo(e);
    else if (kind == QLatin1String("audio"))
        loadAudio(e);
    else if (kind == QLatin1String("pdf"))
        loadPdf(e);
    else
        loadImage(e);

    emit changed();
}

QImage MediaStore::placeholder(Entry *e, const QString &text)
{
    QImage img(960, 320, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0x08, 0x0d, 0x13));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(0x16, 0x2c, 0x3a), 2));
    p.drawRect(QRectF(1, 1, img.width() - 2, img.height() - 2));
    p.setPen(QColor(theme::look().dim));
    QFont f = theme::mono(22);
    p.setFont(f);
    p.drawText(img.rect().adjusted(24, 0, -24, -30), Qt::AlignCenter | Qt::TextWordWrap, text);
    p.setPen(QColor(theme::look().accent));
    QFont small = theme::mono(15);
    p.setFont(small);
    p.drawText(img.rect().adjusted(24, 40, -24, -24), Qt::AlignHCenter | Qt::AlignBottom,
               QFileInfo(e->item.path).fileName());
    return img;
}

void MediaStore::loadImage(Entry *e)
{
    const QString path = e->item.path;
    if (path.endsWith(QLatin1String(".gif"), Qt::CaseInsensitive)) {
        auto *movie = new QMovie(path, QByteArray(), this);
        if (movie->isValid()) {
            e->movie = movie;
            movie->setCacheMode(QMovie::CacheAll);
            const int w = movie->currentPixmap().width();
            const int h = movie->currentPixmap().height();
            if (w > 0 && h > 0)
                e->sourceSize = QSize(w, h);
            e->still = placeholder(e, QStringLiteral("gif"));
            connect(movie, &QMovie::frameChanged, this, [this, e, movie] {
                const QPixmap pm = movie->currentPixmap();
                if (pm.isNull())
                    return;
                e->still = scaledDown(pm.toImage());
                e->sourceSize = e->still.size();
                ++e->rev;
                emit changed();
            });
            movie->start();
            return;
        }
        movie->deleteLater();
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        e->failed = true;
        e->error = reader.errorString();
        e->still = placeholder(e, QStringLiteral("immagine non leggibile:\n%1").arg(e->error));
        return;
    }
    e->sourceSize = img.size();
    e->still = scaledDown(img);
    ++e->rev;
}

void MediaStore::loadPdf(Entry *e)
{
    const QString path = e->item.path;
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    if (exe.isEmpty()) {
        e->failed = true;
        e->error = QStringLiteral("pdftoppm non installato");
        e->still = placeholder(e, QStringLiteral("PDF: serve poppler-utils"));
        return;
    }

    QTemporaryDir dir;
    if (!dir.isValid()) {
        e->still = placeholder(e, QStringLiteral("PDF: cartella temporanea non disponibile"));
        return;
    }
    const QString prefix = dir.path() + QStringLiteral("/page");
    QProcess proc;
    proc.start(exe, {QStringLiteral("-f"), QString::number(e->item.page), QStringLiteral("-l"),
                     QString::number(e->item.page), QStringLiteral("-r"), QStringLiteral("110"),
                     QStringLiteral("-png"), QStringLiteral("-singlefile"), path, prefix});
    if (!proc.waitForFinished(15000) || proc.exitCode() != 0) {
        e->failed = true;
        e->error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        e->still = placeholder(e, QStringLiteral("PDF non leggibile"));
        return;
    }
    QImage page(prefix + QStringLiteral(".png"));
    if (page.isNull()) {
        e->still = placeholder(e, QStringLiteral("PDF: pagina non trovata"));
        return;
    }
    e->sourceSize = page.size();
    e->still = scaledDown(page);
    ++e->rev;
}

void MediaStore::loadVideo(Entry *e)
{
    // poster first: ffmpeg can grab a frame even when Qt has no codec for it
    const QString ff = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    QTemporaryDir dir;
    if (!ff.isEmpty() && dir.isValid()) {
        const QString out = dir.path() + QStringLiteral("/poster.png");
        QProcess proc;
        proc.start(ff, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"),
                        QStringLiteral("1"), QStringLiteral("-i"), e->item.path,
                        QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
                        QStringLiteral("scale=1600:-2"), QStringLiteral("-y"), out});
        if (proc.waitForFinished(8000) && QFileInfo::exists(out)) {
            QImage poster(out);
            if (!poster.isNull()) {
                e->sourceSize = poster.size();
                e->still = scaledDown(poster);
                e->poster = e->still;
            }
        }
    }
    if (e->still.isNull())
        e->still = placeholder(e, QStringLiteral("video"));

    auto *audio = new QAudioOutput(this);
    auto *player = new QMediaPlayer(this);
    auto *sink = new QVideoSink(this);
    player->setAudioOutput(audio);
    player->setVideoSink(sink);
    e->player = player;
    e->audio = audio;
    e->sink = sink;
    e->video = true;

    auto last = QSharedPointer<QElapsedTimer>::create();
    last->start();
    auto *dbg = new QElapsedTimer;
    dbg->start();
    auto *count = new int(0);
    connect(sink, &QVideoSink::videoFrameChanged, this, [this, e, last, dbg, count](const QVideoFrame &f) {
        if (qEnvironmentVariableIsSet("NEONTERM_DEBUG_VIDEO")) {
            ++*count;
            if (dbg->elapsed() >= 2000) {
                qInfo("video: %d frames in %lld ms (%d fps), %dx%d", *count, dbg->elapsed(),
                      int(*count * 1000 / std::max<qint64>(1, dbg->elapsed())), f.width(), f.height());
                *count = 0;
                dbg->restart();
            }
        }
        if (last->elapsed() < 16)
            return;
        last->restart();
        QImage img = f.toImage();
        if (img.isNull())
            return;
        e->frame = scaledDown(img).convertToFormat(QImage::Format_RGBA8888);
        if (e->sourceSize.isEmpty())
            e->sourceSize = img.size();
        ++e->rev;
        emit changed();
    });
    connect(player, &QMediaPlayer::errorOccurred, this, [this, e](QMediaPlayer::Error, const QString &msg) {
        e->failed = true;
        e->error = msg;
        e->still = placeholder(e, QStringLiteral("video non riproducibile:\n%1").arg(msg));
        ++e->rev;
        emit changed();
    });

    player->setSource(QUrl::fromLocalFile(e->item.path));
    player->play();
}

void MediaStore::loadAudio(Entry *e)
{
    e->peaks = waveform(e->item.path);

    auto *audio = new QAudioOutput(this);
    auto *player = new QMediaPlayer(this);
    player->setAudioOutput(audio);
    e->player = player;
    e->audio = audio;

    connect(player, &QMediaPlayer::metaDataChanged, this, [this, e] {
        const QMediaMetaData md = e->player->metaData();
        e->title = md.value(QMediaMetaData::Title).toString();
        e->artist = md.value(QMediaMetaData::ContributingArtist).toString();
        if (e->artist.isEmpty())
            e->artist = md.value(QMediaMetaData::AlbumArtist).toString();
        buildAudioPoster(e);
        ++e->rev;
        emit changed();
    });
    connect(player, &QMediaPlayer::errorOccurred, this, [this, e](QMediaPlayer::Error, const QString &msg) {
        e->failed = true;
        e->error = msg;
        buildAudioPoster(e);
        ++e->rev;
        emit changed();
    });

    e->ticker = new QTimer(this);
    e->ticker->setInterval(100);
    connect(e->ticker, &QTimer::timeout, this, [this, e] {
        buildAudioPoster(e);
        ++e->rev;
        emit changed();
    });
    e->ticker->start();

    player->setSource(QUrl::fromLocalFile(e->item.path));
    player->play();
    buildAudioPoster(e);
    ++e->rev;
}

void MediaStore::buildAudioPoster(Entry *e)
{
    const QSize size(1100, 300);
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(0x07, 0x0c, 0x12));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor accent = theme::look().accent;
    const QColor dim = theme::look().dim;
    const QColor text(0xd6, 0xf5, 0xff);

    const qint64 pos = e->player ? e->player->position() : 0;
    const qint64 dur = e->player ? e->player->duration() : 0;
    const double progress = dur > 0 ? double(pos) / double(dur) : 0.0;

    // frame
    p.setPen(QPen(QColor(0x16, 0x2c, 0x3a), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(1, 1, size.width() - 2, size.height() - 2));

    // title / artist
    QFont titleFont = theme::mono(30, QFont::DemiBold);
    p.setFont(titleFont);
    p.setPen(text);
    const QString title = e->title.isEmpty() ? QFileInfo(e->item.path).fileName() : e->title;
    p.drawText(QRect(28, 22, size.width() - 56, 40), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, size.width() - 200));

    QFont metaFont = theme::mono(18);
    p.setFont(metaFont);
    p.setPen(dim);
    QString sub = e->artist.isEmpty() ? QStringLiteral("audio") : e->artist;
    if (!e->failed && !e->error.isEmpty())
        sub = e->error;
    p.drawText(QRect(28, 58, size.width() - 56, 24), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(metaFont).elidedText(sub, Qt::ElideRight, size.width() - 200));

    p.setPen(accent);
    // keep clear of the close button that the terminal draws in the corner
    p.drawText(QRect(size.width() - 460, 22, 380, 40), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 / %2").arg(humanTime(pos), humanTime(dur)));

    // state, top right: the transport lives in the control strip below, so the
    // poster keeps only what it can show better than the strip
    p.setFont(theme::mono(15, QFont::DemiBold));
    p.setPen(e->failed ? QColor(0xff, 0x7a, 0x8f) : dim);
    const QString state = e->failed ? QStringLiteral("ERRORE")
                        : (e->player && e->player->playbackState() == QMediaPlayer::PlayingState
                               ? QStringLiteral("IN RIPRODUZIONE")
                               : QStringLiteral("IN PAUSA"));
    p.drawText(QRect(size.width() - 520, 58, 440, 24), Qt::AlignRight | Qt::AlignVCenter, state);

    // waveform, kept clear of the control strip drawn on top of the block
    const QRect wave(28, 96, size.width() - 56, 118);
    if (e->peaks.isEmpty()) {
        p.setPen(QPen(QColor(0x1d, 0x3d, 0x4f), 2));
        p.drawLine(wave.left(), wave.center().y(), wave.right(), wave.center().y());
    } else {
        const int n = e->peaks.size();
        const double step = double(wave.width()) / double(n);
        for (int i = 0; i < n; ++i) {
            const float amp = std::max(0.02f, e->peaks[i]);
            const double h = amp * wave.height();
            const double x = wave.left() + i * step;
            p.setPen(Qt::NoPen);
            p.setBrush(i < int(n * progress) ? accent : QColor(0x1d, 0x3d, 0x4f));
            p.drawRect(QRectF(x, wave.center().y() - h / 2, std::max(1.5, step - 1.0), h));
        }
    }

    e->still = img;
    e->sourceSize = size;   // renderers and the hit test must agree on the block
}

MediaStore::State MediaStore::state(const MediaItem &item) const
{
    State st;
    const Entry *e = m_entries.value(item.id);
    if (!e)
        return st;
    st.hasPlayer = e->player != nullptr;
    st.failed = e->failed;
    if (e->player) {
        st.playing = e->player->playbackState() == QMediaPlayer::PlayingState;
        st.position = e->player->position();
        st.duration = e->player->duration();
        st.muted = e->player->audioOutput() && e->player->audioOutput()->isMuted();
    }
    return st;
}

bool MediaStore::hasPlayer(const MediaItem &item) const
{
    const Entry *e = m_entries.value(item.id);
    return e && e->player;
}

void MediaStore::togglePlay(const MediaItem &item)
{
    Entry *e = m_entries.value(item.id);
    if (!e || !e->player)
        return;
    if (e->player->playbackState() == QMediaPlayer::PlayingState) {
        e->player->pause();
        if (e->ticker)
            e->ticker->stop();
    } else {
        e->player->play();
        if (e->ticker)
            e->ticker->start();
    }
    ++e->rev;
    emit changed();
}

void MediaStore::toggleMute(const MediaItem &item)
{
    Entry *e = m_entries.value(item.id);
    if (!e || !e->audio)
        return;
    e->audio->setMuted(!e->audio->isMuted());
    ++e->rev;
    emit changed();
}

void MediaStore::seek(const MediaItem &item, double ratio)
{
    Entry *e = m_entries.value(item.id);
    if (!e || !e->player || e->player->duration() <= 0)
        return;
    ratio = std::clamp(ratio, 0.0, 1.0);
    e->player->setPosition(qint64(e->player->duration() * ratio));
    if (e->video)
        ++e->rev;
    emit changed();
}

void MediaStore::stop(const MediaItem &item)
{
    Entry *e = m_entries.value(item.id);
    if (!e || !e->player)
        return;
    e->player->pause();
    if (e->ticker)
        e->ticker->stop();
    ++e->rev;
}

void MediaStore::forget(int id)
{
    if (Entry *e = m_entries.take(id))
        drop(e);
}

QImage MediaStore::frame(const MediaItem &item) const
{
    const Entry *e = m_entries.value(item.id);
    if (!e)
        return QImage();
    if (e->video && !e->frame.isNull())
        return e->frame;
    return e->still;
}

quint64 MediaStore::revision(const MediaItem &item) const
{
    const Entry *e = m_entries.value(item.id);
    return e ? e->rev : 0;
}

QSize MediaStore::sourceSize(const MediaItem &item) const
{
    const Entry *e = m_entries.value(item.id);
    return e ? e->sourceSize : QSize();
}
