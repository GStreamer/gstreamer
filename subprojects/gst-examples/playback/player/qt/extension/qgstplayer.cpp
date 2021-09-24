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
#include <QtAlgorithms>
#include <QImage>

#include <gst/gst.h>
#include <gst/tag/tag.h>

namespace QGstPlayer {

class RegisterMetaTypes
{
public:
    RegisterMetaTypes()
    {
        qRegisterMetaType<Player::State>("State");
    }

} _register;

MediaInfo::MediaInfo(Player *player)
    : QObject(player)
    , uri_()
    , title_()
    , isSeekable_(false)
    , videoStreams_()
    , audioStreams_()
    , subtitleStreams_()
    , sample_()
{

}

QString MediaInfo::uri() const
{
    return uri_;
}

QString MediaInfo::title() const
{
    return title_;
}

bool MediaInfo::isSeekable() const
{
    return isSeekable_;
}

const QList<QObject *> &MediaInfo::videoStreams() const
{
    return videoStreams_;
}

const QList<QObject *> &MediaInfo::audioStreams() const
{
    return audioStreams_;
}

const QList<QObject*> &MediaInfo::subtitleStreams() const
{
    return subtitleStreams_;
}

const QImage &MediaInfo::sample()
{
    return sample_;
}

void MediaInfo::update(GstPlayerMediaInfo *info)
{
    Q_ASSERT(info != 0);

    // FIXME since media-info signal gets emitted
    // several times, we just update info iff the
    // media uri has changed.
    if (uri_ == gst_player_media_info_get_uri(info)) {
        return;
    }

    uri_ = QString(gst_player_media_info_get_uri(info));

    title_ = QString::fromLocal8Bit(gst_player_media_info_get_title(info));

    // if media has no title, return the file name
    if (title_.isEmpty()) {
        QUrl url(gst_player_media_info_get_uri(info));
        title_ = url.fileName();
    }

    emit titleChanged();

    if (isSeekable_ != gst_player_media_info_is_seekable(info)) {
        isSeekable_ = !isSeekable_;
        emit seekableChanged();
    }

    if (!subtitleStreams_.isEmpty()) {
        qDeleteAll(subtitleStreams_);
        subtitleStreams_.clear();
    }

    if (!videoStreams_.isEmpty()) {
        qDeleteAll(videoStreams_);
        videoStreams_.clear();
    }

    if (!audioStreams_.isEmpty()) {
        qDeleteAll(audioStreams_);
        audioStreams_.clear();
    }

    GList *list = gst_player_get_subtitle_streams(info);

    g_list_foreach (list, [](gpointer data, gpointer user_data) {
        GstPlayerSubtitleInfo *info = static_cast<GstPlayerSubtitleInfo*>(data);
        QList<QObject*> *subs = static_cast<QList<QObject*>*>(user_data);

        subs->append(new SubtitleInfo(info));
    }, &subtitleStreams_);

    list = gst_player_get_video_streams(info);

    g_list_foreach (list, [](gpointer data, gpointer user_data) {
        GstPlayerVideoInfo *info = static_cast<GstPlayerVideoInfo*>(data);
        QList<QObject*> *videos = static_cast<QList<QObject*>*>(user_data);

        videos->append(new VideoInfo(info));
    }, &videoStreams_);

    list = gst_player_get_audio_streams(info);

    g_list_foreach (list, [](gpointer data, gpointer user_data) {
        GstPlayerAudioInfo *info = static_cast<GstPlayerAudioInfo*>(data);
        QList<QObject*> *audios = static_cast<QList<QObject*>*>(user_data);

        audios->append(new AudioInfo(info));
    }, &audioStreams_);

    GstSample *sample;
    GstMapInfo map_info;
    GstBuffer *buffer;
    const GstStructure *caps_struct;
    GstTagImageType type = GST_TAG_IMAGE_TYPE_UNDEFINED;

    /* get image sample buffer from media */
    sample = gst_player_media_info_get_image_sample (info);
    if (!sample)
      return;

    buffer = gst_sample_get_buffer (sample);
    caps_struct = gst_sample_get_info (sample);

    /* if sample is retrieved from preview-image tag then caps struct
     * will not be defined. */
    if (caps_struct)
      gst_structure_get_enum (caps_struct, "image-type",
          GST_TYPE_TAG_IMAGE_TYPE, reinterpret_cast<gint*>(&type));

    /* FIXME: Should we check more type ?? */
    if ((type != GST_TAG_IMAGE_TYPE_FRONT_COVER) &&
        (type != GST_TAG_IMAGE_TYPE_UNDEFINED) &&
        (type != GST_TAG_IMAGE_TYPE_NONE)) {
      gst_print ("unsupport type ... %d \n", type);
      return;
    }

    if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
      gst_print ("failed to map gst buffer \n");
      return;
    }

