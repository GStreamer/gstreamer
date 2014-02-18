/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
 * SECTION:rtsp-media
 * @short_description: The media pipeline
 * @see_also: #GstRTSPMediaFactory, #GstRTSPStream, #GstRTSPSession,
 *     #GstRTSPSessionMedia
 *
 * a #GstRTSPMedia contains the complete GStreamer pipeline to manage the
 * streaming to the clients. The actual data transfer is done by the
 * #GstRTSPStream objects that are created and exposed by the #GstRTSPMedia.
 *
 * The #GstRTSPMedia is usually created from a #GstRTSPMediaFactory when the
 * client does a DESCRIBE or SETUP of a resource.
 *
 * A media is created with gst_rtsp_media_new() that takes the element that will
 * provide the streaming elements. For each of the streams, a new #GstRTSPStream
 * object needs to be made with the gst_rtsp_media_create_stream() which takes
 * the payloader element and the source pad that produces the RTP stream.
 *
 * The pipeline of the media is set to PAUSED with gst_rtsp_media_prepare(). The
 * prepare method will add rtpbin and sinks and sources to send and receive RTP
 * and RTCP packets from the clients. Each stream srcpad is connected to an
 * input into the internal rtpbin.
 *
 * It is also possible to dynamically create #GstRTSPStream objects during the
 * prepare phase. With gst_rtsp_media_get_status() you can check the status of
 * the prepare phase.
 *
 * After the media is prepared, it is ready for streaming. It will usually be
 * managed in a session with gst_rtsp_session_manage_media(). See
 * #GstRTSPSession and #GstRTSPSessionMedia.
 *
 * The state of the media can be controlled with gst_rtsp_media_set_state ().
 * Seeking can be done with gst_rtsp_media_seek().
 *
 * With gst_rtsp_media_unprepare() the pipeline is stopped and shut down. When
 * gst_rtsp_media_set_eos_shutdown() an EOS will be sent to the pipeline to
 * cleanly shut down.
 *
 * With gst_rtsp_media_set_shared(), the media can be shared between multiple
 * clients. With gst_rtsp_media_set_reusable() you can control if the pipeline
 * can be prepared again after an unprepare.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */

#include <string.h>
#include <stdlib.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include "rtsp-media.h"

#define GST_RTSP_MEDIA_GET_PRIVATE(obj)  \
     (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_MEDIA, GstRTSPMediaPrivate))

struct _GstRTSPMediaPrivate
{
  GMutex lock;
  GCond cond;

  /* protected by lock */
  GstRTSPPermissions *permissions;
  gboolean shared;
  gboolean suspend_mode;
  gboolean reusable;
  GstRTSPProfile profiles;
  GstRTSPLowerTrans protocols;
  gboolean reused;
  gboolean eos_shutdown;
  guint buffer_size;
  GstRTSPAddressPool *pool;
  gboolean blocked;

  GstElement *element;
  GRecMutex state_lock;         /* locking order: state lock, lock */
  GPtrArray *streams;           /* protected by lock */
  GList *dynamic;               /* protected by lock */
  GstRTSPMediaStatus status;    /* protected by lock */
  gint prepare_count;
  gint n_active;
  gboolean adding;

  /* the pipeline for the media */
  GstElement *pipeline;
  GstElement *fakesink;         /* protected by lock */
  GSource *source;
  guint id;
  GstRTSPThread *thread;

  gboolean time_provider;
  GstNetTimeProvider *nettime;

  gboolean is_live;
  gboolean seekable;
  gboolean buffering;
  GstState target_state;

  /* RTP session manager */
  GstElement *rtpbin;

  /* the range of media */
  GstRTSPTimeRange range;       /* protected by lock */
  GstClockTime range_start;
  GstClockTime range_stop;
};

#define DEFAULT_SHARED          FALSE
#define DEFAULT_SUSPEND_MODE    GST_RTSP_SUSPEND_MODE_NONE
#define DEFAULT_REUSABLE        FALSE
#define DEFAULT_PROFILES        GST_RTSP_PROFILE_AVP
#define DEFAULT_PROTOCOLS       GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | \
                                        GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_EOS_SHUTDOWN    FALSE
#define DEFAULT_BUFFER_SIZE     0x80000
#define DEFAULT_TIME_PROVIDER   FALSE

/* define to dump received RTCP packets */
#undef DUMP_STATS

enum
{
  PROP_0,
  PROP_SHARED,
  PROP_SUSPEND_MODE,
  PROP_REUSABLE,
  PROP_PROFILES,
  PROP_PROTOCOLS,
  PROP_EOS_SHUTDOWN,
  PROP_BUFFER_SIZE,
  PROP_ELEMENT,
  PROP_TIME_PROVIDER,
  PROP_LAST
};

enum
{
  SIGNAL_NEW_STREAM,
  SIGNAL_REMOVED_STREAM,
  SIGNAL_PREPARED,
  SIGNAL_UNPREPARED,
  SIGNAL_TARGET_STATE,
  SIGNAL_NEW_STATE,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_media_debug);
#define GST_CAT_DEFAULT rtsp_media_debug

static void gst_rtsp_media_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_finalize (GObject * obj);

static gboolean default_handle_message (GstRTSPMedia * media,
    GstMessage * message);
static void finish_unprepare (GstRTSPMedia * media);
static gboolean default_unprepare (GstRTSPMedia * media);
static gboolean default_convert_range (GstRTSPMedia * media,
    GstRTSPTimeRange * range, GstRTSPRangeUnit unit);
static gboolean default_query_position (GstRTSPMedia * media,
    gint64 * position);
static gboolean default_query_stop (GstRTSPMedia * media, gint64 * stop);
static GstElement *default_create_rtpbin (GstRTSPMedia * media);
static gboolean default_setup_sdp (GstRTSPMedia * media, GstSDPMessage * sdp,
    GstSDPInfo * info);

static gboolean wait_preroll (GstRTSPMedia * media);

static guint gst_rtsp_media_signals[SIGNAL_LAST] = { 0 };

#define C_ENUM(v) ((gint) v)

#define GST_TYPE_RTSP_SUSPEND_MODE (gst_rtsp_suspend_mode_get_type())
GType
gst_rtsp_suspend_mode_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_RTSP_SUSPEND_MODE_NONE), "GST_RTSP_SUSPEND_MODE_NONE", "none"},
    {C_ENUM (GST_RTSP_SUSPEND_MODE_PAUSE), "GST_RTSP_SUSPEND_MODE_PAUSE",
        "pause"},
    {C_ENUM (GST_RTSP_SUSPEND_MODE_RESET), "GST_RTSP_SUSPEND_MODE_RESET",
        "reset"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstRTSPSuspendMode", values);
    g_once_init_leave (&id, tmp);
  }
  return (GType) id;
}

G_DEFINE_TYPE (GstRTSPMedia, gst_rtsp_media, G_TYPE_OBJECT);

