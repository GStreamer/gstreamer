/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2015 Brijesh Singh <brijesh.ksingh@gmail.com>
 * Copyright (C) 2019-2020 Stephan Hesse <stephan@emliri.com>
 * Copyright (C) 2020 Philippe Normand <philn@igalia.com>
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

/**
 * SECTION:gstplay
 * @title: GstPlay
 * @short_description: Player
 * @symbols:
 * - GstPlay
 *
 * The goal of the GstPlay library is to ease the integration of multimedia
 * playback features in applications. Thus, if you need to build a media player
 * from the ground-up, GstPlay provides the features you will most likely need.
 *
 * An example player is available in gst-examples/playback/player/gst-play/.
 *
 * Internally the GstPlay makes use of the `playbin3` element. The legacy
 * `playbin2` can be selected if the `GST_PLAY_USE_PLAYBIN3=0` environment
 * variable has been set.
 *
 * **Important note**: If your application relies on the GstBus to get
 * notifications from GstPlay, you need to add some explicit clean-up code in
 * order to prevent the GstPlay object from leaking. See below for the details.
 * If you use the GstPlaySignalAdapter, no special clean-up is required.
 *
 * When the GstPlaySignalAdapter is not used, the GstBus owned by GstPlay should
 * be set to flushing state before any attempt to drop the last reference of the
 * GstPlay object. An example in C:
 *
 * ```c
 * ...
 * GstBus *bus = gst_play_get_message_bus (player);
 * gst_bus_set_flushing (bus, TRUE);
 * gst_object_unref (bus);
 * gst_object_unref (player);
 * ```
 *
 * The messages managed by the player contain a reference to itself, and if the
 * bus watch is just removed together with dropping the player then the bus will
 * simply keep them around forever (and the bus never goes away because the
 * player has a strong reference to it, so there's a reference cycle as long as
 * there are messages). Setting the bus to flushing state forces it to get rid
 * of its queued messages, thus breaking any possible reference cycle.
 *
 * Since: 1.20
 */

/* TODO:
 *
 * - Equalizer
 * - Gapless playback
 * - Frame stepping
 * - Subtitle font, connection speed
 * - Deinterlacing
 * - Buffering control (-> progressive downloading)
 * - Playlist/queue object
 * - Custom video sink (e.g. embed in GL scene)
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstplay.h"
#include "gstplay-video-renderer-private.h"
#include "gstplay-media-info-private.h"
#include "gstplay-message-private.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/tag/tag.h>
#include <gst/pbutils/pbutils.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_play_debug);
#define GST_CAT_DEFAULT gst_play_debug

#define DEFAULT_URI NULL
#define DEFAULT_POSITION GST_CLOCK_TIME_NONE
#define DEFAULT_DURATION GST_CLOCK_TIME_NONE
#define DEFAULT_VOLUME 1.0
#define DEFAULT_MUTE FALSE
#define DEFAULT_RATE 1.0
#define DEFAULT_POSITION_UPDATE_INTERVAL_MS 100
#define DEFAULT_AUDIO_VIDEO_OFFSET 0
#define DEFAULT_SUBTITLE_VIDEO_OFFSET 0

/**
 * gst_play_error_quark:
 * Since: 1.20
 */
GQuark
gst_play_error_quark (void)
{
  return g_quark_from_static_string ("gst-play-error-quark");
}

enum
{
  PROP_0,
  PROP_VIDEO_RENDERER,
  PROP_URI,
  PROP_SUBURI,
  PROP_POSITION,
  PROP_DURATION,
  PROP_MEDIA_INFO,
  PROP_CURRENT_AUDIO_TRACK,
  PROP_CURRENT_VIDEO_TRACK,
  PROP_CURRENT_SUBTITLE_TRACK,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_RATE,
  PROP_PIPELINE,
  PROP_VIDEO_MULTIVIEW_MODE,
  PROP_VIDEO_MULTIVIEW_FLAGS,
  PROP_AUDIO_VIDEO_OFFSET,
  PROP_SUBTITLE_VIDEO_OFFSET,
  PROP_LAST
};

enum
{
  GST_PLAY_FLAG_VIDEO = (1 << 0),
  GST_PLAY_FLAG_AUDIO = (1 << 1),
  GST_PLAY_FLAG_SUBTITLE = (1 << 2),
  GST_PLAY_FLAG_VIS = (1 << 3)
};

struct _GstPlay
{
  GstObject parent;

  GstPlayVideoRenderer *video_renderer;

  gchar *uri;
  gchar *redirect_uri;
  gchar *suburi;

  GThread *thread;
  GMutex lock;
  GCond cond;
  GMainContext *context;
  GMainLoop *loop;

  GstBus *api_bus;

  GstElement *playbin;
  GstBus *bus;
  GstState target_state, current_state;
  gboolean is_live, is_eos;
  GSource *tick_source, *ready_timeout_source;

  GstClockTime cached_duration;
  gint64 cached_position;

  gdouble rate;

  GstPlayState app_state;

  gint buffering_percent;

  GstTagList *global_tags;
  GstPlayMediaInfo *media_info;

  GstElement *current_vis_element;

  GstStructure *config;

  GList *missing_plugin_messages;

  /* Protected by lock */
  gboolean seek_pending;        /* Only set from main context */
  GstClockTime last_seek_time;  /* Only set from main context */
  GSource *seek_source;
  GstClockTime seek_position;

  GstStreamCollection *collection;
  gchar *video_sid;
  gboolean video_enabled;
  gchar *audio_sid;
  gboolean audio_enabled;
  gchar *subtitle_sid;
  gboolean subtitle_enabled;
  gulong stream_notify_id;
};

struct _GstPlayClass
{
  GstObjectClass parent_class;
};

#define parent_class gst_play_parent_class
G_DEFINE_TYPE (GstPlay, gst_play, GST_TYPE_OBJECT);

static GParamSpec *param_specs[PROP_LAST] = { NULL, };

static void gst_play_dispose (GObject * object);
static void gst_play_finalize (GObject * object);
static void gst_play_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_play_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_play_constructed (GObject * object);

static gpointer gst_play_main (gpointer data);

static void gst_play_set_playbin_video_sink (GstPlay * self);

static void gst_play_seek_internal_locked (GstPlay * self);
static void gst_play_stop_internal (GstPlay * self, gboolean transient);
static gboolean gst_play_pause_internal (gpointer user_data);
static gboolean gst_play_play_internal (gpointer user_data);
static gboolean gst_play_seek_internal (gpointer user_data);
static void gst_play_set_rate_internal (GstPlay * self);
static void change_state (GstPlay * self, GstPlayState state);

static GstPlayMediaInfo *gst_play_media_info_create (GstPlay * self);

static void gst_play_stream_info_update (GstPlay * self, GstPlayStreamInfo * s);
static GstPlayStreamInfo *gst_play_stream_info_find (GstPlayMediaInfo *
    media_info, GType type, gint stream_index);

static void gst_play_video_info_update (GstPlay * self,
    GstPlayStreamInfo * stream_info);
static void gst_play_audio_info_update (GstPlay * self,
    GstPlayStreamInfo * stream_info);
static void gst_play_subtitle_info_update (GstPlay * self,
    GstPlayStreamInfo * stream_info);

static gboolean gst_play_select_streams (GstPlay * self);

/* For playbin3 */
static void gst_play_streams_info_create_from_collection (GstPlay * self,
    GstPlayMediaInfo * media_info, GstStreamCollection * collection);
static void gst_play_stream_info_update_from_stream (GstPlay * self,
    GstPlayStreamInfo * s, GstStream * stream);
static GstPlayStreamInfo *gst_play_stream_info_find_from_stream_id
    (GstPlayMediaInfo * media_info, const gchar * stream_id);
static GstPlayStreamInfo *gst_play_stream_info_get_current_from_stream_id
    (GstPlay * self, const gchar * stream_id, GType type);
static void stream_notify_cb (GstStreamCollection * collection,
    GstStream * stream, GParamSpec * pspec, GstPlay * self);

static void on_media_info_updated (GstPlay * self);

static void *get_title (GstTagList * tags);
static void *get_container_format (GstTagList * tags);
static void *get_from_tags (GstPlay * self, GstPlayMediaInfo * media_info,
    void *(*func) (GstTagList *));
static void *get_cover_sample (GstTagList * tags);

static void remove_seek_source (GstPlay * self);

static gboolean query_position (GstPlay * self, GstClockTime * position);

static void gst_play_set_uri_details (GstPlay * self, GstStructure * details);

static void
gst_play_init (GstPlay * self)
{
  GST_TRACE_OBJECT (self, "Initializing");

  self = gst_play_get_instance_private (self);

  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
  self->api_bus = gst_bus_new ();
  gst_object_set_name (GST_OBJECT (self->api_bus), "api_bus");

  /* *INDENT-OFF* */
  self->config = gst_structure_new_static_str ("play-config",
      "position-interval-update", G_TYPE_UINT, DEFAULT_POSITION_UPDATE_INTERVAL_MS,
      "accurate-seek", G_TYPE_BOOLEAN, FALSE,
      "pipeline-dump-error-in-details", G_TYPE_BOOLEAN, FALSE,
      NULL);
  /* *INDENT-ON* */

  self->seek_pending = FALSE;
  self->seek_position = GST_CLOCK_TIME_NONE;
  self->last_seek_time = GST_CLOCK_TIME_NONE;

  self->cached_position = 0;
  self->cached_duration = GST_CLOCK_TIME_NONE;

  self->audio_enabled = TRUE;
  self->video_enabled = TRUE;
  self->subtitle_enabled = TRUE;

  GST_TRACE_OBJECT (self, "Initialized");
}

/*
 * Works same as gst_structure_set to set field/type/value triplets on message data
 */
static void
api_bus_post_message (GstPlay * self, GstPlayMessage message_type,
    const gchar * firstfield, ...)
{
  GstStructure *message_data = NULL;
  GstStructure *details = NULL;
  GstMessage *msg = NULL;
  va_list varargs;

  GST_INFO ("Posting API-bus message-type: %s",
      gst_play_message_get_name (message_type));
  message_data = gst_structure_new_static_str (GST_PLAY_MESSAGE_DATA,
      GST_PLAY_MESSAGE_DATA_TYPE, GST_TYPE_PLAY_MESSAGE, message_type, NULL);

  va_start (varargs, firstfield);
  gst_structure_set_valist (message_data, firstfield, varargs);
  va_end (varargs);

  msg = gst_message_new_custom (GST_MESSAGE_APPLICATION,
      GST_OBJECT (self), message_data);

  // ERROR/WARNING messages store the details in differently named fields for
  // backwards compatibility
  if (message_type == GST_PLAY_MESSAGE_ERROR) {
    const GValue *v = gst_structure_get_value (message_data,
        GST_PLAY_MESSAGE_DATA_ERROR_DETAILS);
    details = g_value_get_boxed (v);
  } else if (message_type == GST_PLAY_MESSAGE_WARNING) {
    const GValue *v = gst_structure_get_value (message_data,
        GST_PLAY_MESSAGE_DATA_WARNING_DETAILS);
    details = g_value_get_boxed (v);
  }

  if (!details)
    details = gst_message_writable_details (msg);

  gst_play_set_uri_details (self, details);

  GST_DEBUG_OBJECT (self,
      "Created message with payload: [ %" GST_PTR_FORMAT " ]", message_data);
  gst_bus_post (self->api_bus, msg);
}

static void
gst_play_class_init (GstPlayClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_play_debug, "gst-play", 0, "GstPlay");
  gst_play_error_quark ();

  gobject_class->set_property = gst_play_set_property;
  gobject_class->get_property = gst_play_get_property;
  gobject_class->dispose = gst_play_dispose;
  gobject_class->finalize = gst_play_finalize;
  gobject_class->constructed = gst_play_constructed;

  param_specs[PROP_VIDEO_RENDERER] =
      g_param_spec_object ("video-renderer",
      "Video Renderer", "Video renderer to use for rendering videos",
      GST_TYPE_PLAY_VIDEO_RENDERER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_URI] = g_param_spec_string ("uri", "URI", "Current URI",
      DEFAULT_URI, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_SUBURI] = g_param_spec_string ("suburi", "Subtitle URI",
      "Current Subtitle URI", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_POSITION] =
      g_param_spec_uint64 ("position", "Position", "Current Position",
      0, G_MAXUINT64, DEFAULT_POSITION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_MEDIA_INFO] =
      g_param_spec_object ("media-info", "Media Info",
      "Current media information", GST_TYPE_PLAY_MEDIA_INFO,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_CURRENT_AUDIO_TRACK] =
      g_param_spec_object ("current-audio-track", "Current Audio Track",
      "Current audio track information", GST_TYPE_PLAY_AUDIO_INFO,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_CURRENT_VIDEO_TRACK] =
      g_param_spec_object ("current-video-track", "Current Video Track",
      "Current video track information", GST_TYPE_PLAY_VIDEO_INFO,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_CURRENT_SUBTITLE_TRACK] =
      g_param_spec_object ("current-subtitle-track", "Current Subtitle Track",
      "Current audio subtitle information", GST_TYPE_PLAY_SUBTITLE_INFO,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "Duration",
      0, G_MAXUINT64, DEFAULT_DURATION,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_VOLUME] =
      g_param_spec_double ("volume", "Volume", "Volume",
      0, 10.0, DEFAULT_VOLUME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_MUTE] =
      g_param_spec_boolean ("mute", "Mute", "Mute",
      DEFAULT_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_PIPELINE] =
      g_param_spec_object ("pipeline", "Pipeline",
      "GStreamer pipeline that is used",
      GST_TYPE_ELEMENT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_RATE] =
      g_param_spec_double ("rate", "rate", "Playback rate",
      -64.0, 64.0, DEFAULT_RATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_VIDEO_MULTIVIEW_MODE] =
      g_param_spec_enum ("video-multiview-mode",
      "Multiview Mode Override",
      "Re-interpret a video stream as one of several frame-packed stereoscopic modes.",
      GST_TYPE_VIDEO_MULTIVIEW_FRAME_PACKING,
      GST_VIDEO_MULTIVIEW_FRAME_PACKING_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_VIDEO_MULTIVIEW_FLAGS] =
      g_param_spec_flags ("video-multiview-flags",
      "Multiview Flags Override",
      "Override details of the multiview frame layout",
      GST_TYPE_VIDEO_MULTIVIEW_FLAGS, GST_VIDEO_MULTIVIEW_FLAGS_NONE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_AUDIO_VIDEO_OFFSET] =
      g_param_spec_int64 ("audio-video-offset", "Audio Video Offset",
      "The synchronisation offset between audio and video in nanoseconds",
      G_MININT64, G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  param_specs[PROP_SUBTITLE_VIDEO_OFFSET] =
      g_param_spec_int64 ("subtitle-video-offset", "Text Video Offset",
      "The synchronisation offset between text and video in nanoseconds",
      G_MININT64, G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, param_specs);
}