    sample_ = QImage::fromData(map_info.data, map_info.size);
    if (sample_.isNull())
        qWarning() << "failed to load media info sample image";

    emit sampleChanged();

    gst_buffer_unmap (buffer, &map_info);
}

VideoInfo::VideoInfo(GstPlayerVideoInfo *info)
    : StreamInfo(reinterpret_cast<GstPlayerStreamInfo*>(info))
    , video_(info)
    , resolution_(gst_player_video_info_get_width(info), gst_player_video_info_get_height(info))
{

}

QSize VideoInfo::resolution() const
{
    return resolution_;
}

AudioInfo::AudioInfo(GstPlayerAudioInfo *info)
    : StreamInfo(reinterpret_cast<GstPlayerStreamInfo*>(info))
    , audio_(info)
    , language_(gst_player_audio_info_get_language(info))
    , channels_(gst_player_audio_info_get_channels(info))
    , bitRate_(gst_player_audio_info_get_bitrate(info))
    , sampleRate_(gst_player_audio_info_get_sample_rate(info))
{

}

const QString &AudioInfo::language() const
{
    return language_;
}

int AudioInfo::channels() const
{
    return channels_;
}

int AudioInfo::bitRate() const
{
    return bitRate_;
}

int AudioInfo::sampleRate() const
{
    return sampleRate_;
}

SubtitleInfo::SubtitleInfo(GstPlayerSubtitleInfo *info)
    : StreamInfo(reinterpret_cast<GstPlayerStreamInfo*>(info))
    , subtitle_(info)
    , language_(gst_player_subtitle_info_get_language(info))
{

}

const QString &SubtitleInfo::language() const
{
    return language_;
}

int StreamInfo::index() const
{
    return index_;
}

StreamInfo::StreamInfo(GstPlayerStreamInfo *info)
    : stream_(info)
    , index_(gst_player_stream_info_get_index(info))
{

}

bool Player::isVideoAvailable() const
{
    GstPlayerVideoInfo *video_info;

    video_info = gst_player_get_current_video_track (player_);
    if (video_info) {
      g_object_unref (video_info);
      return true;
    }

    return false;
}

MediaInfo *Player::mediaInfo() const
{
    return mediaInfo_;
}

QVariant Player::currentVideo() const
{
    Q_ASSERT(player_ != 0);

    GstPlayerVideoInfo *track = gst_player_get_current_video_track(player_);

    if (!track)
        return QVariant();

    return QVariant::fromValue(new VideoInfo(track));
}

QVariant Player::currentAudio() const
{
    Q_ASSERT(player_ != 0);

    GstPlayerAudioInfo *track = gst_player_get_current_audio_track(player_);

    if (!track)
        return QVariant();

    return QVariant::fromValue(new AudioInfo(track));
}

QVariant Player::currentSubtitle() const
{
    Q_ASSERT(player_ != 0);

    GstPlayerSubtitleInfo *track = gst_player_get_current_subtitle_track(player_);

    if (!track)
        return QVariant();

    return QVariant::fromValue(new SubtitleInfo(track));
}

void Player::setCurrentVideo(QVariant track)
{
    Q_ASSERT(player_ != 0);

    VideoInfo* info = track.value<VideoInfo*>();
    Q_ASSERT(info);

    gst_player_set_video_track(player_, info->index());
}

void Player::setCurrentAudio(QVariant track)
{
    Q_ASSERT(player_ != 0);

    AudioInfo* info = track.value<AudioInfo*>();
    Q_ASSERT(info);

    gst_player_set_audio_track(player_, info->index());

}