static void
gst_rtsp_media_class_init (GstRTSPMediaClass * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (GstRTSPMediaPrivate));

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_media_get_property;
  gobject_class->set_property = gst_rtsp_media_set_property;
  gobject_class->finalize = gst_rtsp_media_finalize;

  g_object_class_install_property (gobject_class, PROP_SHARED,
      g_param_spec_boolean ("shared", "Shared",
          "If this media pipeline can be shared", DEFAULT_SHARED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUSPEND_MODE,
      g_param_spec_enum ("suspend-mode", "Suspend Mode",
          "How to suspend the media in PAUSED", GST_TYPE_RTSP_SUSPEND_MODE,
          DEFAULT_SUSPEND_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REUSABLE,
      g_param_spec_boolean ("reusable", "Reusable",
          "If this media pipeline can be reused after an unprepare",
          DEFAULT_REUSABLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROFILES,
      g_param_spec_flags ("profiles", "Profiles",
          "Allowed transfer profiles", GST_TYPE_RTSP_PROFILE,
          DEFAULT_PROFILES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols",
          "Allowed lower transport protocols", GST_TYPE_RTSP_LOWER_TRANS,
          DEFAULT_PROTOCOLS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EOS_SHUTDOWN,
      g_param_spec_boolean ("eos-shutdown", "EOS Shutdown",
          "Send an EOS event to the pipeline before unpreparing",
          DEFAULT_EOS_SHUTDOWN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "Buffer Size",
          "The kernel UDP buffer size to use", 0, G_MAXUINT,
          DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ELEMENT,
      g_param_spec_object ("element", "The Element",
          "The GstBin to use for streaming the media", GST_TYPE_ELEMENT,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TIME_PROVIDER,
      g_param_spec_boolean ("time-provider", "Time Provider",
          "Use a NetTimeProvider for clients",
          DEFAULT_TIME_PROVIDER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_media_signals[SIGNAL_NEW_STREAM] =
      g_signal_new ("new-stream", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, new_stream), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 1, GST_TYPE_RTSP_STREAM);

  gst_rtsp_media_signals[SIGNAL_REMOVED_STREAM] =
      g_signal_new ("removed-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaClass, removed_stream),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1,
      GST_TYPE_RTSP_STREAM);

  gst_rtsp_media_signals[SIGNAL_PREPARED] =
      g_signal_new ("prepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, prepared), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_media_signals[SIGNAL_UNPREPARED] =
      g_signal_new ("unprepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, unprepared), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_media_signals[SIGNAL_TARGET_STATE] =
      g_signal_new ("target-state", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaClass, new_state), NULL,
      NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  gst_rtsp_media_signals[SIGNAL_NEW_STATE] =
      g_signal_new ("new-state", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, new_state), NULL, NULL,
      g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  GST_DEBUG_CATEGORY_INIT (rtsp_media_debug, "rtspmedia", 0, "GstRTSPMedia");

  klass->handle_message = default_handle_message;
  klass->unprepare = default_unprepare;
  klass->convert_range = default_convert_range;
  klass->query_position = default_query_position;
  klass->query_stop = default_query_stop;
  klass->create_rtpbin = default_create_rtpbin;
  klass->setup_sdp = default_setup_sdp;
}

static void
gst_rtsp_media_init (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = GST_RTSP_MEDIA_GET_PRIVATE (media);

  media->priv = priv;

  priv->streams = g_ptr_array_new_with_free_func (g_object_unref);
  g_mutex_init (&priv->lock);
  g_cond_init (&priv->cond);
  g_rec_mutex_init (&priv->state_lock);

  priv->shared = DEFAULT_SHARED;
  priv->suspend_mode = DEFAULT_SUSPEND_MODE;
  priv->reusable = DEFAULT_REUSABLE;
  priv->profiles = DEFAULT_PROFILES;
  priv->protocols = DEFAULT_PROTOCOLS;
  priv->eos_shutdown = DEFAULT_EOS_SHUTDOWN;
  priv->buffer_size = DEFAULT_BUFFER_SIZE;
  priv->time_provider = DEFAULT_TIME_PROVIDER;
}

static void
gst_rtsp_media_finalize (GObject * obj)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPMedia *media;

  media = GST_RTSP_MEDIA (obj);
  priv = media->priv;

  GST_INFO ("finalize media %p", media);

  if (priv->permissions)
    gst_rtsp_permissions_unref (priv->permissions);

  g_ptr_array_unref (priv->streams);

  g_list_free_full (priv->dynamic, gst_object_unref);

  if (priv->pipeline)
    gst_object_unref (priv->pipeline);
  if (priv->nettime)
    gst_object_unref (priv->nettime);
  gst_object_unref (priv->element);
  if (priv->pool)
    g_object_unref (priv->pool);
  g_mutex_clear (&priv->lock);
  g_cond_clear (&priv->cond);
  g_rec_mutex_clear (&priv->state_lock);

  G_OBJECT_CLASS (gst_rtsp_media_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPMedia *media = GST_RTSP_MEDIA (object);

  switch (propid) {
    case PROP_ELEMENT:
      g_value_set_object (value, media->priv->element);
      break;
    case PROP_SHARED:
      g_value_set_boolean (value, gst_rtsp_media_is_shared (media));
      break;
    case PROP_SUSPEND_MODE:
      g_value_set_enum (value, gst_rtsp_media_get_suspend_mode (media));
      break;
    case PROP_REUSABLE:
      g_value_set_boolean (value, gst_rtsp_media_is_reusable (media));
      break;
    case PROP_PROFILES:
      g_value_set_flags (value, gst_rtsp_media_get_profiles (media));
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, gst_rtsp_media_get_protocols (media));
      break;
    case PROP_EOS_SHUTDOWN:
      g_value_set_boolean (value, gst_rtsp_media_is_eos_shutdown (media));
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value, gst_rtsp_media_get_buffer_size (media));
      break;
    case PROP_TIME_PROVIDER:
      g_value_set_boolean (value, gst_rtsp_media_is_time_provider (media));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_media_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPMedia *media = GST_RTSP_MEDIA (object);

  switch (propid) {
    case PROP_ELEMENT:
      media->priv->element = g_value_get_object (value);
      gst_object_ref_sink (media->priv->element);
      break;
    case PROP_SHARED:
      gst_rtsp_media_set_shared (media, g_value_get_boolean (value));
      break;
    case PROP_SUSPEND_MODE:
      gst_rtsp_media_set_suspend_mode (media, g_value_get_enum (value));
      break;
    case PROP_REUSABLE:
      gst_rtsp_media_set_reusable (media, g_value_get_boolean (value));
      break;
    case PROP_PROFILES:
      gst_rtsp_media_set_profiles (media, g_value_get_flags (value));
      break;
    case PROP_PROTOCOLS:
      gst_rtsp_media_set_protocols (media, g_value_get_flags (value));
      break;
    case PROP_EOS_SHUTDOWN:
      gst_rtsp_media_set_eos_shutdown (media, g_value_get_boolean (value));
      break;
    case PROP_BUFFER_SIZE:
      gst_rtsp_media_set_buffer_size (media, g_value_get_uint (value));
      break;
    case PROP_TIME_PROVIDER:
      gst_rtsp_media_use_time_provider (media, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static gboolean
default_query_position (GstRTSPMedia * media, gint64 * position)
{
  return gst_element_query_position (media->priv->pipeline, GST_FORMAT_TIME,
      position);
}

static gboolean
default_query_stop (GstRTSPMedia * media, gint64 * stop)
{
  GstQuery *query;
  gboolean res;

  query = gst_query_new_segment (GST_FORMAT_TIME);
  if ((res = gst_element_query (media->priv->pipeline, query))) {
    GstFormat format;
    gst_query_parse_segment (query, NULL, &format, NULL, stop);
    if (format != GST_FORMAT_TIME)
      *stop = -1;
  }
  gst_query_unref (query);
  return res;
}

static GstElement *
default_create_rtpbin (GstRTSPMedia * media)
{
  GstElement *rtpbin;

  rtpbin = gst_element_factory_make ("rtpbin", NULL);

  return rtpbin;
}

/* must be called with state lock */
static void
collect_media_stats (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gint64 position, stop;

  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED &&
      priv->status != GST_RTSP_MEDIA_STATUS_PREPARING)
    return;

  priv->range.unit = GST_RTSP_RANGE_NPT;

  GST_INFO ("collect media stats");

  if (priv->is_live) {
    priv->range.min.type = GST_RTSP_TIME_NOW;
    priv->range.min.seconds = -1;
    priv->range_start = -1;
    priv->range.max.type = GST_RTSP_TIME_END;
    priv->range.max.seconds = -1;
    priv->range_stop = -1;
  } else {
    GstRTSPMediaClass *klass;
    gboolean ret;

    klass = GST_RTSP_MEDIA_GET_CLASS (media);

    /* get the position */
    ret = FALSE;
    if (klass->query_position)
      ret = klass->query_position (media, &position);

    if (!ret) {
      GST_INFO ("position query failed");
      position = 0;
    }

    /* get the current segment stop */
    ret = FALSE;
    if (klass->query_stop)
      ret = klass->query_stop (media, &stop);

    if (!ret) {
      GST_INFO ("stop query failed");
      stop = -1;
    }

    GST_INFO ("stats: position %" GST_TIME_FORMAT ", stop %"
        GST_TIME_FORMAT, GST_TIME_ARGS (position), GST_TIME_ARGS (stop));

    if (position == -1) {
      priv->range.min.type = GST_RTSP_TIME_NOW;
      priv->range.min.seconds = -1;
      priv->range_start = -1;
    } else {
      priv->range.min.type = GST_RTSP_TIME_SECONDS;
      priv->range.min.seconds = ((gdouble) position) / GST_SECOND;
      priv->range_start = position;
    }
    if (stop == -1) {
      priv->range.max.type = GST_RTSP_TIME_END;
      priv->range.max.seconds = -1;
      priv->range_stop = -1;
    } else {
      priv->range.max.type = GST_RTSP_TIME_SECONDS;
      priv->range.max.seconds = ((gdouble) stop) / GST_SECOND;
      priv->range_stop = stop;
    }
  }
}

/**
 * gst_rtsp_media_new:
 * @element: (transfer full): a #GstElement
 *
 * Create a new #GstRTSPMedia instance. @element is the bin element that
 * provides the different streams. The #GstRTSPMedia object contains the
 * element to produce RTP data for one or more related (audio/video/..)
 * streams.
 *
 * Ownership is taken of @element.
 *
 * Returns: a new #GstRTSPMedia object.
 */
GstRTSPMedia *
gst_rtsp_media_new (GstElement * element)
{
  GstRTSPMedia *result;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);

  result = g_object_new (GST_TYPE_RTSP_MEDIA, "element", element, NULL);

  return result;
}

/**
 * gst_rtsp_media_get_element:
 * @media: a #GstRTSPMedia
 *
 * Get the element that was used when constructing @media.
 *
 * Returns: (transfer full): a #GstElement. Unref after usage.
 */
GstElement *
gst_rtsp_media_get_element (GstRTSPMedia * media)
{
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  return gst_object_ref (media->priv->element);
}

/**
 * gst_rtsp_media_take_pipeline:
 * @media: a #GstRTSPMedia
 * @pipeline: (transfer full): a #GstPipeline
 *
 * Set @pipeline as the #GstPipeline for @media. Ownership is
 * taken of @pipeline.
 */
void
gst_rtsp_media_take_pipeline (GstRTSPMedia * media, GstPipeline * pipeline)
{
  GstRTSPMediaPrivate *priv;
  GstElement *old;
  GstNetTimeProvider *nettime;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  old = priv->pipeline;
  priv->pipeline = GST_ELEMENT_CAST (pipeline);
  nettime = priv->nettime;
  priv->nettime = NULL;
  g_mutex_unlock (&priv->lock);

  if (old)
    gst_object_unref (old);

  if (nettime)
    gst_object_unref (nettime);

  gst_bin_add (GST_BIN_CAST (pipeline), priv->element);
}

/**
 * gst_rtsp_media_set_permissions:
 * @media: a #GstRTSPMedia
 * @permissions: a #GstRTSPPermissions
 *
 * Set @permissions on @media.
 */
void
gst_rtsp_media_set_permissions (GstRTSPMedia * media,
    GstRTSPPermissions * permissions)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  if (priv->permissions)
    gst_rtsp_permissions_unref (priv->permissions);
  if ((priv->permissions = permissions))
    gst_rtsp_permissions_ref (permissions);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_permissions:
 * @media: a #GstRTSPMedia
 *
 * Get the permissions object from @media.
 *
 * Returns: (transfer full): a #GstRTSPPermissions object, unref after usage.
 */
GstRTSPPermissions *
gst_rtsp_media_get_permissions (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPPermissions *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->permissions))
    gst_rtsp_permissions_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_media_set_suspend_mode:
 * @media: a #GstRTSPMedia
 * @mode: the new #GstRTSPSuspendMode
 *
 * Control how @ media will be suspended after the SDP has been generated and
 * after a PAUSE request has been performed.
 *
 * Media must be unprepared when setting the suspend mode.
 */
void
gst_rtsp_media_set_suspend_mode (GstRTSPMedia * media, GstRTSPSuspendMode mode)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARED)
    goto was_prepared;
  priv->suspend_mode = mode;
  g_rec_mutex_unlock (&priv->state_lock);

  return;

  /* ERRORS */
was_prepared:
  {
    GST_WARNING ("media %p was prepared", media);
    g_rec_mutex_unlock (&priv->state_lock);
  }
}

/**
 * gst_rtsp_media_get_suspend_mode:
 * @media: a #GstRTSPMedia
 *
 * Get how @media will be suspended.
 *
 * Returns: #GstRTSPSuspendMode.
 */
GstRTSPSuspendMode
gst_rtsp_media_get_suspend_mode (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPSuspendMode res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), GST_RTSP_SUSPEND_MODE_NONE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  res = priv->suspend_mode;
  g_rec_mutex_unlock (&priv->state_lock);

  return res;
}

/**
 * gst_rtsp_media_set_shared:
 * @media: a #GstRTSPMedia
 * @shared: the new value
 *
 * Set or unset if the pipeline for @media can be shared will multiple clients.
 * When @shared is %TRUE, client requests for this media will share the media
 * pipeline.
 */
void
gst_rtsp_media_set_shared (GstRTSPMedia * media, gboolean shared)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->shared = shared;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_is_shared:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media can be shared between multiple clients.
 *
 * Returns: %TRUE if the media can be shared between clients.
 */
gboolean
gst_rtsp_media_is_shared (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->shared;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_reusable:
 * @media: a #GstRTSPMedia
 * @reusable: the new value
 *
 * Set or unset if the pipeline for @media can be reused after the pipeline has
 * been unprepared.
 */
void
gst_rtsp_media_set_reusable (GstRTSPMedia * media, gboolean reusable)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->reusable = reusable;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_is_reusable:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media can be reused after an unprepare.
 *
 * Returns: %TRUE if the media can be reused
 */
gboolean
gst_rtsp_media_is_reusable (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->reusable;
  g_mutex_unlock (&priv->lock);

  return res;
}

static void
do_set_profiles (GstRTSPStream * stream, GstRTSPProfile * profiles)
{
  gst_rtsp_stream_set_profiles (stream, *profiles);
}

/**
 * gst_rtsp_media_set_profiles:
 * @media: a #GstRTSPMedia
 * @profiles: the new flags
 *
 * Configure the allowed lower transport for @media.
 */
void
gst_rtsp_media_set_profiles (GstRTSPMedia * media, GstRTSPProfile profiles)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->profiles = profiles;
  g_ptr_array_foreach (priv->streams, (GFunc) do_set_profiles, &profiles);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_profiles:
 * @media: a #GstRTSPMedia
 *
 * Get the allowed profiles of @media.
 *
 * Returns: a #GstRTSPProfile
 */
GstRTSPProfile
gst_rtsp_media_get_profiles (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPProfile res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), GST_RTSP_PROFILE_UNKNOWN);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->profiles;
  g_mutex_unlock (&priv->lock);

  return res;
}

static void
do_set_protocols (GstRTSPStream * stream, GstRTSPLowerTrans * protocols)
{
  gst_rtsp_stream_set_protocols (stream, *protocols);
}

/**
 * gst_rtsp_media_set_protocols:
 * @media: a #GstRTSPMedia
 * @protocols: the new flags
 *
 * Configure the allowed lower transport for @media.
 */
void
gst_rtsp_media_set_protocols (GstRTSPMedia * media, GstRTSPLowerTrans protocols)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->protocols = protocols;
  g_ptr_array_foreach (priv->streams, (GFunc) do_set_protocols, &protocols);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_protocols:
 * @media: a #GstRTSPMedia
 *
 * Get the allowed protocols of @media.
 *
 * Returns: a #GstRTSPLowerTrans
 */
GstRTSPLowerTrans
gst_rtsp_media_get_protocols (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPLowerTrans res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media),
      GST_RTSP_LOWER_TRANS_UNKNOWN);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->protocols;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_eos_shutdown:
 * @media: a #GstRTSPMedia
 * @eos_shutdown: the new value
 *
 * Set or unset if an EOS event will be sent to the pipeline for @media before
 * it is unprepared.
 */
void
gst_rtsp_media_set_eos_shutdown (GstRTSPMedia * media, gboolean eos_shutdown)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->eos_shutdown = eos_shutdown;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_is_eos_shutdown:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media will send an EOS down the pipeline before
 * unpreparing.
 *
 * Returns: %TRUE if the media will send EOS before unpreparing.
 */
gboolean
gst_rtsp_media_is_eos_shutdown (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->eos_shutdown;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_buffer_size:
 * @media: a #GstRTSPMedia
 * @size: the new value
 *
 * Set the kernel UDP buffer size.
 */
void
gst_rtsp_media_set_buffer_size (GstRTSPMedia * media, guint size)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  GST_LOG_OBJECT (media, "set buffer size %u", size);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->buffer_size = size;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_buffer_size:
 * @media: a #GstRTSPMedia
 *
 * Get the kernel UDP buffer size.
 *
 * Returns: the kernel UDP buffer size.
 */
guint
gst_rtsp_media_get_buffer_size (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  guint res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_unlock (&priv->lock);
  res = priv->buffer_size;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_use_time_provider:
 * @media: a #GstRTSPMedia
 * @time_provider: if a #GstNetTimeProvider should be used
 *
 * Set @media to provide a #GstNetTimeProvider.
 */
void
gst_rtsp_media_use_time_provider (GstRTSPMedia * media, gboolean time_provider)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->time_provider = time_provider;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_is_time_provider:
 * @media: a #GstRTSPMedia
 *
 * Check if @media can provide a #GstNetTimeProvider for its pipeline clock.
 *
 * Use gst_rtsp_media_get_time_provider() to get the network clock.
 *
 * Returns: %TRUE if @media can provide a #GstNetTimeProvider.
 */
gboolean
gst_rtsp_media_is_time_provider (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_unlock (&priv->lock);
  res = priv->time_provider;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_address_pool:
 * @media: a #GstRTSPMedia
 * @pool: a #GstRTSPAddressPool
 *
 * configure @pool to be used as the address pool of @media.
 */
void
gst_rtsp_media_set_address_pool (GstRTSPMedia * media,
    GstRTSPAddressPool * pool)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPAddressPool *old;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  GST_LOG_OBJECT (media, "set address pool %p", pool);

  g_mutex_lock (&priv->lock);
  if ((old = priv->pool) != pool)
    priv->pool = pool ? g_object_ref (pool) : NULL;
  else
    old = NULL;
  g_ptr_array_foreach (priv->streams, (GFunc) gst_rtsp_stream_set_address_pool,
      pool);
  g_mutex_unlock (&priv->lock);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_media_get_address_pool:
 * @media: a #GstRTSPMedia
 *
 * Get the #GstRTSPAddressPool used as the address pool of @media.
 *
 * Returns: (transfer full): the #GstRTSPAddressPool of @media. g_object_unref() after
 * usage.
 */
GstRTSPAddressPool *
gst_rtsp_media_get_address_pool (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPAddressPool *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->pool))
    g_object_ref (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_media_collect_streams:
 * @media: a #GstRTSPMedia
 *
 * Find all payloader elements, they should be named pay\%d in the
 * element of @media, and create #GstRTSPStreams for them.
 *
 * Collect all dynamic elements, named dynpay\%d, and add them to
 * the list of dynamic elements.
 */
void
gst_rtsp_media_collect_streams (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstElement *element, *elem;
  GstPad *pad;
  gint i;
  gboolean have_elem;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;
  element = priv->element;

  have_elem = TRUE;
  for (i = 0; have_elem; i++) {
    gchar *name;

    have_elem = FALSE;

    name = g_strdup_printf ("pay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      GST_INFO ("found stream %d with payloader %p", i, elem);

      /* take the pad of the payloader */
      pad = gst_element_get_static_pad (elem, "src");
      /* create the stream */
      gst_rtsp_media_create_stream (media, elem, pad);
      gst_object_unref (pad);
      gst_object_unref (elem);

      have_elem = TRUE;
    }
    g_free (name);

    name = g_strdup_printf ("dynpay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      /* a stream that will dynamically create pads to provide RTP packets */

      GST_INFO ("found dynamic element %d, %p", i, elem);

      g_mutex_lock (&priv->lock);
      priv->dynamic = g_list_prepend (priv->dynamic, elem);
      g_mutex_unlock (&priv->lock);

      have_elem = TRUE;
    }
    g_free (name);
  }
}

/**
 * gst_rtsp_media_create_stream:
 * @media: a #GstRTSPMedia
 * @payloader: a #GstElement
 * @srcpad: a source #GstPad
 *
 * Create a new stream in @media that provides RTP data on @srcpad.
 * @srcpad should be a pad of an element inside @media->element.
 *
 * Returns: (transfer none): a new #GstRTSPStream that remains valid for as long
 *          as @media exists.
 */
GstRTSPStream *
gst_rtsp_media_create_stream (GstRTSPMedia * media, GstElement * payloader,
    GstPad * pad)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPStream *stream;
  GstPad *srcpad;
  gchar *name;
  gint idx;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (payloader), NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), NULL);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  idx = priv->streams->len;

  GST_DEBUG ("media %p: creating stream with index %d", media, idx);

  name = g_strdup_printf ("src_%u", idx);
  srcpad = gst_ghost_pad_new (name, pad);
  gst_pad_set_active (srcpad, TRUE);
  gst_element_add_pad (priv->element, srcpad);
  g_free (name);

  stream = gst_rtsp_stream_new (idx, payloader, srcpad);
  if (priv->pool)
    gst_rtsp_stream_set_address_pool (stream, priv->pool);
  gst_rtsp_stream_set_protocols (stream, priv->protocols);

  g_ptr_array_add (priv->streams, stream);
  g_mutex_unlock (&priv->lock);

  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_NEW_STREAM], 0, stream,
      NULL);

  return stream;
}