static void
gst_play_dispose (GObject * object)
{
  GstPlay *self = GST_PLAY (object);

  GST_TRACE_OBJECT (self, "Stopping main thread");

  gst_bus_set_flushing (self->api_bus, TRUE);

  if (self->loop) {
    g_main_loop_quit (self->loop);

    if (self->thread != g_thread_self ())
      g_thread_join (self->thread);
    else
      g_thread_unref (self->thread);
    self->thread = NULL;

    g_main_loop_unref (self->loop);
    self->loop = NULL;

    g_main_context_unref (self->context);
    self->context = NULL;
  }

  gst_clear_object (&self->api_bus);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_finalize (GObject * object)
{
  GstPlay *self = GST_PLAY (object);

  GST_TRACE_OBJECT (self, "Finalizing");

  g_free (self->uri);
  g_free (self->redirect_uri);
  g_free (self->suburi);
  g_free (self->video_sid);
  g_free (self->audio_sid);
  g_free (self->subtitle_sid);
  if (self->global_tags)
    gst_tag_list_unref (self->global_tags);
  if (self->video_renderer)
    g_object_unref (self->video_renderer);
  if (self->current_vis_element)
    gst_object_unref (self->current_vis_element);
  if (self->config)
    gst_structure_free (self->config);
  if (self->collection)
    gst_object_unref (self->collection);
  if (self->media_info)
    g_object_unref (self->media_info);
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_play_constructed (GObject * object)
{
  GstPlay *self = GST_PLAY (object);

  GST_TRACE_OBJECT (self, "Constructed");

  g_mutex_lock (&self->lock);
  self->thread = g_thread_new ("GstPlay", gst_play_main, self);
  while (!self->loop || !g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, &self->lock);

  gst_play_set_playbin_video_sink (self);
  g_mutex_unlock (&self->lock);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static gboolean
gst_play_set_uri_internal (gpointer user_data)
{
  GstPlay *self = user_data;

  gst_play_stop_internal (self, FALSE);

  g_mutex_lock (&self->lock);

  GST_DEBUG_OBJECT (self, "Changing URI to '%s'", GST_STR_NULL (self->uri));

  g_object_set (self->playbin, "uri", self->uri, NULL);

  api_bus_post_message (self, GST_PLAY_MESSAGE_URI_LOADED,
      GST_PLAY_MESSAGE_DATA_URI, G_TYPE_STRING, self->uri, NULL);

  g_object_set (self->playbin, "suburi", NULL, NULL);

  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

static gboolean
gst_play_set_suburi_internal (gpointer user_data)
{
  GstPlay *self = user_data;
  GstClockTime position;
  GstState target_state;

  /* save the state and position */
  target_state = self->target_state;
  position = gst_play_get_position (self);

  gst_play_stop_internal (self, TRUE);
  g_mutex_lock (&self->lock);

  GST_DEBUG_OBJECT (self, "Changing SUBURI to '%s'",
      GST_STR_NULL (self->suburi));

  g_object_set (self->playbin, "suburi", self->suburi, NULL);

  g_mutex_unlock (&self->lock);

  /* restore state and position */
  if (position != GST_CLOCK_TIME_NONE)
    gst_play_seek (self, position);
  if (target_state == GST_STATE_PAUSED)
    gst_play_pause_internal (self);
  else if (target_state == GST_STATE_PLAYING)
    gst_play_play_internal (self);

  return G_SOURCE_REMOVE;
}

static void
gst_play_set_rate_internal (GstPlay * self)
{
  self->seek_position = gst_play_get_position (self);

  /* If there is no seek being dispatch to the main context currently do that,
   * otherwise we just updated the rate so that it will be taken by
   * the seek handler from the main context instead of the old one.
   */
  if (!self->seek_source) {
    /* If no seek is pending then create new seek source */
    if (!self->seek_pending) {
      self->seek_source = g_idle_source_new ();
      g_source_set_callback (self->seek_source,
          (GSourceFunc) gst_play_seek_internal, self, NULL);
      g_source_attach (self->seek_source, self->context);
    }
  }
}

static void
gst_play_set_playbin_video_sink (GstPlay * self)
{
  GstElement *video_sink = NULL;

  if (self->video_renderer != NULL) {
    video_sink =
        gst_play_video_renderer_create_video_sink (self->video_renderer, self);
  }

  if (video_sink) {
    gst_object_ref_sink (video_sink);
    g_object_set (self->playbin, "video-sink", video_sink, NULL);
    gst_object_unref (video_sink);
  }
}

static void
gst_play_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlay *self = GST_PLAY (object);

  switch (prop_id) {
    case PROP_VIDEO_RENDERER:
      g_mutex_lock (&self->lock);
      g_clear_object (&self->video_renderer);
      self->video_renderer = g_value_dup_object (value);

      // When the video_renderer is a GstPlayerWrappedVideoRenderer it cannot be set
      // at construction time because it requires a valid pipeline which is created
      // only after GstPlay has been constructed. That is why the video renderer is
      // set *after* GstPlay has been constructed.
      if (self->thread) {
        gst_play_set_playbin_video_sink (self);
      }
      g_mutex_unlock (&self->lock);
      break;
    case PROP_URI:{
      g_mutex_lock (&self->lock);
      g_free (self->uri);
      g_free (self->redirect_uri);
      self->redirect_uri = NULL;

      g_free (self->suburi);
      self->suburi = NULL;

      self->uri = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "Set uri=%s", GST_STR_NULL (self->uri));
      g_mutex_unlock (&self->lock);

      g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT,
          gst_play_set_uri_internal, self, NULL);
      break;
    }
    case PROP_SUBURI:{
      g_mutex_lock (&self->lock);
      g_free (self->suburi);

      self->suburi = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "Set suburi=%s", self->suburi);
      g_mutex_unlock (&self->lock);

      g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT,
          gst_play_set_suburi_internal, self, NULL);
      break;
    }
    case PROP_VOLUME:
      GST_DEBUG_OBJECT (self, "Set volume=%lf", g_value_get_double (value));
      g_object_set_property (G_OBJECT (self->playbin), "volume", value);
      break;
    case PROP_RATE:
      g_mutex_lock (&self->lock);
      self->rate = g_value_get_double (value);
      GST_DEBUG_OBJECT (self, "Set rate=%lf", g_value_get_double (value));
      gst_play_set_rate_internal (self);
      g_mutex_unlock (&self->lock);
      break;
    case PROP_MUTE:
      GST_DEBUG_OBJECT (self, "Set mute=%d", g_value_get_boolean (value));
      g_object_set_property (G_OBJECT (self->playbin), "mute", value);
      break;
    case PROP_VIDEO_MULTIVIEW_MODE:
      GST_DEBUG_OBJECT (self, "Set multiview mode=%u",
          g_value_get_enum (value));
      g_object_set_property (G_OBJECT (self->playbin), "video-multiview-mode",
          value);
      break;
    case PROP_VIDEO_MULTIVIEW_FLAGS:
      GST_DEBUG_OBJECT (self, "Set multiview flags=%x",
          g_value_get_flags (value));
      g_object_set_property (G_OBJECT (self->playbin), "video-multiview-flags",
          value);
      break;
    case PROP_AUDIO_VIDEO_OFFSET:
      g_object_set_property (G_OBJECT (self->playbin), "av-offset", value);
      break;
    case PROP_SUBTITLE_VIDEO_OFFSET:
      g_object_set_property (G_OBJECT (self->playbin), "text-offset", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_play_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPlay *self = GST_PLAY (object);

  switch (prop_id) {
    case PROP_URI:
      g_mutex_lock (&self->lock);
      g_value_set_string (value, self->uri);
      g_mutex_unlock (&self->lock);
      break;
    case PROP_SUBURI:
      g_mutex_lock (&self->lock);
      g_value_set_string (value, self->suburi);
      g_mutex_unlock (&self->lock);
      GST_DEBUG_OBJECT (self, "Returning suburi=%s",
          g_value_get_string (value));
      break;
    case PROP_POSITION:{
      GstClockTime position = GST_CLOCK_TIME_NONE;
      query_position (self, &position);
      g_value_set_uint64 (value, position);
      GST_TRACE_OBJECT (self, "Returning position=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (g_value_get_uint64 (value)));
      break;
    }
    case PROP_DURATION:{
      g_value_set_uint64 (value, self->cached_duration);
      GST_TRACE_OBJECT (self, "Returning duration=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (g_value_get_uint64 (value)));
      break;
    }
    case PROP_MEDIA_INFO:{
      GstPlayMediaInfo *media_info = gst_play_get_media_info (self);
      g_value_take_object (value, media_info);
      break;
    }
    case PROP_CURRENT_AUDIO_TRACK:{
      GstPlayAudioInfo *audio_info = gst_play_get_current_audio_track (self);
      g_value_take_object (value, audio_info);
      break;
    }
    case PROP_CURRENT_VIDEO_TRACK:{
      GstPlayVideoInfo *video_info = gst_play_get_current_video_track (self);
      g_value_take_object (value, video_info);
      break;
    }
    case PROP_CURRENT_SUBTITLE_TRACK:{
      GstPlaySubtitleInfo *subtitle_info =
          gst_play_get_current_subtitle_track (self);
      g_value_take_object (value, subtitle_info);
      break;
    }
    case PROP_VOLUME:
      g_object_get_property (G_OBJECT (self->playbin), "volume", value);
      GST_TRACE_OBJECT (self, "Returning volume=%lf",
          g_value_get_double (value));
      break;
    case PROP_RATE:
      g_mutex_lock (&self->lock);
      g_value_set_double (value, self->rate);
      g_mutex_unlock (&self->lock);
      break;
    case PROP_MUTE:
      g_object_get_property (G_OBJECT (self->playbin), "mute", value);
      GST_TRACE_OBJECT (self, "Returning mute=%d", g_value_get_boolean (value));
      break;
    case PROP_PIPELINE:
      g_value_set_object (value, self->playbin);
      break;
    case PROP_VIDEO_MULTIVIEW_MODE:{
      g_object_get_property (G_OBJECT (self->playbin), "video-multiview-mode",
          value);
      GST_TRACE_OBJECT (self, "Return multiview mode=%d",
          g_value_get_enum (value));
      break;
    }
    case PROP_VIDEO_MULTIVIEW_FLAGS:{
      g_object_get_property (G_OBJECT (self->playbin), "video-multiview-flags",
          value);
      GST_TRACE_OBJECT (self, "Return multiview flags=%x",
          g_value_get_flags (value));
      break;
    }
    case PROP_AUDIO_VIDEO_OFFSET:
      g_object_get_property (G_OBJECT (self->playbin), "av-offset", value);
      break;
    case PROP_SUBTITLE_VIDEO_OFFSET:
      g_object_get_property (G_OBJECT (self->playbin), "text-offset", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
main_loop_running_cb (gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);

  GST_TRACE_OBJECT (self, "Main loop running now");

  g_mutex_lock (&self->lock);
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

static void
change_state (GstPlay * self, GstPlayState state)
{
  if (state == self->app_state)
    return;

  GST_DEBUG_OBJECT (self, "Changing app state from %s to %s",
      gst_play_state_get_name (self->app_state),
      gst_play_state_get_name (state));

  self->app_state = state;

  api_bus_post_message (self, GST_PLAY_MESSAGE_STATE_CHANGED,
      GST_PLAY_MESSAGE_DATA_PLAY_STATE, GST_TYPE_PLAY_STATE,
      self->app_state, NULL);
}

static gboolean
tick_cb (gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstClockTime position;
  if (query_position (self, &position)) {
    api_bus_post_message (self, GST_PLAY_MESSAGE_POSITION_UPDATED,
        GST_PLAY_MESSAGE_DATA_POSITION, GST_TYPE_CLOCK_TIME, position, NULL);
  }

  return G_SOURCE_CONTINUE;
}

/*
 * Returns true when position is queried and differed from cached position.
 * Sets position to cached value, and to queried value if position can be queried
 * and different.
 */
static gboolean
query_position (GstPlay * self, GstClockTime * position)
{
  gint64 current_position;
  *position = self->cached_position;
  if (self->target_state >= GST_STATE_PAUSED
      && gst_element_query_position (self->playbin, GST_FORMAT_TIME,
          &current_position)) {
    GST_LOG_OBJECT (self, "Queried position %" GST_TIME_FORMAT,
        GST_TIME_ARGS (current_position));
    if (self->cached_position != current_position) {
      self->cached_position = current_position;
      *position = (GstClockTime) current_position;
      return TRUE;
    }
  }
  return FALSE;
}

static void
add_tick_source (GstPlay * self)
{
  guint position_update_interval_ms;

  if (self->tick_source)
    return;

  position_update_interval_ms =
      gst_play_config_get_position_update_interval (self->config);
  if (!position_update_interval_ms)
    return;

  self->tick_source = g_timeout_source_new (position_update_interval_ms);
  g_source_set_callback (self->tick_source, (GSourceFunc) tick_cb, self, NULL);
  g_source_attach (self->tick_source, self->context);
}

static void
remove_tick_source (GstPlay * self)
{
  if (!self->tick_source)
    return;

  g_source_destroy (self->tick_source);
  g_source_unref (self->tick_source);
  self->tick_source = NULL;
}

static gboolean
ready_timeout_cb (gpointer user_data)
{
  GstPlay *self = user_data;

  if (self->target_state <= GST_STATE_READY) {
    GST_DEBUG_OBJECT (self, "Setting pipeline to NULL state");
    self->target_state = GST_STATE_NULL;
    self->current_state = GST_STATE_NULL;
    gst_element_set_state (self->playbin, GST_STATE_NULL);
  }

  return G_SOURCE_REMOVE;
}

static void
add_ready_timeout_source (GstPlay * self)
{
  if (self->ready_timeout_source)
    return;

  self->ready_timeout_source = g_timeout_source_new_seconds (60);
  g_source_set_callback (self->ready_timeout_source,
      (GSourceFunc) ready_timeout_cb, self, NULL);
  g_source_attach (self->ready_timeout_source, self->context);
}

static void
remove_ready_timeout_source (GstPlay * self)
{
  if (!self->ready_timeout_source)
    return;

  g_source_destroy (self->ready_timeout_source);
  g_source_unref (self->ready_timeout_source);
  self->ready_timeout_source = NULL;
}


static void
on_error (GstPlay * self, GError * err, GstStructure * details)
{
#ifndef GST_DISABLE_GST_DEBUG
  gchar *dot_data = NULL;
#endif

  GST_ERROR_OBJECT (self, "Error: %s (%s, %d)", err->message,
      g_quark_to_string (err->domain), err->code);

  if (!details)
    details = gst_structure_new_static_str_empty ("error-details");

#ifndef GST_DISABLE_GST_DEBUG
  if (gst_play_config_get_pipeline_dump_in_error_details (self->config)) {
    dot_data = gst_debug_bin_to_dot_data (GST_BIN_CAST (self->playbin),
        GST_DEBUG_GRAPH_SHOW_ALL);
    gst_structure_set (details, "pipeline-dump", G_TYPE_STRING, dot_data, NULL);
  }
#endif
  api_bus_post_message (self, GST_PLAY_MESSAGE_ERROR,
      GST_PLAY_MESSAGE_DATA_ERROR, G_TYPE_ERROR, err,
      GST_PLAY_MESSAGE_DATA_ERROR_DETAILS, GST_TYPE_STRUCTURE, details, NULL);

#ifndef GST_DISABLE_GST_DEBUG
  g_free (dot_data);
#endif
  gst_structure_free (details);

  g_error_free (err);

  remove_tick_source (self);
  remove_ready_timeout_source (self);

  g_list_free_full (self->missing_plugin_messages,
      (GDestroyNotify) gst_message_unref);
  self->missing_plugin_messages = NULL;

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_NULL;
  self->is_live = FALSE;
  self->is_eos = FALSE;
  gst_element_set_state (self->playbin, GST_STATE_NULL);
  change_state (self, GST_PLAY_STATE_STOPPED);
  self->buffering_percent = 100;

  g_mutex_lock (&self->lock);
  if (self->media_info) {
    g_object_unref (self->media_info);
    self->media_info = NULL;
  }

  if (self->global_tags) {
    gst_tag_list_unref (self->global_tags);
    self->global_tags = NULL;
  }

  self->seek_pending = FALSE;
  remove_seek_source (self);
  self->seek_position = GST_CLOCK_TIME_NONE;
  self->last_seek_time = GST_CLOCK_TIME_NONE;
  g_mutex_unlock (&self->lock);
}

static void
dump_dot_file (GstPlay * self, const gchar * name)
{
  gchar *full_name;

  full_name = g_strdup_printf ("gst-play.%p.%s", self, name);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->playbin),
      GST_DEBUG_GRAPH_SHOW_ALL, full_name);

  g_free (full_name);
}

static void
gst_play_set_missing_plugin_details (GstPlay * self, GstStructure * details)
{
  GValue missing_plugin_details = G_VALUE_INIT;

  g_value_init (&missing_plugin_details, GST_TYPE_ARRAY);

  for (GList * l = self->missing_plugin_messages; l; l = l->next) {
    GstMessage *missing_plugin_message = l->data;
    GValue v = G_VALUE_INIT;
    GstStructure *s;
    gchar *description, *installer_details;

    description =
        gst_missing_plugin_message_get_description (missing_plugin_message);
    installer_details =
        gst_missing_plugin_message_get_installer_detail
        (missing_plugin_message);

    s = gst_structure_new_static_str ("missing-plugin-detail", "description",
        G_TYPE_STRING, description, "installer-details", G_TYPE_STRING,
        installer_details, NULL);
    g_value_init (&v, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&v, s);
    gst_value_array_append_and_take_value (&missing_plugin_details, &v);

    g_free (description);
    g_free (installer_details);
  }


  gst_structure_take_value_static_str (details, "missing-plugin-details",
      &missing_plugin_details);
}

static void
gst_play_set_uri_details (GstPlay * self, GstStructure * details)
{
  if (!gst_structure_has_field (details, "uri")) {
    gchar *uri;

    g_object_get (self->playbin, "current-uri", &uri, NULL);
    if (!uri)
      g_object_get (self->playbin, "uri", &uri, NULL);
    if (!uri)
      uri = g_strdup (self->uri);
    gst_structure_set (details, "uri", G_TYPE_STRING, uri, NULL);
    g_free (uri);
  }
}

static void
gst_play_set_stream_id_details (GstPlay * self, GstMessage * msg,
    GstStructure * details)
{
  if (!gst_structure_has_field (details, "stream-id")) {
    GstPad *pad = NULL;
    gchar *stream_id;

    if (GST_IS_ELEMENT (GST_MESSAGE_SRC (msg))) {
      GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (msg));

      // If the message src has only one sinkpad (or is a source element)
      // grab the stream id from there
      GST_OBJECT_LOCK (element);
      if (element->numsinkpads == 1) {
        pad = gst_object_ref (element->sinkpads->data);
      } else if (element->numsinkpads == 0 && element->numsrcpads > 0) {
        pad = gst_object_ref (element->srcpads->data);
      }
      GST_OBJECT_UNLOCK (element);
    } else if (GST_IS_PAD (GST_MESSAGE_SRC (msg))) {
      pad = gst_object_ref (GST_PAD (GST_MESSAGE_SRC (msg)));
    }

    if (pad) {
      stream_id = gst_pad_get_stream_id (pad);
      if (stream_id)
        gst_structure_set (details, "stream-id", G_TYPE_STRING, stream_id,
            NULL);
      g_free (stream_id);
      gst_object_unref (pad);
    }
  }
}

static void
error_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GError *err, *play_err;
  gchar *name, *debug, *message, *full_message;
  GstStructure *details = NULL;
  GstPlayError play_error = GST_PLAY_ERROR_FAILED;

  dump_dot_file (self, "error");

  gst_message_parse_error (msg, &err, &debug);
  gst_message_parse_error_details (msg, (const GstStructure **) &details);
  if (details)
    details = gst_structure_copy (details);
  else
    details = gst_structure_new_static_str_empty ("message-details");

  name = gst_object_get_path_string (msg->src);
  message = gst_error_get_message (err->domain, err->code);

  if (debug)
    full_message =
        g_strdup_printf ("Error from element %s: %s\n%s\n%s", name, message,
        err->message, debug);
  else
    full_message =
        g_strdup_printf ("Error from element %s: %s\n%s", name, message,
        err->message);

  GST_ERROR_OBJECT (self, "ERROR: from element %s: %s", name, err->message);
  if (debug != NULL)
    GST_ERROR_OBJECT (self, "Additional debug info: %s", debug);

  gst_play_set_stream_id_details (self, msg, details);
  if (g_error_matches (err, GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN) ||
      g_error_matches (err, GST_STREAM_ERROR,
          GST_STREAM_ERROR_CODEC_NOT_FOUND)) {
    play_error = GST_PLAY_ERROR_MISSING_PLUGIN;
    gst_play_set_missing_plugin_details (self, details);
  }

  play_err = g_error_new_literal (GST_PLAY_ERROR, play_error, full_message);
  on_error (self, play_err, details);

  g_clear_error (&err);
  g_free (debug);
  g_free (name);
  g_free (full_message);
  g_free (message);
}

static void
warning_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GError *err, *play_err;
  gchar *name, *debug, *message, *full_message;
  GstStructure *details = NULL;
  GstPlayError play_error = GST_PLAY_ERROR_FAILED;

  dump_dot_file (self, "warning");

  gst_message_parse_warning (msg, &err, &debug);
  gst_message_parse_warning_details (msg, (const GstStructure **) &details);
  if (details)
    details = gst_structure_copy (details);
  else
    details = gst_structure_new_static_str_empty ("message-details");

  name = gst_object_get_path_string (msg->src);
  message = gst_error_get_message (err->domain, err->code);

  if (debug)
    full_message =
        g_strdup_printf ("Warning from element %s: %s\n%s\n%s", name, message,
        err->message, debug);
  else
    full_message =
        g_strdup_printf ("Warning from element %s: %s\n%s", name, message,
        err->message);

  GST_WARNING_OBJECT (self, "WARNING: from element %s: %s", name, err->message);
  if (debug != NULL)
    GST_WARNING_OBJECT (self, "Additional debug info: %s", debug);

  gst_play_set_stream_id_details (self, msg, details);
  if (g_error_matches (err, GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN) ||
      g_error_matches (err, GST_STREAM_ERROR,
          GST_STREAM_ERROR_CODEC_NOT_FOUND)) {
    play_error = GST_PLAY_ERROR_MISSING_PLUGIN;
    gst_play_set_missing_plugin_details (self, details);
  }

  play_err = g_error_new_literal (GST_PLAY_ERROR, play_error, full_message);

  GST_WARNING_OBJECT (self, "Warning: %s (%s, %d)", err->message,
      g_quark_to_string (err->domain), err->code);

  api_bus_post_message (self, GST_PLAY_MESSAGE_WARNING,
      GST_PLAY_MESSAGE_DATA_WARNING, G_TYPE_ERROR, play_err,
      GST_PLAY_MESSAGE_DATA_WARNING_DETAILS, GST_TYPE_STRUCTURE, details, NULL);

  gst_structure_free (details);
  g_clear_error (&play_err);
  g_clear_error (&err);
  g_free (debug);
  g_free (name);
  g_free (full_message);
  g_free (message);
}

static void
eos_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);

  GST_DEBUG_OBJECT (self, "End of stream");

  tick_cb (self);
  remove_tick_source (self);

  api_bus_post_message (self, GST_PLAY_MESSAGE_END_OF_STREAM, NULL);

  change_state (self, GST_PLAY_STATE_STOPPED);
  self->buffering_percent = 100;
  self->is_eos = TRUE;
}