void Player::setCurrentSubtitle(QVariant track)
{
    Q_ASSERT(player_ != 0);

    SubtitleInfo* info = track.value<SubtitleInfo*>();
    Q_ASSERT(info);

    gst_player_set_subtitle_track(player_, info->index());
}

bool Player::isSubtitleEnabled() const
{
    return subtitleEnabled_;
}

void Player::setSubtitleEnabled(bool enabled)
{
    Q_ASSERT(player_ != 0);

    subtitleEnabled_ = enabled;

    gst_player_set_subtitle_track_enabled(player_, enabled);

    emit subtitleEnabledChanged(enabled);
}

Player::Player(QObject *parent, VideoRenderer *renderer)
    : QObject(parent)
    , player_()
    , state_(STOPPED)
    , videoDimensions_(QSize())
    , mediaInfo_()
    , videoAvailable_(false)
    , subtitleEnabled_(false)
    , autoPlay_(false)
{

    player_ = gst_player_new(renderer ? renderer->renderer() : 0,
        gst_player_qt_signal_dispatcher_new(this));

    g_object_connect(player_,
        "swapped-signal::state-changed", G_CALLBACK (Player::onStateChanged), this,
        "swapped-signal::position-updated", G_CALLBACK (Player::onPositionUpdated), this,
        "swapped-signal::duration-changed", G_CALLBACK (Player::onDurationChanged), this,
        "swapped-signal::buffering", G_CALLBACK (Player::onBufferingChanged), this,
        "swapped-signal::video-dimensions-changed", G_CALLBACK (Player::onVideoDimensionsChanged), this,
        "swapped-signal::volume-changed", G_CALLBACK (Player::onVolumeChanged), this,
        "swapped-signal::mute-changed", G_CALLBACK (Player::onMuteChanged), this,
        "swapped-signal::media-info-updated", G_CALLBACK (Player::onMediaInfoUpdated), this,
        "swapped-signal::end-of-stream", G_CALLBACK (Player::onEndOfStreamReached), this, NULL);

    mediaInfo_ = new MediaInfo(this);
    gst_player_set_subtitle_track_enabled(player_, false);
}

Player::~Player()
{
    if (player_) {
      g_signal_handlers_disconnect_by_data(player_, this);
      gst_player_stop(player_);
      g_object_unref(player_);
    }
}

void
Player::onStateChanged(Player * player, GstPlayerState state)
{
    player->state_ =  static_cast<Player::State>(state);

    emit player->stateChanged(player->state_);
}

void
Player::onPositionUpdated(Player * player, GstClockTime position)
{
    emit player->positionUpdated(position);
}

void
Player::onDurationChanged(Player * player, GstClockTime duration)
{
    emit player->durationChanged(duration);
}

void
Player::onBufferingChanged(Player * player, int percent)
{
    emit player->bufferingChanged(percent);
}

void
Player::onVideoDimensionsChanged(Player * player, int w, int h)
{
    QSize res(w,h);

    if (res == player->videoDimensions_)
        return;
    player->videoDimensions_ = res;

    player->setResolution(res);

    emit player->resolutionChanged(res);
}

void
Player::onVolumeChanged(Player *player)
{
    qreal new_val;

    new_val = gst_player_get_volume (player->player_);

    emit player->volumeChanged(new_val);
}

void
Player::onMuteChanged(Player *player)
{
    bool new_val;

    new_val = gst_player_get_mute (player->player_);

    emit player->mutedChanged(new_val);
}

void
Player::onMediaInfoUpdated(Player *player, GstPlayerMediaInfo *media_info)
{
    bool val = player->isVideoAvailable();

    if (player->videoAvailable_ != val) {
        player->videoAvailable_ = val;
        emit player->videoAvailableChanged(val);
    }

    player->mediaInfo()->update(media_info);

    emit player->mediaInfoChanged();
}

void Player::onEndOfStreamReached(Player *player)
{
    Q_ASSERT(player != 0);

    emit player->endOfStream();
}

void Player::setUri(QUrl url)
{
    Q_ASSERT(player_ != 0);
    QByteArray uri = url.toEncoded();

    gst_player_set_uri(player_, uri.data());

    autoPlay_ ? play() : pause();

    emit sourceChanged(url);
}

QList<QUrl> Player::playlist() const
{
    return playlist_;
}

