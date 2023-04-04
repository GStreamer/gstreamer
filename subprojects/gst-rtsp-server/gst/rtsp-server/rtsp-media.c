/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2015 Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
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
 * Seeking can be done with gst_rtsp_media_seek(), or gst_rtsp_media_seek_full()
 * or gst_rtsp_media_seek_trickmode() for finer control of the seek.
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <gst/sdp/gstmikey.h>
#include <gst/rtp/gstrtppayloads.h>

#define AES_128_KEY_LEN 16
#define AES_256_KEY_LEN 32

#define HMAC_32_KEY_LEN 4
#define HMAC_80_KEY_LEN 10

#include "rtsp-media.h"
#include "rtsp-server-internal.h"

struct _GstRTSPMediaPrivate
{
  GMutex lock;
  GCond cond;

  /* the global lock is used to lock the entire media. This is needed by callers
     such as rtsp_client to protect the media when it is shared by many clients.
     The lock prevents that concurrenting clients messes up media.
     Typically the lock is taken in external API calls such as SETUP */
  GMutex global_lock;

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
  gint dscp_qos;
  GstRTSPAddressPool *pool;
  gchar *multicast_iface;
  guint max_mcast_ttl;
  gboolean bind_mcast_address;
  gboolean enable_rtcp;
  gboolean blocked;
  GstRTSPTransportMode transport_mode;
  gboolean stop_on_disconnect;
  guint blocking_msg_received;

  GstElement *element;
  GRecMutex state_lock;         /* locking order: state lock, lock */
  GPtrArray *streams;           /* protected by lock */
  GList *dynamic;               /* protected by lock */
  GstRTSPMediaStatus status;    /* protected by lock */
  gint prepare_count;
  gint n_active;
  gboolean complete;
  gboolean finishing_unprepare;

  /* the pipeline for the media */
  GstElement *pipeline;
  GSource *source;
  GstRTSPThread *thread;
  GList *pending_pipeline_elements;

  gboolean time_provider;
  GstNetTimeProvider *nettime;

  gboolean is_live;
  GstClockTimeDiff seekable;
  gboolean buffering;
  GstState target_state;

  /* RTP session manager */
  GstElement *rtpbin;

  /* the range of media */
  GstRTSPTimeRange range;       /* protected by lock */
  GstClockTime range_start;
  GstClockTime range_stop;

  GList *payloads;              /* protected by lock */
  GstClockTime rtx_time;        /* protected by lock */
  gboolean do_retransmission;   /* protected by lock */
  guint latency;                /* protected by lock */
  GstClock *clock;              /* protected by lock */
  gboolean do_rate_control;     /* protected by lock */
  GstRTSPPublishClockMode publish_clock_mode;

  /* Dynamic element handling */
  guint nb_dynamic_elements;
  guint no_more_pads_pending;
  gboolean expected_async_done;
};

#define DEFAULT_SHARED          FALSE
#define DEFAULT_SUSPEND_MODE    GST_RTSP_SUSPEND_MODE_NONE
#define DEFAULT_REUSABLE        FALSE
#define DEFAULT_PROFILES        GST_RTSP_PROFILE_AVP
#define DEFAULT_PROTOCOLS       GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | \
                                        GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_EOS_SHUTDOWN    FALSE
#define DEFAULT_BUFFER_SIZE     0x80000
#define DEFAULT_DSCP_QOS        (-1)
#define DEFAULT_TIME_PROVIDER   FALSE
#define DEFAULT_LATENCY         200
#define DEFAULT_TRANSPORT_MODE  GST_RTSP_TRANSPORT_MODE_PLAY
#define DEFAULT_STOP_ON_DISCONNECT TRUE
#define DEFAULT_MAX_MCAST_TTL   255
#define DEFAULT_BIND_MCAST_ADDRESS FALSE
#define DEFAULT_DO_RATE_CONTROL TRUE
#define DEFAULT_ENABLE_RTCP     TRUE

#define DEFAULT_DO_RETRANSMISSION FALSE

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
  PROP_LATENCY,
  PROP_TRANSPORT_MODE,
  PROP_STOP_ON_DISCONNECT,
  PROP_CLOCK,
  PROP_MAX_MCAST_TTL,
  PROP_BIND_MCAST_ADDRESS,
  PROP_DSCP_QOS,
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
  SIGNAL_HANDLE_MESSAGE,
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
static gboolean default_prepare (GstRTSPMedia * media, GstRTSPThread * thread);
static gboolean default_unprepare (GstRTSPMedia * media);
static gboolean default_suspend (GstRTSPMedia * media);
static gboolean default_unsuspend (GstRTSPMedia * media);
static gboolean default_convert_range (GstRTSPMedia * media,
    GstRTSPTimeRange * range, GstRTSPRangeUnit unit);
static gboolean default_query_position (GstRTSPMedia * media,
    gint64 * position);
static gboolean default_query_stop (GstRTSPMedia * media, gint64 * stop);
static GstElement *default_create_rtpbin (GstRTSPMedia * media);
static gboolean default_setup_sdp (GstRTSPMedia * media, GstSDPMessage * sdp,
    GstSDPInfo * info);
static gboolean default_handle_sdp (GstRTSPMedia * media, GstSDPMessage * sdp);

static gboolean wait_preroll (GstRTSPMedia * media);

static GstElement *find_payload_element (GstElement * payloader, GstPad * pad);

static guint gst_rtsp_media_signals[SIGNAL_LAST] = { 0 };

static gboolean check_complete (GstRTSPMedia * media);

#define C_ENUM(v) ((gint) v)

#define TRICKMODE_FLAGS (GST_SEEK_FLAG_TRICKMODE | GST_SEEK_FLAG_TRICKMODE_KEY_UNITS | GST_SEEK_FLAG_TRICKMODE_FORWARD_PREDICTED)

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

#define C_FLAGS(v) ((guint) v)