static void
buffering_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  gint percent;

  if (self->target_state < GST_STATE_PAUSED)
    return;
  if (self->is_live)
    return;

  gst_message_parse_buffering (msg, &percent);
  GST_LOG_OBJECT (self, "Buffering %d%%", percent);

  if (percent < 100 && self->target_state >= GST_STATE_PAUSED) {
    GstStateChangeReturn state_ret;

    GST_DEBUG_OBJECT (self, "Waiting for buffering to finish");
    state_ret = gst_element_set_state (self->playbin, GST_STATE_PAUSED);

    if (state_ret == GST_STATE_CHANGE_FAILURE) {
      on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
              "Failed to handle buffering"), NULL);
      return;
    }

    change_state (self, GST_PLAY_STATE_BUFFERING);
  }

  if (self->buffering_percent != percent) {
    self->buffering_percent = percent;

    api_bus_post_message (self, GST_PLAY_MESSAGE_BUFFERING,
        GST_PLAY_MESSAGE_DATA_BUFFERING_PERCENT, G_TYPE_UINT, percent, NULL);
  }

  g_mutex_lock (&self->lock);
  if (percent == 100 && (self->seek_position != GST_CLOCK_TIME_NONE ||
          self->seek_pending)) {
    g_mutex_unlock (&self->lock);

    GST_DEBUG_OBJECT (self, "Buffering finished - seek pending");
  } else if (percent == 100 && self->target_state >= GST_STATE_PLAYING
      && self->current_state >= GST_STATE_PAUSED) {
    GstStateChangeReturn state_ret;

    g_mutex_unlock (&self->lock);

    GST_DEBUG_OBJECT (self, "Buffering finished - going to PLAYING");
    state_ret = gst_element_set_state (self->playbin, GST_STATE_PLAYING);
    /* Application state change is happening when the state change happened */
    if (state_ret == GST_STATE_CHANGE_FAILURE)
      on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
              "Failed to handle buffering"), NULL);
  } else if (percent == 100 && self->target_state >= GST_STATE_PAUSED) {
    g_mutex_unlock (&self->lock);

    GST_DEBUG_OBJECT (self, "Buffering finished - staying PAUSED");
    change_state (self, GST_PLAY_STATE_PAUSED);
  } else {
    g_mutex_unlock (&self->lock);
  }
}

static void
clock_lost_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstStateChangeReturn state_ret;

  GST_DEBUG_OBJECT (self, "Clock lost");
  if (self->target_state >= GST_STATE_PLAYING) {
    state_ret = gst_element_set_state (self->playbin, GST_STATE_PAUSED);
    if (state_ret != GST_STATE_CHANGE_FAILURE)
      state_ret = gst_element_set_state (self->playbin, GST_STATE_PLAYING);

    if (state_ret == GST_STATE_CHANGE_FAILURE)
      on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
              "Failed to handle clock loss"), NULL);
  }
}


static void
check_video_dimensions_changed (GstPlay * self)
{
  GstElement *video_sink;
  GstPad *video_sink_pad;
  GstCaps *caps;
  GstVideoInfo info;
  guint width = 0, height = 0;

  g_object_get (self->playbin, "video-sink", &video_sink, NULL);
  if (!video_sink)
    goto out;

  video_sink_pad = gst_element_get_static_pad (video_sink, "sink");
  if (!video_sink_pad) {
    gst_object_unref (video_sink);
    goto out;
  }

  caps = gst_pad_get_current_caps (video_sink_pad);

  if (caps) {
    if (gst_video_info_from_caps (&info, caps)) {
      info.width = info.width * info.par_n / info.par_d;

      GST_DEBUG_OBJECT (self, "Video dimensions changed: %dx%d", info.width,
          info.height);
      width = info.width;
      height = info.height;
    }

    gst_caps_unref (caps);
  }
  gst_object_unref (video_sink_pad);
  gst_object_unref (video_sink);

out:
  api_bus_post_message (self, GST_PLAY_MESSAGE_VIDEO_DIMENSIONS_CHANGED,
      GST_PLAY_MESSAGE_DATA_VIDEO_WIDTH, G_TYPE_UINT, width,
      GST_PLAY_MESSAGE_DATA_VIDEO_HEIGHT, G_TYPE_UINT, height, NULL);
}

static void
notify_caps_cb (G_GNUC_UNUSED GObject * object,
    G_GNUC_UNUSED GParamSpec * pspec, gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);

  check_video_dimensions_changed (self);
}

static void
on_duration_changed (GstPlay * self, GstClockTime duration)
{
  gboolean updated = FALSE;

  if (self->cached_duration == duration)
    return;

  GST_DEBUG_OBJECT (self, "Duration changed %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration));

  g_mutex_lock (&self->lock);
  self->cached_duration = duration;
  if (self->media_info) {
    self->media_info->duration = duration;
    updated = TRUE;
  }
  g_mutex_unlock (&self->lock);

  api_bus_post_message (self, GST_PLAY_MESSAGE_DURATION_CHANGED,
      GST_PLAY_MESSAGE_DATA_DURATION, GST_TYPE_CLOCK_TIME,
      gst_play_get_duration (self), NULL);

  if (updated) {
    on_media_info_updated (self);
  }
}

static void
on_seek_done (GstPlay * self)
{
  api_bus_post_message (self, GST_PLAY_MESSAGE_SEEK_DONE,
      GST_PLAY_MESSAGE_DATA_POSITION, GST_TYPE_CLOCK_TIME,
      gst_play_get_position (self), NULL);
}

static void
state_changed_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstState old_state, new_state, pending_state;

  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->playbin)) {
    gchar *transition_name;

    GST_DEBUG_OBJECT (self, "Changed state old: %s new: %s pending: %s",
        gst_element_state_get_name (old_state),
        gst_element_state_get_name (new_state),
        gst_element_state_get_name (pending_state));

    transition_name = g_strdup_printf ("%s_%s",
        gst_element_state_get_name (old_state),
        gst_element_state_get_name (new_state));
    dump_dot_file (self, transition_name);
    g_free (transition_name);

    self->current_state = new_state;

    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED
        && pending_state == GST_STATE_VOID_PENDING) {
      GstElement *video_sink;
      GstPad *video_sink_pad;
      gint64 duration = -1;

      GST_DEBUG_OBJECT (self, "Initial PAUSED - pre-rolled");

      g_mutex_lock (&self->lock);
      if (self->media_info)
        g_object_unref (self->media_info);
      self->media_info = gst_play_media_info_create (self);
      g_mutex_unlock (&self->lock);
      on_media_info_updated (self);

      g_object_get (self->playbin, "video-sink", &video_sink, NULL);

      if (video_sink) {
        video_sink_pad = gst_element_get_static_pad (video_sink, "sink");

        if (video_sink_pad) {
          g_signal_connect (video_sink_pad, "notify::caps",
              (GCallback) notify_caps_cb, self);
          gst_object_unref (video_sink_pad);
        }
        gst_object_unref (video_sink);
      }

      check_video_dimensions_changed (self);
      if (gst_element_query_duration (self->playbin, GST_FORMAT_TIME,
              &duration)) {
        on_duration_changed (self, duration);
      } else {
        self->cached_duration = GST_CLOCK_TIME_NONE;
      }
    }

    if (new_state == GST_STATE_PAUSED
        && pending_state == GST_STATE_VOID_PENDING) {
      remove_tick_source (self);

      g_mutex_lock (&self->lock);
      if (self->seek_pending) {
        self->seek_pending = FALSE;

        if (!self->media_info->seekable) {
          GST_DEBUG_OBJECT (self, "Media is not seekable");
          remove_seek_source (self);
          self->seek_position = GST_CLOCK_TIME_NONE;
          self->last_seek_time = GST_CLOCK_TIME_NONE;
        } else if (self->seek_source) {
          GST_DEBUG_OBJECT (self, "Seek finished but new seek is pending");
          gst_play_seek_internal_locked (self);
        } else {
          GST_DEBUG_OBJECT (self, "Seek finished");
          on_seek_done (self);
        }
      }

      if (self->seek_position != GST_CLOCK_TIME_NONE) {
        if (!self->media_info->seekable) {
          GST_DEBUG_OBJECT (self, "Media is not seekable");
        } else {
          GST_DEBUG_OBJECT (self, "Seeking now that we reached PAUSED state");
          gst_play_seek_internal_locked (self);
        }
        g_mutex_unlock (&self->lock);
      } else if (!self->seek_pending) {
        g_mutex_unlock (&self->lock);

        tick_cb (self);

        if (self->target_state >= GST_STATE_PLAYING
            && self->buffering_percent == 100) {
          GstStateChangeReturn state_ret;

          state_ret = gst_element_set_state (self->playbin, GST_STATE_PLAYING);
          if (state_ret == GST_STATE_CHANGE_FAILURE)
            on_error (self, g_error_new (GST_PLAY_ERROR,
                    GST_PLAY_ERROR_FAILED, "Failed to play"), NULL);
        } else if (self->buffering_percent == 100) {
          change_state (self, GST_PLAY_STATE_PAUSED);
        }
      } else {
        g_mutex_unlock (&self->lock);
      }
    } else if (new_state == GST_STATE_PLAYING
        && pending_state == GST_STATE_VOID_PENDING) {
      /* Try to query duration again if needed */
      if (self->cached_duration == GST_CLOCK_TIME_NONE) {
        gint64 duration = -1;

        if (gst_element_query_duration (self->playbin, GST_FORMAT_TIME,
                &duration)) {
          on_duration_changed (self, duration);
        }
      }

      /* Try to query seek information again for live stream */
      if (self->is_live) {
        gboolean seekable = FALSE;
        gboolean updated = FALSE;

        GstQuery *query = gst_query_new_seeking (GST_FORMAT_TIME);
        if (gst_element_query (self->playbin, query))
          gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
        gst_query_unref (query);

        g_mutex_lock (&self->lock);
        if (self->media_info && seekable != self->media_info->seekable) {
          self->media_info->seekable = seekable;
          updated = TRUE;
        }
        g_mutex_unlock (&self->lock);

        if (updated) {
          on_media_info_updated (self);
        }
      }
      /* api_bus_post_message (self, GST_PLAY_MESSAGE_POSITION_UPDATED, */
      /*     GST_PLAY_MESSAGE_DATA_POSITION, GST_TYPE_CLOCK_TIME, 0, NULL); */

      /* If no seek is currently pending, add the tick source. This can happen
       * if we seeked already but the state-change message was still queued up */
      if (!self->seek_pending) {
        add_tick_source (self);
        change_state (self, GST_PLAY_STATE_PLAYING);
      }
    } else if (new_state == GST_STATE_READY && old_state > GST_STATE_READY) {
      change_state (self, GST_PLAY_STATE_STOPPED);
    } else {
      /* Otherwise we neither reached PLAYING nor PAUSED, so must
       * wait for something to happen... i.e. are BUFFERING now */
      change_state (self, GST_PLAY_STATE_BUFFERING);
    }
  }
}

static void
duration_changed_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  gint64 duration = GST_CLOCK_TIME_NONE;

  if (gst_element_query_duration (self->playbin, GST_FORMAT_TIME, &duration)) {
    on_duration_changed (self, duration);
  }
}

static void
latency_cb (G_GNUC_UNUSED GstBus * bus, G_GNUC_UNUSED GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);

  GST_DEBUG_OBJECT (self, "Latency changed");

  gst_bin_recalculate_latency (GST_BIN (self->playbin));
}

static void
request_state_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstState state;
  GstStateChangeReturn state_ret;

  gst_message_parse_request_state (msg, &state);

  GST_DEBUG_OBJECT (self, "State %s requested",
      gst_element_state_get_name (state));

  self->target_state = state;
  state_ret = gst_element_set_state (self->playbin, state);
  if (state_ret == GST_STATE_CHANGE_FAILURE)
    on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
            "Failed to change to requested state %s",
            gst_element_state_get_name (state)), NULL);
}

static void
media_info_update (GstPlay * self, GstPlayMediaInfo * info)
{
  g_free (info->title);
  info->title = get_from_tags (self, info, get_title);

  g_free (info->container);
  info->container = get_from_tags (self, info, get_container_format);

  if (info->image_sample)
    gst_sample_unref (info->image_sample);
  info->image_sample = get_from_tags (self, info, get_cover_sample);

  GST_DEBUG_OBJECT (self, "title: %s, container: %s "
      "image_sample: %p", info->title, info->container, info->image_sample);
}

static void
tags_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstTagList *tags = NULL;

  gst_message_parse_tag (msg, &tags);

  GST_DEBUG_OBJECT (self, "received %s tags",
      gst_tag_list_get_scope (tags) ==
      GST_TAG_SCOPE_GLOBAL ? "global" : "stream");

  if (gst_tag_list_get_scope (tags) == GST_TAG_SCOPE_GLOBAL) {
    g_mutex_lock (&self->lock);
    if (self->media_info) {
      if (self->media_info->tags)
        gst_tag_list_unref (self->media_info->tags);
      self->media_info->tags = gst_tag_list_ref (tags);
      media_info_update (self, self->media_info);
      g_mutex_unlock (&self->lock);
      on_media_info_updated (self);
    } else {
      if (self->global_tags)
        gst_tag_list_unref (self->global_tags);
      self->global_tags = gst_tag_list_ref (tags);
      g_mutex_unlock (&self->lock);
    }
  }

  gst_tag_list_unref (tags);
}

static void
element_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  const GstStructure *s;

  s = gst_message_get_structure (msg);
  if (gst_structure_has_name (s, "redirect")) {
    const gchar *new_location;

    new_location = gst_structure_get_string (s, "new-location");
    if (!new_location) {
      const GValue *locations_list, *location_val;
      guint i, size;

      locations_list = gst_structure_get_value (s, "locations");
      size = gst_value_list_get_size (locations_list);
      for (i = 0; i < size; ++i) {
        const GstStructure *location_s;

        location_val = gst_value_list_get_value (locations_list, i);
        if (!GST_VALUE_HOLDS_STRUCTURE (location_val))
          continue;

        location_s = (const GstStructure *) g_value_get_boxed (location_val);
        if (!gst_structure_has_name (location_s, "redirect"))
          continue;

        new_location = gst_structure_get_string (location_s, "new-location");
        if (new_location)
          break;
      }
    }

    if (new_location) {
      GstState target_state;

      GST_DEBUG_OBJECT (self, "Redirect to '%s'", new_location);

      /* Remember target state and restore after setting the URI */
      target_state = self->target_state;

      gst_play_stop_internal (self, TRUE);

      g_mutex_lock (&self->lock);
      g_free (self->redirect_uri);
      self->redirect_uri = g_strdup (new_location);
      g_object_set (self->playbin, "uri", self->redirect_uri, NULL);
      g_mutex_unlock (&self->lock);

      if (target_state == GST_STATE_PAUSED)
        gst_play_pause_internal (self);
      else if (target_state == GST_STATE_PLAYING)
        gst_play_play_internal (self);
    }
  } else if (gst_is_missing_plugin_message (msg)) {
    self->missing_plugin_messages =
        g_list_prepend (self->missing_plugin_messages, gst_message_ref (msg));
  }
}

/* Must be called with lock */
static gboolean
update_stream_collection (GstPlay * self, GstStreamCollection * collection)
{
  if (self->collection && self->collection == collection)
    return FALSE;

  if (self->collection && self->stream_notify_id)
    g_signal_handler_disconnect (self->collection, self->stream_notify_id);

  gst_object_replace ((GstObject **) & self->collection,
      (GstObject *) collection);
  if (self->media_info) {
    gst_object_unref (self->media_info);
    self->media_info = gst_play_media_info_create (self);
  }

  self->stream_notify_id =
      g_signal_connect (self->collection, "stream-notify",
      G_CALLBACK (stream_notify_cb), self);

  return self->media_info != NULL;
}

static void
stream_collection_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstStreamCollection *collection = NULL;
  gboolean updated = FALSE;
  gboolean do_default_selection;

  gst_message_parse_stream_collection (msg, &collection);

  if (!collection)
    return;

  g_mutex_lock (&self->lock);
  do_default_selection = self->collection != NULL;
  updated = update_stream_collection (self, collection);
  gst_object_unref (collection);

  if (do_default_selection) {
    gboolean select_audio = self->audio_enabled;
    gboolean select_video = self->video_enabled;
    gboolean select_subtitle = self->subtitle_enabled;
    guint i, len;

    GST_DEBUG_OBJECT (self, "Do initial default selection");
    len = gst_stream_collection_get_size (collection);

    for (i = 0; i < len; i++) {
      GstStream *stream = gst_stream_collection_get_stream (collection, i);
      GstStreamType stream_type = gst_stream_get_stream_type (stream);
      const gchar *stream_id = gst_stream_get_stream_id (stream);

      if ((stream_type & GST_STREAM_TYPE_AUDIO) && select_audio) {
        g_free (self->audio_sid);
        self->audio_sid = g_strdup (stream_id);
        select_audio = FALSE;
      } else if ((stream_type & GST_STREAM_TYPE_VIDEO) && select_video) {
        g_free (self->video_sid);
        self->video_sid = g_strdup (stream_id);
        select_video = FALSE;
      } else if ((stream_type & GST_STREAM_TYPE_TEXT) && select_subtitle) {
        g_free (self->subtitle_sid);
        self->subtitle_sid = g_strdup (stream_id);
        select_subtitle = FALSE;
      }
    }

    gst_play_select_streams (self);
  }

  g_mutex_unlock (&self->lock);

  if (self->media_info && updated)
    on_media_info_updated (self);
}

static void
streams_selected_cb (G_GNUC_UNUSED GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstStreamCollection *collection = NULL;
  gboolean updated = FALSE;
  guint i, len;

  gst_message_parse_streams_selected (msg, &collection);

  if (!collection)
    return;

  g_mutex_lock (&self->lock);
  updated = update_stream_collection (self, collection);
  gst_object_unref (collection);

  g_free (self->video_sid);
  g_free (self->audio_sid);
  g_free (self->subtitle_sid);
  self->video_sid = NULL;
  self->audio_sid = NULL;
  self->subtitle_sid = NULL;

  len = gst_message_streams_selected_get_size (msg);
  for (i = 0; i < len; i++) {
    GstStream *stream;
    GstStreamType stream_type;
    const gchar *stream_id;
    gchar **current_sid;

    stream = gst_message_streams_selected_get_stream (msg, i);
    stream_type = gst_stream_get_stream_type (stream);
    stream_id = gst_stream_get_stream_id (stream);

    if (stream_type & GST_STREAM_TYPE_AUDIO)
      current_sid = &self->audio_sid;
    else if (stream_type & GST_STREAM_TYPE_VIDEO)
      current_sid = &self->video_sid;
    else if (stream_type & GST_STREAM_TYPE_TEXT)
      current_sid = &self->subtitle_sid;
    else {
      GST_WARNING_OBJECT (self,
          "Unknown stream-id %s with type 0x%x", stream_id, stream_type);
      continue;
    }

    if (G_UNLIKELY (*current_sid)) {
      GST_FIXME_OBJECT (self,
          "Multiple streams are selected for type %s, choose the first one",
          gst_stream_type_get_name (stream_type));
      continue;
    }

    *current_sid = g_strdup (stream_id);
  }
  g_mutex_unlock (&self->lock);

  if (self->media_info && updated)
    on_media_info_updated (self);
}

static void
play_set_flag (GstPlay * self, gint pos)
{
  gint flags;

  g_object_get (self->playbin, "flags", &flags, NULL);
  flags |= pos;
  g_object_set (self->playbin, "flags", flags, NULL);

  GST_DEBUG_OBJECT (self, "setting flags=%#x", flags);
}

static void
play_clear_flag (GstPlay * self, gint pos)
{
  gint flags;

  g_object_get (self->playbin, "flags", &flags, NULL);
  flags &= ~pos;
  g_object_set (self->playbin, "flags", flags, NULL);

  GST_DEBUG_OBJECT (self, "setting flags=%#x", flags);
}

/*
 * on_media_info_updated:
 *
 * create a new copy of self->media_info object and post it to the user
 * application.
 */
