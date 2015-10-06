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

#include "qgstplayer.h"
#include <QDebug>
#include <QSize>
#include <QMetaObject>
#include <functional>
#include <QThread>

class QGstPlayerRegisterMetaTypes
{
public:
    QGstPlayerRegisterMetaTypes()
    {
        qRegisterMetaType<QGstPlayer::State>("State");
    }

} _register;

QGstPlayer::MediaInfo::MediaInfo(GstPlayerMediaInfo *media_info)
    : mediaInfo_(media_info)
{

}

QString QGstPlayer::MediaInfo::title() const
{
    QString title = QString::fromLocal8Bit
        (gst_player_media_info_get_title(mediaInfo_));

    // if media has no title, return the file name
    if (title.isEmpty()) {
        QUrl url(gst_player_media_info_get_uri(mediaInfo_));
        title = url.fileName();
    }

    return title;
}

bool QGstPlayer::MediaInfo::isSeekable() const
{
    return gst_player_media_info_is_seekable(mediaInfo_);
}

bool QGstPlayer::isVideoAvailable() const
{
    GstPlayerVideoInfo *video_info;

    video_info = gst_player_get_current_video_track (player_);
    if (video_info) {
      g_object_unref (video_info);
      return true;
    }

    return false;
}

QQmlPropertyMap *QGstPlayer::mediaInfo() const
{
    return mediaInfoMap_;
}

QGstPlayer::QGstPlayer(QObject *parent, QGstPlayer::VideoRenderer *renderer)
    : QObject(parent)
    , player_()
    , state_(STOPPED)
    , videoDimensions_(QSize())
    , mediaInfoMap_()
    , videoAvailable_(false)
{

    player_ = gst_player_new_full(renderer ? renderer->renderer() : 0,
        gst_player_qt_signal_dispatcher_new(this));

    g_object_connect(player_,
        "swapped-signal::state-changed", G_CALLBACK (QGstPlayer::onStateChanged), this,
        "swapped-signal::position-updated", G_CALLBACK (QGstPlayer::onPositionUpdated), this,
        "swapped-signal::duration-changed", G_CALLBACK (QGstPlayer::onDurationChanged), this,
        "swapped-signal::buffering", G_CALLBACK (QGstPlayer::onBufferingChanged), this,
        "swapped-signal::video-dimensions-changed", G_CALLBACK (QGstPlayer::onVideoDimensionsChanged), this,
        "swapped-signal::volume-changed", G_CALLBACK (QGstPlayer::onVolumeChanged), this,
        "swapped-signal::mute-changed", G_CALLBACK (QGstPlayer::onMuteChanged), this,
        "swapped-signal::media-info-updated", G_CALLBACK (QGstPlayer::onMediaInfoUpdated), this, NULL);

    mediaInfoMap_ = new QQmlPropertyMap(this);
}

void
QGstPlayer::onStateChanged(QGstPlayer * player, GstPlayerState state)
{
    player->state_ =  static_cast<QGstPlayer::State>(state);

    emit player->stateChanged(player->state_);
}

void
QGstPlayer::onPositionUpdated(QGstPlayer * player, GstClockTime position)
{
    emit player->positionChanged(position);
}

void
QGstPlayer::onDurationChanged(QGstPlayer * player, GstClockTime duration)
{
    emit player->durationChanged(duration);
}

void
QGstPlayer::onBufferingChanged(QGstPlayer * player, int percent)
{
    emit player->bufferingChanged(percent);
}

void
QGstPlayer::onVideoDimensionsChanged(QGstPlayer * player, int w, int h)
{
    QSize res(w,h);

    player->setResolution(res);

    emit player->resolutionChanged(res);
}

void
QGstPlayer::onVolumeChanged(QGstPlayer *player)
{
    qreal new_val;

    new_val = gst_player_get_volume (player->player_);

    emit player->volumeChanged(new_val);
}

void
QGstPlayer::onMuteChanged(QGstPlayer *player)
{
    bool new_val;

    new_val = gst_player_get_mute (player->player_);

    emit player->mutedChanged(new_val);
}

void
QGstPlayer::onMediaInfoUpdated(QGstPlayer *player, GstPlayerMediaInfo *media_info)
{
    MediaInfo mediaInfo(media_info);

    player->mediaInfoMap_->insert(QLatin1String("title"),
                                  QVariant(mediaInfo.title()));
    player->mediaInfoMap_->insert(QLatin1String("isSeekable"),
                                  QVariant(mediaInfo.isSeekable()));

    bool val = player->isVideoAvailable();

    if (player->videoAvailable_ != val) {
        player->videoAvailable_ = val;
        emit player->videoAvailableChanged(val);
    }

    emit player->mediaInfoChanged();
}