GType
gst_rtsp_transport_mode_get_type (void)
{
  static gsize id = 0;
  static const GFlagsValue values[] = {
    {C_FLAGS (GST_RTSP_TRANSPORT_MODE_PLAY), "GST_RTSP_TRANSPORT_MODE_PLAY",
        "play"},
    {C_FLAGS (GST_RTSP_TRANSPORT_MODE_RECORD), "GST_RTSP_TRANSPORT_MODE_RECORD",
        "record"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_flags_register_static ("GstRTSPTransportMode", values);
    g_once_init_leave (&id, tmp);
  }
  return (GType) id;
}

GType
gst_rtsp_publish_clock_mode_get_type (void)
{
  static gsize id = 0;
  static const GEnumValue values[] = {
    {C_ENUM (GST_RTSP_PUBLISH_CLOCK_MODE_NONE),
        "GST_RTSP_PUBLISH_CLOCK_MODE_NONE", "none"},
    {C_ENUM (GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK),
          "GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK",
        "clock"},
    {C_ENUM (GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK_AND_OFFSET),
          "GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK_AND_OFFSET",
        "clock-and-offset"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_enum_register_static ("GstRTSPPublishClockMode", values);
    g_once_init_leave (&id, tmp);
  }
  return (GType) id;
}

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPMedia, gst_rtsp_media, G_TYPE_OBJECT);

static void
gst_rtsp_media_class_init (GstRTSPMediaClass * klass)
{
  GObjectClass *gobject_class;

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

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Latency",
          "Latency used for receiving media in milliseconds", 0, G_MAXUINT,
          DEFAULT_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRANSPORT_MODE,
      g_param_spec_flags ("transport-mode", "Transport Mode",
          "If this media pipeline can be used for PLAY or RECORD",
          GST_TYPE_RTSP_TRANSPORT_MODE, DEFAULT_TRANSPORT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STOP_ON_DISCONNECT,
      g_param_spec_boolean ("stop-on-disconnect", "Stop On Disconnect",
          "If this media pipeline should be stopped "
          "when a client disconnects without TEARDOWN",
          DEFAULT_STOP_ON_DISCONNECT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLOCK,
      g_param_spec_object ("clock", "Clock",
          "Clock to be used by the media pipeline",
          GST_TYPE_CLOCK, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_MCAST_TTL,
      g_param_spec_uint ("max-mcast-ttl", "Maximum multicast ttl",
          "The maximum time-to-live value of outgoing multicast packets", 1,
          255, DEFAULT_MAX_MCAST_TTL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BIND_MCAST_ADDRESS,
      g_param_spec_boolean ("bind-mcast-address", "Bind mcast address",
          "Whether the multicast sockets should be bound to multicast addresses "
          "or INADDR_ANY",
          DEFAULT_BIND_MCAST_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DSCP_QOS,
      g_param_spec_int ("dscp-qos", "DSCP QoS",
          "The IP DSCP field to use for each related stream", -1, 63,
          DEFAULT_DSCP_QOS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_media_signals[SIGNAL_NEW_STREAM] =
      g_signal_new ("new-stream", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, new_stream), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_STREAM);

  gst_rtsp_media_signals[SIGNAL_REMOVED_STREAM] =
      g_signal_new ("removed-stream", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaClass, removed_stream),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_RTSP_STREAM);

  gst_rtsp_media_signals[SIGNAL_PREPARED] =
      g_signal_new ("prepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, prepared), NULL, NULL, NULL,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_media_signals[SIGNAL_UNPREPARED] =
      g_signal_new ("unprepared", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, unprepared), NULL, NULL, NULL,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gst_rtsp_media_signals[SIGNAL_TARGET_STATE] =
      g_signal_new ("target-state", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaClass, target_state),
      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);

  gst_rtsp_media_signals[SIGNAL_NEW_STATE] =
      g_signal_new ("new-state", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRTSPMediaClass, new_state), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_INT);

  /**
   * GstRTSPMedia::handle-message:
   * @media: a #GstRTSPMedia
   * @message: a #GstMessage
   *
   * Will be emitted when a message appears on the pipeline bus.
   *
   * Returns: a #gboolean indicating if the call was successful or not.
   *
   * Since: 1.22
   */
  gst_rtsp_media_signals[SIGNAL_HANDLE_MESSAGE] =
      g_signal_new ("handle-message", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, G_STRUCT_OFFSET (GstRTSPMediaClass,
          handle_message), NULL, NULL, NULL, G_TYPE_BOOLEAN, 1,
      GST_TYPE_MESSAGE);

  GST_DEBUG_CATEGORY_INIT (rtsp_media_debug, "rtspmedia", 0, "GstRTSPMedia");

  klass->handle_message = default_handle_message;
  klass->prepare = default_prepare;
  klass->unprepare = default_unprepare;
  klass->suspend = default_suspend;
  klass->unsuspend = default_unsuspend;
  klass->convert_range = default_convert_range;
  klass->query_position = default_query_position;
  klass->query_stop = default_query_stop;
  klass->create_rtpbin = default_create_rtpbin;
  klass->setup_sdp = default_setup_sdp;
  klass->handle_sdp = default_handle_sdp;
}

static void
gst_rtsp_media_init (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = gst_rtsp_media_get_instance_private (media);

  media->priv = priv;

  priv->streams = g_ptr_array_new_with_free_func (g_object_unref);
  g_mutex_init (&priv->lock);
  g_mutex_init (&priv->global_lock);
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
  priv->transport_mode = DEFAULT_TRANSPORT_MODE;
  priv->stop_on_disconnect = DEFAULT_STOP_ON_DISCONNECT;
  priv->publish_clock_mode = GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK;
  priv->do_retransmission = DEFAULT_DO_RETRANSMISSION;
  priv->max_mcast_ttl = DEFAULT_MAX_MCAST_TTL;
  priv->bind_mcast_address = DEFAULT_BIND_MCAST_ADDRESS;
  priv->enable_rtcp = DEFAULT_ENABLE_RTCP;
  priv->do_rate_control = DEFAULT_DO_RATE_CONTROL;
  priv->dscp_qos = DEFAULT_DSCP_QOS;
  priv->expected_async_done = FALSE;
  priv->blocking_msg_received = 0;
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
  g_list_free_full (priv->pending_pipeline_elements, gst_object_unref);

  if (priv->pipeline)
    gst_object_unref (priv->pipeline);
  if (priv->nettime)
    gst_object_unref (priv->nettime);
  gst_object_unref (priv->element);
  if (priv->pool)
    g_object_unref (priv->pool);
  if (priv->payloads)
    g_list_free (priv->payloads);
  if (priv->clock)
    gst_object_unref (priv->clock);
  g_free (priv->multicast_iface);
  g_mutex_clear (&priv->lock);
  g_mutex_clear (&priv->global_lock);
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
    case PROP_LATENCY:
      g_value_set_uint (value, gst_rtsp_media_get_latency (media));
      break;
    case PROP_TRANSPORT_MODE:
      g_value_set_flags (value, gst_rtsp_media_get_transport_mode (media));
      break;
    case PROP_STOP_ON_DISCONNECT:
      g_value_set_boolean (value, gst_rtsp_media_is_stop_on_disconnect (media));
      break;
    case PROP_CLOCK:
      g_value_take_object (value, gst_rtsp_media_get_clock (media));
      break;
    case PROP_MAX_MCAST_TTL:
      g_value_set_uint (value, gst_rtsp_media_get_max_mcast_ttl (media));
      break;
    case PROP_BIND_MCAST_ADDRESS:
      g_value_set_boolean (value, gst_rtsp_media_is_bind_mcast_address (media));
      break;
    case PROP_DSCP_QOS:
      g_value_set_int (value, gst_rtsp_media_get_dscp_qos (media));
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
    case PROP_LATENCY:
      gst_rtsp_media_set_latency (media, g_value_get_uint (value));
      break;
    case PROP_TRANSPORT_MODE:
      gst_rtsp_media_set_transport_mode (media, g_value_get_flags (value));
      break;
    case PROP_STOP_ON_DISCONNECT:
      gst_rtsp_media_set_stop_on_disconnect (media,
          g_value_get_boolean (value));
      break;
    case PROP_CLOCK:
      gst_rtsp_media_set_clock (media, g_value_get_object (value));
      break;
    case PROP_MAX_MCAST_TTL:
      gst_rtsp_media_set_max_mcast_ttl (media, g_value_get_uint (value));
      break;
    case PROP_BIND_MCAST_ADDRESS:
      gst_rtsp_media_set_bind_mcast_address (media,
          g_value_get_boolean (value));
      break;
    case PROP_DSCP_QOS:
      gst_rtsp_media_set_dscp_qos (media, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

typedef struct
{
  gint64 position;
  gboolean complete_streams_only;
  gboolean ret;
} DoQueryPositionData;

static void
do_query_position (GstRTSPStream * stream, DoQueryPositionData * data)
{
  gint64 tmp;

  if (!gst_rtsp_stream_is_sender (stream))
    return;

  if (data->complete_streams_only && !gst_rtsp_stream_is_complete (stream)) {
    GST_DEBUG_OBJECT (stream, "stream not complete, do not query position");
    return;
  }

  if (gst_rtsp_stream_query_position (stream, &tmp)) {
    data->position = MIN (data->position, tmp);
    data->ret = TRUE;
  }

  GST_INFO_OBJECT (stream, "media position: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (data->position));
}

static gboolean
default_query_position (GstRTSPMedia * media, gint64 * position)
{
  GstRTSPMediaPrivate *priv;
  DoQueryPositionData data;

  priv = media->priv;

  data.position = G_MAXINT64;
  data.ret = FALSE;

  /* if the media is complete, i.e. one or more streams have been configured
   * with sinks, then we want to query the position on those streams only.
   * a query on an incmplete stream may return a position that originates from
   * an earlier preroll */
  if (check_complete (media))
    data.complete_streams_only = TRUE;
  else
    data.complete_streams_only = FALSE;

  g_ptr_array_foreach (priv->streams, (GFunc) do_query_position, &data);

  if (!data.ret)
    *position = GST_CLOCK_TIME_NONE;
  else
    *position = data.position;

  return data.ret;
}

typedef struct
{
  gint64 stop;
  gboolean ret;
} DoQueryStopData;

static void
do_query_stop (GstRTSPStream * stream, DoQueryStopData * data)
{
  gint64 tmp = 0;

  if (gst_rtsp_stream_query_stop (stream, &tmp)) {
    data->stop = MAX (data->stop, tmp);
    data->ret = TRUE;
  }
}

static gboolean
default_query_stop (GstRTSPMedia * media, gint64 * stop)
{
  GstRTSPMediaPrivate *priv;
  DoQueryStopData data;

  priv = media->priv;

  data.stop = -1;
  data.ret = FALSE;

  g_ptr_array_foreach (priv->streams, (GFunc) do_query_stop, &data);

  *stop = data.stop;

  return data.ret;
}

static GstElement *
default_create_rtpbin (GstRTSPMedia * media)
{
  GstElement *rtpbin;

  rtpbin = gst_element_factory_make ("rtpbin", NULL);

  return rtpbin;
}

/* Must be called with priv->lock */
static gboolean
is_receive_only (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gboolean receive_only = TRUE;
  guint i;

  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
    if (gst_rtsp_stream_is_sender (stream) ||
        !gst_rtsp_stream_is_receiver (stream)) {
      receive_only = FALSE;
      break;
    }
  }

  return receive_only;
}

/* must be called with state lock */
static void
check_seekable (GstRTSPMedia * media)
{
  GstQuery *query;
  GstRTSPMediaPrivate *priv = media->priv;

  g_mutex_lock (&priv->lock);
  /* Update the seekable state of the pipeline in case it changed */
  if (is_receive_only (media)) {
    /* TODO: Seeking for "receive-only"? */
    priv->seekable = -1;
  } else {
    guint i, n = priv->streams->len;

    for (i = 0; i < n; i++) {
      GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);

      if (gst_rtsp_stream_get_publish_clock_mode (stream) ==
          GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK_AND_OFFSET) {
        priv->seekable = -1;
        g_mutex_unlock (&priv->lock);
        return;
      }
    }
  }

  query = gst_query_new_seeking (GST_FORMAT_TIME);
  if (gst_element_query (priv->pipeline, query)) {
    GstFormat format;
    gboolean seekable;
    gint64 start, end;

    gst_query_parse_seeking (query, &format, &seekable, &start, &end);
    priv->seekable = seekable ? G_MAXINT64 : 0;
  } else if (priv->streams->len) {
    gboolean seekable = TRUE;
    guint i, n = priv->streams->len;

    GST_DEBUG_OBJECT (media, "Checking %d streams", n);
    for (i = 0; i < n; i++) {
      GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
      seekable &= gst_rtsp_stream_seekable (stream);
    }
    priv->seekable = seekable ? G_MAXINT64 : -1;
  }

  GST_DEBUG_OBJECT (media, "seekable:%" G_GINT64_FORMAT, priv->seekable);
  g_mutex_unlock (&priv->lock);
  gst_query_unref (query);
}

/* must be called with state lock */
static gboolean
check_complete (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;

  guint i, n = priv->streams->len;

  for (i = 0; i < n; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);

    if (gst_rtsp_stream_is_complete (stream))
      return TRUE;
  }

  return FALSE;
}

/* must be called with state lock and private lock */
static void
collect_media_stats (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gint64 position = 0, stop = -1;

  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED &&
      priv->status != GST_RTSP_MEDIA_STATUS_PREPARING) {
    return;
  }

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
    g_mutex_unlock (&priv->lock);
    check_seekable (media);
    g_mutex_lock (&priv->lock);
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
 * Returns: (transfer full): a new #GstRTSPMedia object.
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
 * @pipeline: (transfer floating): a #GstPipeline
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
  GList *l;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));
  g_return_if_fail (GST_IS_PIPELINE (pipeline));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  old = priv->pipeline;
  priv->pipeline = gst_object_ref_sink (GST_ELEMENT_CAST (pipeline));
  nettime = priv->nettime;
  priv->nettime = NULL;
  g_mutex_unlock (&priv->lock);

  if (old)
    gst_object_unref (old);

  if (nettime)
    gst_object_unref (nettime);

  gst_bin_add (GST_BIN_CAST (pipeline), priv->element);

  for (l = priv->pending_pipeline_elements; l; l = l->next) {
    gst_bin_add (GST_BIN_CAST (pipeline), l->data);
  }
  g_list_free (priv->pending_pipeline_elements);
  priv->pending_pipeline_elements = NULL;
}

/**
 * gst_rtsp_media_set_permissions:
 * @media: a #GstRTSPMedia
 * @permissions: (transfer none) (nullable): a #GstRTSPPermissions
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
 * Returns: (transfer full) (nullable): a #GstRTSPPermissions object, unref after usage.
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
  guint i;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  GST_LOG_OBJECT (media, "set buffer size %u", size);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->buffer_size = size;

  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
    gst_rtsp_stream_set_buffer_size (stream, size);
  }
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

  g_mutex_lock (&priv->lock);
  res = priv->buffer_size;
  g_mutex_unlock (&priv->lock);

  return res;
}

static void
do_set_dscp_qos (GstRTSPStream * stream, gint * dscp_qos)
{
  gst_rtsp_stream_set_dscp_qos (stream, *dscp_qos);
}

/**
 * gst_rtsp_media_set_dscp_qos:
 * @media: a #GstRTSPMedia
 * @dscp_qos: a new dscp qos value (0-63, or -1 to disable)
 *
 * Configure the dscp qos of attached streams to @dscp_qos.
 *
 * Since: 1.18
 */
void
gst_rtsp_media_set_dscp_qos (GstRTSPMedia * media, gint dscp_qos)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  GST_LOG_OBJECT (media, "set DSCP QoS %d", dscp_qos);

  if (dscp_qos < -1 || dscp_qos > 63) {
    GST_WARNING_OBJECT (media, "trying to set illegal dscp qos %d", dscp_qos);
    return;
  }

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->dscp_qos = dscp_qos;
  g_ptr_array_foreach (priv->streams, (GFunc) do_set_dscp_qos, &dscp_qos);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_dscp_qos:
 * @media: a #GstRTSPMedia
 *
 * Get the configured DSCP QoS of attached media.
 *
 * Returns: the DSCP QoS value of attached streams or -1 if disabled.
 *
 * Since: 1.18
 */
gint
gst_rtsp_media_get_dscp_qos (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gint res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_unlock (&priv->lock);
  res = priv->dscp_qos;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_stop_on_disconnect:
 * @media: a #GstRTSPMedia
 * @stop_on_disconnect: the new value
 *
 * Set or unset if the pipeline for @media should be stopped when a
 * client disconnects without sending TEARDOWN.
 */
void
gst_rtsp_media_set_stop_on_disconnect (GstRTSPMedia * media,
    gboolean stop_on_disconnect)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->stop_on_disconnect = stop_on_disconnect;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_is_stop_on_disconnect:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media will be stopped when a client disconnects
 * without sending TEARDOWN.
 *
 * Returns: %TRUE if the media will be stopped when a client disconnects
 *     without sending TEARDOWN.
 */
gboolean
gst_rtsp_media_is_stop_on_disconnect (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), TRUE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->stop_on_disconnect;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_retransmission_time:
 * @media: a #GstRTSPMedia
 * @time: the new value
 *
 * Set the amount of time to store retransmission packets.
 */
void
gst_rtsp_media_set_retransmission_time (GstRTSPMedia * media, GstClockTime time)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  GST_LOG_OBJECT (media, "set retransmission time %" G_GUINT64_FORMAT, time);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->rtx_time = time;
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);

    gst_rtsp_stream_set_retransmission_time (stream, time);
  }
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_retransmission_time:
 * @media: a #GstRTSPMedia
 *
 * Get the amount of time to store retransmission data.
 *
 * Returns: the amount of time to store retransmission data.
 */