static void
on_media_info_updated (GstPlay * self)
{
  GstPlayMediaInfo *media_info_copy;

  g_mutex_lock (&self->lock);
  media_info_copy = gst_play_media_info_copy (self->media_info);
  g_mutex_unlock (&self->lock);

  api_bus_post_message (self, GST_PLAY_MESSAGE_MEDIA_INFO_UPDATED,
      GST_PLAY_MESSAGE_DATA_MEDIA_INFO, GST_TYPE_PLAY_MEDIA_INFO,
      media_info_copy, NULL);
  g_object_unref (media_info_copy);
}

static void
gst_play_subtitle_info_update (GstPlay * self, GstPlayStreamInfo * stream_info)
{
  GstPlaySubtitleInfo *info = (GstPlaySubtitleInfo *) stream_info;

  if (stream_info->tags) {

    /* free the old language info */
    g_free (info->language);
    info->language = NULL;

    /* First try to get the language full name from tag, if name is not
     * available then try language code. If we find the language code
     * then use gstreamer api to translate code to full name.
     */
    gst_tag_list_get_string (stream_info->tags, GST_TAG_LANGUAGE_NAME,
        &info->language);
    if (!info->language) {
      gchar *lang_code = NULL;

      gst_tag_list_get_string (stream_info->tags, GST_TAG_LANGUAGE_CODE,
          &lang_code);
      if (lang_code) {
        info->language = g_strdup (gst_tag_get_language_name (lang_code));
        g_free (lang_code);
      }
    }

    /* If we are still failed to find language name then check if external
     * subtitle is loaded and compare the stream index between current sub
     * stream index with our stream index and if matches then declare it as
     * external subtitle and use the filename.
     */
    if (!info->language) {
      gchar *suburi = NULL;

      g_object_get (G_OBJECT (self->playbin), "current-suburi", &suburi, NULL);
      if (suburi) {
        if (self->subtitle_sid &&
            g_str_equal (self->subtitle_sid, stream_info->stream_id)) {
          info->language = g_path_get_basename (suburi);
        }
        g_free (suburi);
      }
    }

  } else {
    g_free (info->language);
    info->language = NULL;
  }

  GST_DEBUG_OBJECT (self, "language=%s", info->language);
}

static void
gst_play_video_info_update (GstPlay * self, GstPlayStreamInfo * stream_info)
{
  GstPlayVideoInfo *info = (GstPlayVideoInfo *) stream_info;

  if (stream_info->caps) {
    GstStructure *s;

    s = gst_caps_get_structure (stream_info->caps, 0);
    if (s) {
      gint width, height;
      gint fps_n, fps_d;
      gint par_n, par_d;

      if (gst_structure_get_int (s, "width", &width))
        info->width = width;
      else
        info->width = -1;

      if (gst_structure_get_int (s, "height", &height))
        info->height = height;
      else
        info->height = -1;

      if (gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d)) {
        info->framerate_num = fps_n;
        info->framerate_denom = fps_d;
      } else {
        info->framerate_num = 0;
        info->framerate_denom = 1;
      }


      if (gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d)) {
        info->par_num = par_n;
        info->par_denom = par_d;
      } else {
        info->par_num = 1;
        info->par_denom = 1;
      }
    }
  } else {
    info->width = info->height = -1;
    info->par_num = info->par_denom = 1;
    info->framerate_num = 0;
    info->framerate_denom = 1;
  }

  if (stream_info->tags) {
    guint bitrate, max_bitrate;

    if (gst_tag_list_get_uint (stream_info->tags, GST_TAG_BITRATE, &bitrate))
      info->bitrate = bitrate;
    else
      info->bitrate = -1;

    if (gst_tag_list_get_uint (stream_info->tags, GST_TAG_MAXIMUM_BITRATE,
            &max_bitrate) || gst_tag_list_get_uint (stream_info->tags,
            GST_TAG_NOMINAL_BITRATE, &max_bitrate))
      info->max_bitrate = max_bitrate;
    else
      info->max_bitrate = -1;
  } else {
    info->bitrate = info->max_bitrate = -1;
  }

  GST_DEBUG_OBJECT (self, "width=%d height=%d fps=%.2f par=%d:%d "
      "bitrate=%d max_bitrate=%d", info->width, info->height,
      (gdouble) info->framerate_num / info->framerate_denom,
      info->par_num, info->par_denom, info->bitrate, info->max_bitrate);
}

static void
gst_play_audio_info_update (GstPlay * self, GstPlayStreamInfo * stream_info)
{
  GstPlayAudioInfo *info = (GstPlayAudioInfo *) stream_info;

  if (stream_info->caps) {
    GstStructure *s;

    s = gst_caps_get_structure (stream_info->caps, 0);
    if (s) {
      gint rate, channels;

      if (gst_structure_get_int (s, "rate", &rate))
        info->sample_rate = rate;
      else
        info->sample_rate = -1;

      if (gst_structure_get_int (s, "channels", &channels))
        info->channels = channels;
      else
        info->channels = 0;
    }
  } else {
    info->sample_rate = -1;
    info->channels = 0;
  }

  if (stream_info->tags) {
    guint bitrate, max_bitrate;

    if (gst_tag_list_get_uint (stream_info->tags, GST_TAG_BITRATE, &bitrate))
      info->bitrate = bitrate;
    else
      info->bitrate = -1;

    if (gst_tag_list_get_uint (stream_info->tags, GST_TAG_MAXIMUM_BITRATE,
            &max_bitrate) || gst_tag_list_get_uint (stream_info->tags,
            GST_TAG_NOMINAL_BITRATE, &max_bitrate))
      info->max_bitrate = max_bitrate;
    else
      info->max_bitrate = -1;

    /* if we have old language the free it */
    g_free (info->language);
    info->language = NULL;

    /* First try to get the language full name from tag, if name is not
     * available then try language code. If we find the language code
     * then use gstreamer api to translate code to full name.
     */
    gst_tag_list_get_string (stream_info->tags, GST_TAG_LANGUAGE_NAME,
        &info->language);
    if (!info->language) {
      gchar *lang_code = NULL;

      gst_tag_list_get_string (stream_info->tags, GST_TAG_LANGUAGE_CODE,
          &lang_code);
      if (lang_code) {
        info->language = g_strdup (gst_tag_get_language_name (lang_code));
        g_free (lang_code);
      }
    }
  } else {
    g_free (info->language);
    info->language = NULL;
    info->max_bitrate = info->bitrate = -1;
  }

  GST_DEBUG_OBJECT (self, "language=%s rate=%d channels=%d bitrate=%d "
      "max_bitrate=%d", info->language, info->sample_rate, info->channels,
      info->bitrate, info->max_bitrate);
}

static GstPlayStreamInfo *
gst_play_stream_info_find (GstPlayMediaInfo * media_info,
    GType type, gint stream_index)
{
  GList *list, *l;
  GstPlayStreamInfo *info = NULL;

  if (!media_info)
    return NULL;

  list = gst_play_media_info_get_stream_list (media_info);
  for (l = list; l != NULL; l = l->next) {
    info = (GstPlayStreamInfo *) l->data;
    if ((G_OBJECT_TYPE (info) == type) && (info->stream_index == stream_index)) {
      return g_object_ref (info);
    }
  }

  return NULL;
}

static GstPlayStreamInfo *
gst_play_stream_info_find_from_stream_id (GstPlayMediaInfo * media_info,
    const gchar * stream_id)
{
  GList *list, *l;
  GstPlayStreamInfo *info = NULL;

  if (!media_info)
    return NULL;

  list = gst_play_media_info_get_stream_list (media_info);
  for (l = list; l != NULL; l = l->next) {
    info = (GstPlayStreamInfo *) l->data;
    if (g_str_equal (info->stream_id, stream_id)) {
      return info;
    }
  }

  return NULL;
}

static gboolean
is_track_enabled (GstPlay * self, gint pos)
{
  gint flags;

  g_object_get (G_OBJECT (self->playbin), "flags", &flags, NULL);

  if ((flags & pos))
    return TRUE;

  return FALSE;
}

static GstPlayStreamInfo *
gst_play_stream_info_get_current_from_stream_id (GstPlay * self,
    const gchar * stream_id, GType type)
{
  GstPlayStreamInfo *info;

  if (!self->media_info || !stream_id)
    return NULL;

  g_mutex_lock (&self->lock);
  info = gst_play_stream_info_find_from_stream_id (self->media_info, stream_id);
  if (info && G_OBJECT_TYPE (info) == type)
    info = gst_play_stream_info_copy (info);
  else
    info = NULL;
  g_mutex_unlock (&self->lock);

  return info;
}

static void
stream_notify_cb (GstStreamCollection * collection, GstStream * stream,
    GParamSpec * pspec, GstPlay * self)
{
  GstPlayStreamInfo *info;
  const gchar *stream_id;
  gboolean emit_signal = FALSE;

  if (!self->media_info)
    return;

  if (G_PARAM_SPEC_VALUE_TYPE (pspec) != GST_TYPE_CAPS &&
      G_PARAM_SPEC_VALUE_TYPE (pspec) != GST_TYPE_TAG_LIST)
    return;

  stream_id = gst_stream_get_stream_id (stream);
  g_mutex_lock (&self->lock);
  info = gst_play_stream_info_find_from_stream_id (self->media_info, stream_id);
  if (info) {
    gst_play_stream_info_update_from_stream (self, info, stream);
    emit_signal = TRUE;
  }
  g_mutex_unlock (&self->lock);

  if (emit_signal)
    on_media_info_updated (self);
}

static void
gst_play_stream_info_update (GstPlay * self, GstPlayStreamInfo * s)
{
  if (GST_IS_PLAY_VIDEO_INFO (s))
    gst_play_video_info_update (self, s);
  else if (GST_IS_PLAY_AUDIO_INFO (s))
    gst_play_audio_info_update (self, s);
  else
    gst_play_subtitle_info_update (self, s);
}

static gchar *
stream_info_get_codec (GstPlayStreamInfo * s)
{
  const gchar *type;
  GstTagList *tags;
  gchar *codec = NULL;

  if (GST_IS_PLAY_VIDEO_INFO (s))
    type = GST_TAG_VIDEO_CODEC;
  else if (GST_IS_PLAY_AUDIO_INFO (s))
    type = GST_TAG_AUDIO_CODEC;
  else
    type = GST_TAG_SUBTITLE_CODEC;

  tags = gst_play_stream_info_get_tags (s);
  if (tags) {
    gst_tag_list_get_string (tags, type, &codec);
    if (!codec)
      gst_tag_list_get_string (tags, GST_TAG_CODEC, &codec);
  }

  if (!codec) {
    GstCaps *caps;
    caps = gst_play_stream_info_get_caps (s);
    if (caps) {
      codec = gst_pb_utils_get_codec_description (caps);
    }
  }

  return codec;
}

static void
gst_play_stream_info_update_from_stream (GstPlay * self,
    GstPlayStreamInfo * s, GstStream * stream)
{
  if (s->tags)
    gst_tag_list_unref (s->tags);
  s->tags = gst_stream_get_tags (stream);

  if (s->caps)
    gst_caps_unref (s->caps);
  s->caps = gst_stream_get_caps (stream);

  g_free (s->codec);
  s->codec = stream_info_get_codec (s);

  GST_DEBUG_OBJECT (self, "%s stream id: %s tags: %p caps: %p",
      gst_play_stream_info_get_stream_type (s), s->stream_id, s->tags, s->caps);

  gst_play_stream_info_update (self, s);
}

static void
gst_play_streams_info_create_from_collection (GstPlay * self,
    GstPlayMediaInfo * media_info, GstStreamCollection * collection)
{
  guint i;
  guint total;
  GstPlayStreamInfo *s;
  guint n_audio = 0;
  guint n_video = 0;
  guint n_text = 0;

  if (!media_info || !collection)
    return;

  total = gst_stream_collection_get_size (collection);

  for (i = 0; i < total; i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    GstStreamType stream_type = gst_stream_get_stream_type (stream);
    const gchar *stream_id = gst_stream_get_stream_id (stream);

    if (stream_type & GST_STREAM_TYPE_AUDIO) {
      s = gst_play_stream_info_new (n_audio, stream_id,
          GST_TYPE_PLAY_AUDIO_INFO);
      n_audio++;
    } else if (stream_type & GST_STREAM_TYPE_VIDEO) {
      s = gst_play_stream_info_new (n_video, stream_id,
          GST_TYPE_PLAY_VIDEO_INFO);
      n_video++;
    } else if (stream_type & GST_STREAM_TYPE_TEXT) {
      s = gst_play_stream_info_new (n_text, stream_id,
          GST_TYPE_PLAY_SUBTITLE_INFO);
      n_text++;
    } else {
      GST_DEBUG_OBJECT (self, "Unknown type stream %d", i);
      continue;
    }

    /* add the object in stream list */
    media_info->stream_list = g_list_append (media_info->stream_list, s);

    /* based on type, add the object in its corresponding stream_ list */
    if (GST_IS_PLAY_AUDIO_INFO (s))
      media_info->audio_stream_list = g_list_append
          (media_info->audio_stream_list, s);
    else if (GST_IS_PLAY_VIDEO_INFO (s))
      media_info->video_stream_list = g_list_append
          (media_info->video_stream_list, s);
    else
      media_info->subtitle_stream_list = g_list_append
          (media_info->subtitle_stream_list, s);

    GST_DEBUG_OBJECT (self, "create %s stream id %s",
        gst_play_stream_info_get_stream_type (s), s->stream_id);

    gst_play_stream_info_update_from_stream (self, s, stream);
  }
}

static void *
get_title (GstTagList * tags)
{
  gchar *title = NULL;

  gst_tag_list_get_string (tags, GST_TAG_TITLE, &title);
  if (!title)
    gst_tag_list_get_string (tags, GST_TAG_TITLE_SORTNAME, &title);

  return title;
}

static void *
get_container_format (GstTagList * tags)
{
  gchar *container = NULL;

  gst_tag_list_get_string (tags, GST_TAG_CONTAINER_FORMAT, &container);

  /* TODO: If container is not available then maybe consider
   * parsing caps or file extension to guess the container format.
   */

  return container;
}

static void *
get_from_tags (GstPlay * self, GstPlayMediaInfo * media_info,
    void *(*func) (GstTagList *))
{
  GList *l;
  void *ret = NULL;

  if (media_info->tags) {
    ret = func (media_info->tags);
    if (ret)
      return ret;
  }

  /* if global tag does not exit then try video and audio streams */
  GST_DEBUG_OBJECT (self, "trying video tags");
  for (l = gst_play_media_info_get_video_streams (media_info); l != NULL;
      l = l->next) {
    GstTagList *tags;

    tags = gst_play_stream_info_get_tags ((GstPlayStreamInfo *) l->data);
    if (tags)
      ret = func (tags);

    if (ret)
      return ret;
  }

  GST_DEBUG_OBJECT (self, "trying audio tags");
  for (l = gst_play_media_info_get_audio_streams (media_info); l != NULL;
      l = l->next) {
    GstTagList *tags;

    tags = gst_play_stream_info_get_tags ((GstPlayStreamInfo *) l->data);
    if (tags)
      ret = func (tags);

    if (ret)
      return ret;
  }

  GST_DEBUG_OBJECT (self, "failed to get the information from tags");
  return NULL;
}

static void *
get_cover_sample (GstTagList * tags)
{
  GstSample *cover_sample = NULL;

  gst_tag_list_get_sample (tags, GST_TAG_IMAGE, &cover_sample);
  if (!cover_sample)
    gst_tag_list_get_sample (tags, GST_TAG_PREVIEW_IMAGE, &cover_sample);

  return cover_sample;
}

static GstPlayMediaInfo *
gst_play_media_info_create (GstPlay * self)
{
  GstPlayMediaInfo *media_info;
  GstQuery *query;

  GST_DEBUG_OBJECT (self, "begin");
  media_info = gst_play_media_info_new (self->uri);
  media_info->duration = gst_play_get_duration (self);
  media_info->tags = self->global_tags;
  media_info->is_live = self->is_live;
  self->global_tags = NULL;

  query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (gst_element_query (self->playbin, query))
    gst_query_parse_seeking (query, NULL, &media_info->seekable, NULL, NULL);
  gst_query_unref (query);

  gst_play_streams_info_create_from_collection (self, media_info,
      self->collection);

  media_info->title = get_from_tags (self, media_info, get_title);
  media_info->container =
      get_from_tags (self, media_info, get_container_format);
  media_info->image_sample = get_from_tags (self, media_info, get_cover_sample);

  GST_DEBUG_OBJECT (self, "uri: %s title: %s duration: %" GST_TIME_FORMAT
      " seekable: %s live: %s container: %s image_sample %p",
      media_info->uri, media_info->title, GST_TIME_ARGS (media_info->duration),
      media_info->seekable ? "yes" : "no", media_info->is_live ? "yes" : "no",
      media_info->container, media_info->image_sample);

  GST_DEBUG_OBJECT (self, "end");
  return media_info;
}

static void
volume_notify_cb (G_GNUC_UNUSED GObject * obj, G_GNUC_UNUSED GParamSpec * pspec,
    GstPlay * self)
{
  api_bus_post_message (self, GST_PLAY_MESSAGE_VOLUME_CHANGED,
      GST_PLAY_MESSAGE_DATA_VOLUME, G_TYPE_DOUBLE,
      gst_play_get_volume (self), NULL);
}

static void
mute_notify_cb (G_GNUC_UNUSED GObject * obj, G_GNUC_UNUSED GParamSpec * pspec,
    GstPlay * self)
{

  api_bus_post_message (self, GST_PLAY_MESSAGE_MUTE_CHANGED,
      GST_PLAY_MESSAGE_DATA_IS_MUTED, G_TYPE_BOOLEAN,
      gst_play_get_mute (self), NULL);
}

static void
source_setup_cb (GstElement * playbin, GstElement * source, GstPlay * self)
{
  gchar *user_agent;

  user_agent = gst_play_config_get_user_agent (self->config);
  if (user_agent) {
    GParamSpec *prop;

    prop = g_object_class_find_property (G_OBJECT_GET_CLASS (source),
        "user-agent");
    if (prop && prop->value_type == G_TYPE_STRING) {
      GST_INFO_OBJECT (self, "Setting source user-agent: %s", user_agent);
      g_object_set (source, "user-agent", user_agent, NULL);
    }

    g_free (user_agent);
  }
}