QUrl QGstPlayer::source() const
{
    Q_ASSERT(player_ != 0);
    QString url = QString::fromLocal8Bit(gst_player_get_uri(player_));

    return QUrl(url);
}

qint64 QGstPlayer::duration() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_duration(player_);
}

qint64 QGstPlayer::position() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_position(player_);
}

qreal QGstPlayer::volume() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_volume(player_);
}

bool QGstPlayer::isMuted() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_mute(player_);
}

int QGstPlayer::buffering() const
{
    return 0;
}

QSize QGstPlayer::resolution() const
{
    return videoDimensions_;
}

void QGstPlayer::setResolution(QSize size)
{
    videoDimensions_ = size;
}

QGstPlayer::State QGstPlayer::state() const
{
    return state_;
}

GstElement *QGstPlayer::pipeline() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_pipeline(player_);
}

void QGstPlayer::play()
{
    Q_ASSERT(player_ != 0);

    gst_player_play(player_);
}

void QGstPlayer::pause()
{
    Q_ASSERT(player_ != 0);

    gst_player_pause(player_);
}

void QGstPlayer::stop()
{
    Q_ASSERT(player_ != 0);

    gst_player_stop(player_);
}

void QGstPlayer::seek(qint64 position)
{
    Q_ASSERT(player_ != 0);

    gst_player_seek(player_, position);
}

void QGstPlayer::setSource(QUrl const& url)
{
    Q_ASSERT(player_ != 0);
    QByteArray uri = url.toString().toLocal8Bit();

    gst_player_set_uri(player_, uri.data());

    emit sourceChanged(url);
}

void QGstPlayer::setVolume(qreal val)
{
    Q_ASSERT(player_ != 0);

    gst_player_set_volume(player_, val);
}

void QGstPlayer::setMuted(bool val)
{
    Q_ASSERT(player_ != 0);

    gst_player_set_mute(player_, val);
}

void QGstPlayer::setPosition(qint64 pos)
{
    Q_ASSERT(player_ != 0);

    gst_player_seek(player_, pos);
}

GstPlayerVideoRenderer *QGstPlayer::VideoRenderer::renderer()
{
    return renderer_;
}

QGstPlayer::VideoRenderer::VideoRenderer()
{
    renderer_ = static_cast<GstPlayerVideoRenderer*>
        (g_object_new (GST_TYPE_PLAYER_QT_VIDEO_RENDERER, "renderer", this, NULL));
}

QGstPlayer::VideoRenderer::~VideoRenderer()
{
    if (renderer_) gst_object_unref(renderer_);
}

struct _GstPlayerQtVideoRenderer
{
  GObject parent;

  gpointer renderer;
};

struct _GstPlayerQtVideoRendererClass
{
  GObjectClass parent_class;
};

static void
gst_player_qt_video_renderer_interface_init
    (GstPlayerVideoRendererInterface * iface);

G_DEFINE_TYPE_WITH_CODE (GstPlayerQtVideoRenderer,
    gst_player_qt_video_renderer, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PLAYER_VIDEO_RENDERER,
        gst_player_qt_video_renderer_interface_init))

enum
{
  QT_VIDEO_RENDERER_PROP_0,
  QT_VIDEO_RENDERER_PROP_RENDERER,
  QT_VIDEO_RENDERER_PROP_LAST
};

static GParamSpec * qt_video_renderer_param_specs
    [QT_VIDEO_RENDERER_PROP_LAST] = { NULL, };