static void
gst_rtsp_media_remove_stream (GstRTSPMedia * media, GstRTSPStream * stream)
{
  GstRTSPMediaPrivate *priv;
  GstPad *srcpad;

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  /* remove the ghostpad */
  srcpad = gst_rtsp_stream_get_srcpad (stream);
  gst_element_remove_pad (priv->element, srcpad);
  gst_object_unref (srcpad);
  /* now remove the stream */
  g_object_ref (stream);
  g_ptr_array_remove (priv->streams, stream);
  g_mutex_unlock (&priv->lock);

  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_REMOVED_STREAM], 0,
      stream, NULL);

  g_object_unref (stream);
}

/**
 * gst_rtsp_media_n_streams:
 * @media: a #GstRTSPMedia
 *
 * Get the number of streams in this media.
 *
 * Returns: The number of streams.
 */
guint
gst_rtsp_media_n_streams (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  guint res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), 0);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->streams->len;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_get_stream:
 * @media: a #GstRTSPMedia
 * @idx: the stream index
 *
 * Retrieve the stream with index @idx from @media.
 *
 * Returns: (transfer none): the #GstRTSPStream at index @idx or %NULL when a stream with
 * that index did not exist.
 */
GstRTSPStream *
gst_rtsp_media_get_stream (GstRTSPMedia * media, guint idx)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPStream *res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  if (idx < priv->streams->len)
    res = g_ptr_array_index (priv->streams, idx);
  else
    res = NULL;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_find_stream:
 * @media: a #GstRTSPMedia
 * @control: the control of the stream
 *
 * Find a stream in @media with @control as the control uri.
 *
 * Returns: (transfer none): the #GstRTSPStream with control uri @control
 * or %NULL when a stream with that control did not exist.
 */