GstClockTime
gst_rtsp_media_get_retransmission_time (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstClockTime res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->rtx_time;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_do_retransmission:
 *
 * Set whether retransmission requests will be sent
 *
 * Since: 1.16
 */
void
gst_rtsp_media_set_do_retransmission (GstRTSPMedia * media,
    gboolean do_retransmission)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->do_retransmission = do_retransmission;

  if (priv->rtpbin)
    g_object_set (priv->rtpbin, "do-retransmission", do_retransmission, NULL);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_do_retransmission:
 *
 * Returns: Whether retransmission requests will be sent
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_media_get_do_retransmission (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), 0);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->do_retransmission;
  g_mutex_unlock (&priv->lock);

  return res;
}

static void
update_stream_storage_size (GstRTSPMedia * media, GstRTSPStream * stream,
    guint sessid)
{
  GObject *storage = NULL;

  g_signal_emit_by_name (G_OBJECT (media->priv->rtpbin), "get-storage",
      sessid, &storage);

  if (storage) {
    guint64 size_time = 0;

    if (!gst_rtsp_stream_is_tcp_receiver (stream))
      size_time = (media->priv->latency + 50) * GST_MSECOND;

    g_object_set (storage, "size-time", size_time, NULL);

    g_object_unref (storage);
  }
}

/**
 * gst_rtsp_media_set_latency:
 * @media: a #GstRTSPMedia
 * @latency: latency in milliseconds
 *
 * Configure the latency used for receiving media.
 */
void
gst_rtsp_media_set_latency (GstRTSPMedia * media, guint latency)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  GST_LOG_OBJECT (media, "set latency %ums", latency);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->latency = latency;
  if (priv->rtpbin) {
    g_object_set (priv->rtpbin, "latency", latency, NULL);

    for (i = 0; i < media->priv->streams->len; i++) {
      GstRTSPStream *stream = g_ptr_array_index (media->priv->streams, i);
      update_stream_storage_size (media, stream, i);
    }
  }

  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_latency:
 * @media: a #GstRTSPMedia
 *
 * Get the latency that is used for receiving media.
 *
 * Returns: latency in milliseconds
 */
guint
gst_rtsp_media_get_latency (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  guint res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->latency;
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

  g_mutex_lock (&priv->lock);
  res = priv->time_provider;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_clock:
 * @media: a #GstRTSPMedia
 * @clock: (nullable): #GstClock to be used
 *
 * Configure the clock used for the media.
 */
void
gst_rtsp_media_set_clock (GstRTSPMedia * media, GstClock * clock)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));
  g_return_if_fail (GST_IS_CLOCK (clock) || clock == NULL);

  GST_LOG_OBJECT (media, "setting clock %" GST_PTR_FORMAT, clock);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  if (priv->clock)
    gst_object_unref (priv->clock);
  priv->clock = clock ? gst_object_ref (clock) : NULL;
  if (priv->pipeline) {
    if (clock)
      gst_pipeline_use_clock (GST_PIPELINE_CAST (priv->pipeline), clock);
    else
      gst_pipeline_auto_clock (GST_PIPELINE_CAST (priv->pipeline));
  }

  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_set_publish_clock_mode:
 * @media: a #GstRTSPMedia
 * @mode: the clock publish mode
 *
 * Sets if and how the media clock should be published according to RFC7273.
 *
 * Since: 1.8
 */
void
gst_rtsp_media_set_publish_clock_mode (GstRTSPMedia * media,
    GstRTSPPublishClockMode mode)
{
  GstRTSPMediaPrivate *priv;
  guint i, n;

  priv = media->priv;
  g_mutex_lock (&priv->lock);
  priv->publish_clock_mode = mode;

  n = priv->streams->len;
  for (i = 0; i < n; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);

    gst_rtsp_stream_set_publish_clock_mode (stream, mode);
  }
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_publish_clock_mode:
 * @media: a #GstRTSPMedia
 *
 * Gets if and how the media clock should be published according to RFC7273.
 *
 * Returns: The GstRTSPPublishClockMode
 *
 * Since: 1.8
 */
GstRTSPPublishClockMode
gst_rtsp_media_get_publish_clock_mode (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPPublishClockMode ret;

  priv = media->priv;
  g_mutex_lock (&priv->lock);
  ret = priv->publish_clock_mode;
  g_mutex_unlock (&priv->lock);

  return ret;
}

/**
 * gst_rtsp_media_set_address_pool:
 * @media: a #GstRTSPMedia
 * @pool: (transfer none) (nullable): a #GstRTSPAddressPool
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
 * Returns: (transfer full) (nullable): the #GstRTSPAddressPool of @media.
 * g_object_unref() after usage.
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
 * gst_rtsp_media_set_multicast_iface:
 * @media: a #GstRTSPMedia
 * @multicast_iface: (transfer none) (nullable): a multicast interface name
 *
 * configure @multicast_iface to be used for @media.
 */
void
gst_rtsp_media_set_multicast_iface (GstRTSPMedia * media,
    const gchar * multicast_iface)
{
  GstRTSPMediaPrivate *priv;
  gchar *old;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  GST_LOG_OBJECT (media, "set multicast interface %s", multicast_iface);

  g_mutex_lock (&priv->lock);
  if ((old = priv->multicast_iface) != multicast_iface)
    priv->multicast_iface = multicast_iface ? g_strdup (multicast_iface) : NULL;
  else
    old = NULL;
  g_ptr_array_foreach (priv->streams,
      (GFunc) gst_rtsp_stream_set_multicast_iface, (gchar *) multicast_iface);
  g_mutex_unlock (&priv->lock);

  if (old)
    g_free (old);
}

/**
 * gst_rtsp_media_get_multicast_iface:
 * @media: a #GstRTSPMedia
 *
 * Get the multicast interface used for @media.
 *
 * Returns: (transfer full) (nullable): the multicast interface for @media.
 * g_free() after usage.
 */
gchar *
gst_rtsp_media_get_multicast_iface (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->multicast_iface))
    result = g_strdup (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_media_set_max_mcast_ttl:
 * @media: a #GstRTSPMedia
 * @ttl: the new multicast ttl value
 *
 * Set the maximum time-to-live value of outgoing multicast packets.
 *
 * Returns: %TRUE if the requested ttl has been set successfully.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_media_set_max_mcast_ttl (GstRTSPMedia * media, guint ttl)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  GST_LOG_OBJECT (media, "set max mcast ttl %u", ttl);

  priv = media->priv;

  g_mutex_lock (&priv->lock);

  if (ttl == 0 || ttl > DEFAULT_MAX_MCAST_TTL) {
    GST_WARNING_OBJECT (media, "The reqested mcast TTL value is not valid.");
    g_mutex_unlock (&priv->lock);
    return FALSE;
  }
  priv->max_mcast_ttl = ttl;

  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
    gst_rtsp_stream_set_max_mcast_ttl (stream, ttl);
  }
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_media_get_max_mcast_ttl:
 * @media: a #GstRTSPMedia
 *
 * Get the the maximum time-to-live value of outgoing multicast packets.
 *
 * Returns: the maximum time-to-live value of outgoing multicast packets.
 *
 * Since: 1.16
 */
guint
gst_rtsp_media_get_max_mcast_ttl (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  guint res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->max_mcast_ttl;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_set_bind_mcast_address:
 * @media: a #GstRTSPMedia
 * @bind_mcast_addr: the new value
 *
 * Decide whether the multicast socket should be bound to a multicast address or
 * INADDR_ANY.
 *
 * Since: 1.16
 */
void
gst_rtsp_media_set_bind_mcast_address (GstRTSPMedia * media,
    gboolean bind_mcast_addr)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->bind_mcast_address = bind_mcast_addr;
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
    gst_rtsp_stream_set_bind_mcast_address (stream, bind_mcast_addr);
  }
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_is_bind_mcast_address:
 * @media: a #GstRTSPMedia
 *
 * Check if multicast sockets are configured to be bound to multicast addresses.
 *
 * Returns: %TRUE if multicast sockets are configured to be bound to multicast addresses.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_media_is_bind_mcast_address (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  result = priv->bind_mcast_address;
  g_mutex_unlock (&priv->lock);

  return result;
}

void
gst_rtsp_media_set_enable_rtcp (GstRTSPMedia * media, gboolean enable)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->enable_rtcp = enable;
  g_mutex_unlock (&priv->lock);
}

static GList *
_find_payload_types (GstRTSPMedia * media)
{
  gint i, n;
  GQueue queue = G_QUEUE_INIT;

  n = media->priv->streams->len;
  for (i = 0; i < n; i++) {
    GstRTSPStream *stream = g_ptr_array_index (media->priv->streams, i);
    guint pt = gst_rtsp_stream_get_pt (stream);

    g_queue_push_tail (&queue, GUINT_TO_POINTER (pt));
  }

  return queue.head;
}

