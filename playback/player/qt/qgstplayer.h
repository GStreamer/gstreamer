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
#include <QtQml/QQmlPropertyMap>
#include <gst/player/player.h>

class QGstPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(QSize resolution READ resolution WRITE setResolution NOTIFY resolutionChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QObject *mediaInfo READ mediaInfo NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool videoAvailable READ isVideoAvailable NOTIFY videoAvailableChanged)

    Q_ENUMS(State)

public:

    class VideoRenderer;

    explicit QGstPlayer(QObject *parent = 0, VideoRenderer *renderer = 0);

    typedef GstPlayerError Error;
    enum State {
        STOPPED = GST_PLAYER_STATE_STOPPED,
        BUFFERING = GST_PLAYER_STATE_BUFFERING,
        PAUSED = GST_PLAYER_STATE_PAUSED,
        PLAYING = GST_PLAYER_STATE_PLAYING
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

    // TODO add remaining bits
    class MediaInfo
    {
    public:
        MediaInfo(GstPlayerMediaInfo *media_info);
        QString title() const;
        bool isSeekable() const;
    private:
        GstPlayerMediaInfo *mediaInfo_;
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
    QQmlPropertyMap *mediaInfo() const;


signals:
    void stateChanged(State new_state);
    void bufferingChanged(int percent);
    void enfOfStream();
    void positionChanged(qint64 new_position);
    void durationChanged(qint64 duration);
    void resolutionChanged(QSize resolution);
    void volumeChanged(qreal volume);
    void mutedChanged(bool muted);
    void mediaInfoChanged();
    void sourceChanged(QUrl new_url);
    void videoAvailableChanged(bool videoAvailable);

public slots:
    void play();
    void pause();
    void stop();
    void seek(qint64 position);
    void setSource(QUrl const& url);
    void setVolume(qreal val);
    void setMuted(bool val);
    void setPosition(qint64 pos);

private:
    Q_DISABLE_COPY(QGstPlayer)
    static void onStateChanged(QGstPlayer *, GstPlayerState state);
    static void onPositionUpdated(QGstPlayer *, GstClockTime position);
    static void onDurationChanged(QGstPlayer *, GstClockTime duration);
    static void onBufferingChanged(QGstPlayer *, int percent);
    static void onVideoDimensionsChanged(QGstPlayer *, int w, int h);
    static void onVolumeChanged(QGstPlayer *);
    static void onMuteChanged(QGstPlayer *);
    static void onMediaInfoUpdated(QGstPlayer *, GstPlayerMediaInfo *media_info);

    GstPlayer *player_;
    State state_;
    QSize videoDimensions_;
    QQmlPropertyMap *mediaInfoMap_;
    bool videoAvailable_;
};

Q_DECLARE_METATYPE(QGstPlayer*)
Q_DECLARE_METATYPE(QGstPlayer::State)

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
