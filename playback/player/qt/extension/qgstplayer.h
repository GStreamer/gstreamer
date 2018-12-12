/* GStreamer
 *
 * Copyright (C) 2015 Alexandre Moreno <alexmorenocano@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef QGSTPLAYER_H
#define QGSTPLAYER_H

#include <QObject>
#include <QUrl>
#include <QSize>
//#include <QtGui/qwindowdefs.h>
#include <QVariant>
#include <QList>
#include <QImage>
#include <gst/player/player.h>

namespace QGstPlayer {

class VideoRenderer;
class MediaInfo;
class StreamInfo;
class VideInfo;
class AudioInfo;
class SubtitleInfo;

class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionUpdated)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(QSize resolution READ resolution WRITE setResolution NOTIFY resolutionChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QObject *mediaInfo READ mediaInfo NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool videoAvailable READ isVideoAvailable NOTIFY videoAvailableChanged)
    Q_PROPERTY(QVariant currentVideo READ currentVideo WRITE setCurrentVideo)
    Q_PROPERTY(QVariant currentAudio READ currentAudio WRITE setCurrentAudio)
    Q_PROPERTY(QVariant currentSubtitle READ currentSubtitle WRITE setCurrentSubtitle)
    Q_PROPERTY(bool subtitleEnabled READ isSubtitleEnabled WRITE setSubtitleEnabled
               NOTIFY subtitleEnabledChanged)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay)
    Q_PROPERTY(QList<QUrl> playlist READ playlist WRITE setPlaylist)

    Q_ENUMS(State)

public:
    explicit Player(QObject *parent = 0, VideoRenderer *renderer = 0);
    virtual ~Player();

    typedef GstPlayerError Error;
    enum State {
        STOPPED = GST_PLAYER_STATE_STOPPED,
        BUFFERING = GST_PLAYER_STATE_BUFFERING,
        PAUSED = GST_PLAYER_STATE_PAUSED,
        PLAYING = GST_PLAYER_STATE_PLAYING
    };

    QUrl source() const;
    qint64 duration() const;
    qint64 position() const;
    qreal volume() const;
    bool isMuted() const;
    int buffering() const;
    State state() const;
    GstElement *pipeline() const;
    QSize resolution() const;
    void setResolution(QSize size);
    bool isVideoAvailable() const;
    MediaInfo *mediaInfo() const;
    QVariant currentVideo() const;
    QVariant currentAudio() const;
    QVariant currentSubtitle() const;
    bool isSubtitleEnabled() const;
    bool autoPlay() const;
    QList<QUrl> playlist() const;

signals:
    void stateChanged(State new_state);
    void bufferingChanged(int percent);
    void endOfStream();
    void positionUpdated(qint64 new_position);
    void durationChanged(qint64 duration);
    void resolutionChanged(QSize resolution);
    void volumeChanged(qreal volume);
    void mutedChanged(bool muted);
    void mediaInfoChanged();
    void sourceChanged(QUrl new_url);
    void videoAvailableChanged(bool videoAvailable);
    void subtitleEnabledChanged(bool enabled);

public slots:
    void play();
    void pause();
    void stop();
    void seek(qint64 position);
    void setSource(QUrl const& url);
    void setVolume(qreal val);
    void setMuted(bool val);
    void setPosition(qint64 pos);
    void setCurrentVideo(QVariant track);
    void setCurrentAudio(QVariant track);
    void setCurrentSubtitle(QVariant track);
    void setSubtitleEnabled(bool enabled);
    void setPlaylist(const QList<QUrl> &playlist);
    void next();
    void previous();
    void setAutoPlay(bool auto_play);

private:
    Q_DISABLE_COPY(Player)
    static void onStateChanged(Player *, GstPlayerState state);
    static void onPositionUpdated(Player *, GstClockTime position);
    static void onDurationChanged(Player *, GstClockTime duration);
    static void onBufferingChanged(Player *, int percent);
    static void onVideoDimensionsChanged(Player *, int w, int h);
    static void onVolumeChanged(Player *);
    static void onMuteChanged(Player *);
    static void onMediaInfoUpdated(Player *, GstPlayerMediaInfo *media_info);
    static void onEndOfStreamReached(Player *);

    void setUri(QUrl url);

    GstPlayer *player_;
    State state_;
    QSize videoDimensions_;
    MediaInfo *mediaInfo_;
    bool videoAvailable_;
    bool subtitleEnabled_;
    bool autoPlay_;
    QList<QUrl> playlist_;
    QList<QUrl>::iterator iter_;
};

class VideoRenderer
{
public:
    GstPlayerVideoRenderer *renderer();
    virtual GstElement *createVideoSink() = 0;
protected:
    VideoRenderer();
    virtual ~VideoRenderer();
private:
    GstPlayerVideoRenderer *renderer_;
};

class MediaInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString uri READ uri NOTIFY uriChanged)
    Q_PROPERTY(bool seekable READ isSeekable NOTIFY seekableChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QList<QObject*> videoStreams READ videoStreams CONSTANT)
    Q_PROPERTY(QList<QObject*> audioStreams READ audioStreams CONSTANT)
    Q_PROPERTY(QList<QObject*> subtitleStreams READ subtitleStreams CONSTANT)
    Q_PROPERTY(QImage sample READ sample NOTIFY sampleChanged)

public:
    explicit MediaInfo(Player *player = 0);
    QString uri() const;
    QString title() const;
    bool isSeekable() const;
    const QList<QObject*> &videoStreams() const;
    const QList<QObject*> &audioStreams() const;
    const QList<QObject*> &subtitleStreams() const;
    const QImage &sample();

signals:
    void uriChanged();
    void seekableChanged();
    void titleChanged();
    void sampleChanged();

public Q_SLOTS:
    void update(GstPlayerMediaInfo *info);
private:
    QString uri_;
    QString title_;
    bool isSeekable_;
    QList<QObject*> videoStreams_;
    QList<QObject*> audioStreams_;
    QList<QObject*> subtitleStreams_;
    QImage sample_;
};

class StreamInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int index READ index CONSTANT)
public:
    int index() const;

protected:
    StreamInfo(GstPlayerStreamInfo* info);
private:
    GstPlayerStreamInfo *stream_;
    int index_;
};

class VideoInfo : public StreamInfo
{
    Q_OBJECT
    Q_PROPERTY(QSize resolution READ resolution CONSTANT)
public:
    VideoInfo(GstPlayerVideoInfo *info);
    QSize resolution() const;

private:
    GstPlayerVideoInfo *video_;
    QSize resolution_;
};

class AudioInfo : public StreamInfo
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language CONSTANT)
    Q_PROPERTY(int channels READ channels CONSTANT)
    Q_PROPERTY(int bitRate READ bitRate CONSTANT)
    Q_PROPERTY(int sampleRate READ sampleRate CONSTANT)

public:
    AudioInfo(GstPlayerAudioInfo *info);
    QString const& language() const;
    int channels() const;
    int bitRate() const;
    int sampleRate() const;

private:
    GstPlayerAudioInfo *audio_;
    QString language_;
    int channels_;
    int bitRate_;
    int sampleRate_;
};

class SubtitleInfo : public StreamInfo
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language CONSTANT)
public:
    SubtitleInfo(GstPlayerSubtitleInfo *info);
    QString const& language() const;

private:
    GstPlayerSubtitleInfo *subtitle_;
    QString language_;
};

}

Q_DECLARE_METATYPE(QGstPlayer::Player*)
Q_DECLARE_METATYPE(QGstPlayer::Player::State)
Q_DECLARE_METATYPE(QGstPlayer::MediaInfo*)

extern "C" {

typedef struct _GstPlayerQtVideoRenderer
    GstPlayerQtVideoRenderer;

typedef struct _GstPlayerQtVideoRendererClass
    GstPlayerQtVideoRendererClass;

#define GST_TYPE_PLAYER_QT_VIDEO_RENDERER             (gst_player_qt_video_renderer_get_type ())
#define GST_IS_PLAYER_QT_VIDEO_RENDERER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAYER_QT_VIDEO_RENDERER))
#define GST_IS_PLAYER_QT_VIDEO_RENDERER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAYER_QT_VIDEO_RENDERER))
#define GST_PLAYER_QT_VIDEO_RENDERER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLAYER_QT_VIDEO_RENDERER, GstPlayerQtVideoRendererClass))
#define GST_PLAYER_QT_VIDEO_RENDERER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAYER_QT_VIDEO_RENDERER, GstPlayerQtVideoRenderer))
#define GST_PLAYER_QT_VIDEO_RENDERER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAYER_QT_VIDEO_RENDERER, GstPlayerQtVideoRendererClass))
#define GST_PLAYER_QT_VIDEO_RENDERER_CAST(obj)        ((GstPlayerQtVideoRenderer*)(obj))

GType gst_player_qt_video_renderer_get_type (void);

typedef struct _GstPlayerQtSignalDispatcher
    GstPlayerQtSignalDispatcher;

typedef struct _GstPlayerQtSignalDispatcherClass
    GstPlayerQtSignalDispatcherClass;

#define GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER             (gst_player_qt_signal_dispatcher_get_type ())
#define GST_IS_PLAYER_QT_SIGNAL_DISPATCHER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER))
#define GST_IS_PLAYER_QT_SIGNAL_DISPATCHER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER))
#define GST_PLAYER_QT_SIGNAL_DISPATCHER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER, GstPlayerQtSignalDispatcherClass))
#define GST_PLAYER_QT_SIGNAL_DISPATCHER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER, GstPlayerQtSignalDispatcher))
#define GST_PLAYER_QT_SIGNAL_DISPATCHER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER, GstPlayerQtSignalDispatcherClass))
#define GST_PLAYER_QT_SIGNAL_DISPATCHER_CAST(obj)        ((GstPlayerQtSignalDispatcher*)(obj))

GType gst_player_qt_video_renderer_get_type (void);

GstPlayerSignalDispatcher *
gst_player_qt_signal_dispatcher_new (gpointer player);

}

#endif // QGSTPLAYER_H