static guint
_next_available_pt (GList * payloads)
{
  guint i;

  for (i = 96; i <= 127; i++) {
    GList *iter = g_list_find (payloads, GINT_TO_POINTER (i));
    if (!iter)
      return GPOINTER_TO_UINT (i);
  }

  return 0;
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
 *
 * Find all depayloader elements, they should be named depay\%d in the
 * element of @media, and create #GstRTSPStreams for them.
 */
void
gst_rtsp_media_collect_streams (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstElement *element, *elem;
  GstPad *pad;
  gint i;
  gboolean have_elem;
  gboolean more_elem_remaining = TRUE;
  GstRTSPTransportMode mode = 0;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;
  element = priv->element;

  have_elem = FALSE;
  for (i = 0; more_elem_remaining; i++) {
    gchar *name;

    more_elem_remaining = FALSE;

    name = g_strdup_printf ("pay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      GstElement *pay;
      GST_INFO ("found stream %d with payloader %p", i, elem);

      /* take the pad of the payloader */
      pad = gst_element_get_static_pad (elem, "src");

      /* find the real payload element in case elem is a GstBin */
      pay = find_payload_element (elem, pad);

      /* create the stream */
      if (pay == NULL) {
        GST_WARNING ("could not find real payloader, using bin");
        gst_rtsp_media_create_stream (media, elem, pad);
      } else {
        gst_rtsp_media_create_stream (media, pay, pad);
        gst_object_unref (pay);
      }

      gst_object_unref (pad);
      gst_object_unref (elem);

      have_elem = TRUE;
      more_elem_remaining = TRUE;
      mode |= GST_RTSP_TRANSPORT_MODE_PLAY;
    }
    g_free (name);

    name = g_strdup_printf ("dynpay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      /* a stream that will dynamically create pads to provide RTP packets */
      GST_INFO ("found dynamic element %d, %p", i, elem);

      g_mutex_lock (&priv->lock);
      priv->dynamic = g_list_prepend (priv->dynamic, elem);
      g_mutex_unlock (&priv->lock);

      priv->nb_dynamic_elements++;

      have_elem = TRUE;
      more_elem_remaining = TRUE;
      mode |= GST_RTSP_TRANSPORT_MODE_PLAY;
    }
    g_free (name);

    name = g_strdup_printf ("depay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      GST_INFO ("found stream %d with depayloader %p", i, elem);

      /* take the pad of the payloader */
      pad = gst_element_get_static_pad (elem, "sink");
      /* create the stream */
      gst_rtsp_media_create_stream (media, elem, pad);
      gst_object_unref (pad);
      gst_object_unref (elem);

      have_elem = TRUE;
      more_elem_remaining = TRUE;
      mode |= GST_RTSP_TRANSPORT_MODE_RECORD;
    }
    g_free (name);
  }

  if (have_elem) {
    if (priv->transport_mode != mode)
      GST_WARNING ("found different mode than expected (0x%02x != 0x%02d)",
          priv->transport_mode, mode);
  }
}

typedef struct
{
  GstElement *appsink, *appsrc;
  GstRTSPStream *stream;
} AppSinkSrcData;

static GstFlowReturn
appsink_new_sample (GstAppSink * appsink, gpointer user_data)
{
  AppSinkSrcData *data = user_data;
  GstSample *sample;
  GstFlowReturn ret;

  sample = gst_app_sink_pull_sample (appsink);
  if (!sample)
    return GST_FLOW_FLUSHING;


  ret = gst_app_src_push_sample (GST_APP_SRC (data->appsrc), sample);
  gst_sample_unref (sample);
  return ret;
}

static GstAppSinkCallbacks appsink_callbacks = {
  NULL,
  NULL,
  appsink_new_sample,
};

static GstPadProbeReturn
appsink_pad_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  AppSinkSrcData *data = user_data;

  if (GST_IS_EVENT (info->data)
      && GST_EVENT_TYPE (info->data) == GST_EVENT_LATENCY) {
    GstClockTime min, max;

    if (gst_base_sink_query_latency (GST_BASE_SINK (data->appsink), NULL, NULL,
            &min, &max)) {
      g_object_set (data->appsrc, "min-latency", min, "max-latency", max, NULL);
      GST_DEBUG ("setting latency to min %" GST_TIME_FORMAT " max %"
          GST_TIME_FORMAT, GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    }
  } else if (GST_IS_QUERY (info->data)) {
    GstPad *srcpad = gst_element_get_static_pad (data->appsrc, "src");
    if (gst_pad_peer_query (srcpad, GST_QUERY_CAST (info->data))) {
      gst_object_unref (srcpad);
      return GST_PAD_PROBE_HANDLED;
    }
    gst_object_unref (srcpad);
  }

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
appsrc_pad_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  AppSinkSrcData *data = user_data;

  if (GST_IS_QUERY (info->data)) {
    GstPad *sinkpad = gst_element_get_static_pad (data->appsink, "sink");
    if (gst_pad_peer_query (sinkpad, GST_QUERY_CAST (info->data))) {
      gst_object_unref (sinkpad);
      return GST_PAD_PROBE_HANDLED;
    }
    gst_object_unref (sinkpad);
  }

  return GST_PAD_PROBE_OK;
}

/**
 * gst_rtsp_media_create_stream:
 * @media: a #GstRTSPMedia
 * @payloader: a #GstElement
 * @pad: a #GstPad
 *
 * Create a new stream in @media that provides RTP data on @pad.
 * @pad should be a pad of an element inside @media->element.
 *
 * Returns: (transfer none): a new #GstRTSPStream that remains valid for as long
 * as @media exists.
 */
GstRTSPStream *
gst_rtsp_media_create_stream (GstRTSPMedia * media, GstElement * payloader,
    GstPad * pad)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPStream *stream;
  GstPad *streampad;
  gchar *name;
  gint idx;
  AppSinkSrcData *data = NULL;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (payloader), NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  idx = priv->streams->len;

  GST_DEBUG ("media %p: creating stream with index %d and payloader %"
      GST_PTR_FORMAT, media, idx, payloader);

  if (GST_PAD_IS_SRC (pad))
    name = g_strdup_printf ("src_%u", idx);
  else
    name = g_strdup_printf ("sink_%u", idx);

  if ((GST_PAD_IS_SRC (pad) && priv->element->numsinkpads > 0) ||
      (GST_PAD_IS_SINK (pad) && priv->element->numsrcpads > 0)) {
    GstElement *appsink, *appsrc;
    GstPad *sinkpad, *srcpad;

    appsink = gst_element_factory_make ("appsink", NULL);
    appsrc = gst_element_factory_make ("appsrc", NULL);

    if (GST_PAD_IS_SINK (pad)) {
      srcpad = gst_element_get_static_pad (appsrc, "src");

      gst_bin_add (GST_BIN (priv->element), appsrc);

      gst_pad_link (srcpad, pad);
      gst_object_unref (srcpad);

      streampad = gst_element_get_static_pad (appsink, "sink");

      priv->pending_pipeline_elements =
          g_list_prepend (priv->pending_pipeline_elements, appsink);
    } else {
      sinkpad = gst_element_get_static_pad (appsink, "sink");

      gst_pad_link (pad, sinkpad);
      gst_object_unref (sinkpad);

      streampad = gst_element_get_static_pad (appsrc, "src");

      priv->pending_pipeline_elements =
          g_list_prepend (priv->pending_pipeline_elements, appsrc);
    }

    g_object_set (appsrc, "block", TRUE, "format", GST_FORMAT_TIME, "is-live",
        TRUE, "emit-signals", FALSE, NULL);
    g_object_set (appsink, "sync", FALSE, "async", FALSE, "emit-signals",
        FALSE, "buffer-list", TRUE, NULL);

    data = g_new0 (AppSinkSrcData, 1);
    data->appsink = appsink;
    data->appsrc = appsrc;

    sinkpad = gst_element_get_static_pad (appsink, "sink");
    gst_pad_add_probe (sinkpad,
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM | GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
        appsink_pad_probe, data, NULL);
    gst_object_unref (sinkpad);

    srcpad = gst_element_get_static_pad (appsrc, "src");
    gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_QUERY_UPSTREAM,
        appsrc_pad_probe, data, NULL);
    gst_object_unref (srcpad);

    gst_app_sink_set_callbacks (GST_APP_SINK (appsink), &appsink_callbacks,
        data, NULL);
    g_object_set_data_full (G_OBJECT (streampad), "media-appsink-appsrc", data,
        g_free);
  } else {
    streampad = gst_ghost_pad_new (name, pad);
    gst_pad_set_active (streampad, TRUE);
    gst_element_add_pad (priv->element, streampad);
  }
  g_free (name);

  stream = gst_rtsp_stream_new (idx, payloader, streampad);
  if (data)
    data->stream = stream;
  if (priv->pool)
    gst_rtsp_stream_set_address_pool (stream, priv->pool);
  gst_rtsp_stream_set_multicast_iface (stream, priv->multicast_iface);
  gst_rtsp_stream_set_max_mcast_ttl (stream, priv->max_mcast_ttl);
  gst_rtsp_stream_set_bind_mcast_address (stream, priv->bind_mcast_address);
  gst_rtsp_stream_set_enable_rtcp (stream, priv->enable_rtcp);
  gst_rtsp_stream_set_profiles (stream, priv->profiles);
  gst_rtsp_stream_set_protocols (stream, priv->protocols);
  gst_rtsp_stream_set_retransmission_time (stream, priv->rtx_time);
  gst_rtsp_stream_set_buffer_size (stream, priv->buffer_size);
  gst_rtsp_stream_set_publish_clock_mode (stream, priv->publish_clock_mode);
  gst_rtsp_stream_set_rate_control (stream, priv->do_rate_control);

  g_ptr_array_add (priv->streams, stream);

  if (GST_PAD_IS_SRC (pad)) {
    gint i, n;

    if (priv->payloads)
      g_list_free (priv->payloads);
    priv->payloads = _find_payload_types (media);

    n = priv->streams->len;
    for (i = 0; i < n; i++) {
      GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
      guint rtx_pt = _next_available_pt (priv->payloads);

      if (rtx_pt == 0) {
        GST_WARNING ("Ran out of space of dynamic payload types");
        break;
      }

      gst_rtsp_stream_set_retransmission_pt (stream, rtx_pt);

      priv->payloads =
          g_list_append (priv->payloads, GUINT_TO_POINTER (rtx_pt));
    }
  }
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
  AppSinkSrcData *data;

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  /* remove the ghostpad */
  srcpad = gst_rtsp_stream_get_srcpad (stream);
  data = g_object_get_data (G_OBJECT (srcpad), "media-appsink-appsrc");
  if (data) {
    if (GST_OBJECT_PARENT (data->appsrc) == GST_OBJECT_CAST (priv->pipeline))
      gst_bin_remove (GST_BIN_CAST (priv->pipeline), data->appsrc);
    else if (GST_OBJECT_PARENT (data->appsrc) ==
        GST_OBJECT_CAST (priv->element))
      gst_bin_remove (GST_BIN_CAST (priv->element), data->appsrc);
    if (GST_OBJECT_PARENT (data->appsink) == GST_OBJECT_CAST (priv->pipeline))
      gst_bin_remove (GST_BIN_CAST (priv->pipeline), data->appsink);
    else if (GST_OBJECT_PARENT (data->appsink) ==
        GST_OBJECT_CAST (priv->element))
      gst_bin_remove (GST_BIN_CAST (priv->element), data->appsink);
  } else {
    gst_element_remove_pad (priv->element, srcpad);
  }
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
 * Returns: (nullable) (transfer none): the #GstRTSPStream at index
 * @idx or %NULL when a stream with that index did not exist.
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
 * Returns: (nullable) (transfer none): the #GstRTSPStream with
 * control uri @control or %NULL when a stream with that control did
 * not exist.
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
 * Returns: (transfer full) (nullable): The range as a string, g_free() after usage.
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

  /* Update the range value with current position/duration */
  g_mutex_lock (&priv->lock);
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

/**
 * gst_rtsp_media_get_rates:
 * @media: a #GstRTSPMedia
 * @rate: (optional) (out caller-allocates): the rate of the current segment
 * @applied_rate: (optional) (out caller-allocates): the applied_rate of the current segment
 *
 * Get the rate and applied_rate of the current segment.
 *
 * Returns: %FALSE if looking up the rate and applied rate failed. Otherwise
 * %TRUE is returned and @rate and @applied_rate are set to the rate and
 * applied_rate of the current segment.
 * Since: 1.18
 */
gboolean
gst_rtsp_media_get_rates (GstRTSPMedia * media, gdouble * rate,
    gdouble * applied_rate)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPStream *stream;
  gdouble save_rate, save_applied_rate;
  gboolean result = TRUE;
  gboolean first_stream = TRUE;
  gint i;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  if (!rate && !applied_rate) {
    GST_WARNING_OBJECT (media, "rate and applied_rate are both NULL");
    return FALSE;
  }

  priv = media->priv;

  g_mutex_lock (&priv->lock);

  g_assert (priv->streams->len > 0);
  for (i = 0; i < priv->streams->len; i++) {
    stream = g_ptr_array_index (priv->streams, i);
    if (gst_rtsp_stream_is_complete (stream)
        && gst_rtsp_stream_is_sender (stream)) {
      if (gst_rtsp_stream_get_rates (stream, rate, applied_rate)) {
        if (first_stream) {
          save_rate = *rate;
          save_applied_rate = *applied_rate;
          first_stream = FALSE;
        } else {
          if (save_rate != *rate || save_applied_rate != *applied_rate) {
            /* diffrent rate or applied_rate, weird */
            g_assert (FALSE);
            result = FALSE;
            break;
          }
        }
      } else {
        /* complete stream withot rate and applied_rate, weird */
        g_assert (FALSE);
        result = FALSE;
        break;
      }
    }
  }

  if (!result) {
    GST_WARNING_OBJECT (media,
        "failed to obtain consistent rate and applied_rate");
  }

  g_mutex_unlock (&priv->lock);

  return result;
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

  if (!blocked)
    priv->blocking_msg_received = 0;
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

