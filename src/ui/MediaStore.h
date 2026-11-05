#pragma once

#include "core/Screen.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>

class QMediaPlayer;
class QAudioOutput;
class QVideoSink;
class QTimer;
class QMovie;

// MediaStore: decodes and keeps alive whatever the `show` tool put on the grid.
// Images and PDF pages become a still QImage, videos a live frame from
// QMediaPlayer, audio a generated poster with waveform and progress. Renderers
// only ever ask for a QImage plus a revision, so both draw the same thing.
class MediaStore : public QObject
{
    Q_OBJECT
public:
    explicit MediaStore(QObject *parent = nullptr);
    ~MediaStore() override;

    void add(const MediaItem &item);
    void retain(const QSet<int> &ids);   // drop everything not on screen any more
    void clear();

    QImage frame(const MediaItem &item) const;
    quint64 revision(const MediaItem &item) const;
    QSize sourceSize(const MediaItem &item) const;

    // playback state and controls
    struct State {
        bool hasPlayer = false;
        bool playing = false;
        bool muted = false;
        bool failed = false;
        qint64 position = 0;   // ms
        qint64 duration = 0;   // ms
    };
    State state(const MediaItem &item) const;
    bool hasPlayer(const MediaItem &item) const;
    void togglePlay(const MediaItem &item);
    void toggleMute(const MediaItem &item);
    void seek(const MediaItem &item, double ratio);
    void stop(const MediaItem &item);
    void forget(int id);

signals:
    void changed();

private:
    struct Entry {
        MediaItem item;
        QImage still;
        QImage frame;
        QImage poster;
        quint64 rev = 0;
        QSize sourceSize;
        QMediaPlayer *player = nullptr;
        QAudioOutput *audio = nullptr;
        QVideoSink *sink = nullptr;
        QMovie *movie = nullptr;
        QTimer *ticker = nullptr;
        QVector<float> peaks;
        QString title;
        QString artist;
        bool video = false;
        bool failed = false;
        QString error;
    };

    Entry *ensure(const MediaItem &item);
    void loadImage(Entry *e);
    void loadPdf(Entry *e);
    void loadVideo(Entry *e);
    void loadAudio(Entry *e);
    void buildAudioPoster(Entry *e);
    QImage placeholder(Entry *e, const QString &text);
    void drop(Entry *e);

    QHash<int, Entry *> m_entries;
};