static gpointer
gst_play_main (gpointer data)
{
  GstPlay *self = GST_PLAY (data);
  GstBus *bus;
  GSource *source;
  GstElement *scaletempo;

  GST_TRACE_OBJECT (self, "Starting main thread");

  g_main_context_push_thread_default (self->context);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) main_loop_running_cb, self,
      NULL);
  g_source_attach (source, self->context);
  g_source_unref (source);

  self->playbin = gst_element_factory_make ("playbin3", "playbin3");
  if (!self->playbin) {
    g_error ("GstPlay: 'playbin3' element not found, please check your setup");
    g_assert_not_reached ();
  }

  gst_object_ref_sink (self->playbin);

  if (self->video_renderer) {
    gst_play_set_playbin_video_sink (self);
  }

  scaletempo = gst_element_factory_make ("scaletempo", NULL);
  if (scaletempo) {
    g_object_set (self->playbin, "audio-filter", scaletempo, NULL);
  } else {
    g_warning ("GstPlay: scaletempo element not available. Audio pitch "
        "will not be preserved during trick modes");
  }

  self->bus = bus = gst_element_get_bus (self->playbin);
  gst_object_set_name (GST_OBJECT (self->bus), "playbin_bus");
  gst_bus_add_signal_watch (bus);
  gst_bus_enable_sync_message_emission (bus);

  g_signal_connect (G_OBJECT (bus), "message::error", G_CALLBACK (error_cb),
      self);
  g_signal_connect (G_OBJECT (bus), "message::warning", G_CALLBACK (warning_cb),
      self);
  g_signal_connect (G_OBJECT (bus), "message::eos", G_CALLBACK (eos_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      G_CALLBACK (state_changed_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::buffering",
      G_CALLBACK (buffering_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost",
      G_CALLBACK (clock_lost_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::duration-changed",
      G_CALLBACK (duration_changed_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::latency",
      G_CALLBACK (latency_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::request-state",
      G_CALLBACK (request_state_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::element",
      G_CALLBACK (element_cb), self);
  g_signal_connect (G_OBJECT (bus), "message::tag", G_CALLBACK (tags_cb), self);
  g_signal_connect (G_OBJECT (bus), "sync-message::stream-collection",
      G_CALLBACK (stream_collection_cb), self);
  g_signal_connect (G_OBJECT (bus), "sync-message::streams-selected",
      G_CALLBACK (streams_selected_cb), self);
  g_signal_connect (self->playbin, "notify::volume",
      G_CALLBACK (volume_notify_cb), self);
  g_signal_connect (self->playbin, "notify::mute",
      G_CALLBACK (mute_notify_cb), self);
  g_signal_connect (self->playbin, "source-setup",
      G_CALLBACK (source_setup_cb), self);

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_NULL;
  change_state (self, GST_PLAY_STATE_STOPPED);
  self->buffering_percent = 100;
  self->is_eos = FALSE;
  self->is_live = FALSE;
  self->rate = 1.0;

  GST_TRACE_OBJECT (self, "Starting main loop");
  g_main_loop_run (self->loop);
  GST_TRACE_OBJECT (self, "Stopped main loop");

  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);

  remove_tick_source (self);
  remove_ready_timeout_source (self);

  g_list_free_full (self->missing_plugin_messages,
      (GDestroyNotify) gst_message_unref);
  self->missing_plugin_messages = NULL;

  g_mutex_lock (&self->lock);
  if (self->media_info) {
    g_object_unref (self->media_info);
    self->media_info = NULL;
  }

  remove_seek_source (self);
  g_mutex_unlock (&self->lock);

  g_main_context_pop_thread_default (self->context);

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_NULL;
  if (self->playbin) {
    gst_element_set_state (self->playbin, GST_STATE_NULL);
    gst_object_unref (self->playbin);
    self->playbin = NULL;
  }

  GST_TRACE_OBJECT (self, "Stopped main thread");

  return NULL;
}

static gpointer
gst_play_init_once (G_GNUC_UNUSED gpointer user_data)
{
  gst_init (NULL, NULL);

  return NULL;
}

/**
 * gst_play_new:
 * @video_renderer: (transfer full) (allow-none): GstPlayVideoRenderer to use
 *
 * Creates a new #GstPlay instance.
 *
 * Video is going to be rendered by @video_renderer, or if %NULL is provided
 * no special video set up will be done and some default handling will be
 * performed.
 *
 * This also initializes GStreamer via `gst_init()` on the first call if this
 * didn't happen before.
 *
 * Returns: (transfer full): a new #GstPlay instance
 * Since: 1.20
 */
GstPlay *
gst_play_new (GstPlayVideoRenderer * video_renderer)
{
  static GOnce once = G_ONCE_INIT;
  GstPlay *self;

  g_once (&once, gst_play_init_once, NULL);

  self = g_object_new (GST_TYPE_PLAY, "video-renderer", video_renderer, NULL);

  gst_object_ref_sink (self);

  if (video_renderer)
    g_object_unref (video_renderer);

  return self;
}

/**
 * gst_play_get_message_bus:
 * @play: #GstPlay instance
 *
 * GstPlay API exposes a #GstBus instance which purpose is to provide data
 * structures representing play-internal events in form of #GstMessage<!-- -->s of
 * type GST_MESSAGE_APPLICATION.
 *
 * Each message carries a "play-message" field of type #GstPlayMessage.
 * Further fields of the message data are specific to each possible value of
 * that enumeration.
 *
 * Applications can consume the messages asynchronously within their own
 * event-loop / UI-thread etc. Note that in case the application does not
 * consume the messages, the bus will accumulate these internally and eventually
 * fill memory. To avoid that, the bus has to be set "flushing".
 *
 * Returns: (transfer full): The play message bus instance
 *
 * Since: 1.20
 */
GstBus *
gst_play_get_message_bus (GstPlay * self)
{
  return g_object_ref (self->api_bus);
}

static gboolean
gst_play_play_internal (gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstStateChangeReturn state_ret;

  GST_DEBUG_OBJECT (self, "Play");

  g_mutex_lock (&self->lock);
  if (!self->uri) {
    g_mutex_unlock (&self->lock);
    return G_SOURCE_REMOVE;
  }
  g_mutex_unlock (&self->lock);

  remove_ready_timeout_source (self);
  self->target_state = GST_STATE_PLAYING;

  if (self->current_state < GST_STATE_PAUSED)
    change_state (self, GST_PLAY_STATE_BUFFERING);

  if (self->current_state >= GST_STATE_PAUSED && !self->is_eos
      && self->buffering_percent >= 100
      && !(self->seek_position != GST_CLOCK_TIME_NONE || self->seek_pending)) {
    state_ret = gst_element_set_state (self->playbin, GST_STATE_PLAYING);
  } else {
    state_ret = gst_element_set_state (self->playbin, GST_STATE_PAUSED);
  }

  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
            "Failed to play"), NULL);
    return G_SOURCE_REMOVE;
  } else if (state_ret == GST_STATE_CHANGE_NO_PREROLL) {
    self->is_live = TRUE;
    GST_DEBUG_OBJECT (self, "Pipeline is live");
  }

  if (self->is_eos) {
    gboolean ret;

    GST_DEBUG_OBJECT (self, "Was EOS, seeking to beginning");
    self->is_eos = FALSE;
    ret =
        gst_element_seek_simple (self->playbin, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH, 0);
    if (!ret) {
      GST_ERROR_OBJECT (self, "Seek to beginning failed");
      gst_play_stop_internal (self, TRUE);
      gst_play_play_internal (self);
    }
  }

  return G_SOURCE_REMOVE;
}

/**
 * gst_play_play:
 * @play: #GstPlay instance
 *
 * Request to play the loaded stream.
 * Since: 1.20
 */
void
gst_play_play (GstPlay * self)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT,
      gst_play_play_internal, self, NULL);
}

static gboolean
gst_play_pause_internal (gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);
  GstStateChangeReturn state_ret;

  GST_DEBUG_OBJECT (self, "Pause");

  g_mutex_lock (&self->lock);
  if (!self->uri) {
    g_mutex_unlock (&self->lock);
    return G_SOURCE_REMOVE;
  }
  g_mutex_unlock (&self->lock);

  tick_cb (self);
  remove_tick_source (self);
  remove_ready_timeout_source (self);

  self->target_state = GST_STATE_PAUSED;

  if (self->current_state < GST_STATE_PAUSED)
    change_state (self, GST_PLAY_STATE_BUFFERING);

  state_ret = gst_element_set_state (self->playbin, GST_STATE_PAUSED);
  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
            "Failed to pause"), NULL);
    return G_SOURCE_REMOVE;
  } else if (state_ret == GST_STATE_CHANGE_NO_PREROLL) {
    self->is_live = TRUE;
    GST_DEBUG_OBJECT (self, "Pipeline is live");
  }

  if (self->is_eos) {
    gboolean ret;

    GST_DEBUG_OBJECT (self, "Was EOS, seeking to beginning");
    self->is_eos = FALSE;
    ret =
        gst_element_seek_simple (self->playbin, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH, 0);
    if (!ret) {
      GST_ERROR_OBJECT (self, "Seek to beginning failed");
      gst_play_stop_internal (self, TRUE);
      gst_play_pause_internal (self);
    }
  }

  return G_SOURCE_REMOVE;
}

/**
 * gst_play_pause:
 * @play: #GstPlay instance
 *
 * Pauses the current stream.
 * Since: 1.20
 */
void
gst_play_pause (GstPlay * self)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT,
      gst_play_pause_internal, self, NULL);
}

static void
gst_play_stop_internal (GstPlay * self, gboolean transient)
{
  /* directly return if we're already stopped */
  if (self->current_state <= GST_STATE_READY &&
      self->target_state <= GST_STATE_READY)
    return;

  GST_DEBUG_OBJECT (self, "Stop (transient %d)", transient);

  tick_cb (self);
  remove_tick_source (self);

  g_list_free_full (self->missing_plugin_messages,
      (GDestroyNotify) gst_message_unref);
  self->missing_plugin_messages = NULL;

  add_ready_timeout_source (self);

  self->target_state = GST_STATE_NULL;
  self->current_state = GST_STATE_READY;
  self->is_live = FALSE;
  self->is_eos = FALSE;
  gst_bus_set_flushing (self->bus, TRUE);
  gst_element_set_state (self->playbin, GST_STATE_READY);
  gst_bus_set_flushing (self->bus, FALSE);
  change_state (self, transient
      && self->app_state !=
      GST_PLAY_STATE_STOPPED ? GST_PLAY_STATE_BUFFERING :
      GST_PLAY_STATE_STOPPED);
  self->buffering_percent = 100;
  self->cached_duration = GST_CLOCK_TIME_NONE;
  g_mutex_lock (&self->lock);
  if (self->media_info) {
    g_object_unref (self->media_info);
    self->media_info = NULL;
  }
  if (self->global_tags) {
    gst_tag_list_unref (self->global_tags);
    self->global_tags = NULL;
  }
  self->seek_pending = FALSE;
  remove_seek_source (self);
  self->seek_position = GST_CLOCK_TIME_NONE;
  self->last_seek_time = GST_CLOCK_TIME_NONE;
  self->rate = 1.0;
  if (self->collection) {
    if (self->stream_notify_id)
      g_signal_handler_disconnect (self->collection, self->stream_notify_id);
    self->stream_notify_id = 0;
    gst_object_unref (self->collection);
    self->collection = NULL;
  }
  g_free (self->video_sid);
  g_free (self->audio_sid);
  g_free (self->subtitle_sid);
  self->video_sid = NULL;
  self->audio_sid = NULL;
  self->subtitle_sid = NULL;
  g_mutex_unlock (&self->lock);
}

static gboolean
gst_play_stop_internal_dispatch (gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);

  gst_play_stop_internal (self, FALSE);

  return G_SOURCE_REMOVE;
}


/**
 * gst_play_stop:
 * @play: #GstPlay instance
 *
 * Stops playing the current stream and resets to the first position
 * in the stream.
 * Since: 1.20
 */
void
gst_play_stop (GstPlay * self)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_main_context_invoke_full (self->context, G_PRIORITY_DEFAULT,
      gst_play_stop_internal_dispatch, self, NULL);
}

/* Must be called with lock from main context, releases lock! */
static void
gst_play_seek_internal_locked (GstPlay * self)
{
  gboolean ret;
  GstClockTime position;
  gdouble rate;
  GstStateChangeReturn state_ret;
  GstEvent *s_event;
  GstSeekFlags flags = 0;
  gboolean accurate = FALSE;

  remove_seek_source (self);

  /* Only seek in PAUSED */
  if (self->current_state < GST_STATE_PAUSED) {
    return;
  } else if (self->current_state != GST_STATE_PAUSED) {
    g_mutex_unlock (&self->lock);
    state_ret = gst_element_set_state (self->playbin, GST_STATE_PAUSED);
    if (state_ret == GST_STATE_CHANGE_FAILURE) {
      on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
              "Failed to seek"), NULL);
      g_mutex_lock (&self->lock);
      return;
    }
    g_mutex_lock (&self->lock);
    return;
  }

  self->last_seek_time = gst_util_get_timestamp ();
  position = self->seek_position;
  self->seek_position = GST_CLOCK_TIME_NONE;
  self->seek_pending = TRUE;
  rate = self->rate;
  g_mutex_unlock (&self->lock);

  remove_tick_source (self);
  self->is_eos = FALSE;

  flags |= GST_SEEK_FLAG_FLUSH;

  accurate = gst_play_config_get_seek_accurate (self->config);

  if (accurate) {
    flags |= GST_SEEK_FLAG_ACCURATE;
  } else {
    flags &= ~GST_SEEK_FLAG_ACCURATE;
  }

  if (rate != 1.0) {
    flags |= GST_SEEK_FLAG_TRICKMODE;
  }

  if (rate >= 0.0) {
    s_event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
        GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
  } else {
    s_event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags,
        GST_SEEK_TYPE_SET, G_GINT64_CONSTANT (0), GST_SEEK_TYPE_SET, position);
  }

  GST_DEBUG_OBJECT (self, "Seek with rate %.2lf to %" GST_TIME_FORMAT,
      rate, GST_TIME_ARGS (position));

  ret = gst_element_send_event (self->playbin, s_event);
  if (!ret)
    on_error (self, g_error_new (GST_PLAY_ERROR, GST_PLAY_ERROR_FAILED,
            "Failed to seek to %" GST_TIME_FORMAT, GST_TIME_ARGS (position)),
        NULL);

  g_mutex_lock (&self->lock);
}

static gboolean
gst_play_seek_internal (gpointer user_data)
{
  GstPlay *self = GST_PLAY (user_data);

  g_mutex_lock (&self->lock);
  gst_play_seek_internal_locked (self);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

/**
 * gst_play_set_rate:
 * @play: #GstPlay instance
 * @rate: playback rate
 *
 * Playback at specified rate
 * Since: 1.20
 */
void
gst_play_set_rate (GstPlay * self, gdouble rate)
{
  g_return_if_fail (GST_IS_PLAY (self));
  g_return_if_fail (rate != 0.0);

  g_object_set (self, "rate", rate, NULL);
}

/**
 * gst_play_get_rate:
 * @play: #GstPlay instance
 *
 * Returns: current playback rate
 * Since: 1.20
 */
gdouble
gst_play_get_rate (GstPlay * self)
{
  gdouble val;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_RATE);

  g_object_get (self, "rate", &val, NULL);

  return val;
}

/**
 * gst_play_seek:
 * @play: #GstPlay instance
 * @position: position to seek in nanoseconds
 *
 * Seeks the currently-playing stream to the absolute @position time
 * in nanoseconds.
 * Since: 1.20
 */
void
gst_play_seek (GstPlay * self, GstClockTime position)
{
  g_return_if_fail (GST_IS_PLAY (self));
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (position));

  g_mutex_lock (&self->lock);
  if (self->media_info && !self->media_info->seekable) {
    GST_DEBUG_OBJECT (self, "Media is not seekable");
    g_mutex_unlock (&self->lock);
    return;
  }

  self->seek_position = position;

  /* If there is no seek being dispatch to the main context currently do that,
   * otherwise we just updated the seek position so that it will be taken by
   * the seek handler from the main context instead of the old one.
   */
  if (!self->seek_source) {
    GstClockTime now = gst_util_get_timestamp ();

    /* If no seek is pending or it was started more than 250 mseconds ago seek
     * immediately, otherwise wait until the 250 mseconds have passed */
    if (!self->seek_pending || (now - self->last_seek_time > 250 * GST_MSECOND)) {
      self->seek_source = g_idle_source_new ();
      g_source_set_callback (self->seek_source,
          (GSourceFunc) gst_play_seek_internal, self, NULL);
      GST_TRACE_OBJECT (self, "Dispatching seek to position %" GST_TIME_FORMAT,
          GST_TIME_ARGS (position));
      g_source_attach (self->seek_source, self->context);
    } else {
      guint delay = 250000 - (now - self->last_seek_time) / 1000;

      /* Note that last_seek_time must be set to something at this point and
       * it must be smaller than 250 mseconds */
      self->seek_source = g_timeout_source_new (delay);
      g_source_set_callback (self->seek_source,
          (GSourceFunc) gst_play_seek_internal, self, NULL);

      GST_TRACE_OBJECT (self,
          "Delaying seek to position %" GST_TIME_FORMAT " by %u us",
          GST_TIME_ARGS (position), delay);
      g_source_attach (self->seek_source, self->context);
    }
  }
  g_mutex_unlock (&self->lock);
}

static void
remove_seek_source (GstPlay * self)
{
  if (!self->seek_source)
    return;

  g_source_destroy (self->seek_source);
  g_source_unref (self->seek_source);
  self->seek_source = NULL;
}

/**
 * gst_play_get_uri:
 * @play: #GstPlay instance
 *
 * Gets the URI of the currently-playing stream.
 *
 * Returns: (transfer full) (nullable): a string containing the URI of the
 * currently-playing stream. g_free() after usage.
 * Since: 1.20
 */
gchar *
gst_play_get_uri (GstPlay * self)
{
  gchar *val;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_URI);

  g_object_get (self, "uri", &val, NULL);

  return val;
}

/**
 * gst_play_set_uri:
 * @play: #GstPlay instance
 * @uri: (nullable): next URI to play.
 *
 * Sets the next URI to play.
 * Since: 1.20
 */
void
gst_play_set_uri (GstPlay * self, const gchar * val)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "uri", val, NULL);
}

/**
 * gst_play_set_subtitle_uri:
 * @play: #GstPlay instance
 * @uri: (nullable): subtitle URI
 *
 * Sets the external subtitle URI. This should be combined with a call to
 * gst_play_set_subtitle_track_enabled(@play, TRUE) so the subtitles are actually
 * rendered.
 * Since: 1.20
 */
void
gst_play_set_subtitle_uri (GstPlay * self, const gchar * suburi)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "suburi", suburi, NULL);
}

/**
 * gst_play_get_subtitle_uri:
 * @play: #GstPlay instance
 *
 * Current subtitle URI
 *
 * Returns: (transfer full) (nullable): URI of the current external subtitle.
 *   g_free() after usage.
 * Since: 1.20
 */
gchar *
gst_play_get_subtitle_uri (GstPlay * self)
{
  gchar *val = NULL;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  g_object_get (self, "suburi", &val, NULL);

  return val;
}

/**
 * gst_play_get_position:
 * @play: #GstPlay instance
 *
 * Returns: the absolute position time, in nanoseconds, of the
 * currently-playing stream.
 * Since: 1.20
 */
GstClockTime
gst_play_get_position (GstPlay * self)
{
  GstClockTime val;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_POSITION);

  g_object_get (self, "position", &val, NULL);

  return val;
}

/**
 * gst_play_get_duration:
 * @play: #GstPlay instance
 *
 * Retrieves the duration of the media stream that self represents.
 *
 * Returns: the duration of the currently-playing media stream, in
 * nanoseconds.
 * Since: 1.20
 */
GstClockTime
gst_play_get_duration (GstPlay * self)
{
  GstClockTime val;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_DURATION);

  g_object_get (self, "duration", &val, NULL);

  return val;
}

/**
 * gst_play_get_volume:
 * @play: #GstPlay instance
 *
 * Returns the current volume level, as a percentage between 0 and 1.
 *
 * Returns: the volume as percentage between 0 and 1.
 * Since: 1.20
 */
gdouble
gst_play_get_volume (GstPlay * self)
{
  gdouble val;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_VOLUME);

  g_object_get (self, "volume", &val, NULL);

  return val;
}

/**
 * gst_play_set_volume:
 * @play: #GstPlay instance
 * @val: the new volume level, as a percentage between 0 and 1
 *
 * Sets the volume level of the stream as a percentage between 0 and 1.
 * Since: 1.20
 */
void
gst_play_set_volume (GstPlay * self, gdouble val)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "volume", val, NULL);
}

/**
 * gst_play_get_mute:
 * @play: #GstPlay instance
 *
 * Returns: %TRUE if the currently-playing stream is muted.
 * Since: 1.20
 */