/**
 * gst_rtsp_media_seek_trickmode:
 * @media: a #GstRTSPMedia
 * @range: (transfer none): a #GstRTSPTimeRange
 * @flags: The minimal set of #GstSeekFlags to use
 * @rate: the rate to use in the seek
 * @trickmode_interval: The trickmode interval to use for KEY_UNITS trick mode
 *
 * Seek the pipeline of @media to @range with the given @flags and @rate,
 * and @trickmode_interval.
 * @media must be prepared with gst_rtsp_media_prepare().
 * In order to perform the seek operation, the pipeline must contain all
 * needed transport parts (transport sinks).
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.18
 */
gboolean
gst_rtsp_media_seek_trickmode (GstRTSPMedia * media,
    GstRTSPTimeRange * range, GstSeekFlags flags, gdouble rate,
    GstClockTime trickmode_interval)
{
  GstRTSPMediaClass *klass;
  GstRTSPMediaPrivate *priv;
  gboolean res;
  GstClockTime start, stop;
  GstSeekType start_type, stop_type;
  gint64 current_position;
  gboolean force_seek;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  /* if there's a range then klass->convert_range must be set */
  g_return_val_if_fail (range == NULL || klass->convert_range != NULL, FALSE);

  GST_DEBUG ("flags=%x  rate=%f", flags, rate);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED)
    goto not_prepared;

  /* check if the media pipeline is complete in order to perform a
   * seek operation on it */
  if (!check_complete (media))
    goto not_complete;

  /* Update the seekable state of the pipeline in case it changed */
  check_seekable (media);

  if (priv->seekable == 0) {
    GST_FIXME_OBJECT (media, "Handle going back to 0 for none live"
        " not seekable streams.");

    goto not_seekable;
  } else if (priv->seekable < 0) {
    goto not_seekable;
  }

  start_type = stop_type = GST_SEEK_TYPE_NONE;
  start = stop = GST_CLOCK_TIME_NONE;

  /* if caller provided a range convert it to NPT format
   * if no range provided the seek is assumed to be the same position but with
   * e.g. the rate changed */
  if (range != NULL) {
    if (!klass->convert_range (media, range, GST_RTSP_RANGE_NPT))
      goto not_supported;
    gst_rtsp_range_get_times (range, &start, &stop);

    GST_INFO ("got %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
    GST_INFO ("current %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (priv->range_start), GST_TIME_ARGS (priv->range_stop));
  }

  current_position = -1;
  if (klass->query_position)
    klass->query_position (media, &current_position);
  GST_INFO ("current media position %" GST_TIME_FORMAT,
      GST_TIME_ARGS (current_position));

  if (start != GST_CLOCK_TIME_NONE)
    start_type = GST_SEEK_TYPE_SET;

  if (stop != GST_CLOCK_TIME_NONE)
    stop_type = GST_SEEK_TYPE_SET;

  /* we force a seek if any trickmode flag is set, or if the flush flag is set or
   * the rate is non-standard, i.e. not 1.0 */
  force_seek = (flags & TRICKMODE_FLAGS) || (flags & GST_SEEK_FLAG_FLUSH) ||
      rate != 1.0;

  if (start != GST_CLOCK_TIME_NONE || stop != GST_CLOCK_TIME_NONE || force_seek) {
    GST_INFO ("seeking to %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

    /* depends on the current playing state of the pipeline. We might need to
     * queue this until we get EOS. */
    flags |= GST_SEEK_FLAG_FLUSH;

    /* if range start was not supplied we must continue from current position.
     * but since we're doing a flushing seek, let us query the current position
     * so we end up at exactly the same position after the seek. */
    if (range == NULL || range->min.type == GST_RTSP_TIME_END) {
      if (current_position == -1) {
        GST_WARNING ("current position unknown");
      } else {
        GST_DEBUG ("doing accurate seek to %" GST_TIME_FORMAT,
            GST_TIME_ARGS (current_position));
        start = current_position;
        start_type = GST_SEEK_TYPE_SET;
      }
    }

    if (!force_seek &&
        (start_type == GST_SEEK_TYPE_NONE || start == current_position) &&
        (stop_type == GST_SEEK_TYPE_NONE || stop == priv->range_stop)) {
      GST_DEBUG ("no position change, no flags set by caller, so not seeking");
      res = TRUE;
    } else {
      GstEvent *seek_event;
      gboolean unblock = FALSE;

      /* Handle expected async-done before waiting on next async-done.
       *
       * Since the seek further down in code will cause a preroll and
       * a async-done will be generated it's important to wait on async-done
       * if that is expected. Otherwise there is the risk that the waiting
       * for async-done after the seek is detecting the expected async-done
       * instead of the one that corresponds to the seek. Then execution
       * continue and act as if the pipeline is prerolled, but it's not.
       *
       * During wait_preroll message GST_MESSAGE_ASYNC_DONE will come
       * and then the state will change from preparing to prepared */
      if (priv->expected_async_done) {
        GST_DEBUG (" expected to get async-done, waiting ");
        gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARING);
        g_rec_mutex_unlock (&priv->state_lock);

        /* wait until pipeline is prerolled  */
        if (!wait_preroll (media))
          goto preroll_failed_expected_async_done;

        g_rec_mutex_lock (&priv->state_lock);
        GST_DEBUG (" got expected async-done");
      }

      gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARING);

      if (rate < 0.0) {
        GstClockTime temp_time = start;
        GstSeekType temp_type = start_type;

        start = stop;
        start_type = stop_type;
        stop = temp_time;
        stop_type = temp_type;
      }

      seek_event = gst_event_new_seek (rate, GST_FORMAT_TIME, flags, start_type,
          start, stop_type, stop);

      gst_event_set_seek_trickmode_interval (seek_event, trickmode_interval);

      if (!media->priv->blocked) {
        /* Prevent a race condition with multiple streams,
         * where one stream may have time to preroll before others
         * have even started flushing, causing async-done to be
         * posted too early.
         */
        media_streams_set_blocked (media, TRUE);
        unblock = TRUE;
      }

      res = gst_element_send_event (priv->pipeline, seek_event);

      if (unblock)
        media_streams_set_blocked (media, FALSE);

      /* and block for the seek to complete */
      GST_INFO ("done seeking %d", res);
      if (!res)
        goto seek_failed;

      g_rec_mutex_unlock (&priv->state_lock);

      /* wait until pipeline is prerolled again, this will also collect stats */
      if (!wait_preroll (media))
        goto preroll_failed;

      g_rec_mutex_lock (&priv->state_lock);
      GST_INFO ("prerolled again");
    }
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
not_complete:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("pipeline is not complete");
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
seek_failed:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("seeking failed");
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    return FALSE;
  }
preroll_failed:
  {
    GST_WARNING ("failed to preroll after seek");
    return FALSE;
  }
preroll_failed_expected_async_done:
  {
    GST_WARNING ("failed to preroll");
    return FALSE;
  }
}

/**
 * gst_rtsp_media_seek_full:
 * @media: a #GstRTSPMedia
 * @range: (transfer none): a #GstRTSPTimeRange
 * @flags: The minimal set of #GstSeekFlags to use
 *
 * Seek the pipeline of @media to @range with the given @flags.
 * @media must be prepared with gst_rtsp_media_prepare().
 *
 * Returns: %TRUE on success.
 * Since: 1.18
 */
gboolean
gst_rtsp_media_seek_full (GstRTSPMedia * media, GstRTSPTimeRange * range,
    GstSeekFlags flags)
{
  return gst_rtsp_media_seek_trickmode (media, range, flags, 1.0, 0);
}

/**
 * gst_rtsp_media_seek:
 * @media: a #GstRTSPMedia
 * @range: (transfer none): a #GstRTSPTimeRange
 *
 * Seek the pipeline of @media to @range. @media must be prepared with
 * gst_rtsp_media_prepare().
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_seek (GstRTSPMedia * media, GstRTSPTimeRange * range)
{
  return gst_rtsp_media_seek_trickmode (media, range, GST_SEEK_FLAG_NONE,
      1.0, 0);
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

static void
stream_collect_receiver_streams (GstRTSPStream * stream,
    guint * receiver_streams)
{
  if (!gst_rtsp_stream_is_sender (stream))
    (*receiver_streams)++;
}

static guint
get_num_receiver_streams (GstRTSPMedia * media)
{
  guint ret = 0;

  g_ptr_array_foreach (media->priv->streams,
      (GFunc) stream_collect_receiver_streams, &ret);

  return ret;
}


static void
stream_collect_complete_sender (GstRTSPStream * stream, guint * active_streams)
{
  if (gst_rtsp_stream_is_complete (stream)
      && gst_rtsp_stream_is_sender (stream))
    (*active_streams)++;
}

static guint
get_num_complete_sender_streams (GstRTSPMedia * media)
{
  guint ret = 0;

  g_ptr_array_foreach (media->priv->streams,
      (GFunc) stream_collect_complete_sender, &ret);

  return ret;
}

 /* called with state-lock */
/* called with state-lock */
static gboolean
default_handle_message (GstRTSPMedia * media, GstMessage * message)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstMessageType type;

  type = GST_MESSAGE_TYPE (message);

  switch (type) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;

      if (GST_MESSAGE_SRC (message) != GST_OBJECT (priv->pipeline))
        break;

      gst_message_parse_state_changed (message, &old, &new, &pending);

      GST_DEBUG ("%p: went from %s to %s (pending %s)", media,
          gst_element_state_get_name (old), gst_element_state_get_name (new),
          gst_element_state_get_name (pending));
      if (priv->no_more_pads_pending == 0
          && gst_rtsp_media_is_receive_only (media) && old == GST_STATE_READY
          && new == GST_STATE_PAUSED) {
        GST_INFO ("%p: went to PAUSED, prepared now", media);
        g_mutex_lock (&priv->lock);
        collect_media_stats (media);
        g_mutex_unlock (&priv->lock);

        if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING)
          gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
      }

      break;
    }
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
        gboolean is_complete = FALSE;
        guint num_complete_sender_streams =
            get_num_complete_sender_streams (media);
        guint num_recv_streams = get_num_receiver_streams (media);
        guint expected_num_blocking_msg;

        /* to prevent problems when some streams are complete, some are not,
         * we will ignore incomplete streams. When there are no complete
         * streams (during DESCRIBE), we will listen to all streams. */

        gst_structure_get_boolean (s, "is_complete", &is_complete);
        expected_num_blocking_msg = num_complete_sender_streams;
        GST_DEBUG_OBJECT (media, "media received blocking message,"
            " num_complete_sender_streams = %d, is_complete = %d",
            num_complete_sender_streams, is_complete);

        if (num_complete_sender_streams == 0 || is_complete)
          priv->blocking_msg_received++;

        if (num_complete_sender_streams == 0)
          expected_num_blocking_msg = priv->streams->len - num_recv_streams;

        if (priv->blocked && media_streams_blocking (media) &&
            priv->no_more_pads_pending == 0 &&
            priv->blocking_msg_received == expected_num_blocking_msg) {
          GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message), "media is blocking");
          g_mutex_lock (&priv->lock);
          collect_media_stats (media);
          g_mutex_unlock (&priv->lock);

          if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING)
            gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);

          priv->blocking_msg_received = 0;
        }
      }
      break;
    }
    case GST_MESSAGE_STREAM_STATUS:
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (priv->expected_async_done)
        priv->expected_async_done = FALSE;
      if (priv->complete) {
        /* receive the final ASYNC_DONE, that is posted by the media pipeline
         * after all the transport parts have been successfully added to
         * the media streams. */
        GST_DEBUG_OBJECT (media, "got async-done");
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
  GQuark detail = 0;
  gboolean ret;

  detail = gst_message_type_to_quark (GST_MESSAGE_TYPE (message));

  g_rec_mutex_lock (&priv->state_lock);
  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_HANDLE_MESSAGE], detail,
      message, &ret);
  if (!ret) {
    GST_DEBUG_OBJECT (media, "failed emitting pipeline message");
  }
  g_rec_mutex_unlock (&priv->state_lock);

  return TRUE;
}