GstRTSPStream *
gst_rtsp_media_find_stream (GstRTSPMedia * media, const gchar * control)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPStream *res;
  gint i;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (control != NULL, NULL);

  priv = media->priv;

  res = NULL;

  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *test;

    test = g_ptr_array_index (priv->streams, i);
    if (gst_rtsp_stream_has_control (test, control)) {
      res = test;
      break;
    }
  }
  g_mutex_unlock (&priv->lock);

  return res;
}

/* called with state-lock */
static gboolean
default_convert_range (GstRTSPMedia * media, GstRTSPTimeRange * range,
    GstRTSPRangeUnit unit)
{
  return gst_rtsp_range_convert_units (range, unit);
}

/**
 * gst_rtsp_media_get_range_string:
 * @media: a #GstRTSPMedia
 * @play: for the PLAY request
 * @unit: the unit to use for the string
 *
 * Get the current range as a string. @media must be prepared with
 * gst_rtsp_media_prepare ().
 *
 * Returns: The range as a string, g_free() after usage.
 */
gchar *
gst_rtsp_media_get_range_string (GstRTSPMedia * media, gboolean play,
    GstRTSPRangeUnit unit)
{
  GstRTSPMediaClass *klass;
  GstRTSPMediaPrivate *priv;
  gchar *result;
  GstRTSPTimeRange range;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (klass->convert_range != NULL, FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED &&
      priv->status != GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto not_prepared;

  g_mutex_lock (&priv->lock);

  /* Update the range value with current position/duration */
  collect_media_stats (media);

  /* make copy */
  range = priv->range;

  if (!play && priv->n_active > 0) {
    range.min.type = GST_RTSP_TIME_NOW;
    range.min.seconds = -1;
  }
  g_mutex_unlock (&priv->lock);
  g_rec_mutex_unlock (&priv->state_lock);

  if (!klass->convert_range (media, &range, unit))
    goto conversion_failed;

  result = gst_rtsp_range_to_string (&range);

  return result;

  /* ERRORS */
not_prepared:
  {
    GST_WARNING ("media %p was not prepared", media);
    g_rec_mutex_unlock (&priv->state_lock);
    return NULL;
  }
conversion_failed:
  {
    GST_WARNING ("range conversion to unit %d failed", unit);
    return NULL;
  }
}

static void
stream_update_blocked (GstRTSPStream * stream, GstRTSPMedia * media)
{
  gst_rtsp_stream_set_blocked (stream, media->priv->blocked);
}

static void
media_streams_set_blocked (GstRTSPMedia * media, gboolean blocked)
{
  GstRTSPMediaPrivate *priv = media->priv;

  GST_DEBUG ("media %p set blocked %d", media, blocked);
  priv->blocked = blocked;
  g_ptr_array_foreach (priv->streams, (GFunc) stream_update_blocked, media);
}

/**
 * gst_rtsp_media_seek:
 * @media: a #GstRTSPMedia
 * @range: a #GstRTSPTimeRange
 *
 * Seek the pipeline of @media to @range. @media must be prepared with
 * gst_rtsp_media_prepare().
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_seek (GstRTSPMedia * media, GstRTSPTimeRange * range)
{
  GstRTSPMediaClass *klass;
  GstRTSPMediaPrivate *priv;
  gboolean res;
  GstClockTime start, stop;
  GstSeekType start_type, stop_type;
  GstQuery *query;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (range != NULL, FALSE);
  g_return_val_if_fail (klass->convert_range != NULL, FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED)
    goto not_prepared;

  /* Update the seekable state of the pipeline in case it changed */
  query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (gst_element_query (priv->pipeline, query)) {
    GstFormat format;
    gboolean seekable;
    gint64 start, end;

    gst_query_parse_seeking (query, &format, &seekable, &start, &end);
    priv->seekable = seekable;
  }
  gst_query_unref (query);

  if (!priv->seekable)
    goto not_seekable;

  start_type = stop_type = GST_SEEK_TYPE_NONE;

  if (!klass->convert_range (media, range, GST_RTSP_RANGE_NPT))
    goto not_supported;
  gst_rtsp_range_get_times (range, &start, &stop);

  GST_INFO ("got %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
  GST_INFO ("current %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
      GST_TIME_ARGS (priv->range_start), GST_TIME_ARGS (priv->range_stop));

  if (start != GST_CLOCK_TIME_NONE)
    start_type = GST_SEEK_TYPE_SET;

  if (priv->range_stop == stop)
    stop = GST_CLOCK_TIME_NONE;
  else if (stop != GST_CLOCK_TIME_NONE)
    stop_type = GST_SEEK_TYPE_SET;

  if (start != GST_CLOCK_TIME_NONE || stop != GST_CLOCK_TIME_NONE) {
    GstSeekFlags flags;

    GST_INFO ("seeking to %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

    priv->status = GST_RTSP_MEDIA_STATUS_PREPARING;
    if (priv->blocked)
      media_streams_set_blocked (media, TRUE);

    /* depends on the current playing state of the pipeline. We might need to
     * queue this until we get EOS. */
    flags = GST_SEEK_FLAG_FLUSH;

    /* if range start was not supplied we must continue from current position.
     * but since we're doing a flushing seek, let us query the current position
     * so we end up at exactly the same position after the seek. */
    if (range->min.type == GST_RTSP_TIME_END) { /* Yepp, that's right! */
      gint64 position;
      gboolean ret = FALSE;

      if (klass->query_position)
        ret = klass->query_position (media, &position);

      if (!ret) {
        GST_WARNING ("position query failed");
      } else {
        GST_DEBUG ("doing accurate seek to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (position));
        start = position;
        start_type = GST_SEEK_TYPE_SET;
        flags |= GST_SEEK_FLAG_ACCURATE;
      }
    } else {
      /* only set keyframe flag when modifying start */
      if (start_type != GST_SEEK_TYPE_NONE)
        flags |= GST_SEEK_FLAG_KEY_UNIT;
    }

    /* FIXME, we only do forwards */
    res = gst_element_seek (priv->pipeline, 1.0, GST_FORMAT_TIME,
        flags, start_type, start, stop_type, stop);

    /* and block for the seek to complete */
    GST_INFO ("done seeking %d", res);
    g_rec_mutex_unlock (&priv->state_lock);

    /* wait until pipeline is prerolled again, this will also collect stats */
    if (!wait_preroll (media))
      goto preroll_failed;

    g_rec_mutex_lock (&priv->state_lock);
    GST_INFO ("prerolled again");
  } else {
    GST_INFO ("no seek needed");
    res = TRUE;
  }
  g_rec_mutex_unlock (&priv->state_lock);

  return res;

  /* ERRORS */
not_prepared:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("media %p is not prepared", media);
    return FALSE;
  }
not_seekable:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("pipeline is not seekable");
    return FALSE;
  }
not_supported:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("conversion to npt not supported");
    return FALSE;
  }
preroll_failed:
  {
    GST_WARNING ("failed to preroll after seek");
    return FALSE;
  }
}