void Player::setPlaylist(const QList<QUrl> &playlist)
{
    if (!playlist_.isEmpty()) {
        playlist_.erase(playlist_.begin(), playlist_.end());
    }

    playlist_ = playlist;

    iter_ = playlist_.begin();
    setUri(*iter_);
}

void Player::next()
{
    if (playlist_.isEmpty())
        return;

    if (iter_ == playlist_.end())
        return;

    setUri(*++iter_);
}

void Player::previous()
{
    if (playlist_.isEmpty())
        return;

    if (iter_ == playlist_.begin())
        return;

    setUri(*--iter_);
}

bool Player::autoPlay() const
{
    return autoPlay_;
}

void Player::setAutoPlay(bool auto_play)
{
    autoPlay_ = auto_play;

    if (autoPlay_) {
        connect(this, SIGNAL(endOfStream()), SLOT(next()));
    }
}

QUrl Player::source() const
{
    Q_ASSERT(player_ != 0);

    QString url = QString::fromLocal8Bit(gst_player_get_uri(player_));

    return QUrl(url);
}

qint64 Player::duration() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_duration(player_);
}

qint64 Player::position() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_position(player_);
}

qreal Player::volume() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_volume(player_);
}

bool Player::isMuted() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_mute(player_);
}

int Player::buffering() const
{
    return 0;
}

QSize Player::resolution() const
{
    return videoDimensions_;
}

void Player::setResolution(QSize size)
{
    videoDimensions_ = size;
}

Player::State Player::state() const
{
    return state_;
}

GstElement *Player::pipeline() const
{
    Q_ASSERT(player_ != 0);

    return gst_player_get_pipeline(player_);
}

void Player::play()
{
    Q_ASSERT(player_ != 0);

    gst_player_play(player_);
}

void Player::pause()
{
    Q_ASSERT(player_ != 0);

    gst_player_pause(player_);
}

void Player::stop()
{
    Q_ASSERT(player_ != 0);

    gst_player_stop(player_);
}

void Player::seek(qint64 position)
{
    Q_ASSERT(player_ != 0);

    gst_player_seek(player_, position);
}

void Player::setSource(QUrl const& url)
{
    Q_ASSERT(player_ != 0);

    // discard playlist
    if (!playlist_.isEmpty()) {
        playlist_.erase(playlist_.begin(), playlist_.end());
    }

    setUri(url);

    emit sourceChanged(url);
}

void Player::setVolume(qreal val)
{
    Q_ASSERT(player_ != 0);

    gst_player_set_volume(player_, val);
}

void Player::setMuted(bool val)
{
    Q_ASSERT(player_ != 0);

    gst_player_set_mute(player_, val);
}

void Player::setPosition(qint64 pos)
{
    Q_ASSERT(player_ != 0);

    gst_player_seek(player_, pos);
}

GstPlayerVideoRenderer *VideoRenderer::renderer()
{
    return static_cast<GstPlayerVideoRenderer*> (gst_object_ref (renderer_));
}

VideoRenderer::VideoRenderer()
{
    renderer_ = static_cast<GstPlayerVideoRenderer*>
        (g_object_new (GST_TYPE_PLAYER_QT_VIDEO_RENDERER, "renderer", this, NULL));
}

VideoRenderer::~VideoRenderer()
{
    if (renderer_) gst_object_unref(renderer_);
}

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
gst_player_qt_video_renderer_init (G_GNUC_UNUSED GstPlayerQtVideoRenderer * self)
{

}

static GstElement *
gst_player_qt_video_renderer_create_video_sink
    (GstPlayerVideoRenderer * iface, G_GNUC_UNUSED GstPlayer *player)
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
      g_param_spec_pointer ("player", "Player instance", "",
      static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (gobject_class,
      QT_SIGNAL_DISPATCHER_PROP_LAST,
      qt_signal_dispatcher_param_specs);
}

static void
gst_player_qt_signal_dispatcher_init
    (G_GNUC_UNUSED GstPlayerQtSignalDispatcher * self)
{

}

static void
gst_player_qt_signal_dispatcher_dispatch (GstPlayerSignalDispatcher
    * iface, G_GNUC_UNUSED GstPlayer * player, void (*emitter) (gpointer data), gpointer data,
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