static void
watch_destroyed (GstRTSPMedia * media)
{
  GST_DEBUG_OBJECT (media, "source destroyed");
  g_object_unref (media);
}

static gboolean
is_payloader (GstElement * element)
{
  GstElementClass *eclass = GST_ELEMENT_GET_CLASS (element);
  const gchar *klass;

  klass = gst_element_class_get_metadata (eclass, GST_ELEMENT_METADATA_KLASS);
  if (klass == NULL)
    return FALSE;

  if (strstr (klass, "Payloader") && strstr (klass, "RTP")) {
    return TRUE;
  }

  return FALSE;
}

static GstElement *
find_payload_element (GstElement * payloader, GstPad * pad)
{
  GstElement *pay = NULL;

  if (GST_IS_BIN (payloader)) {
    GstIterator *iter;
    GValue item = { 0 };
    gchar *pad_name, *payloader_name;
    GstElement *element;

    if ((element = gst_bin_get_by_name (GST_BIN (payloader), "pay"))) {
      if (is_payloader (element))
        return element;
      gst_object_unref (element);
    }

    pad_name = gst_object_get_name (GST_OBJECT (pad));
    payloader_name = g_strdup_printf ("pay_%s", pad_name);
    g_free (pad_name);
    if ((element = gst_bin_get_by_name (GST_BIN (payloader), payloader_name))) {
      g_free (payloader_name);
      if (is_payloader (element))
        return element;
      gst_object_unref (element);
    } else {
      g_free (payloader_name);
    }

    iter = gst_bin_iterate_recurse (GST_BIN (payloader));
    while (gst_iterator_next (iter, &item) == GST_ITERATOR_OK) {
      element = (GstElement *) g_value_get_object (&item);

      if (is_payloader (element)) {
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
  pay = find_payload_element (element, pad);
  stream = gst_rtsp_media_create_stream (media, pay, pad);
  gst_object_unref (pay);

  GST_INFO ("pad added %s:%s, stream %p", GST_DEBUG_PAD_NAME (pad), stream);

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARING)
    goto not_preparing;

  g_object_set_data (G_OBJECT (pad), "gst-rtsp-dynpad-stream", stream);

  /* join the element in the PAUSED state because this callback is
   * called from the streaming thread and it is PAUSED */
  if (!gst_rtsp_stream_join_bin (stream, GST_BIN (priv->pipeline),
          priv->rtpbin, GST_STATE_PAUSED)) {
    GST_WARNING ("failed to join bin element");
  }

  if (priv->blocked)
    gst_rtsp_stream_set_blocked (stream, TRUE);

  g_rec_mutex_unlock (&priv->state_lock);

  return;

  /* ERRORS */
not_preparing:
  {
    gst_rtsp_media_remove_stream (media, stream);
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("ignore pad because we are not preparing");
    return;
  }
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
no_more_pads_cb (GstElement * element, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;

  GST_INFO_OBJECT (element, "no more pads");
  g_mutex_lock (&priv->lock);
  priv->no_more_pads_pending--;
  g_mutex_unlock (&priv->lock);
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

  /* start blocked since it is possible that there are no sink elements yet */
  media_streams_set_blocked (media, TRUE);
  ret = set_target_state (media, GST_STATE_PAUSED, TRUE);

  switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
      GST_INFO ("SUCCESS state change for media %p", media);
      break;
    case GST_STATE_CHANGE_ASYNC:
      GST_INFO ("ASYNC state change for media %p", media);
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      /* we need to go to PLAYING */
      GST_INFO ("NO_PREROLL state change: live media %p", media);
      /* FIXME we disable seeking for live streams for now. We should perform a
       * seeking query in preroll instead */
      priv->seekable = -1;
      priv->is_live = TRUE;

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

static GstElement *
request_aux_sender (GstElement * rtpbin, guint sessid, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPStream *stream = NULL;
  guint i;
  GstElement *res = NULL;

  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    stream = g_ptr_array_index (priv->streams, i);

    if (sessid == gst_rtsp_stream_get_index (stream))
      break;

    stream = NULL;
  }
  g_mutex_unlock (&priv->lock);

  if (stream)
    res = gst_rtsp_stream_request_aux_sender (stream, sessid);

  return res;
}

static GstElement *
request_aux_receiver (GstElement * rtpbin, guint sessid, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPStream *stream = NULL;
  guint i;
  GstElement *res = NULL;

  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    stream = g_ptr_array_index (priv->streams, i);

    if (sessid == gst_rtsp_stream_get_index (stream))
      break;

    stream = NULL;
  }
  g_mutex_unlock (&priv->lock);

  if (stream)
    res = gst_rtsp_stream_request_aux_receiver (stream, sessid);

  return res;
}

static GstElement *
request_fec_decoder (GstElement * rtpbin, guint sessid, GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPStream *stream = NULL;
  guint i;
  GstElement *res = NULL;

  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    stream = g_ptr_array_index (priv->streams, i);

    if (sessid == gst_rtsp_stream_get_index (stream))
      break;

    stream = NULL;
  }
  g_mutex_unlock (&priv->lock);

  if (stream) {
    res = gst_rtsp_stream_request_ulpfec_decoder (stream, rtpbin, sessid);
  }

  return res;
}

static gboolean
start_prepare (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  guint i;
  GList *walk;

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARING)
    goto no_longer_preparing;

  g_signal_connect (priv->rtpbin, "request-fec-decoder",
      G_CALLBACK (request_fec_decoder), media);

  /* link streams we already have, other streams might appear when we have
   * dynamic elements */
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream;

    stream = g_ptr_array_index (priv->streams, i);

    if (priv->rtx_time > 0) {
      /* enable retransmission by setting rtprtxsend as the "aux" element of rtpbin */
      g_signal_connect (priv->rtpbin, "request-aux-sender",
          (GCallback) request_aux_sender, media);
    }

    if (priv->do_retransmission) {
      g_signal_connect (priv->rtpbin, "request-aux-receiver",
          (GCallback) request_aux_receiver, media);
    }

    if (!gst_rtsp_stream_join_bin (stream, GST_BIN (priv->pipeline),
            priv->rtpbin, GST_STATE_NULL)) {
      goto join_bin_failed;
    }
  }

  if (priv->rtpbin)
    g_object_set (priv->rtpbin, "do-retransmission", priv->do_retransmission,
        "do-lost", TRUE, NULL);

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
  }

  if (priv->nb_dynamic_elements == 0 && gst_rtsp_media_is_receive_only (media)) {
    /* If we are receive_only (RECORD), do not try to preroll, to avoid
     * a second ASYNC state change failing */
    priv->is_live = TRUE;
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
  } else if (!start_preroll (media)) {
    goto preroll_failed;
  }

  g_rec_mutex_unlock (&priv->state_lock);

  return FALSE;

no_longer_preparing:
  {
    GST_INFO ("media is no longer preparing");
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
join_bin_failed:
  {
    GST_WARNING ("failed to join bin element");
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
preroll_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
}

static gboolean
default_prepare (GstRTSPMedia * media, GstRTSPThread * thread)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPMediaClass *klass;
  GstBus *bus;
  GMainContext *context;
  GSource *source;

  priv = media->priv;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  if (!klass->create_rtpbin)
    goto no_create_rtpbin;

  priv->rtpbin = klass->create_rtpbin (media);
  if (priv->rtpbin != NULL) {
    gboolean success = TRUE;

    g_object_set (priv->rtpbin, "latency", priv->latency, NULL);

    if (klass->setup_rtpbin)
      success = klass->setup_rtpbin (media, priv->rtpbin);

    if (success == FALSE) {
      gst_object_unref (priv->rtpbin);
      priv->rtpbin = NULL;
    }
  }
  if (priv->rtpbin == NULL)
    goto no_rtpbin;

  priv->thread = thread;
  context = (thread != NULL) ? (thread->context) : NULL;

  bus = gst_pipeline_get_bus (GST_PIPELINE_CAST (priv->pipeline));

  /* add the pipeline bus to our custom mainloop */
  priv->source = gst_bus_create_watch (bus);
  gst_object_unref (bus);

  g_source_set_callback (priv->source, (GSourceFunc) bus_message,
      g_object_ref (media), (GDestroyNotify) watch_destroyed);

  g_source_attach (priv->source, context);

  /* add stuff to the bin */
  gst_bin_add (GST_BIN (priv->pipeline), priv->rtpbin);

  /* do remainder in context */
  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) start_prepare,
      g_object_ref (media), (GDestroyNotify) g_object_unref);
  g_source_attach (source, context);
  g_source_unref (source);

  return TRUE;

  /* ERRORS */
no_create_rtpbin:
  {
    GST_ERROR ("no create_rtpbin function");
    g_critical ("no create_rtpbin vmethod function set");
    return FALSE;
  }
no_rtpbin:
  {
    GST_WARNING ("no rtpbin element");
    g_warning ("failed to create element 'rtpbin', check your installation");
    return FALSE;
  }
}

/**
 * gst_rtsp_media_prepare:
 * @media: a #GstRTSPMedia
 * @thread: (transfer full) (allow-none): a #GstRTSPThread to run the
 *   bus handler or %NULL
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
  GstRTSPMediaClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);
  priv->prepare_count++;

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARED ||
      priv->status == GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto was_prepared;

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING)
    goto is_preparing;

  if (priv->status != GST_RTSP_MEDIA_STATUS_UNPREPARED)
    goto not_unprepared;

  if (!priv->reusable && priv->reused)
    goto is_reused;

  GST_INFO ("preparing media %p", media);

  /* reset some variables */
  priv->is_live = FALSE;
  priv->seekable = -1;
  priv->buffering = FALSE;
  priv->no_more_pads_pending = priv->nb_dynamic_elements;

  /* we're preparing now */
  gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARING);

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  if (klass->prepare) {
    if (!klass->prepare (media, thread))
      goto prepare_failed;
  }

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
is_preparing:
  {
    /* we are not going to use the giving thread, so stop it. */
    if (thread)
      gst_rtsp_thread_stop (thread);
    goto wait_status;
  }
was_prepared:
  {
    GST_LOG ("media %p was prepared", media);
    /* we are not going to use the giving thread, so stop it. */
    if (thread)
      gst_rtsp_thread_stop (thread);
    g_rec_mutex_unlock (&priv->state_lock);
    return TRUE;
  }
  /* ERRORS */
not_unprepared:
  {
    /* we are not going to use the giving thread, so stop it. */
    if (thread)
      gst_rtsp_thread_stop (thread);
    GST_WARNING ("media %p was not unprepared", media);
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    return FALSE;
  }