static void
gst_rtsp_media_set_status (GstRTSPMedia * media, GstRTSPMediaStatus status)
{
  GstRTSPMediaPrivate *priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->status = status;
  GST_DEBUG ("setting new status to %d", status);
  g_cond_broadcast (&priv->cond);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_status:
 * @media: a #GstRTSPMedia
 *
 * Get the status of @media. When @media is busy preparing, this function waits
 * until @media is prepared or in error.
 *
 * Returns: the status of @media.
 */
GstRTSPMediaStatus
gst_rtsp_media_get_status (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPMediaStatus result;
  gint64 end_time;

  g_mutex_lock (&priv->lock);
  end_time = g_get_monotonic_time () + 20 * G_TIME_SPAN_SECOND;
  /* while we are preparing, wait */
  while (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING) {
    GST_DEBUG ("waiting for status change");
    if (!g_cond_wait_until (&priv->cond, &priv->lock, end_time)) {
      GST_DEBUG ("timeout, assuming error status");
      priv->status = GST_RTSP_MEDIA_STATUS_ERROR;
    }
  }
  /* could be success or error */
  result = priv->status;
  GST_DEBUG ("got status %d", result);
  g_mutex_unlock (&priv->lock);

  return result;
}

static void
stream_collect_blocking (GstRTSPStream * stream, gboolean * blocked)
{
  *blocked &= gst_rtsp_stream_is_blocking (stream);
}

static gboolean
media_streams_blocking (GstRTSPMedia * media)
{
  gboolean blocking = TRUE;

  g_ptr_array_foreach (media->priv->streams, (GFunc) stream_collect_blocking,
      &blocking);

  return blocking;
}

static GstStateChangeReturn
set_state (GstRTSPMedia * media, GstState state)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstStateChangeReturn ret;

  GST_INFO ("set state to %s for media %p", gst_element_state_get_name (state),
      media);
  ret = gst_element_set_state (priv->pipeline, state);

  return ret;
}

static GstStateChangeReturn
set_target_state (GstRTSPMedia * media, GstState state, gboolean do_state)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstStateChangeReturn ret;

  GST_INFO ("set target state to %s for media %p",
      gst_element_state_get_name (state), media);
  priv->target_state = state;

  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_TARGET_STATE], 0,
      priv->target_state, NULL);

  if (do_state)
    ret = set_state (media, state);
  else
    ret = GST_STATE_CHANGE_SUCCESS;

  return ret;
}

/* called with state-lock */
static gboolean
default_handle_message (GstRTSPMedia * media, GstMessage * message)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstMessageType type;

  type = GST_MESSAGE_TYPE (message);

  switch (type) {
    case GST_MESSAGE_STATE_CHANGED:
      break;
    case GST_MESSAGE_BUFFERING:
    {
      gint percent;

      gst_message_parse_buffering (message, &percent);

      /* no state management needed for live pipelines */
      if (priv->is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        priv->buffering = FALSE;
        /* if the desired state is playing, go back */
        if (priv->target_state == GST_STATE_PLAYING) {
          GST_INFO ("Buffering done, setting pipeline to PLAYING");
          set_state (media, GST_STATE_PLAYING);
        } else {
          GST_INFO ("Buffering done");
        }
      } else {
        /* buffering busy */
        if (priv->buffering == FALSE) {
          if (priv->target_state == GST_STATE_PLAYING) {
            /* we were not buffering but PLAYING, PAUSE  the pipeline. */
            GST_INFO ("Buffering, setting pipeline to PAUSED ...");
            set_state (media, GST_STATE_PAUSED);
          } else {
            GST_INFO ("Buffering ...");
          }
        }
        priv->buffering = TRUE;
      }
      break;
    }
    case GST_MESSAGE_LATENCY:
    {
      gst_bin_recalculate_latency (GST_BIN_CAST (priv->pipeline));
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      GST_WARNING ("%p: got error %s (%s)", media, gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);

      gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *gerror;
      gchar *debug;

      gst_message_parse_warning (message, &gerror, &debug);
      GST_WARNING ("%p: got warning %s (%s)", media, gerror->message, debug);
      g_error_free (gerror);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s;

      s = gst_message_get_structure (message);
      if (gst_structure_has_name (s, "GstRTSPStreamBlocking")) {
        GST_DEBUG ("media received blocking message");
        if (priv->blocked && media_streams_blocking (media)) {
          GST_DEBUG ("media is blocking");
          collect_media_stats (media);

          if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING)
            gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
        }
      }
      break;
    }
    case GST_MESSAGE_STREAM_STATUS:
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (priv->adding) {
        /* when we are dynamically adding pads, the addition of the udpsrc will
         * temporarily produce ASYNC_DONE messages. We have to ignore them and
         * wait for the final ASYNC_DONE after everything prerolled */
        GST_INFO ("%p: ignoring ASYNC_DONE", media);
      } else {
        GST_INFO ("%p: got ASYNC_DONE", media);
        collect_media_stats (media);

        if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING)
          gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
      }
      break;
    case GST_MESSAGE_EOS:
      GST_INFO ("%p: got EOS", media);

      if (priv->status == GST_RTSP_MEDIA_STATUS_UNPREPARING) {
        GST_DEBUG ("shutting down after EOS");
        finish_unprepare (media);
      }
      break;
    default:
      GST_INFO ("%p: got message type %d (%s)", media, type,
          gst_message_type_get_name (type));
      break;
  }
  return TRUE;
}