gboolean
gst_play_get_mute (GstPlay * self)
{
  gboolean val;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_MUTE);

  g_object_get (self, "mute", &val, NULL);

  return val;
}

/**
 * gst_play_set_mute:
 * @play: #GstPlay instance
 * @val: Mute state the should be set
 *
 * %TRUE if the currently-playing stream should be muted.
 * Since: 1.20
 */
void
gst_play_set_mute (GstPlay * self, gboolean val)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "mute", val, NULL);
}

/**
 * gst_play_get_pipeline:
 * @play: #GstPlay instance
 *
 * Returns: (transfer full): The internal playbin instance.
 *
 * The caller should free it with g_object_unref()
 * Since: 1.20
 */
GstElement *
gst_play_get_pipeline (GstPlay * self)
{
  GstElement *val;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  g_object_get (self, "pipeline", &val, NULL);

  return val;
}

/**
 * gst_play_get_media_info:
 * @play: #GstPlay instance
 *
 * A Function to get the current media info #GstPlayMediaInfo instance.
 *
 * Returns: (transfer full) (nullable): media info instance.
 *
 * The caller should free it with g_object_unref()
 * Since: 1.20
 */
GstPlayMediaInfo *
gst_play_get_media_info (GstPlay * self)
{
  GstPlayMediaInfo *info;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  if (!self->media_info)
    return NULL;

  g_mutex_lock (&self->lock);
  info = gst_play_media_info_copy (self->media_info);
  g_mutex_unlock (&self->lock);

  return info;
}

/**
 * gst_play_get_current_audio_track:
 * @play: #GstPlay instance
 *
 * A Function to get current audio #GstPlayAudioInfo instance.
 *
 * Returns: (transfer full) (nullable): current audio track.
 *
 * The caller should free it with g_object_unref()
 * Since: 1.20
 */
GstPlayAudioInfo *
gst_play_get_current_audio_track (GstPlay * self)
{
  GstPlayAudioInfo *info;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  if (!is_track_enabled (self, GST_PLAY_FLAG_AUDIO))
    return NULL;

  info = (GstPlayAudioInfo *)
      gst_play_stream_info_get_current_from_stream_id (self,
      self->audio_sid, GST_TYPE_PLAY_AUDIO_INFO);

  return info;
}

/**
 * gst_play_get_current_video_track:
 * @play: #GstPlay instance
 *
 * A Function to get current video #GstPlayVideoInfo instance.
 *
 * Returns: (transfer full) (nullable): current video track.
 *
 * The caller should free it with g_object_unref()
 * Since: 1.20
 */
GstPlayVideoInfo *
gst_play_get_current_video_track (GstPlay * self)
{
  GstPlayVideoInfo *info;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  if (!is_track_enabled (self, GST_PLAY_FLAG_VIDEO))
    return NULL;

  info = (GstPlayVideoInfo *)
      gst_play_stream_info_get_current_from_stream_id (self,
      self->video_sid, GST_TYPE_PLAY_VIDEO_INFO);

  return info;
}

/**
 * gst_play_get_current_subtitle_track:
 * @play: #GstPlay instance
 *
 * A Function to get current subtitle #GstPlaySubtitleInfo instance.
 *
 * Returns: (transfer full) (nullable): current subtitle track.
 *
 * The caller should free it with g_object_unref()
 * Since: 1.20
 */
GstPlaySubtitleInfo *
gst_play_get_current_subtitle_track (GstPlay * self)
{
  GstPlaySubtitleInfo *info;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  if (!is_track_enabled (self, GST_PLAY_FLAG_SUBTITLE))
    return NULL;

  info = (GstPlaySubtitleInfo *)
      gst_play_stream_info_get_current_from_stream_id (self,
      self->subtitle_sid, GST_TYPE_PLAY_SUBTITLE_INFO);

  return info;
}

/* Must be called with lock */
static gboolean
gst_play_select_streams (GstPlay * self)
{
  GList *stream_list = NULL;
  gboolean ret = FALSE;

  if (self->audio_sid && self->audio_enabled)
    stream_list = g_list_append (stream_list, g_strdup (self->audio_sid));
  if (self->video_sid && self->video_enabled)
    stream_list = g_list_append (stream_list, g_strdup (self->video_sid));
  if (self->subtitle_sid && self->subtitle_enabled)
    stream_list = g_list_append (stream_list, g_strdup (self->subtitle_sid));

  g_mutex_unlock (&self->lock);
  if (stream_list) {
    ret = gst_element_send_event (self->playbin,
        gst_event_new_select_streams (stream_list));
    g_list_free_full (stream_list, g_free);
  } else {
    GST_ERROR_OBJECT (self, "No available streams for select-streams");
  }
  g_mutex_lock (&self->lock);

  return ret;
}

/**
 * gst_play_set_audio_track:
 * @play: #GstPlay instance
 * @stream_index: stream index
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the audio track @stream_index.
 *
 * Deprecated: 1.26: Use gst_play_set_audio_track_id() instead.
 *
 * Since: 1.20
 */
gboolean
gst_play_set_audio_track (GstPlay * self, gint stream_index)
{
  GstPlayStreamInfo *info;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  info = gst_play_stream_info_find (self->media_info,
      GST_TYPE_PLAY_AUDIO_INFO, stream_index);
  if (!info) {
    GST_ERROR_OBJECT (self, "invalid audio stream index %d", stream_index);
    g_mutex_unlock (&self->lock);
    return FALSE;
  }

  g_free (self->audio_sid);
  self->audio_sid = g_strdup (info->stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);
  GST_DEBUG_OBJECT (self, "set stream id '%s'", info->stream_id);
  g_object_unref (info);

  return ret;
}

/**
 * gst_play_set_video_track:
 * @play: #GstPlay instance
 * @stream_index: stream index
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the video track @stream_index.
 *
 * Deprecated: 1.26: Use gst_play_set_video_track_id() instead.
 *
 * Since: 1.20
 */
gboolean
gst_play_set_video_track (GstPlay * self, gint stream_index)
{
  GstPlayStreamInfo *info;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  /* check if stream_index exist in our internal media_info list */
  g_mutex_lock (&self->lock);
  info = gst_play_stream_info_find (self->media_info,
      GST_TYPE_PLAY_VIDEO_INFO, stream_index);
  if (!info) {
    GST_ERROR_OBJECT (self, "invalid video stream index %d", stream_index);
    g_mutex_unlock (&self->lock);
    return FALSE;
  }

  g_free (self->video_sid);
  self->video_sid = g_strdup (info->stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);
  GST_DEBUG_OBJECT (self, "set stream id '%s'", info->stream_id);
  g_object_unref (info);

  return ret;
}

/**
 * gst_play_set_subtitle_track:
 * @play: #GstPlay instance
 * @stream_index: stream index
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the subtitle stack @stream_index.
 *
 * Deprecated: 1.26: Use gst_play_set_subtitle_track_id() instead.
 *
 * Since: 1.20
 */
gboolean
gst_play_set_subtitle_track (GstPlay * self, gint stream_index)
{
  GstPlayStreamInfo *info;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  info = gst_play_stream_info_find (self->media_info,
      GST_TYPE_PLAY_SUBTITLE_INFO, stream_index);
  if (!info) {
    GST_ERROR_OBJECT (self, "invalid subtitle stream index %d", stream_index);
    g_mutex_unlock (&self->lock);
    return FALSE;
  }

  g_free (self->subtitle_sid);
  self->subtitle_sid = g_strdup (info->stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);
  GST_DEBUG_OBJECT (self, "set stream id '%s'", info->stream_id);
  g_object_unref (info);

  return ret;
}

/**
 * gst_play_set_audio_track_id:
 * @play: #GstPlay instance
 * @stream_id: (nullable): stream id
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the audio track @stream_id.
 *
 * Since: 1.26
 */
gboolean
gst_play_set_audio_track_id (GstPlay * self, const gchar * stream_id)
{
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  if (stream_id) {
    GstPlayStreamInfo *info;

    info = gst_play_stream_info_find_from_stream_id (self->media_info,
        stream_id);
    if (!info) {
      GST_ERROR_OBJECT (self, "invalid audio stream id %s", stream_id);
      g_mutex_unlock (&self->lock);
      return FALSE;
    }

    if (!GST_IS_PLAY_AUDIO_INFO (info)) {
      GST_ERROR_OBJECT (self, "invalid audio stream id %s", stream_id);
      g_mutex_unlock (&self->lock);
      g_object_unref (info);
      return FALSE;
    }
    g_object_unref (info);
  }

  g_free (self->audio_sid);
  self->audio_sid = g_strdup (stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "set stream id '%s'", GST_STR_NULL (stream_id));
  return ret;
}

/**
 * gst_play_set_video_track_id:
 * @play: #GstPlay instance
 * @stream_id: (nullable): stream id
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the video track @stream_id.
 *
 * Since: 1.26
 */
gboolean
gst_play_set_video_track_id (GstPlay * self, const gchar * stream_id)
{
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  if (stream_id) {
    GstPlayStreamInfo *info;

    info = gst_play_stream_info_find_from_stream_id (self->media_info,
        stream_id);
    if (!info) {
      GST_ERROR_OBJECT (self, "invalid video stream index %s", stream_id);
      g_mutex_unlock (&self->lock);
      return FALSE;
    }

    if (!GST_IS_PLAY_VIDEO_INFO (info)) {
      GST_ERROR_OBJECT (self, "invalid video stream id %s", stream_id);
      g_mutex_unlock (&self->lock);
      g_object_unref (info);
      return FALSE;
    }
    g_object_unref (info);
  }

  g_free (self->video_sid);
  self->video_sid = g_strdup (stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "set stream id '%s'", GST_STR_NULL (stream_id));
  return ret;
}

/**
 * gst_play_set_subtitle_track_id:
 * @play: #GstPlay instance
 * @stream_id: (nullable): stream id
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the subtitle track @stream_id.
 *
 * Since: 1.26
 */
gboolean
gst_play_set_subtitle_track_id (GstPlay * self, const gchar * stream_id)
{
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  if (stream_id) {
    GstPlayStreamInfo *info;

    info = gst_play_stream_info_find_from_stream_id (self->media_info,
        stream_id);
    if (!info) {
      GST_ERROR_OBJECT (self, "invalid subtitle stream index %s", stream_id);
      g_mutex_unlock (&self->lock);
      return FALSE;
    }

    if (!GST_IS_PLAY_SUBTITLE_INFO (info)) {
      GST_ERROR_OBJECT (self, "invalid subtile stream id %s", stream_id);
      g_mutex_unlock (&self->lock);
      g_object_unref (info);
      return FALSE;
    }
    g_object_unref (info);
  }

  g_free (self->subtitle_sid);
  self->subtitle_sid = g_strdup (stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "set stream id '%s'", GST_STR_NULL (stream_id));
  return ret;
}

/**
 * gst_play_set_track_ids:
 * @play: #GstPlay instance
 * @audio_stream_id: (nullable): audio stream id
 * @video_stream_id: (nullable): video stream id
 * @subtitle_stream_id: (nullable): subtitle stream id
 *
 * Returns: %TRUE or %FALSE
 *
 * Sets the selected track stream ids. Setting %NULL as stream id disables the
 * corresponding track.
 *
 * Since: 1.26
 */
gboolean
gst_play_set_track_ids (GstPlay * self, const gchar * audio_stream_id,
    const gchar * video_stream_id, const gchar * subtitle_stream_id)
{
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  if (audio_stream_id) {
    GstPlayStreamInfo *info;

    info = gst_play_stream_info_find_from_stream_id (self->media_info,
        audio_stream_id);
    if (!info) {
      GST_ERROR_OBJECT (self, "invalid audio stream id %s", audio_stream_id);
      g_mutex_unlock (&self->lock);
      return FALSE;
    }

    if (!GST_IS_PLAY_AUDIO_INFO (info)) {
      GST_ERROR_OBJECT (self, "invalid audio stream id %s", audio_stream_id);
      g_mutex_unlock (&self->lock);
      g_object_unref (info);
      return FALSE;
    }
    g_object_unref (info);
  }

  if (video_stream_id) {
    GstPlayStreamInfo *info;

    info = gst_play_stream_info_find_from_stream_id (self->media_info,
        video_stream_id);
    if (!info) {
      GST_ERROR_OBJECT (self, "invalid video stream index %s", video_stream_id);
      g_mutex_unlock (&self->lock);
      return FALSE;
    }

    if (!GST_IS_PLAY_VIDEO_INFO (info)) {
      GST_ERROR_OBJECT (self, "invalid video stream id %s", video_stream_id);
      g_mutex_unlock (&self->lock);
      g_object_unref (info);
      return FALSE;
    }
    g_object_unref (info);
  }

  if (subtitle_stream_id) {
    GstPlayStreamInfo *info;

    info = gst_play_stream_info_find_from_stream_id (self->media_info,
        subtitle_stream_id);
    if (!info) {
      GST_ERROR_OBJECT (self, "invalid subtitle stream index %s",
          subtitle_stream_id);
      g_mutex_unlock (&self->lock);
      return FALSE;
    }

    if (!GST_IS_PLAY_SUBTITLE_INFO (info)) {
      GST_ERROR_OBJECT (self, "invalid subtile stream id %s",
          subtitle_stream_id);
      g_mutex_unlock (&self->lock);
      g_object_unref (info);
      return FALSE;
    }
    g_object_unref (info);
  }

  g_free (self->audio_sid);
  self->audio_sid = g_strdup (audio_stream_id);
  g_free (self->video_sid);
  self->video_sid = g_strdup (video_stream_id);
  g_free (self->subtitle_sid);
  self->subtitle_sid = g_strdup (subtitle_stream_id);
  ret = gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "set stream ids audio '%s' video '%s' subtitle '%s'",
      GST_STR_NULL (audio_stream_id), GST_STR_NULL (video_stream_id),
      GST_STR_NULL (subtitle_stream_id));

  return ret;
}

/**
 * gst_play_set_audio_track_enabled:
 * @play: #GstPlay instance
 * @enabled: TRUE or FALSE
 *
 * Enable or disable the current audio track.
 * Since: 1.20
 */
void
gst_play_set_audio_track_enabled (GstPlay * self, gboolean enabled)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_mutex_lock (&self->lock);
  self->audio_enabled = enabled;
  gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "track is '%s'", enabled ? "Enabled" : "Disabled");
}

/**
 * gst_play_set_video_track_enabled:
 * @play: #GstPlay instance
 * @enabled: TRUE or FALSE
 *
 * Enable or disable the current video track.
 * Since: 1.20
 */
void
gst_play_set_video_track_enabled (GstPlay * self, gboolean enabled)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_mutex_lock (&self->lock);
  self->video_enabled = enabled;
  gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "track is '%s'", enabled ? "Enabled" : "Disabled");
}

/**
 * gst_play_set_subtitle_track_enabled:
 * @play: #GstPlay instance
 * @enabled: TRUE or FALSE
 *
 * Enable or disable the current subtitle track.
 * Since: 1.20
 */
void
gst_play_set_subtitle_track_enabled (GstPlay * self, gboolean enabled)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_mutex_lock (&self->lock);
  self->subtitle_enabled = enabled;
  gst_play_select_streams (self);
  g_mutex_unlock (&self->lock);

  GST_DEBUG_OBJECT (self, "track is '%s'", enabled ? "Enabled" : "Disabled");
}

/**
 * gst_play_set_visualization:
 * @play: #GstPlay instance
 * @name: (nullable): visualization element obtained from
 * #gst_play_visualizations_get()
 *
 * Returns: %TRUE if the visualization was set correctly. Otherwise,
 * %FALSE.
 * Since: 1.20
 */
gboolean
gst_play_set_visualization (GstPlay * self, const gchar * name)
{
  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  g_mutex_lock (&self->lock);
  if (self->current_vis_element) {
    gst_object_unref (self->current_vis_element);
    self->current_vis_element = NULL;
  }

  if (name) {
    self->current_vis_element = gst_element_factory_make (name, NULL);
    if (!self->current_vis_element)
      goto error_no_element;
    gst_object_ref_sink (self->current_vis_element);
  }
  g_object_set (self->playbin, "vis-plugin", self->current_vis_element, NULL);

  g_mutex_unlock (&self->lock);
  GST_DEBUG_OBJECT (self, "set vis-plugin to '%s'", name);

  return TRUE;

error_no_element:
  g_mutex_unlock (&self->lock);
  GST_WARNING_OBJECT (self, "could not find visualization '%s'", name);
  return FALSE;
}

/**
 * gst_play_get_current_visualization:
 * @play: #GstPlay instance
 *
 * Returns: (transfer full) (nullable): Name of the currently enabled
 *   visualization.
 *   g_free() after usage.
 * Since: 1.20
 */
gchar *
gst_play_get_current_visualization (GstPlay * self)
{
  gchar *name = NULL;
  GstElement *vis_plugin = NULL;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  if (!is_track_enabled (self, GST_PLAY_FLAG_VIS))
    return NULL;

  g_object_get (self->playbin, "vis-plugin", &vis_plugin, NULL);

  if (vis_plugin) {
    GstElementFactory *factory = gst_element_get_factory (vis_plugin);
    if (factory)
      name = g_strdup (gst_plugin_feature_get_name (factory));
    gst_object_unref (vis_plugin);
  }

  GST_DEBUG_OBJECT (self, "vis-plugin '%s' %p", name, vis_plugin);

  return name;
}

/**
 * gst_play_set_visualization_enabled:
 * @play: #GstPlay instance
 * @enabled: TRUE or FALSE
 *
 * Enable or disable the visualization.
 * Since: 1.20
 */
void
gst_play_set_visualization_enabled (GstPlay * self, gboolean enabled)
{
  g_return_if_fail (GST_IS_PLAY (self));

  if (enabled)
    play_set_flag (self, GST_PLAY_FLAG_VIS);
  else
    play_clear_flag (self, GST_PLAY_FLAG_VIS);

  GST_DEBUG_OBJECT (self, "visualization is '%s'",
      enabled ? "Enabled" : "Disabled");
}

struct CBChannelMap
{
  const gchar *label;           /* channel label name */
  const gchar *name;            /* get_name () */
};

static const struct CBChannelMap cb_channel_map[] = {
  /* GST_PLAY_COLOR_BALANCE_BRIGHTNESS */ {"BRIGHTNESS", "brightness"},
  /* GST_PLAY_COLOR_BALANCE_CONTRAST   */ {"CONTRAST", "contrast"},
  /* GST_PLAY_COLOR_BALANCE_SATURATION */ {"SATURATION", "saturation"},
  /* GST_PLAY_COLOR_BALANCE_HUE        */ {"HUE", "hue"},
};

static GstColorBalanceChannel *
gst_play_color_balance_find_channel (GstPlay * self,
    GstPlayColorBalanceType type)
{
  GstColorBalanceChannel *channel;
  const GList *l, *channels;

  if (type < GST_PLAY_COLOR_BALANCE_BRIGHTNESS ||
      type > GST_PLAY_COLOR_BALANCE_HUE)
    return NULL;

  channels =
      gst_color_balance_list_channels (GST_COLOR_BALANCE (self->playbin));
  for (l = channels; l; l = l->next) {
    channel = l->data;
    if (g_strrstr (channel->label, cb_channel_map[type].label))
      return channel;
  }

  return NULL;
}

/**
 * gst_play_has_color_balance:
 * @play:#GstPlay instance
 *
 * Checks whether the @play has color balance support available.
 *
 * Returns: %TRUE if @play has color balance support. Otherwise,
 *   %FALSE.
 * Since: 1.20
 */