static void
gst_player_qt_video_renderer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPlayerQtVideoRenderer *self = GST_PLAYER_QT_VIDEO_RENDERER (object);

  switch (prop_id) {
    case QT_VIDEO_RENDERER_PROP_RENDERER:
      self->renderer = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_player_qt_video_renderer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPlayerQtVideoRenderer *self = GST_PLAYER_QT_VIDEO_RENDERER (object);

  switch (prop_id) {
    case QT_VIDEO_RENDERER_PROP_RENDERER:
      g_value_set_pointer (value, self->renderer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_player_qt_video_renderer_finalize (GObject * object)
{
  GstPlayerQtVideoRenderer *self = GST_PLAYER_QT_VIDEO_RENDERER (object);

  G_OBJECT_CLASS
      (gst_player_qt_video_renderer_parent_class)->finalize(object);
}

static void
gst_player_qt_video_renderer_class_init
    (GstPlayerQtVideoRendererClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property =
      gst_player_qt_video_renderer_set_property;
  gobject_class->get_property =
      gst_player_qt_video_renderer_get_property;
  gobject_class->finalize = gst_player_qt_video_renderer_finalize;

  qt_video_renderer_param_specs
      [QT_VIDEO_RENDERER_PROP_RENDERER] =
      g_param_spec_pointer ("renderer", "Qt Renderer", "",
      static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (gobject_class,
      QT_VIDEO_RENDERER_PROP_LAST,
      qt_video_renderer_param_specs);
}

static void
gst_player_qt_video_renderer_init (GstPlayerQtVideoRenderer * self)
{

}

static GstElement *
gst_player_qt_video_renderer_create_video_sink
    (GstPlayerVideoRenderer * iface, GstPlayer *player)
{
    GstPlayerQtVideoRenderer *self = GST_PLAYER_QT_VIDEO_RENDERER (iface);

    g_assert(self->renderer != NULL);

    return static_cast<QGstPlayer::VideoRenderer*>(self->renderer)->createVideoSink();
}

static void
gst_player_qt_video_renderer_interface_init
    (GstPlayerVideoRendererInterface * iface)
{
  iface->create_video_sink = gst_player_qt_video_renderer_create_video_sink;
}

struct _GstPlayerQtSignalDispatcher
{
  GObject parent;

  gpointer player;
};

struct _GstPlayerQtSignalDispatcherClass
{
  GObjectClass parent_class;
};

static void
gst_player_qt_signal_dispatcher_interface_init
    (GstPlayerSignalDispatcherInterface * iface);

enum
{
  QT_SIGNAL_DISPATCHER_PROP_0,
  QT_SIGNAL_DISPATCHER_PROP_PLAYER,
  QT_SIGNAL_DISPATCHER_PROP_LAST
};

G_DEFINE_TYPE_WITH_CODE (GstPlayerQtSignalDispatcher,
    gst_player_qt_signal_dispatcher, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PLAYER_SIGNAL_DISPATCHER,
        gst_player_qt_signal_dispatcher_interface_init));

static GParamSpec
* qt_signal_dispatcher_param_specs
[QT_SIGNAL_DISPATCHER_PROP_LAST] = { NULL, };

static void
gst_player_qt_signal_dispatcher_finalize (GObject * object)
{
  GstPlayerQtSignalDispatcher *self =
      GST_PLAYER_QT_SIGNAL_DISPATCHER (object);

  G_OBJECT_CLASS
      (gst_player_qt_signal_dispatcher_parent_class)->finalize
      (object);
}

static void
gst_player_qt_signal_dispatcher_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPlayerQtSignalDispatcher *self =
      GST_PLAYER_QT_SIGNAL_DISPATCHER (object);

  switch (prop_id) {
    case QT_SIGNAL_DISPATCHER_PROP_PLAYER:
      self->player = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_player_qt_signal_dispatcher_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPlayerQtSignalDispatcher *self =
      GST_PLAYER_QT_SIGNAL_DISPATCHER (object);

  switch (prop_id) {
    case QT_SIGNAL_DISPATCHER_PROP_PLAYER:
      g_value_set_pointer (value, self->player);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
    gst_player_qt_signal_dispatcher_class_init
    (GstPlayerQtSignalDispatcherClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize =
      gst_player_qt_signal_dispatcher_finalize;
  gobject_class->set_property =
      gst_player_qt_signal_dispatcher_set_property;
  gobject_class->get_property =
      gst_player_qt_signal_dispatcher_get_property;

  qt_signal_dispatcher_param_specs
      [QT_SIGNAL_DISPATCHER_PROP_PLAYER] =
      g_param_spec_pointer ("player", "QGstPlayer instance", "",
      static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (gobject_class,
      QT_SIGNAL_DISPATCHER_PROP_LAST,
      qt_signal_dispatcher_param_specs);
}

static void
gst_player_qt_signal_dispatcher_init
    (GstPlayerQtSignalDispatcher * self)
{

}

static void
gst_player_qt_signal_dispatcher_dispatch (GstPlayerSignalDispatcher
    * iface, GstPlayer * player, void (*emitter) (gpointer data), gpointer data,
    GDestroyNotify destroy)
{
  GstPlayerQtSignalDispatcher *self = GST_PLAYER_QT_SIGNAL_DISPATCHER (iface);
  QObject dispatch;
  QObject *receiver = static_cast<QObject*>(self->player);

  QObject::connect(&dispatch, &QObject::destroyed, receiver, [=]() {
    emitter(data);
    if (destroy) destroy(data);
  }, Qt::QueuedConnection);
}

static void
gst_player_qt_signal_dispatcher_interface_init
(GstPlayerSignalDispatcherInterface * iface)
{
  iface->dispatch = gst_player_qt_signal_dispatcher_dispatch;
}

GstPlayerSignalDispatcher *
gst_player_qt_signal_dispatcher_new (gpointer player)
{
  return static_cast<GstPlayerSignalDispatcher*>
  (g_object_new (GST_TYPE_PLAYER_QT_SIGNAL_DISPATCHER,
      "player", player, NULL));
}