static gboolean
bus_message (GstBus * bus, GstMessage * message, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPMediaClass *klass;
  gboolean ret;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  g_rec_mutex_lock (&priv->state_lock);
  if (klass->handle_message)
    ret = klass->handle_message (media, message);
  else
    ret = FALSE;
  g_rec_mutex_unlock (&priv->state_lock);

  return ret;
}

static void
watch_destroyed (GstRTSPMedia * media)
{
  GST_DEBUG_OBJECT (media, "source destroyed");
  g_object_unref (media);
}

static GstElement *
find_payload_element (GstElement * payloader)
{
  GstElement *pay = NULL;

  if (GST_IS_BIN (payloader)) {
    GstIterator *iter;
    GValue item = { 0 };

    iter = gst_bin_iterate_recurse (GST_BIN (payloader));
    while (gst_iterator_next (iter, &item) == GST_ITERATOR_OK) {
      GstElement *element = (GstElement *) g_value_get_object (&item);
      GstElementClass *eclass = GST_ELEMENT_GET_CLASS (element);
      const gchar *klass;

      klass =
          gst_element_class_get_metadata (eclass, GST_ELEMENT_METADATA_KLASS);
      if (klass == NULL)
        continue;

      if (strstr (klass, "Payloader") && strstr (klass, "RTP")) {
        pay = gst_object_ref (element);
        g_value_unset (&item);
        break;
      }
      g_value_unset (&item);
    }
    gst_iterator_free (iter);
  } else {
    pay = g_object_ref (payloader);
  }

  return pay;
}

/* called from streaming threads */
static void
pad_added_cb (GstElement * element, GstPad * pad, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPStream *stream;
  GstElement *pay;

  /* find the real payload element */
  pay = find_payload_element (element);
  stream = gst_rtsp_media_create_stream (media, pay, pad);
  gst_object_unref (pay);

  g_object_set_data (G_OBJECT (pad), "gst-rtsp-dynpad-stream", stream);

  GST_INFO ("pad added %s:%s, stream %p", GST_DEBUG_PAD_NAME (pad), stream);

  g_rec_mutex_lock (&priv->state_lock);
  /* we will be adding elements below that will cause ASYNC_DONE to be
   * posted in the bus. We want to ignore those messages until the
   * pipeline really prerolled. */
  priv->adding = TRUE;

  /* join the element in the PAUSED state because this callback is
   * called from the streaming thread and it is PAUSED */
  gst_rtsp_stream_join_bin (stream, GST_BIN (priv->pipeline),
      priv->rtpbin, GST_STATE_PAUSED);

  priv->adding = FALSE;
  g_rec_mutex_unlock (&priv->state_lock);
}

static void
pad_removed_cb (GstElement * element, GstPad * pad, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPStream *stream;

  stream = g_object_get_data (G_OBJECT (pad), "gst-rtsp-dynpad-stream");
  if (stream == NULL)
    return;

  GST_INFO ("pad removed %s:%s, stream %p", GST_DEBUG_PAD_NAME (pad), stream);

  g_rec_mutex_lock (&priv->state_lock);
  gst_rtsp_stream_leave_bin (stream, GST_BIN (priv->pipeline), priv->rtpbin);
  g_rec_mutex_unlock (&priv->state_lock);

  gst_rtsp_media_remove_stream (media, stream);
}

static void
remove_fakesink (GstRTSPMediaPrivate * priv)
{
  GstElement *fakesink;

  g_mutex_lock (&priv->lock);
  if ((fakesink = priv->fakesink))
    gst_object_ref (fakesink);
  priv->fakesink = NULL;
  g_mutex_unlock (&priv->lock);

  if (fakesink) {
    gst_bin_remove (GST_BIN (priv->pipeline), fakesink);
    gst_element_set_state (fakesink, GST_STATE_NULL);
    gst_object_unref (fakesink);
    GST_INFO ("removed fakesink");
  }
}

static void
no_more_pads_cb (GstElement * element, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;

  GST_INFO ("no more pads");
  remove_fakesink (priv);
}

typedef struct _DynPaySignalHandlers DynPaySignalHandlers;

struct _DynPaySignalHandlers
{
  gulong pad_added_handler;
  gulong pad_removed_handler;
  gulong no_more_pads_handler;
};

static gboolean
start_preroll (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstStateChangeReturn ret;

  GST_INFO ("setting pipeline to PAUSED for media %p", media);
  /* first go to PAUSED */
  ret = set_target_state (media, GST_STATE_PAUSED, TRUE);

  switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
      GST_INFO ("SUCCESS state change for media %p", media);
      priv->seekable = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      GST_INFO ("ASYNC state change for media %p", media);
      priv->seekable = TRUE;
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      /* we need to go to PLAYING */
      GST_INFO ("NO_PREROLL state change: live media %p", media);
      /* FIXME we disable seeking for live streams for now. We should perform a
       * seeking query in preroll instead */
      priv->seekable = FALSE;
      priv->is_live = TRUE;
      /* start blocked  to make sure nothing goes to the sink */
      media_streams_set_blocked (media, TRUE);
      ret = set_state (media, GST_STATE_PLAYING);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto state_failed;
      break;
    case GST_STATE_CHANGE_FAILURE:
      goto state_failed;
  }

  return TRUE;

state_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    return FALSE;
  }
}

static gboolean
wait_preroll (GstRTSPMedia * media)
{
  GstRTSPMediaStatus status;

  GST_DEBUG ("wait to preroll pipeline");

  /* wait until pipeline is prerolled */
  status = gst_rtsp_media_get_status (media);
  if (status == GST_RTSP_MEDIA_STATUS_ERROR)
    goto preroll_failed;

  return TRUE;

preroll_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    return FALSE;
  }
}

static gboolean
start_prepare (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  guint i;
  GList *walk;

  /* link streams we already have, other streams might appear when we have
   * dynamic elements */
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream;

    stream = g_ptr_array_index (priv->streams, i);

    gst_rtsp_stream_join_bin (stream, GST_BIN (priv->pipeline),
        priv->rtpbin, GST_STATE_NULL);
  }

  for (walk = priv->dynamic; walk; walk = g_list_next (walk)) {
    GstElement *elem = walk->data;
    DynPaySignalHandlers *handlers = g_slice_new (DynPaySignalHandlers);

    GST_INFO ("adding callbacks for dynamic element %p", elem);

    handlers->pad_added_handler = g_signal_connect (elem, "pad-added",
        (GCallback) pad_added_cb, media);
    handlers->pad_removed_handler = g_signal_connect (elem, "pad-removed",
        (GCallback) pad_removed_cb, media);
    handlers->no_more_pads_handler = g_signal_connect (elem, "no-more-pads",
        (GCallback) no_more_pads_cb, media);

    g_object_set_data (G_OBJECT (elem), "gst-rtsp-dynpay-handlers", handlers);

    /* we add a fakesink here in order to make the state change async. We remove
     * the fakesink again in the no-more-pads callback. */
    priv->fakesink = gst_element_factory_make ("fakesink", "fakesink");
    gst_bin_add (GST_BIN (priv->pipeline), priv->fakesink);
  }

  if (!start_preroll (media))
    goto preroll_failed;

  return FALSE;

preroll_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    return FALSE;
  }
}

/**
 * gst_rtsp_media_prepare:
 * @media: a #GstRTSPMedia
 * @thread: a #GstRTSPThread to run the bus handler or %NULL
 *
 * Prepare @media for streaming. This function will create the objects
 * to manage the streaming. A pipeline must have been set on @media with
 * gst_rtsp_media_take_pipeline().
 *
 * It will preroll the pipeline and collect vital information about the streams
 * such as the duration.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_prepare (GstRTSPMedia * media, GstRTSPThread * thread)
{
  GstRTSPMediaPrivate *priv;
  GstBus *bus;
  GSource *source;
  GstRTSPMediaClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (GST_IS_RTSP_THREAD (thread), FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  priv->prepare_count++;

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARED ||
      priv->status == GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto was_prepared;

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING)
    goto wait_status;

  if (priv->status != GST_RTSP_MEDIA_STATUS_UNPREPARED)
    goto not_unprepared;

  if (!priv->reusable && priv->reused)
    goto is_reused;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  if (!klass->create_rtpbin)
    goto no_create_rtpbin;

  priv->rtpbin = klass->create_rtpbin (media);
  if (priv->rtpbin != NULL) {
    gboolean success = TRUE;

    if (klass->setup_rtpbin)
      success = klass->setup_rtpbin (media, priv->rtpbin);

    if (success == FALSE) {
      gst_object_unref (priv->rtpbin);
      priv->rtpbin = NULL;
    }
  }
  if (priv->rtpbin == NULL)
    goto no_rtpbin;

  GST_INFO ("preparing media %p", media);

  /* reset some variables */
  priv->is_live = FALSE;
  priv->seekable = FALSE;
  priv->buffering = FALSE;
  priv->thread = thread;
  /* we're preparing now */
  priv->status = GST_RTSP_MEDIA_STATUS_PREPARING;

  bus = gst_pipeline_get_bus (GST_PIPELINE_CAST (priv->pipeline));

  /* add the pipeline bus to our custom mainloop */
  priv->source = gst_bus_create_watch (bus);
  gst_object_unref (bus);

  g_source_set_callback (priv->source, (GSourceFunc) bus_message,
      g_object_ref (media), (GDestroyNotify) watch_destroyed);

  priv->id = g_source_attach (priv->source, thread->context);

  /* add stuff to the bin */
  gst_bin_add (GST_BIN (priv->pipeline), priv->rtpbin);

  /* do remainder in context */
  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) start_prepare, media, NULL);
  g_source_attach (source, thread->context);
  g_source_unref (source);