is_reused:
  {
    /* we are not going to use the giving thread, so stop it. */
    if (thread)
      gst_rtsp_thread_stop (thread);
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("can not reuse media %p", media);
    return FALSE;
  }
prepare_failed:
  {
    /* we are not going to use the giving thread, so stop it. */
    if (thread)
      gst_rtsp_thread_stop (thread);
    priv->prepare_count--;
    g_rec_mutex_unlock (&priv->state_lock);
    GST_ERROR ("failed to prepare media");
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

  if (priv->finishing_unprepare)
    return;
  priv->finishing_unprepare = TRUE;

  GST_DEBUG ("shutting down");

  /* release the lock on shutdown, otherwise pad_added_cb might try to
   * acquire the lock and then we deadlock */
  g_rec_mutex_unlock (&priv->state_lock);
  set_state (media, GST_STATE_NULL);
  g_rec_mutex_lock (&priv->state_lock);

  media_streams_set_blocked (media, FALSE);

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
  gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_UNPREPARED);

  /* when the media is not reusable, this will effectively unref the media and
   * recreate it */
  g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_UNPREPARED], 0, NULL);

  /* the source has the last ref to the media */
  if (priv->source) {
    GstBus *bus;

    GST_DEBUG ("removing bus watch");
    bus = gst_pipeline_get_bus (GST_PIPELINE_CAST (priv->pipeline));
    gst_bus_remove_watch (bus);
    gst_object_unref (bus);

    GST_DEBUG ("destroy source");
    g_source_destroy (priv->source);
    g_source_unref (priv->source);
    priv->source = NULL;
  }
  if (priv->thread) {
    GST_DEBUG ("stop thread");
    gst_rtsp_thread_stop (priv->thread);
  }

  priv->finishing_unprepare = FALSE;
}

/* called with state-lock */
static gboolean
default_unprepare (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;

  gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_UNPREPARING);

  if (priv->eos_shutdown) {
    /* we need to go to playing again for the EOS to propagate, normally in this
     * state, nothing is receiving data from us anymore so this is ok. */
    GST_DEBUG ("Temporarily go to PLAYING again for sending EOS");
    set_state (media, GST_STATE_PLAYING);
    GST_DEBUG ("sending EOS for shutdown");
    gst_element_send_event (priv->pipeline, gst_event_new_eos ());
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
  if (priv->status == GST_RTSP_MEDIA_STATUS_UNPREPARING)
    goto is_unpreparing;

  GST_INFO ("unprepare media %p", media);
  set_target_state (media, GST_STATE_NULL, FALSE);
  success = TRUE;

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARED
      || priv->status == GST_RTSP_MEDIA_STATUS_SUSPENDED) {
    GstRTSPMediaClass *klass;

    klass = GST_RTSP_MEDIA_GET_CLASS (media);
    if (klass->unprepare)
      success = klass->unprepare (media);
  } else {
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_UNPREPARING);
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
is_unpreparing:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_INFO ("media %p is already unpreparing", media);
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
 * gst_rtsp_media_lock:
 * @media: a #GstRTSPMedia
 *
 * Lock the entire media. This is needed by callers such as rtsp_client to
 * protect the media when it is shared by many clients.
 * The lock prevents that concurrent clients alters the shared media,
 * while one client already is working with it.
 * Typically the lock is taken in external RTSP API calls that uses shared media
 * such as DESCRIBE, SETUP, ANNOUNCE, TEARDOWN, PLAY, PAUSE.
 *
 * As best practice take the lock as soon as the function get hold of a shared
 * media object. Release the lock right before the function returns.
 *
 * Since: 1.18
 */
void
gst_rtsp_media_lock (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->global_lock);
}

/**
 * gst_rtsp_media_unlock:
 * @media: a #GstRTSPMedia
 *
 * Unlock the media.
 *
 * Since: 1.18
 */
void
gst_rtsp_media_unlock (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_unlock (&priv->global_lock);
}

/**
 * gst_rtsp_media_get_clock:
 * @media: a #GstRTSPMedia
 *
 * Get the clock that is used by the pipeline in @media.
 *
 * @media must be prepared before this method returns a valid clock object.
 *
 * Returns: (transfer full) (nullable): the #GstClock used by @media. unref after usage.
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
 * @address: (allow-none): an address or %NULL
 * @port: a port or 0
 *
 * Get the #GstNetTimeProvider for the clock used by @media. The time provider
 * will listen on @address and @port for client time requests.
 *
 * Returns: (transfer full) (nullable): the #GstNetTimeProvider of @media.
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
 * @media: a #GstRTSPMedia
 * @sdp: (transfer none): a #GstSDPMessage
 * @info: (transfer none): a #GstSDPInfo
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

static gboolean
default_handle_sdp (GstRTSPMedia * media, GstSDPMessage * sdp)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gint i, medias_len;

  medias_len = gst_sdp_message_medias_len (sdp);
  if (medias_len != priv->streams->len) {
    GST_ERROR ("%p: Media has more or less streams than SDP (%d /= %d)", media,
        priv->streams->len, medias_len);
    return FALSE;
  }

  for (i = 0; i < medias_len; i++) {
    const gchar *proto;
    const GstSDPMedia *sdp_media = gst_sdp_message_get_media (sdp, i);
    GstRTSPStream *stream;
    gint j, formats_len;
    const gchar *control;
    GstRTSPProfile profile, profiles;

    stream = g_ptr_array_index (priv->streams, i);

    /* TODO: Should we do something with the other SDP information? */

    /* get proto */
    proto = gst_sdp_media_get_proto (sdp_media);
    if (proto == NULL) {
      GST_ERROR ("%p: SDP media %d has no proto", media, i);
      return FALSE;
    }

    if (g_str_equal (proto, "RTP/AVP")) {
      profile = GST_RTSP_PROFILE_AVP;
    } else if (g_str_equal (proto, "RTP/SAVP")) {
      profile = GST_RTSP_PROFILE_SAVP;
    } else if (g_str_equal (proto, "RTP/AVPF")) {
      profile = GST_RTSP_PROFILE_AVPF;
    } else if (g_str_equal (proto, "RTP/SAVPF")) {
      profile = GST_RTSP_PROFILE_SAVPF;
    } else {
      GST_ERROR ("%p: unsupported profile '%s' for stream %d", media, proto, i);
      return FALSE;
    }

    profiles = gst_rtsp_stream_get_profiles (stream);
    if ((profiles & profile) == 0) {
      GST_ERROR ("%p: unsupported profile '%s' for stream %d", media, proto, i);
      return FALSE;
    }

    formats_len = gst_sdp_media_formats_len (sdp_media);
    for (j = 0; j < formats_len; j++) {
      gint pt;
      GstCaps *caps;
      GstStructure *s;

      pt = atoi (gst_sdp_media_get_format (sdp_media, j));

      GST_DEBUG (" looking at %d pt: %d", j, pt);

      /* convert caps */
      caps = gst_sdp_media_get_caps_from_media (sdp_media, pt);
      if (caps == NULL) {
        GST_WARNING (" skipping pt %d without caps", pt);
        continue;
      }

      /* do some tweaks */
      GST_DEBUG ("mapping sdp session level attributes to caps");
      gst_sdp_message_attributes_to_caps (sdp, caps);
      GST_DEBUG ("mapping sdp media level attributes to caps");
      gst_sdp_media_attributes_to_caps (sdp_media, caps);

      s = gst_caps_get_structure (caps, 0);
      gst_structure_set_name (s, "application/x-rtp");

      if (!g_strcmp0 (gst_structure_get_string (s, "encoding-name"), "ULPFEC"))
        gst_structure_set (s, "is-fec", G_TYPE_BOOLEAN, TRUE, NULL);

      gst_rtsp_stream_set_pt_map (stream, pt, caps);
      gst_caps_unref (caps);
    }

    control = gst_sdp_media_get_attribute_val (sdp_media, "control");
    if (control)
      gst_rtsp_stream_set_control (stream, control);

  }

  return TRUE;
}

/**
 * gst_rtsp_media_handle_sdp:
 * @media: a #GstRTSPMedia
 * @sdp: (transfer none): a #GstSDPMessage
 *
 * Configure an SDP on @media for receiving streams
 *
 * Returns: TRUE on success.
 */
gboolean
gst_rtsp_media_handle_sdp (GstRTSPMedia * media, GstSDPMessage * sdp)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPMediaClass *klass;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (sdp != NULL, FALSE);

  priv = media->priv;

  g_rec_mutex_lock (&priv->state_lock);

  klass = GST_RTSP_MEDIA_GET_CLASS (media);

  if (!klass->handle_sdp)
    goto no_handle_sdp;

  res = klass->handle_sdp (media, sdp);

  g_rec_mutex_unlock (&priv->state_lock);

  return res;

  /* ERRORS */
no_handle_sdp:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_ERROR ("no handle_sdp function");
    g_critical ("no handle_sdp vmethod function set");
    return FALSE;
  }
}

static void
do_set_seqnum (GstRTSPStream * stream)
{
  guint16 seq_num;

  if (gst_rtsp_stream_is_sender (stream)) {
    seq_num = gst_rtsp_stream_get_current_seqnum (stream);
    gst_rtsp_stream_set_seqnum_offset (stream, seq_num + 1);
  }
}

/* call with state_lock */
static gboolean
default_suspend (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

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
      /* Because payloader needs to set the sequence number as
       * monotonic, we need to preserve the sequence number
       * after pause. (otherwise going from pause to play,  which
       * is actually from NULL to PLAY will create a new sequence
       * number. */
      g_ptr_array_foreach (priv->streams, (GFunc) do_set_seqnum, NULL);
      break;
    default:
      break;
  }

  /* If we use any suspend mode that changes the state then we must update
   * expected_async_done, since we might not be doing an asyncronous state
   * change anymore. */
  if (ret != GST_STATE_CHANGE_FAILURE && ret != GST_STATE_CHANGE_ASYNC)
    priv->expected_async_done = FALSE;

  return TRUE;

  /* ERRORS */
state_failed:
  {
    GST_WARNING ("failed changing pipeline's state for media %p", media);
    return FALSE;
  }
}

/**
 * gst_rtsp_media_suspend:
 * @media: a #GstRTSPMedia
 *
 * Suspend @media. The state of the pipeline managed by @media is set to
 * GST_STATE_NULL but all streams are kept. @media can be prepared again
 * with gst_rtsp_media_unsuspend()
 *
 * @media must be prepared with gst_rtsp_media_prepare();
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtsp_media_suspend (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstRTSPMediaClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  GST_FIXME ("suspend for dynamic pipelines needs fixing");

  /* this typically can happen for shared media. */
  if (priv->prepare_count > 1 &&
      priv->status == GST_RTSP_MEDIA_STATUS_SUSPENDED) {
    goto done;
  } else if (priv->prepare_count > 1) {
    goto prepared_by_other_client;
  }

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED)
    goto not_prepared;

  /* don't attempt to suspend when something is busy */
  if (priv->n_active > 0)
    goto done;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  if (klass->suspend) {
    if (!klass->suspend (media))
      goto suspend_failed;
  }

  gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_SUSPENDED);
done:
  g_rec_mutex_unlock (&priv->state_lock);

  return TRUE;

  /* ERRORS */
prepared_by_other_client:
  {
    GST_WARNING ("media %p was prepared by other client", media);
    return FALSE;
  }
not_prepared:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("media %p was not prepared", media);
    return FALSE;
  }