gboolean
gst_play_has_color_balance (GstPlay * self)
{
  const GList *channels;

  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);

  if (!GST_IS_COLOR_BALANCE (self->playbin))
    return FALSE;

  channels =
      gst_color_balance_list_channels (GST_COLOR_BALANCE (self->playbin));
  return (channels != NULL);
}

/**
 * gst_play_set_color_balance:
 * @play: #GstPlay instance
 * @type: #GstPlayColorBalanceType
 * @value: The new value for the @type, ranged [0,1]
 *
 * Sets the current value of the indicated channel @type to the passed
 * value.
 * Since: 1.20
 */
void
gst_play_set_color_balance (GstPlay * self, GstPlayColorBalanceType type,
    gdouble value)
{
  GstColorBalanceChannel *channel;
  gdouble new_val;

  g_return_if_fail (GST_IS_PLAY (self));
  g_return_if_fail (value >= 0.0 && value <= 1.0);

  if (!GST_IS_COLOR_BALANCE (self->playbin))
    return;

  channel = gst_play_color_balance_find_channel (self, type);
  if (!channel)
    return;

  value = CLAMP (value, 0.0, 1.0);

  /* Convert to channel range */
  new_val = channel->min_value + value * ((gdouble) channel->max_value -
      (gdouble) channel->min_value);

  gst_color_balance_set_value (GST_COLOR_BALANCE (self->playbin), channel,
      new_val);
}

/**
 * gst_play_get_color_balance:
 * @play: #GstPlay instance
 * @type: #GstPlayColorBalanceType
 *
 * Retrieve the current value of the indicated @type.
 *
 * Returns: The current value of @type, between [0,1]. In case of
 *   error -1 is returned.
 * Since: 1.20
 */
gdouble
gst_play_get_color_balance (GstPlay * self, GstPlayColorBalanceType type)
{
  GstColorBalanceChannel *channel;
  gint value;

  g_return_val_if_fail (GST_IS_PLAY (self), -1);

  if (!GST_IS_COLOR_BALANCE (self->playbin))
    return -1;

  channel = gst_play_color_balance_find_channel (self, type);
  if (!channel)
    return -1;

  value = gst_color_balance_get_value (GST_COLOR_BALANCE (self->playbin),
      channel);

  return ((gdouble) value -
      (gdouble) channel->min_value) / ((gdouble) channel->max_value -
      (gdouble) channel->min_value);
}

/**
 * gst_play_get_multiview_mode:
 * @play: #GstPlay instance
 *
 * Retrieve the current value of the indicated @type.
 *
 * Returns: The current value of @type, Default: -1 "none"
 *
 * Since: 1.20
 */
GstVideoMultiviewFramePacking
gst_play_get_multiview_mode (GstPlay * self)
{
  GstVideoMultiviewFramePacking val = GST_VIDEO_MULTIVIEW_FRAME_PACKING_NONE;

  g_return_val_if_fail (GST_IS_PLAY (self),
      GST_VIDEO_MULTIVIEW_FRAME_PACKING_NONE);

  g_object_get (self, "video-multiview-mode", &val, NULL);

  return val;
}

/**
 * gst_play_set_multiview_mode:
 * @play: #GstPlay instance
 * @mode: The new value for the @type
 *
 * Sets the current value of the indicated mode @type to the passed
 * value.
 *
 * Since: 1.20
 */
void
gst_play_set_multiview_mode (GstPlay * self, GstVideoMultiviewFramePacking mode)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "video-multiview-mode", mode, NULL);
}

/**
 * gst_play_get_multiview_flags:
 * @play: #GstPlay instance
 *
 * Retrieve the current value of the indicated @type.
 *
 * Returns: The current value of @type, Default: 0x00000000 "none
 *
 * Since: 1.20
 */
GstVideoMultiviewFlags
gst_play_get_multiview_flags (GstPlay * self)
{
  GstVideoMultiviewFlags val = GST_VIDEO_MULTIVIEW_FLAGS_NONE;

  g_return_val_if_fail (GST_IS_PLAY (self), val);

  g_object_get (self, "video-multiview-flags", &val, NULL);

  return val;
}

/**
 * gst_play_set_multiview_flags:
 * @play: #GstPlay instance
 * @flags: The new value for the @type
 *
 * Sets the current value of the indicated mode @type to the passed
 * value.
 *
 * Since: 1.20
 */
void
gst_play_set_multiview_flags (GstPlay * self, GstVideoMultiviewFlags flags)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "video-multiview-flags", flags, NULL);
}

/**
 * gst_play_get_audio_video_offset:
 * @play: #GstPlay instance
 *
 * Retrieve the current value of audio-video-offset property
 *
 * Returns: The current value of audio-video-offset in nanoseconds
 *
 * Since: 1.20
 */
gint64
gst_play_get_audio_video_offset (GstPlay * self)
{
  gint64 val = 0;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_AUDIO_VIDEO_OFFSET);

  g_object_get (self, "audio-video-offset", &val, NULL);

  return val;
}

/**
 * gst_play_set_audio_video_offset:
 * @play: #GstPlay instance
 * @offset: #gint64 in nanoseconds
 *
 * Sets audio-video-offset property by value of @offset
 *
 * Since: 1.20
 */
void
gst_play_set_audio_video_offset (GstPlay * self, gint64 offset)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "audio-video-offset", offset, NULL);
}

/**
 * gst_play_get_subtitle_video_offset:
 * @play: #GstPlay instance
 *
 * Retrieve the current value of subtitle-video-offset property
 *
 * Returns: The current value of subtitle-video-offset in nanoseconds
 *
 * Since: 1.20
 */
gint64
gst_play_get_subtitle_video_offset (GstPlay * self)
{
  gint64 val = 0;

  g_return_val_if_fail (GST_IS_PLAY (self), DEFAULT_SUBTITLE_VIDEO_OFFSET);

  g_object_get (self, "subtitle-video-offset", &val, NULL);

  return val;
}

/**
 * gst_play_set_subtitle_video_offset:
 * @play: #GstPlay instance
 * @offset: #gint64 in nanoseconds
 *
 * Sets subtitle-video-offset property by value of @offset
 *
 * Since: 1.20
 */
void
gst_play_set_subtitle_video_offset (GstPlay * self, gint64 offset)
{
  g_return_if_fail (GST_IS_PLAY (self));

  g_object_set (self, "subtitle-video-offset", offset, NULL);
}


#define C_ENUM(v) ((gint) v)
#define C_FLAGS(v) ((guint) v)

GType
gst_play_color_balance_type_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_PLAY_COLOR_BALANCE_HUE), "GST_PLAY_COLOR_BALANCE_HUE",
        "hue"},
    {C_ENUM (GST_PLAY_COLOR_BALANCE_BRIGHTNESS),
        "GST_PLAY_COLOR_BALANCE_BRIGHTNESS", "brightness"},
    {C_ENUM (GST_PLAY_COLOR_BALANCE_SATURATION),
        "GST_PLAY_COLOR_BALANCE_SATURATION", "saturation"},
    {C_ENUM (GST_PLAY_COLOR_BALANCE_CONTRAST),
        "GST_PLAY_COLOR_BALANCE_CONTRAST", "contrast"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstPlayColorBalanceType", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

/**
 * gst_play_color_balance_type_get_name:
 * @type: a #GstPlayColorBalanceType
 *
 * Gets a string representing the given color balance type.
 *
 * Returns: (transfer none): a string with the name of the color
 *   balance type.
 * Since: 1.20
 */
const gchar *
gst_play_color_balance_type_get_name (GstPlayColorBalanceType type)
{
  g_return_val_if_fail (type >= GST_PLAY_COLOR_BALANCE_BRIGHTNESS &&
      type <= GST_PLAY_COLOR_BALANCE_HUE, NULL);

  return cb_channel_map[type].name;
}

GType
gst_play_state_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_PLAY_STATE_STOPPED), "GST_PLAY_STATE_STOPPED", "stopped"},
    {C_ENUM (GST_PLAY_STATE_BUFFERING), "GST_PLAY_STATE_BUFFERING",
        "buffering"},
    {C_ENUM (GST_PLAY_STATE_PAUSED), "GST_PLAY_STATE_PAUSED", "paused"},
    {C_ENUM (GST_PLAY_STATE_PLAYING), "GST_PLAY_STATE_PLAYING", "playing"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstPlayState", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

GType
gst_play_message_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_PLAY_MESSAGE_URI_LOADED), "GST_PLAY_MESSAGE_URI_LOADED",
        "uri-loaded"},
    {C_ENUM (GST_PLAY_MESSAGE_POSITION_UPDATED),
        "GST_PLAY_MESSAGE_POSITION_UPDATED", "position-updated"},
    {C_ENUM (GST_PLAY_MESSAGE_DURATION_CHANGED),
        "GST_PLAY_MESSAGE_DURATION_CHANGED", "duration-changed"},
    {C_ENUM (GST_PLAY_MESSAGE_STATE_CHANGED),
        "GST_PLAY_MESSAGE_STATE_CHANGED", "state-changed"},
    {C_ENUM (GST_PLAY_MESSAGE_BUFFERING), "GST_PLAY_MESSAGE_BUFFERING",
        "buffering"},
    {C_ENUM (GST_PLAY_MESSAGE_END_OF_STREAM),
        "GST_PLAY_MESSAGE_END_OF_STREAM", "end-of-stream"},
    {C_ENUM (GST_PLAY_MESSAGE_ERROR), "GST_PLAY_MESSAGE_ERROR", "error"},
    {C_ENUM (GST_PLAY_MESSAGE_WARNING), "GST_PLAY_MESSAGE_WARNING",
        "warning"},
    {C_ENUM (GST_PLAY_MESSAGE_VIDEO_DIMENSIONS_CHANGED),
          "GST_PLAY_MESSAGE_VIDEO_DIMENSIONS_CHANGED",
        "video-dimensions-changed"},
    {C_ENUM (GST_PLAY_MESSAGE_MEDIA_INFO_UPDATED),
        "GST_PLAY_MESSAGE_MEDIA_INFO_UPDATED", "media-info-updated"},
    {C_ENUM (GST_PLAY_MESSAGE_VOLUME_CHANGED),
        "GST_PLAY_MESSAGE_VOLUME_CHANGED", "volume-changed"},
    {C_ENUM (GST_PLAY_MESSAGE_MUTE_CHANGED),
        "GST_PLAY_MESSAGE_MUTE_CHANGED", "mute-changed"},
    {C_ENUM (GST_PLAY_MESSAGE_SEEK_DONE), "GST_PLAY_MESSAGE_SEEK_DONE",
        "seek-done"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstPlayMessage", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

/**
 * gst_play_state_get_name:
 * @state: a #GstPlayState
 *
 * Gets a string representing the given state.
 *
 * Returns: (transfer none): a string with the name of the state.
 * Since: 1.20
 */
const gchar *
gst_play_state_get_name (GstPlayState state)
{
  switch (state) {
    case GST_PLAY_STATE_STOPPED:
      return "stopped";
    case GST_PLAY_STATE_BUFFERING:
      return "buffering";
    case GST_PLAY_STATE_PAUSED:
      return "paused";
    case GST_PLAY_STATE_PLAYING:
      return "playing";
  }

  g_assert_not_reached ();
  return NULL;
}

/**
 * gst_play_message_get_name:
 * @message_type: a #GstPlayMessage
 *
 * Returns: (transfer none): a string with the name of the message.
 * Since: 1.20
 */
const gchar *
gst_play_message_get_name (GstPlayMessage message_type)
{
  GEnumClass *enum_class;
  GEnumValue *enum_value;
  enum_class = g_type_class_ref (GST_TYPE_PLAY_MESSAGE);
  enum_value = g_enum_get_value (enum_class, message_type);
  g_assert (enum_value != NULL);
  g_type_class_unref (enum_class);
  return enum_value->value_name;
}

GType
gst_play_error_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_PLAY_ERROR_FAILED), "GST_PLAY_ERROR_FAILED", "failed"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstPlayError", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

/**
 * gst_play_error_get_name:
 * @error: a #GstPlayError
 *
 * Gets a string representing the given error.
 *
 * Returns: (transfer none): a string with the given error.
 * Since: 1.20
 */
const gchar *
gst_play_error_get_name (GstPlayError error)
{
  switch (error) {
    case GST_PLAY_ERROR_FAILED:
      return "failed";
    case GST_PLAY_ERROR_MISSING_PLUGIN:
      return "missing-plugin";
  }

  g_assert_not_reached ();
  return NULL;
}

/**
 * gst_play_set_config:
 * @play: #GstPlay instance
 * @config: (transfer full): a #GstStructure
 *
 * Set the configuration of the play. If the play is already configured, and
 * the configuration hasn't changed, this function will return %TRUE. If the
 * play is not in the GST_PLAY_STATE_STOPPED, this method will return %FALSE
 * and active configuration will remain.
 *
 * @config is a #GstStructure that contains the configuration parameters for
 * the play.
 *
 * This function takes ownership of @config.
 *
 * Returns: %TRUE when the configuration could be set.
 * Since: 1.20
 */
gboolean
gst_play_set_config (GstPlay * self, GstStructure * config)
{
  g_return_val_if_fail (GST_IS_PLAY (self), FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  g_mutex_lock (&self->lock);

  if (self->app_state != GST_PLAY_STATE_STOPPED) {
    GST_INFO_OBJECT (self, "can't change config while play is %s",
        gst_play_state_get_name (self->app_state));
    g_mutex_unlock (&self->lock);
    return FALSE;
  }

  if (self->config)
    gst_structure_free (self->config);
  self->config = config;
  g_mutex_unlock (&self->lock);

  return TRUE;
}

/**
 * gst_play_get_config:
 * @play: #GstPlay instance
 *
 * Get a copy of the current configuration of the play. This configuration
 * can either be modified and used for the gst_play_set_config() call
 * or it must be freed after usage.
 *
 * Returns: (transfer full): a copy of the current configuration of @play. Use
 * gst_structure_free() after usage or gst_play_set_config().
 *
 * Since: 1.20
 */
GstStructure *
gst_play_get_config (GstPlay * self)
{
  GstStructure *ret;

  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  g_mutex_lock (&self->lock);
  ret = gst_structure_copy (self->config);
  g_mutex_unlock (&self->lock);

  return ret;
}

/**
 * gst_play_config_set_user_agent:
 * @config: a #GstPlay configuration
 * @agent: (nullable): the string to use as user agent
 *
 * Set the user agent to pass to the server if @play needs to connect
 * to a server during playback. This is typically used when playing HTTP
 * or RTSP streams.
 *
 * Since: 1.20
 */
void
gst_play_config_set_user_agent (GstStructure * config, const gchar * agent)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (agent != NULL);

  gst_structure_set (config, "user-agent", G_TYPE_STRING, agent, NULL);
}

/**
 * gst_play_config_get_user_agent:
 * @config: a #GstPlay configuration
 *
 * Return the user agent which has been configured using
 * gst_play_config_set_user_agent() if any.
 *
 * Returns: (transfer full) (nullable): the configured agent, or %NULL
 * Since: 1.20
 */
gchar *
gst_play_config_get_user_agent (const GstStructure * config)
{
  gchar *agent = NULL;

  g_return_val_if_fail (config != NULL, NULL);

  gst_structure_get (config, "user-agent", G_TYPE_STRING, &agent, NULL);

  return agent;
}

/**
 * gst_play_config_set_position_update_interval:
 * @config: a #GstPlay configuration
 * @interval: interval in ms
 *
 * Set desired interval in milliseconds between two position-updated messages.
 * Pass 0 to stop updating the position.
 * Since: 1.20
 */
void
gst_play_config_set_position_update_interval (GstStructure * config,
    guint interval)
{
  g_return_if_fail (config != NULL);
  g_return_if_fail (interval <= 10000);

  gst_structure_set (config,
      "position-update-interval", G_TYPE_UINT, interval, NULL);
}

/**
 * gst_play_config_get_position_update_interval:
 * @config: a #GstPlay configuration
 *
 * Returns: current position update interval in milliseconds
 *
 * Since: 1.20
 */
guint
gst_play_config_get_position_update_interval (const GstStructure * config)
{
  guint interval = DEFAULT_POSITION_UPDATE_INTERVAL_MS;

  g_return_val_if_fail (config != NULL, DEFAULT_POSITION_UPDATE_INTERVAL_MS);

  gst_structure_get (config,
      "position-update-interval", G_TYPE_UINT, &interval, NULL);

  return interval;
}

/**
 * gst_play_config_set_seek_accurate:
 * @config: a #GstPlay configuration
 * @accurate: accurate seek or not
 *
 * Enable or disable accurate seeking. When enabled, elements will try harder
 * to seek as accurately as possible to the requested seek position. Generally
 * it will be slower especially for formats that don't have any indexes or
 * timestamp markers in the stream.
 *
 * If accurate seeking is disabled, elements will seek as close as the request
 * position without slowing down seeking too much.
 *
 * Accurate seeking is disabled by default.
 *
 * Since: 1.20
 */
void
gst_play_config_set_seek_accurate (GstStructure * config, gboolean accurate)
{
  g_return_if_fail (config != NULL);

  gst_structure_set (config, "accurate-seek", G_TYPE_BOOLEAN, accurate, NULL);
}

/**
 * gst_play_config_get_seek_accurate:
 * @config: a #GstPlay configuration
 *
 * Returns: %TRUE if accurate seeking is enabled
 *
 * Since: 1.20
 */
gboolean
gst_play_config_get_seek_accurate (const GstStructure * config)
{
  gboolean accurate = FALSE;

  g_return_val_if_fail (config != NULL, FALSE);

  gst_structure_get (config, "accurate-seek", G_TYPE_BOOLEAN, &accurate, NULL);

  return accurate;
}

/**
 * gst_play_config_set_pipeline_dump_in_error_details:
 * @config: a #GstPlay configuration
 * @value: Include pipeline dumps in error details, or not.
 *
 * When enabled, the error message emitted by #GstPlay will include a pipeline
 * dump (in Graphviz DOT format) in the error details #GstStructure. The field
 * name is `pipeline-dump`.
 *
 * This option is disabled by default.
 *
 * Since: 1.24
 */
void
gst_play_config_set_pipeline_dump_in_error_details (GstStructure * config,
    gboolean value)
{
  g_return_if_fail (config != NULL);

  gst_structure_set (config, "pipeline-dump-in-error-details",
      G_TYPE_BOOLEAN, value, NULL);
}

/**
 * gst_play_config_get_pipeline_dump_in_error_details:
 * @config: a #GstPlay configuration
 *
 * Returns: %TRUE if pipeline dumps are included in #GstPlay error message
 * details.
 *
 * Since: 1.24
 */
gboolean
gst_play_config_get_pipeline_dump_in_error_details (const GstStructure * config)
{
  gboolean value = FALSE;

  g_return_val_if_fail (config != NULL, FALSE);

  gst_structure_get (config, "pipeline-dump-in-error-details",
      G_TYPE_BOOLEAN, &value, NULL);

  return value;
}

/**
 * gst_play_get_video_snapshot:
 * @play: #GstPlay instance
 * @format: output format of the video snapshot
 * @config: (allow-none): Additional configuration
 *
 * Get a snapshot of the currently selected video stream, if any. The format can be
 * selected with @format and optional configuration is possible with @config.
 * Currently supported settings are:
 * - width, height of type G_TYPE_INT
 * - pixel-aspect-ratio of type GST_TYPE_FRACTION
 *  Except for GST_PLAY_THUMBNAIL_RAW_NATIVE format, if no config is set, pixel-aspect-ratio would be 1/1
 *
 * Returns: (transfer full) (nullable):  Current video snapshot sample or %NULL on failure
 *
 * Since: 1.20
 */