wait_status:
  g_rec_mutex_unlock (&priv->state_lock);

  /* now wait for all pads to be prerolled, FIXME, we should somehow be
   * able to do this async so that we don't block the server thread. */
  if (!wait_preroll (media))
    goto preroll_failed;

  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_PREPARED], 0, NULL);

  GST_INFO ("object %p is prerolled", media);

  return TRUE;

  /* OK */
was_prepared:
  {
    GST_LOG ("media %p was prepared", media);
    /* we are not going to use the giving thread, so stop it. */
    gst_rtsp_thread_stop (thread);
    g_rec_mutex_unlock (&priv->state_lock);
    return TRUE;
  }
  /* ERRORS */
not_unprepared:
  {
    GST_WARNING ("media %p was not unprepared", media);
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
is_reused:
  {
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("can not reuse media %p", media);
    return FALSE;
  }
no_create_rtpbin:
  {
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    GST_ERROR ("no create_rtpbin function");
    g_critical ("no create_rtpbin vmethod function set");
    return FALSE;
  }
no_rtpbin:
  {
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("no rtpbin element");
    g_warning ("failed to create element 'rtpbin', check your installation");
    return FALSE;
  }
preroll_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    gst_rtsp_media_unprepare (media);
    return FALSE;
  }
}

/* must be called with state-lock */
static void
finish_unprepare (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gint i;
  GList *walk;

  GST_DEBUG ("shutting down");

  set_state (media, GST_STATE_NULL);
  remove_fakesink (priv);

  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream;

    GST_INFO ("Removing elements of stream %d from pipeline", i);

    stream = g_ptr_array_index (priv->streams, i);

    gst_rtsp_stream_leave_bin (stream, GST_BIN (priv->pipeline), priv->rtpbin);
  }

  /* remove the pad signal handlers */
  for (walk = priv->dynamic; walk; walk = g_list_next (walk)) {
    GstElement *elem = walk->data;
    DynPaySignalHandlers *handlers;

    handlers =
        g_object_steal_data (G_OBJECT (elem), "gst-rtsp-dynpay-handlers");
    g_assert (handlers != NULL);

    g_signal_handler_disconnect (G_OBJECT (elem), handlers->pad_added_handler);
    g_signal_handler_disconnect (G_OBJECT (elem),
        handlers->pad_removed_handler);
    g_signal_handler_disconnect (G_OBJECT (elem),
        handlers->no_more_pads_handler);

    g_slice_free (DynPaySignalHandlers, handlers);
  }

  gst_bin_remove (GST_BIN (priv->pipeline), priv->rtpbin);
  priv->rtpbin = NULL;

  if (priv->nettime)
    gst_object_unref (priv->nettime);
  priv->nettime = NULL;

  priv->reused = TRUE;
  priv->status = GST_RTSP_MEDIA_STATUS_UNPREPARED;

  /* when the media is not reusable, this will effectively unref the media and
   * recreate it */
  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_UNPREPARED], 0, NULL);

  /* the source has the last ref to the media */
  if (priv->source) {
    GST_DEBUG ("destroy source");
    g_source_destroy (priv->source);
    g_source_unref (priv->source);
  }
  if (priv->thread) {
    GST_DEBUG ("stop thread");
    gst_rtsp_thread_stop (priv->thread);
  }
}

/* called with state-lock */
static gboolean
default_unprepare (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;

  if (priv->eos_shutdown) {
    GST_DEBUG ("sending EOS for shutdown");
    /* ref so that we don't disappear */
    gst_element_send_event (priv->pipeline, gst_event_new_eos ());
    /* we need to go to playing again for the EOS to propagate, normally in this
     * state, nothing is receiving data from us anymore so this is ok. */
    set_state (media, GST_STATE_PLAYING);
    priv->status = GST_RTSP_MEDIA_STATUS_UNPREPARING;
  } else {
    finish_unprepare (media);
  }
  return TRUE;
}

/**
 * gst_rtsp_media_unprepare:
 * @media: a #GstRTSPMedia
 *
 * Unprepare @media. After this call, the media should be prepared again before
 * it can be used again. If the media is set to be non-reusable, a new instance
 * must be created.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_unprepare (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean success;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status == GST_RTSP_MEDIA_STATUS_UNPREPARED)
    goto was_unprepared;

  priv->prepare_count--;
  if (priv->prepare_count > 0)
    goto is_busy;

  GST_INFO ("unprepare media %p", media);
  set_target_state (media, GST_STATE_NULL, FALSE);
  success = TRUE;

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARED) {
    GstRTSPMediaClass *klass;

    klass = GST_RTSP_MEDIA_GET_CLASS (media);
    if (klass->unprepare)
      success = klass->unprepare (media);
  } else {
    finish_unprepare (media);
  }
  g_rec_mutex_unlock (&priv->state_lock);

  return success;

was_unprepared:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("media %p was already unprepared", media);
    return TRUE;
  }
is_busy:
  {
    GST_INFO ("media %p still prepared %d times", media, priv->prepare_count);
    g_rec_mutex_unlock (&priv->state_lock);
    return TRUE;
  }
}

/* should be called with state-lock */
static GstClock *
get_clock_unlocked (GstRTSPMedia * media)
{
  if (media->priv->status != GST_RTSP_MEDIA_STATUS_PREPARED) {
    GST_DEBUG_OBJECT (media, "media was not prepared");
    return NULL;
  }
  return gst_pipeline_get_clock (GST_PIPELINE_CAST (media->priv->pipeline));
}

/**
 * gst_rtsp_media_get_clock:
 * @media: a #GstRTSPMedia
 *
 * Get the clock that is used by the pipeline in @media.
 *
 * @media must be prepared before this method returns a valid clock object.
 *
 * Returns: (transfer full): the #GstClock used by @media. unref after usage.
 */
GstClock *
gst_rtsp_media_get_clock (GstRTSPMedia * media)
{
  GstClock *clock;
  GstRTSPMediaPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  clock = get_clock_unlocked (media);
  g_rec_mutex_unlock (&priv->state_lock);

  return clock;
}

/**
 * gst_rtsp_media_get_base_time:
 * @media: a #GstRTSPMedia
 *
 * Get the base_time that is used by the pipeline in @media.
 *
 * @media must be prepared before this method returns a valid base_time.
 *
 * Returns: the base_time used by @media.
 */
GstClockTime
gst_rtsp_media_get_base_time (GstRTSPMedia * media)
{
  GstClockTime result;
  GstRTSPMediaPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), GST_CLOCK_TIME_NONE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (media->priv->status != GST_RTSP_MEDIA_STATUS_PREPARED)
    goto not_prepared;

  result = gst_element_get_base_time (media->priv->pipeline);
  g_rec_mutex_unlock (&priv->state_lock);

  return result;

  /* ERRORS */
not_prepared:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_DEBUG_OBJECT (media, "media was not prepared");
    return GST_CLOCK_TIME_NONE;
  }
}

/**
 * gst_rtsp_media_get_time_provider:
 * @media: a #GstRTSPMedia
 * @address: an address or %NULL
 * @port: a port or 0
 *
 * Get the #GstNetTimeProvider for the clock used by @media. The time provider
 * will listen on @address and @port for client time requests.
 *
 * Returns: (transfer full): the #GstNetTimeProvider of @media.
 */
GstNetTimeProvider *
gst_rtsp_media_get_time_provider (GstRTSPMedia * media, const gchar * address,
    guint16 port)
{
  GstRTSPMediaPrivate *priv;
  GstNetTimeProvider *provider = NULL;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->time_provider) {
    if ((provider = priv->nettime) == NULL) {
      GstClock *clock;

      if (priv->time_provider && (clock = get_clock_unlocked (media))) {
        provider = gst_net_time_provider_new (clock, address, port);
        gst_object_unref (clock);

        priv->nettime = provider;
      }
    }
  }
  g_rec_mutex_unlock (&priv->state_lock);

  if (provider)
    gst_object_ref (provider);

  return provider;
}