suspend_failed:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    GST_WARNING ("failed to suspend media %p", media);
    return FALSE;
  }
}

/* call with state_lock */
static gboolean
default_unsuspend (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gboolean preroll_ok;

  switch (priv->suspend_mode) {
    case GST_RTSP_SUSPEND_MODE_NONE:
      if (!gst_rtsp_media_is_receive_only (media)
          && media_streams_blocking (media)) {
        g_rec_mutex_unlock (&priv->state_lock);
        if (gst_rtsp_media_get_status (media) == GST_RTSP_MEDIA_STATUS_ERROR) {
          g_rec_mutex_lock (&priv->state_lock);
          goto preroll_failed;
        }
        g_rec_mutex_lock (&priv->state_lock);
      }
      gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
      break;
    case GST_RTSP_SUSPEND_MODE_PAUSE:
      gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARED);
      break;
    case GST_RTSP_SUSPEND_MODE_RESET:
    {
      gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_PREPARING);
      /* at this point the media pipeline has been updated and contain all
       * specific transport parts: all active streams contain at least one sink
       * element and it's safe to unblock all blocked streams */
      media_streams_set_blocked (media, FALSE);
      if (!start_preroll (media))
        goto start_failed;

      g_rec_mutex_unlock (&priv->state_lock);
      preroll_ok = wait_preroll (media);
      g_rec_mutex_lock (&priv->state_lock);

      if (!preroll_ok)
        goto preroll_failed;
    }
    default:
      break;
  }

  return TRUE;

  /* ERRORS */
start_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    return FALSE;
  }
preroll_failed:
  {
    GST_WARNING ("failed to preroll pipeline");
    return FALSE;
  }
}

static void
gst_rtsp_media_unblock_rtcp (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  priv = media->priv;
  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
    gst_rtsp_stream_unblock_rtcp (stream);
  }
  g_mutex_unlock (&priv->lock);
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
  GstRTSPMediaClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  g_rec_mutex_lock (&priv->state_lock);
  if (priv->status != GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto done;

  klass = GST_RTSP_MEDIA_GET_CLASS (media);
  if (klass->unsuspend) {
    if (!klass->unsuspend (media))
      goto unsuspend_failed;
  }

done:
  gst_rtsp_media_unblock_rtcp (media);
  g_rec_mutex_unlock (&priv->state_lock);

  return TRUE;

  /* ERRORS */
unsuspend_failed:
  {
    g_rec_mutex_unlock (&priv->state_lock);
    GST_WARNING ("failed to unsuspend media %p", media);
    gst_rtsp_media_set_status (media, GST_RTSP_MEDIA_STATUS_ERROR);
    return FALSE;
  }
}

/* must be called with state-lock */
static void
media_set_pipeline_state_locked (GstRTSPMedia * media, GstState state)
{
  GstRTSPMediaPrivate *priv = media->priv;
  GstStateChangeReturn set_state_ret;
  priv->expected_async_done = FALSE;

  if (state == GST_STATE_NULL) {
    gst_rtsp_media_unprepare (media);
  } else {
    GST_INFO ("state %s media %p", gst_element_state_get_name (state), media);
    set_target_state (media, state, FALSE);

    if (state == GST_STATE_PLAYING) {
      /* make sure pads are not blocking anymore when going to PLAYING */
      media_streams_set_blocked (media, FALSE);
    }

    /* when we are buffering, don't update the state yet, this will be done
     * when buffering finishes */
    if (priv->buffering) {
      GST_INFO ("Buffering busy, delay state change");
    } else {
      if (state == GST_STATE_PAUSED) {
        set_state_ret = set_state (media, state);
        if (set_state_ret == GST_STATE_CHANGE_ASYNC)
          priv->expected_async_done = TRUE;
        /* and suspend after pause */
        gst_rtsp_media_suspend (media);
      } else {
        set_state (media, state);
      }
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
 * @transports: (transfer none) (element-type GstRtspServer.RTSPStreamTransport):
 * a #GPtrArray of #GstRTSPStreamTransport pointers
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

  if (priv->status == GST_RTSP_MEDIA_STATUS_PREPARING
      && gst_rtsp_media_is_shared (media)) {
    g_rec_mutex_unlock (&priv->state_lock);
    gst_rtsp_media_get_status (media);
    g_rec_mutex_lock (&priv->state_lock);
  }
  if (priv->status == GST_RTSP_MEDIA_STATUS_ERROR)
    goto error_status;
  if (priv->status != GST_RTSP_MEDIA_STATUS_PREPARED &&
      priv->status != GST_RTSP_MEDIA_STATUS_SUSPENDED)
    goto not_prepared;

  /* NULL and READY are the same */
  if (state == GST_STATE_READY)
    state = GST_STATE_NULL;

  activate = deactivate = FALSE;

  GST_INFO ("going to state %s media %p, target state %s",
      gst_element_state_get_name (state), media,
      gst_element_state_get_name (priv->target_state));

  switch (state) {
    case GST_STATE_NULL:
      /* we're going from PLAYING or PAUSED to READY or NULL, deactivate */
      if (priv->target_state >= GST_STATE_PAUSED)
        deactivate = TRUE;
      break;
    case GST_STATE_PAUSED:
      /* we're going from PLAYING to PAUSED, deactivate */
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

  GST_DEBUG ("%d transports, activate %d, deactivate %d", transports->len,
      activate, deactivate);
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

  if (activate)
    media_streams_set_blocked (media, FALSE);

  /* we just activated the first media, do the playing state change */
  if (old_active == 0 && activate)
    do_state = TRUE;
  /* if we have no more active media and prepare count is not indicate
   * that there are new session/sessions ongoing,
   * do the downward state changes */
  else if (priv->n_active == 0 && priv->prepare_count <= 1)
    do_state = TRUE;
  else
    do_state = FALSE;

  GST_INFO ("state %d active %d media %p do_state %d", state, priv->n_active,
      media, do_state);

  if (priv->target_state != state) {
    if (do_state) {
      media_set_pipeline_state_locked (media, state);
      g_signal_emit (media, gst_rtsp_media_signals[SIGNAL_NEW_STATE], 0, state,
          NULL);
    }
  }

  /* remember where we are */
  if (state != GST_STATE_NULL && (state == GST_STATE_PAUSED ||
          old_active != priv->n_active)) {
    g_mutex_lock (&priv->lock);
    collect_media_stats (media);
    g_mutex_unlock (&priv->lock);
  }
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

/**
 * gst_rtsp_media_set_transport_mode:
 * @media: a #GstRTSPMedia
 * @mode: the new value
 *
 * Sets if the media pipeline can work in PLAY or RECORD mode
 */
void
gst_rtsp_media_set_transport_mode (GstRTSPMedia * media,
    GstRTSPTransportMode mode)
{
  GstRTSPMediaPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->transport_mode = mode;
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_transport_mode:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media can be used for PLAY or RECORD methods.
 *
 * Returns: The transport mode.
 */
GstRTSPTransportMode
gst_rtsp_media_get_transport_mode (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstRTSPTransportMode res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->transport_mode;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_seekable:
 * @media: a #GstRTSPMedia
 *
 * Check if the pipeline for @media seek and up to what point in time,
 * it can seek.
 *
 * Returns: -1 if the stream is not seekable, 0 if seekable only to the beginning
 * and > 0 to indicate the longest duration between any two random access points.
 * %G_MAXINT64 means any value is possible.
 *
 * Since: 1.14
 */
GstClockTimeDiff
gst_rtsp_media_seekable (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  GstClockTimeDiff res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  /* Currently we are not able to seek on live streams,
   * and no stream is seekable only to the beginning */
  g_mutex_lock (&priv->lock);
  res = priv->seekable;
  g_mutex_unlock (&priv->lock);

  return res;
}

/**
 * gst_rtsp_media_complete_pipeline:
 * @media: a #GstRTSPMedia
 * @transports: (element-type GstRTSPTransport): a list of #GstRTSPTransport
 *
 * Add a receiver and sender parts to the pipeline based on the transport from
 * SETUP.
 *
 * Returns: %TRUE if the media pipeline has been sucessfully updated.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_media_complete_pipeline (GstRTSPMedia * media, GPtrArray * transports)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);
  g_return_val_if_fail (transports, FALSE);

  GST_DEBUG_OBJECT (media, "complete pipeline");

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStreamTransport *transport;
    GstRTSPStream *stream;
    const GstRTSPTransport *rtsp_transport;

    transport = g_ptr_array_index (transports, i);
    if (!transport)
      continue;

    stream = gst_rtsp_stream_transport_get_stream (transport);
    if (!stream)
      continue;

    rtsp_transport = gst_rtsp_stream_transport_get_transport (transport);

    if (!gst_rtsp_stream_complete_stream (stream, rtsp_transport)) {
      g_mutex_unlock (&priv->lock);
      return FALSE;
    }

    if (!gst_rtsp_stream_add_transport (stream, transport)) {
      g_mutex_unlock (&priv->lock);
      return FALSE;
    }

    update_stream_storage_size (media, stream, i);
  }

  priv->complete = TRUE;
  g_mutex_unlock (&priv->lock);

  return TRUE;
}

/**
 * gst_rtsp_media_is_receive_only:
 *
 * Returns: %TRUE if @media is receive-only, %FALSE otherwise.
 * Since: 1.18
 */
gboolean
gst_rtsp_media_is_receive_only (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gboolean receive_only;

  g_mutex_lock (&priv->lock);
  receive_only = is_receive_only (media);
  g_mutex_unlock (&priv->lock);

  return receive_only;
}

/**
 * gst_rtsp_media_has_completed_sender:
 *
 * See gst_rtsp_stream_is_complete(), gst_rtsp_stream_is_sender().
 *
 * Returns: whether @media has at least one complete sender stream.
 * Since: 1.18
 */
gboolean
gst_rtsp_media_has_completed_sender (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv = media->priv;
  gboolean sender = FALSE;
  guint i;

  g_mutex_lock (&priv->lock);
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);
    if (gst_rtsp_stream_is_complete (stream))
      if (gst_rtsp_stream_is_sender (stream) ||
          !gst_rtsp_stream_is_receiver (stream)) {
        sender = TRUE;
        break;
      }
  }
  g_mutex_unlock (&priv->lock);

  return sender;
}

/**
 * gst_rtsp_media_set_rate_control:
 *
 * Define whether @media will follow the Rate-Control=no behaviour as specified
 * in the ONVIF replay spec.
 *
 * Since: 1.18
 */
void
gst_rtsp_media_set_rate_control (GstRTSPMedia * media, gboolean enabled)
{
  GstRTSPMediaPrivate *priv;
  guint i;

  g_return_if_fail (GST_IS_RTSP_MEDIA (media));

  GST_LOG_OBJECT (media, "%s rate control", enabled ? "Enabling" : "Disabling");

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  priv->do_rate_control = enabled;
  for (i = 0; i < priv->streams->len; i++) {
    GstRTSPStream *stream = g_ptr_array_index (priv->streams, i);

    gst_rtsp_stream_set_rate_control (stream, enabled);

  }
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_get_rate_control:
 *
 * Returns: whether @media will follow the Rate-Control=no behaviour as specified
 * in the ONVIF replay spec.
 *
 * Since: 1.18
 */
gboolean
gst_rtsp_media_get_rate_control (GstRTSPMedia * media)
{
  GstRTSPMediaPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA (media), FALSE);

  priv = media->priv;

  g_mutex_lock (&priv->lock);
  res = priv->do_rate_control;
  g_mutex_unlock (&priv->lock);

  return res;
}