GstSample *
gst_play_get_video_snapshot (GstPlay * self,
    GstPlaySnapshotFormat format, const GstStructure * config)
{
  GstPlayVideoInfo *video_info = NULL;
  GstSample *sample = NULL;
  GstCaps *caps = NULL;
  gint width = -1;
  gint height = -1;
  gint par_n = 1;
  gint par_d = 1;
  g_return_val_if_fail (GST_IS_PLAY (self), NULL);

  video_info = gst_play_get_current_video_track (self);
  if (video_info == NULL) {
    GST_DEBUG_OBJECT (self, "no current video track");
    return NULL;
  } else {
    g_object_unref (video_info);
  }

  switch (format) {
    case GST_PLAY_THUMBNAIL_RAW_xRGB:
      caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "xRGB", NULL);
      break;
    case GST_PLAY_THUMBNAIL_RAW_BGRx:
      caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "BGRx", NULL);
      break;
    case GST_PLAY_THUMBNAIL_JPG:
      caps = gst_caps_new_empty_simple ("image/jpeg");
      break;
    case GST_PLAY_THUMBNAIL_PNG:
      caps = gst_caps_new_empty_simple ("image/png");
      break;
    case GST_PLAY_THUMBNAIL_RAW_NATIVE:
    default:
      caps = gst_caps_new_empty_simple ("video/x-raw");
      break;
  }

  if (NULL != config) {
    if (!gst_structure_get_int (config, "width", &width))
      width = -1;
    if (!gst_structure_get_int (config, "height", &height))
      height = -1;
    if (!gst_structure_get_fraction (config, "pixel-aspect-ratio", &par_n,
            &par_d)) {
      if (format != GST_PLAY_THUMBNAIL_RAW_NATIVE) {
        par_n = 1;
        par_d = 1;
      } else {
        par_n = 0;
        par_d = 0;
      }
    }
  }

  if (width > 0 && height > 0) {
    gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height, NULL);
  }

  if (format != GST_PLAY_THUMBNAIL_RAW_NATIVE) {
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        par_n, par_d, NULL);
  } else if (NULL != config && par_n != 0 && par_d != 0) {
    gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
        par_n, par_d, NULL);
  }

  g_signal_emit_by_name (self->playbin, "convert-sample", caps, &sample);
  gst_caps_unref (caps);
  if (!sample) {
    GST_WARNING_OBJECT (self, "Failed to retrieve or convert video frame");
    return NULL;
  }

  return sample;
}

/**
 * gst_play_is_play_message:
 * @msg: A #GstMessage
 *
 * Returns: A #gboolean indicating whether the passed message represents a #GstPlay message or not.
 *
 * Since: 1.20
 */
gboolean
gst_play_is_play_message (GstMessage * msg)
{
  const GstStructure *data = NULL;
  g_return_val_if_fail (GST_IS_MESSAGE (msg), FALSE);

  data = gst_message_get_structure (msg);
  g_return_val_if_fail (data, FALSE);

  return g_str_equal (gst_structure_get_name (data), GST_PLAY_MESSAGE_DATA);
}

#define PARSE_MESSAGE_FIELD(msg, expected_msg_type, field, value_type, value) G_STMT_START { \
    const GstStructure *data = NULL;                                      \
    GstPlayMessage msg_type;                                              \
    g_return_if_fail (gst_play_is_play_message (msg));                    \
    gst_play_message_parse_type (msg, &msg_type);                         \
    g_return_if_fail (msg_type == expected_msg_type);                     \
    data = gst_message_get_structure (msg);                               \
    gst_structure_get (data, field, value_type, value, NULL);             \
} G_STMT_END

/**
 * gst_play_message_parse_type:
 * @msg: A #GstMessage
 * @type: (out) (optional): the resulting message type
 *
 * Parse the given @msg and extract its #GstPlayMessage type.
 *
 * Since: 1.20
 */
void
gst_play_message_parse_type (GstMessage * msg, GstPlayMessage * type)
{
  const GstStructure *data = NULL;
  g_return_if_fail (gst_play_is_play_message (msg));
  data = gst_message_get_structure (msg);
  gst_structure_get (data, GST_PLAY_MESSAGE_DATA_TYPE, GST_TYPE_PLAY_MESSAGE,
      type, NULL);
}

/**
 * gst_play_message_get_uri:
 * @msg: A #GstMessage
 *
 * Reads the URI the play message @msg applies to.
 *
 * Returns: (transfer none): The URI this message applies to
 *
 * Since: 1.26
 */
const gchar *
gst_play_message_get_uri (GstMessage * msg)
{
  const GstStructure *details = NULL;
  const gchar *uri;
  GstPlayMessage msg_type;

  g_return_val_if_fail (gst_play_is_play_message (msg), NULL);

  gst_play_message_parse_type (msg, &msg_type);

  // ERROR/WARNING messages store the details in differently named fields for
  // backwards compatibility
  if (msg_type == GST_PLAY_MESSAGE_ERROR) {
    const GstStructure *s = gst_message_get_structure (msg);
    const GValue *v =
        gst_structure_get_value (s, GST_PLAY_MESSAGE_DATA_ERROR_DETAILS);
    details = g_value_get_boxed (v);
  } else if (msg_type == GST_PLAY_MESSAGE_WARNING) {
    const GstStructure *s = gst_message_get_structure (msg);
    const GValue *v =
        gst_structure_get_value (s, GST_PLAY_MESSAGE_DATA_WARNING_DETAILS);
    details = g_value_get_boxed (v);
  }

  if (!details)
    details = gst_message_get_details (msg);

  g_return_val_if_fail (details, NULL);
  uri = gst_structure_get_string (details, "uri");
  g_return_val_if_fail (uri, NULL);

  return uri;
}

/**
 * gst_play_message_get_stream_id:
 * @msg: A #GstMessage
 *
 * Reads the stream ID the play message @msg applies to, if any.
 *
 * Returns: (transfer none) (nullable): The stream ID this message applies to
 *
 * Since: 1.26
 */
const gchar *
gst_play_message_get_stream_id (GstMessage * msg)
{
  const GstStructure *details = NULL;
  const gchar *stream_id;
  GstPlayMessage msg_type;

  g_return_val_if_fail (gst_play_is_play_message (msg), NULL);

  gst_play_message_parse_type (msg, &msg_type);

  // ERROR/WARNING messages store the details in differently named fields for
  // backwards compatibility
  if (msg_type == GST_PLAY_MESSAGE_ERROR) {
    const GstStructure *s = gst_message_get_structure (msg);
    const GValue *v =
        gst_structure_get_value (s, GST_PLAY_MESSAGE_DATA_ERROR_DETAILS);
    details = g_value_get_boxed (v);
  } else if (msg_type == GST_PLAY_MESSAGE_WARNING) {
    const GstStructure *s = gst_message_get_structure (msg);
    const GValue *v =
        gst_structure_get_value (s, GST_PLAY_MESSAGE_DATA_WARNING_DETAILS);
    details = g_value_get_boxed (v);
  }

  if (!details)
    details = gst_message_get_details (msg);
  g_return_val_if_fail (details, NULL);
  stream_id = gst_structure_get_string (details, "stream-id");

  return stream_id;
}

/**
 * gst_play_message_parse_uri_loaded:
 * @msg: A #GstMessage
 * @uri: (out) (optional) (transfer full): the resulting URI
 *
 * Parse the given uri-loaded @msg and extract the corresponding value
 *
 * Since: 1.26
 */
void
gst_play_message_parse_uri_loaded (GstMessage * msg, gchar ** uri)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_URI_LOADED,
      GST_PLAY_MESSAGE_DATA_URI, G_TYPE_STRING, uri);
}

/**
 * gst_play_message_parse_duration_changed:
 * @msg: A #GstMessage
 * @duration: (out) (optional): the resulting duration
 *
 * Parse the given duration-changed @msg and extract the corresponding #GstClockTime
 *
 * Since: 1.26
 */
void
gst_play_message_parse_duration_changed (GstMessage * msg,
    GstClockTime * duration)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_DURATION_CHANGED,
      GST_PLAY_MESSAGE_DATA_DURATION, GST_TYPE_CLOCK_TIME, duration);
}

/**
 * gst_play_message_parse_duration_updated:
 * @msg: A #GstMessage
 * @duration: (out) (optional): the resulting duration
 *
 * Parse the given duration-changed @msg and extract the corresponding #GstClockTime
 *
 * Since: 1.20
 *
 * Deprecated: 1.26: Use gst_play_message_parse_duration_changed().
 */
void
gst_play_message_parse_duration_updated (GstMessage * msg,
    GstClockTime * duration)
{
  gst_play_message_parse_duration_changed (msg, duration);
}

/**
 * gst_play_message_parse_position_updated:
 * @msg: A #GstMessage
 * @position: (out) (optional): the resulting position
 *
 * Parse the given position-updated @msg and extract the corresponding #GstClockTime
 *
 * Since: 1.20
 */
void
gst_play_message_parse_position_updated (GstMessage * msg,
    GstClockTime * position)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_POSITION_UPDATED,
      GST_PLAY_MESSAGE_DATA_POSITION, GST_TYPE_CLOCK_TIME, position);
}

/**
 * gst_play_message_parse_state_changed:
 * @msg: A #GstMessage
 * @state: (out) (optional): the resulting play state
 *
 * Parse the given state-changed @msg and extract the corresponding #GstPlayState
 *
 * Since: 1.20
 */
void
gst_play_message_parse_state_changed (GstMessage * msg, GstPlayState * state)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_STATE_CHANGED,
      GST_PLAY_MESSAGE_DATA_PLAY_STATE, GST_TYPE_PLAY_STATE, state);
}

/**
 * gst_play_message_parse_buffering:
 * @msg: A #GstMessage
 * @percent: (out) (optional): the resulting buffering percent
 *
 * Parse the given buffering @msg and extract the corresponding value
 *
 * Since: 1.26
 */
void
gst_play_message_parse_buffering (GstMessage * msg, guint * percent)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_BUFFERING,
      GST_PLAY_MESSAGE_DATA_BUFFERING_PERCENT, G_TYPE_UINT, percent);
}

/**
 * gst_play_message_parse_buffering_percent:
 * @msg: A #GstMessage
 * @percent: (out) (optional): the resulting buffering percent
 *
 * Parse the given buffering @msg and extract the corresponding value
 *
 * Since: 1.20
 *
 * Deprecated: 1.26: Use gst_play_message_parse_buffering().
 */
void
gst_play_message_parse_buffering_percent (GstMessage * msg, guint * percent)
{
  gst_play_message_parse_buffering (msg, percent);
}

/**
 * gst_play_message_parse_error:
 * @msg: A #GstMessage
 * @error: (out) (optional) (transfer full): the resulting error
 * @details: (out) (optional) (nullable) (transfer full): A #GstStructure containing additional details about the error
 *
 * Parse the given error @msg and extract the corresponding #GError.
 *
 * Since 1.26 the details will always contain the URI this refers to in an
 * "uri" field of type string, and (if known) the string "stream-id" it is
 * referring to.
 *
 * Since: 1.20
 */
void
gst_play_message_parse_error (GstMessage * msg, GError ** error,
    GstStructure ** details)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_ERROR, GST_PLAY_MESSAGE_DATA_ERROR,
      G_TYPE_ERROR, error);
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_ERROR,
      GST_PLAY_MESSAGE_DATA_ERROR_DETAILS, GST_TYPE_STRUCTURE, details);
}

static gboolean
gst_play_message_parse_missing_plugin (GstMessage * msg,
    GstPlayMessage msg_type, gchar *** descriptions,
    gchar *** installer_details)
{
  const GError *err;
  const GValue *v, *details_array;
  const GstStructure *s, *details;
  guint n_details;

  if (descriptions)
    *descriptions = NULL;
  if (installer_details)
    *installer_details = NULL;

  s = gst_message_get_structure (msg);

  v = gst_structure_get_value (s,
      msg_type ==
      GST_PLAY_MESSAGE_ERROR ? GST_PLAY_MESSAGE_DATA_ERROR :
      GST_PLAY_MESSAGE_DATA_WARNING);
  if (!v)
    return FALSE;
  err = g_value_get_boxed (v);
  if (!err)
    return FALSE;

  if (!g_error_matches (err, GST_PLAY_ERROR, GST_PLAY_ERROR_MISSING_PLUGIN))
    return FALSE;

  v = gst_structure_get_value (s,
      msg_type ==
      GST_PLAY_MESSAGE_ERROR ? GST_PLAY_MESSAGE_DATA_ERROR_DETAILS :
      GST_PLAY_MESSAGE_DATA_WARNING_DETAILS);
  if (!v)
    return FALSE;
  details = g_value_get_boxed (v);
  if (!details)
    return FALSE;

  details_array = gst_structure_get_value (details, "missing-plugin-details");

  n_details = gst_value_array_get_size (details_array);
  if (descriptions)
    *descriptions = g_new0 (gchar *, n_details + 1);
  if (installer_details)
    *installer_details = g_new0 (gchar *, n_details + 1);

  for (guint i = 0; i < n_details; i++) {
    const GValue *details_v = gst_value_array_get_value (details_array, i);
    const GstStructure *details_s = g_value_get_boxed (details_v);
    gchar *str;

    if (descriptions) {
      gst_structure_get (details_s, "description", G_TYPE_STRING, &str, NULL);
      (*descriptions)[i] = str;
    }

    if (installer_details) {
      gst_structure_get (details_s, "installer-details", G_TYPE_STRING, &str,
          NULL);
      (*installer_details)[i] = str;
    }
  }

  return TRUE;

}

/**
 * gst_play_message_parse_error_missing_plugin:
 * @msg: A #GstMessage
 * @descriptions: (out) (optional) (transfer full): a %NULL-terminated array of descriptions
 * @installer_details: (out) (optional) (nullable) (transfer full): a %NULL-terminated array of installer details
 *
 * Parses missing plugin descriptions and installer details from a
 * GST_PLAY_ERROR_MISSING_PLUGIN error message.
 *
 * Both arrays will have the same length, and strings at the same index
 * correspond to each other.
 *
 * The installer details can be passed to gst_install_plugins_sync() or
 * gst_install_plugins_async().
 *
 * Returns: %TRUE if the message contained a missing-plugin error.
 *
 * Since: 1.26
 */
gboolean
gst_play_message_parse_error_missing_plugin (GstMessage * msg,
    gchar *** descriptions, gchar *** installer_details)
{
  GstPlayMessage msg_type;

  gst_play_message_parse_type (msg, &msg_type);
  g_return_val_if_fail (msg_type == GST_PLAY_MESSAGE_ERROR, FALSE);

  return gst_play_message_parse_missing_plugin (msg, msg_type, descriptions,
      installer_details);
}

/**
 * gst_play_message_parse_warning:
 * @msg: A #GstMessage
 * @error: (out) (optional) (transfer full): the resulting warning
 * @details: (out) (optional) (nullable) (transfer full): A #GstStructure containing additional details about the warning
 *
 * Parse the given warning @msg and extract the corresponding #GError.
 *
 * Since 1.26 the details will always contain the URI this refers to in an
 * "uri" field of type string, and (if known) the string "stream-id" it is
 * referring to.
 *
 * Since: 1.20
 */
void
gst_play_message_parse_warning (GstMessage * msg, GError ** error,
    GstStructure ** details)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_WARNING,
      GST_PLAY_MESSAGE_DATA_WARNING, G_TYPE_ERROR, error);
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_WARNING,
      GST_PLAY_MESSAGE_DATA_WARNING_DETAILS, GST_TYPE_STRUCTURE, details);
}

/**
 * gst_play_message_parse_warning_missing_plugin:
 * @msg: A #GstMessage
 * @descriptions: (out) (optional) (transfer full): a %NULL-terminated array of descriptions
 * @installer_details: (out) (optional) (nullable) (transfer full): a %NULL-terminated array of installer details
 *
 * Parses missing plugin descriptions and installer details from a
 * GST_PLAY_ERROR_MISSING_PLUGIN warning message.
 *
 * Both arrays will have the same length, and strings at the same index
 * correspond to each other.
 *
 * The installer details can be passed to gst_install_plugins_sync() or
 * gst_install_plugins_async().
 *
 * Returns: %TRUE if the message contained a missing-plugin error.
 *
 * Since: 1.26
 */
gboolean
gst_play_message_parse_warning_missing_plugin (GstMessage * msg,
    gchar *** descriptions, gchar *** installer_details)
{
  GstPlayMessage msg_type;

  gst_play_message_parse_type (msg, &msg_type);
  g_return_val_if_fail (msg_type == GST_PLAY_MESSAGE_WARNING, FALSE);

  return gst_play_message_parse_missing_plugin (msg, msg_type, descriptions,
      installer_details);
}

/**
 * gst_play_message_parse_video_dimensions_changed:
 * @msg: A #GstMessage
 * @width: (out) (optional): the resulting video width
 * @height: (out) (optional): the resulting video height
 *
 * Parse the given video-dimensions-changed @msg and extract the corresponding video dimensions
 *
 * Since: 1.20
 */
void
gst_play_message_parse_video_dimensions_changed (GstMessage * msg,
    guint * width, guint * height)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_VIDEO_DIMENSIONS_CHANGED,
      GST_PLAY_MESSAGE_DATA_VIDEO_WIDTH, G_TYPE_UINT, width);
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_VIDEO_DIMENSIONS_CHANGED,
      GST_PLAY_MESSAGE_DATA_VIDEO_HEIGHT, G_TYPE_UINT, height);
}

/**
 * gst_play_message_parse_media_info_updated:
 * @msg: A #GstMessage
 * @info: (out) (optional) (transfer full): the resulting media info
 *
 * Parse the given media-info-updated @msg and extract the corresponding media information
 *
 * Since: 1.20
 */
void
gst_play_message_parse_media_info_updated (GstMessage * msg,
    GstPlayMediaInfo ** info)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_MEDIA_INFO_UPDATED,
      GST_PLAY_MESSAGE_DATA_MEDIA_INFO, GST_TYPE_PLAY_MEDIA_INFO, info);
}

/**
 * gst_play_message_parse_volume_changed:
 * @msg: A #GstMessage
 * @volume: (out) (optional): the resulting audio volume
 *
 * Parse the given volume-changed @msg and extract the corresponding audio volume
 *
 * Since: 1.20
 */
void
gst_play_message_parse_volume_changed (GstMessage * msg, gdouble * volume)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_VOLUME_CHANGED,
      GST_PLAY_MESSAGE_DATA_VOLUME, G_TYPE_DOUBLE, volume);
}

/**
 * gst_play_message_parse_muted_changed:
 * @msg: A #GstMessage
 * @muted: (out) (optional): the resulting audio muted state
 *
 * Parse the given mute-changed @msg and extract the corresponding audio muted state
 *
 * Since: 1.20
 */
void
gst_play_message_parse_muted_changed (GstMessage * msg, gboolean * muted)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_MUTE_CHANGED,
      GST_PLAY_MESSAGE_DATA_IS_MUTED, G_TYPE_BOOLEAN, muted);
}

/**
 * gst_play_message_parse_seek_done:
 * @msg: A #GstMessage
 * @position: (out) (optional): the resulting position
 *
 * Parse the given seek-done @msg and extract the corresponding #GstClockTime
 *
 * Since: 1.26
 */
void
gst_play_message_parse_seek_done (GstMessage * msg, GstClockTime * position)
{
  PARSE_MESSAGE_FIELD (msg, GST_PLAY_MESSAGE_SEEK_DONE,
      GST_PLAY_MESSAGE_DATA_POSITION, GST_TYPE_CLOCK_TIME, position);
}