static gboolean
default_setup_sdp (GstRTSPMedia * media, GstSDPMessage * sdp, GstSDPInfo * info)
{
  return gst_rtsp_sdp_from_media (sdp, info, media);
}

/**
 * gst_rtsp_media_setup_sdp:
 * @sdp: a #GstSDPMessage
 * @info: info
 * @media: a #GstRTSPMedia
 *
 * Add @media specific info to @sdp. @info is used to configure the connection
 * information in the SDP.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_rtsp_media_setup_sdp (GstRTSPMedia * media, GstSDPMessage * sdp,
    GstSDPInfo * info)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPMediaClass *klass;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (sdp != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  if (!klass->setup_sdp)
    goto no_setup_sdp;

  res = klass->setup_sdp (media, sdp, info);

  g_rec_mutex_unlock (&priv->state_lock);

  return res;

  /* ERRORS */
no_setup_sdp:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_ERROR ("no setup_sdp function");
    g_critical ("no setup_sdp vmethod function set");
    return FALSE;
  }
}

/**
 * gst_rtsp_media_suspend:
 * @media: a #GstRTSPMedia
 *
 * Suspend @media. The state of the pipeline managed by @media is set to
 * GST_STATE_NULL but all streams are kept. @media can be prepared again
 * with gst_rtsp_media_undo_reset()
 *
 * @media must be prepared with gst_rtsp_media_prepare();
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_suspend (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  GST_FIXME ("suspend for dynamic pipelines needs fixing");

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED)
    goto not_prepared;

  /* don't attempt to suspend when something is busy */
  if (priv->n_active > 0)
    goto done;

  switch (priv->suspend_mode) {
    case GST_RTSP_SUSPEND_MODE_NONE:
      GST_DEBUG ("media %p no suspend", media);
      break;
    case GST_RTSP_SUSPEND_MODE_PAUSE:
      GST_DEBUG ("media %p suspend to PAUSED", media);
      ret = set_target_state (media, GST_STATE_PAUSED, TRUE);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto state_failed;
      break;
    case GST_RTSP_SUSPEND_MODE_RESET:
      GST_DEBUG ("media %p suspend to NULL", media);
      ret = set_target_state (media, GST_STATE_NULL, TRUE);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto state_failed;
      break;
    default:
      break;
  }
  /* let the streams do the state changes freely, if any */
  media_streams_set_blocked (media, FALSE);
  priv->status = GST_RTSP_MEDIA_STATUS_SUSPENDED;
done:
  g_rec_mutex_unlock (&priv->state_lock);

  return TRUE;

  /* ERRORS */
not_prepared:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("media %p was not prepared", media);
    return FALSE;
  }
state_failed:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    GST_WARNING ("failed changing pipeline's state for media %p", media);
    return FALSE;
  }
}

/**
 * gst_rtsp_media_unsuspend:
 * @media: a #GstRTSPMedia
 *
 * Unsuspend @media if it was in a suspended state. This method does nothing
 * when the media was not in the suspended state.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_unsuspend (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto done;

  switch (priv->suspend_mode) {
    case GST_RTSP_SUSPEND_MODE_NONE:
      priv->status = GST_RTSP_MEDIA_STATUS_PREPARED;
      break;
    case GST_RTSP_SUSPEND_MODE_PAUSE:
      priv->status = GST_RTSP_MEDIA_STATUS_PREPARED;
      break;
    case GST_RTSP_SUSPEND_MODE_RESET:
    {
      priv->status = GST_RTSP_MEDIA_STATUS_PREPARING;
      if (!start_preroll (media))
        goto start_failed;
      g_rec_mutex_unlock (&priv->state_lock);

      if (!wait_preroll (media))
        goto preroll_failed;

      g_rec_mutex_lock (&priv->state_lock);
    }
    default:
      break;
  }
done:
  g_rec_mutex_unlock (&priv->state_lock);

  return TRUE;

  /* ERRORS */
start_failed:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("failed to preroll pipeline");
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    return FALSE;
  }
preroll_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    return FALSE;
  }
}

/* must be called with state-lock */
static void
media_set_pipeline_state_locked (GstRTSPMedia * media, GstState state)
{
  GstRTSPMediaPrivate *priv = media->priv;

  if (state == GST_STATE_NULL) {
    gst_rtsp_media_unprepare (media);
  } else {
    GST_INFO ("state %s media %p", gst_element_state_get_name (state), media);
    set_target_state (media, state, FALSE);
    /* when we are buffering, don't update the state yet, this will be done
     * when buffering finishes */
    if (priv->buffering) {
      GST_INFO ("Buffering busy, delay state change");
    } else {
      if (state == GST_STATE_PLAYING)
        /* make sure pads are not blocking anymore when going to PLAYING */
        media_streams_set_blocked (media, FALSE);

      set_state (media, state);

      /* and suspend after pause */
      if (state == GST_STATE_PAUSED)
        gst_rtsp_media_suspend (media);
    }
  }
}

/**
 * gst_rtsp_media_set_pipeline_state:
 * @media: a #GstRTSPMedia
 * @state: the target state of the pipeline
 *
 * Set the state of the pipeline managed by @media to @state
 */
void
gst_rtsp_media_set_pipeline_state (GstRTSPMedia * media, GstState state)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  g_rec_mutex_lock (&media->priv->state_lock);
  media_set_pipeline_state_locked (media, state);
  g_rec_mutex_unlock (&media->priv->state_lock);
}

/**
 * gst_rtsp_media_set_state:
 * @media: a #GstRTSPMedia
 * @state: the target state of the media
 * @transports: (element-type GstRtspServer.RTSPStreamTransport): a #GPtrArray
 * of #GstRTSPStreamTransport pointers
 *
 * Set the state of @media to @state and for the transports in @transports.
 *
 * @media must be prepared with gst_rtsp_media_prepare();
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_set_state (GstRTSPMedia * media, GstState state,
    GPtrArray * transports)
{
  GstRTSPMediaPrivate *priv;
  gint i;
  gboolean activate, deactivate, do_state;
  gint old_active;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (transports != NULL, FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status == GST_RTSP_MEDIA_STATUS_ERROR)
    goto error_status;
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED &&
      priv->status != GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto not_prepared;

  /* NULL and READY are the same */
  if (state == GST_STATE_READY)
    state = GST_STATE_NULL;

  activate = deactivate = FALSE;

  GST_INFO ("going to state %s media %p", gst_element_state_get_name (state),
      media);

  switch (state) {
    case GST_STATE_NULL:
    case GST_STATE_PAUSED:
      /* we're going from PLAYING to PAUSED, READY or NULL, deactivate */
      if (priv->target_state == GST_STATE_PLAYING)
        deactivate = TRUE;
      break;
    case GST_STATE_PLAYING:
      /* we're going to PLAYING, activate */
      activate = TRUE;
      break;
    default:
      break;
  }
  old_active = priv->n_active;

  for (i = 0; i < transports->len; i++) {
    GstRTSPStreamTransport *trans;

    /* we need a non-NULL entry in the array */
    trans = g_ptr_array_index (transports, i);
    if (trans == NULL)
      continue;

    if (activate) {
      if (gst_rtsp_stream_transport_set_active (trans, TRUE))
        priv->n_active++;
    } else if (deactivate) {
      if (gst_rtsp_stream_transport_set_active (trans, FALSE))
        priv->n_active--;
    }
  }

  /* we just activated the first media, do the playing state change */
  if (old_active == 0 && activate)
    do_state = TRUE;
  /* if we have no more active media, do the downward state changes */
  else if (priv->n_active == 0)
    do_state = TRUE;
  else
    do_state = FALSE;

  GST_INFO ("state %d active %d media %p do_state %d", state, priv->n_active,
      media, do_state);

  if (priv->target_state != state) {
    if (do_state)
      media_set_pipeline_state_locked (media, state);

    g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_NEW_STATE], 0, state,
        NULL);
  }

  /* remember where we are */
  if (state != GST_STATE_NULL && (state == GST_STATE_PAUSED ||
          old_active != priv->n_active))
    collect_media_stats (media);

  g_rec_mutex_unlock (&priv->state_lock);

  return TRUE;

  /* ERRORS */
not_prepared:
  {
    GST_WARNING ("media %p was not prepared", media);
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
error_status:
  {
    GST_WARNING ("media %p in error status while changing to state %d",
        media, state);
    if (state == GST_STATE_NULL) {
      for (i = 0; i < transports->len; i++) {
        GstRTSPStreamTransport *trans;

        /* we need a non-NULL entry in the array */
        trans = g_ptr_array_index (transports, i);
        if (trans == NULL)
          continue;

        gst_rtsp_stream_transport_set_active (trans, FALSE);
      }
      priv->n_active = 0;
    }
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
}
