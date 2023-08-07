/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <thiagoss@osg.samsung.com>
 *
 * Copyright (C) 2021-2022 Centricular Ltd
 *   Author: Edward Hervey <edward@centricular.com>
 *   Author: Jan Schmidt <jan@centricular.com>
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
 * SECTION:plugin-adaptivedemux2
 * @short_description: Next Generation adaptive demuxers
 *
 * What is an adaptive demuxer?  Adaptive demuxers are special demuxers in the
 * sense that they don't actually demux data received from upstream but download
 * the data themselves.
 *
 * Adaptive formats (HLS, DASH, MSS) are composed of a manifest file and a set
 * of fragments. The manifest describes the available media and the sequence of
 * fragments to use. Each fragment contains a small part of the media (typically
 * only a few seconds). It is possible for the manifest to have the same media
 * available in different configurations (bitrates for example) so that the
 * client can select the one that best suits its scenario (network fluctuation,
 * hardware requirements...).
 *
 * Furthermore, that manifest can also specify alternative medias (such as audio
 * or subtitle tracks in different languages). Only the fragments for the
 * requested selection will be download.
 *
 * These elements can therefore "adapt" themselves to the network conditions (as
 * opposed to the server doing that adaptation) and user choices, which is why
 * they are called "adaptive" demuxers.
 *
 * Note: These elements require a "streams-aware" container to work
 * (i.e. urisourcebin, decodebin3, playbin3, or any bin/pipeline with the
 * GST_BIN_FLAG_STREAMS_AWARE flag set).
 *
 * Subclasses:
 * While GstAdaptiveDemux is responsible for the workflow, it knows nothing
 * about the intrinsics of the subclass formats, so the subclasses are
 * responsible for maintaining the manifest data structures and stream
 * information.
 *
 * Since: 1.22
 */

/*
See the adaptive-demuxer.md design documentation for more information

MT safety.
The following rules were observed while implementing MT safety in adaptive demux:
1. If a variable is accessed from multiple threads and at least one thread
writes to it, then all the accesses needs to be done from inside a critical section.
2. If thread A wants to join thread B then at the moment it calls gst_task_join
it must not hold any mutexes that thread B might take.

Adaptive demux API can be called from several threads. More, adaptive demux
starts some threads to monitor the download of fragments. In order to protect
accesses to shared variables (demux and streams) all the API functions that
can be run in different threads will need to get a mutex (manifest_lock)
when they start and release it when they end. Because some of those functions
can indirectly call other API functions (eg they can generate events or messages
that are processed in the same thread) the manifest_lock must be recursive.

The manifest_lock will serialize the public API making access to shared
variables safe. But some of these functions will try at some moment to join
threads created by adaptive demux, or to change the state of src elements
(which will block trying to join the src element streaming thread). Because
of rule 2, those functions will need to release the manifest_lock during the
call of gst_task_join. During this time they can be interrupted by other API calls.
For example, during the precessing of a seek event, gst_adaptive_demux_stop_tasks
is called and this will join all threads. In order to prevent interruptions
during such period, all the API functions will also use a second lock: api_lock.
This will be taken at the beginning of the function and released at the end,
but this time this lock will not be temporarily released during join.
This lock will be used only by API calls (not by the SCHEDULER task)
so it is safe to hold it while joining the threads or changing the src element state. The
api_lock will serialise all external requests to adaptive demux. In order to
avoid deadlocks, if a function needs to acquire both manifest and api locks,
the api_lock will be taken first and the manifest_lock second.

By using the api_lock a thread is protected against other API calls.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstadaptivedemux.h"
#include "gstadaptivedemux-private.h"

#include <glib/gi18n-lib.h>
#include <gst/base/gstadapter.h>
#include <gst/app/gstappsrc.h>

GST_DEBUG_CATEGORY_EXTERN (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

#define DEFAULT_FAILED_COUNT 3
#define DEFAULT_CONNECTION_BITRATE 0
#define DEFAULT_BANDWIDTH_TARGET_RATIO 0.8f

#define DEFAULT_MIN_BITRATE 0
#define DEFAULT_MAX_BITRATE 0

#define DEFAULT_MAX_BUFFERING_TIME (30 *  GST_SECOND)

#define DEFAULT_BUFFERING_HIGH_WATERMARK_TIME (30 * GST_SECOND)
#define DEFAULT_BUFFERING_LOW_WATERMARK_TIME 0  /* Automatic */
#define DEFAULT_BUFFERING_HIGH_WATERMARK_FRAGMENTS 0.0
#define DEFAULT_BUFFERING_LOW_WATERMARK_FRAGMENTS 0.0

#define DEFAULT_CURRENT_LEVEL_TIME_VIDEO 0
#define DEFAULT_CURRENT_LEVEL_TIME_AUDIO 0

enum
{
  PROP_0,
  PROP_CONNECTION_SPEED,
  PROP_BANDWIDTH_TARGET_RATIO,
  PROP_CONNECTION_BITRATE,
  PROP_MIN_BITRATE,
  PROP_MAX_BITRATE,
  PROP_CURRENT_BANDWIDTH,
  PROP_MAX_BUFFERING_TIME,
  PROP_BUFFERING_HIGH_WATERMARK_TIME,
  PROP_BUFFERING_LOW_WATERMARK_TIME,
  PROP_BUFFERING_HIGH_WATERMARK_FRAGMENTS,
  PROP_BUFFERING_LOW_WATERMARK_FRAGMENTS,
  PROP_CURRENT_LEVEL_TIME_VIDEO,
  PROP_CURRENT_LEVEL_TIME_AUDIO,
  PROP_LAST
};

static GstStaticPadTemplate gst_adaptive_demux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_adaptive_demux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_adaptive_demux_subtitlesrc_template =
GST_STATIC_PAD_TEMPLATE ("subtitle_%02u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

/* Private structure for a track being outputted */
typedef struct _OutputSlot
{
  /* Output pad */
  GstPad *pad;

  /* Last flow return */
  GstFlowReturn flow_ret;

  /* Stream Type */
  GstStreamType type;

  /* Target track (reference) */
  GstAdaptiveDemuxTrack *track;

  /* Pending track (which will replace track) */
  GstAdaptiveDemuxTrack *pending_track;

  /* TRUE if a buffer or a gap event was pushed through this slot. */
  gboolean pushed_timed_data;
} OutputSlot;

static GstBinClass *parent_class = NULL;
static gint private_offset = 0;

static void gst_adaptive_demux_class_init (GstAdaptiveDemuxClass * klass);
static void gst_adaptive_demux_init (GstAdaptiveDemux * dec,
    GstAdaptiveDemuxClass * klass);
static void gst_adaptive_demux_finalize (GObject * object);
static GstStateChangeReturn gst_adaptive_demux_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_adaptive_demux_query (GstElement * element,
    GstQuery * query);
static gboolean gst_adaptive_demux_send_event (GstElement * element,
    GstEvent * event);

static void gst_adaptive_demux_handle_message (GstBin * bin, GstMessage * msg);

static gboolean gst_adaptive_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_adaptive_demux_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_adaptive_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_adaptive_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_adaptive_demux_handle_seek_event (GstAdaptiveDemux * demux,
    GstEvent * event);
static gboolean gst_adaptive_demux_handle_select_streams_event (GstAdaptiveDemux
    * demux, GstEvent * event);

static gboolean
gst_adaptive_demux_push_src_event (GstAdaptiveDemux * demux, GstEvent * event);

static void gst_adaptive_demux_output_loop (GstAdaptiveDemux * demux);
static void gst_adaptive_demux_reset (GstAdaptiveDemux * demux);
static gboolean gst_adaptive_demux_prepare_streams (GstAdaptiveDemux * demux,
    gboolean first_and_live);

static GstFlowReturn
gst_adaptive_demux_update_manifest_default (GstAdaptiveDemux * demux);

static void gst_adaptive_demux_stop_manifest_update_task (GstAdaptiveDemux *
    demux);
static void gst_adaptive_demux_start_manifest_update_task (GstAdaptiveDemux *
    demux);

static void gst_adaptive_demux_start_tasks (GstAdaptiveDemux * demux);
static void gst_adaptive_demux_stop_tasks (GstAdaptiveDemux * demux,
    gboolean stop_updates);

static gboolean
gst_adaptive_demux_requires_periodical_playlist_update_default (GstAdaptiveDemux
    * demux);

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_adaptive_demux_ng_get_type (void)
{
  static gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstAdaptiveDemuxClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_adaptive_demux_class_init,
      NULL,
      NULL,
      sizeof (GstAdaptiveDemux),
      0,
      (GInstanceInitFunc) gst_adaptive_demux_init,
    };

    _type = g_type_register_static (GST_TYPE_BIN,
        "GstAdaptiveDemux2", &info, G_TYPE_FLAG_ABSTRACT);

    private_offset =
        g_type_add_instance_private (_type, sizeof (GstAdaptiveDemuxPrivate));

    g_once_init_leave (&type, _type);
  }
  return type;
}

static inline GstAdaptiveDemuxPrivate *
gst_adaptive_demux_get_instance_private (GstAdaptiveDemux * self)
{
  return (G_STRUCT_MEMBER_P (self, private_offset));
}

static void
gst_adaptive_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX (object);

  GST_OBJECT_LOCK (demux);

  switch (prop_id) {
    case PROP_CONNECTION_SPEED:
      demux->connection_speed = g_value_get_uint (value) * 1000;
      GST_DEBUG_OBJECT (demux, "Connection speed set to %u",
          demux->connection_speed);
      break;
    case PROP_BANDWIDTH_TARGET_RATIO:
      demux->bandwidth_target_ratio = g_value_get_float (value);
      break;
    case PROP_MIN_BITRATE:
      demux->min_bitrate = g_value_get_uint (value);
      break;
    case PROP_MAX_BITRATE:
      demux->max_bitrate = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_BITRATE:
      demux->connection_speed = g_value_get_uint (value);
      break;
      /* FIXME: Recalculate track and buffering levels
       * when watermarks change? */
    case PROP_MAX_BUFFERING_TIME:
      demux->max_buffering_time = g_value_get_uint64 (value);
      break;
    case PROP_BUFFERING_HIGH_WATERMARK_TIME:
      demux->buffering_high_watermark_time = g_value_get_uint64 (value);
      break;
    case PROP_BUFFERING_LOW_WATERMARK_TIME:
      demux->buffering_low_watermark_time = g_value_get_uint64 (value);
      break;
    case PROP_BUFFERING_HIGH_WATERMARK_FRAGMENTS:
      demux->buffering_high_watermark_fragments = g_value_get_double (value);
      break;
    case PROP_BUFFERING_LOW_WATERMARK_FRAGMENTS:
      demux->buffering_low_watermark_fragments = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (demux);
}

static void
gst_adaptive_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX (object);

  GST_OBJECT_LOCK (demux);

  switch (prop_id) {
    case PROP_CONNECTION_SPEED:
      g_value_set_uint (value, demux->connection_speed / 1000);
      break;
    case PROP_BANDWIDTH_TARGET_RATIO:
      g_value_set_float (value, demux->bandwidth_target_ratio);
      break;
    case PROP_MIN_BITRATE:
      g_value_set_uint (value, demux->min_bitrate);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, demux->max_bitrate);
      break;
    case PROP_CONNECTION_BITRATE:
      g_value_set_uint (value, demux->connection_speed);
      break;
    case PROP_CURRENT_BANDWIDTH:
      g_value_set_uint (value, demux->current_download_rate);
      break;
    case PROP_MAX_BUFFERING_TIME:
      g_value_set_uint64 (value, demux->max_buffering_time);
      break;
    case PROP_BUFFERING_HIGH_WATERMARK_TIME:
      g_value_set_uint64 (value, demux->buffering_high_watermark_time);
      break;
    case PROP_BUFFERING_LOW_WATERMARK_TIME:
      g_value_set_uint64 (value, demux->buffering_low_watermark_time);
      break;
    case PROP_BUFFERING_HIGH_WATERMARK_FRAGMENTS:
      g_value_set_double (value, demux->buffering_high_watermark_fragments);
      break;
    case PROP_BUFFERING_LOW_WATERMARK_FRAGMENTS:
      g_value_set_double (value, demux->buffering_low_watermark_fragments);
      break;
    case PROP_CURRENT_LEVEL_TIME_VIDEO:
      g_value_set_uint64 (value, demux->current_level_time_video);
      break;
    case PROP_CURRENT_LEVEL_TIME_AUDIO:
      g_value_set_uint64 (value, demux->current_level_time_audio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (demux);
}

static void
gst_adaptive_demux_class_init (GstAdaptiveDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (adaptivedemux2_debug, "adaptivedemux2", 0,
      "Base Adaptive Demux (ng)");

  parent_class = g_type_class_peek_parent (klass);

  if (private_offset != 0)
    g_type_class_adjust_private_offset (klass, &private_offset);

  gobject_class->set_property = gst_adaptive_demux_set_property;
  gobject_class->get_property = gst_adaptive_demux_get_property;
  gobject_class->finalize = gst_adaptive_demux_finalize;

  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed to use in kbps (0 = calculate from downloaded"
          " fragments)", 0, G_MAXUINT / 1000, DEFAULT_CONNECTION_BITRATE / 1000,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BANDWIDTH_TARGET_RATIO,
      g_param_spec_float ("bandwidth-target-ratio",
          "Ratio of target bandwidth / available bandwidth",
          "Limit of the available bitrate to use when switching to alternates",
          0, 1, DEFAULT_BANDWIDTH_TARGET_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONNECTION_BITRATE,
      g_param_spec_uint ("connection-bitrate", "Connection Speed (bits/s)",
          "Network connection speed to use (0 = automatic) (bits/s)",
          0, G_MAXUINT, DEFAULT_CONNECTION_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MIN_BITRATE,
      g_param_spec_uint ("min-bitrate", "Minimum Bitrate",
          "Minimum bitrate to use when switching to alternates (bits/s)",
          0, G_MAXUINT, DEFAULT_MIN_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Maximum Bitrate",
          "Maximum bitrate to use when switching to alternates (bits/s)",
          0, G_MAXUINT, DEFAULT_MAX_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CURRENT_BANDWIDTH,
      g_param_spec_uint ("current-bandwidth",
          "Current download bandwidth (bits/s)",
          "Report of current download bandwidth (based on arriving data) (bits/s)",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BUFFERING_TIME,
      g_param_spec_uint64 ("max-buffering-time",
          "Buffering maximum size (ns)",
          "Upper limit on the high watermark for parsed data, above which downloads are paused (in ns, 0=disable)",
          0, G_MAXUINT64, DEFAULT_MAX_BUFFERING_TIME,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUFFERING_HIGH_WATERMARK_TIME,
      g_param_spec_uint64 ("high-watermark-time",
          "High buffering watermark size (ns)",
          "High watermark for parsed data above which downloads are paused (in ns, 0=disable)",
          0, G_MAXUINT64, DEFAULT_BUFFERING_HIGH_WATERMARK_TIME,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUFFERING_LOW_WATERMARK_TIME,
      g_param_spec_uint64 ("low-watermark-time",
          "Low buffering watermark size (ns)",
          "Low watermark for parsed data below which downloads are resumed (in ns, 0=automatic)",
          0, G_MAXUINT64, DEFAULT_BUFFERING_LOW_WATERMARK_TIME,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUFFERING_HIGH_WATERMARK_FRAGMENTS,
      g_param_spec_double ("high-watermark-fragments",
          "High buffering watermark size (fragments)",
          "High watermark for parsed data above which downloads are paused (in fragments, 0=disable)",
          0, G_MAXFLOAT, DEFAULT_BUFFERING_HIGH_WATERMARK_FRAGMENTS,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_BUFFERING_LOW_WATERMARK_FRAGMENTS,
      g_param_spec_double ("low-watermark-fragments",
          "Low buffering watermark size (fragments)",
          "Low watermark for parsed data below which downloads are resumed (in fragments, 0=disable)",
          0, G_MAXFLOAT, DEFAULT_BUFFERING_LOW_WATERMARK_FRAGMENTS,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CURRENT_LEVEL_TIME_VIDEO,
      g_param_spec_uint64 ("current-level-time-video",
          "Currently buffered level of video (ns)",
          "Currently buffered level of video track(s) (ns)",
          0, G_MAXUINT64, DEFAULT_CURRENT_LEVEL_TIME_VIDEO,
          G_PARAM_READABLE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CURRENT_LEVEL_TIME_AUDIO,
      g_param_spec_uint64 ("current-level-time-audio",
          "Currently buffered level of audio (ns)",
          "Currently buffered level of audio track(s) (ns)",
          0, G_MAXUINT64, DEFAULT_CURRENT_LEVEL_TIME_AUDIO,
          G_PARAM_READABLE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_adaptive_demux_audiosrc_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_adaptive_demux_videosrc_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_adaptive_demux_subtitlesrc_template);

  gstelement_class->change_state = gst_adaptive_demux_change_state;
  gstelement_class->query = gst_adaptive_demux_query;
  gstelement_class->send_event = gst_adaptive_demux_send_event;

  gstbin_class->handle_message = gst_adaptive_demux_handle_message;

  klass->update_manifest = gst_adaptive_demux_update_manifest_default;
  klass->requires_periodical_playlist_update =
      gst_adaptive_demux_requires_periodical_playlist_update_default;
  gst_type_mark_as_plugin_api (GST_TYPE_ADAPTIVE_DEMUX, 0);
}

static void
gst_adaptive_demux_init (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxClass * klass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG_OBJECT (demux, "gst_adaptive_demux_init");

  demux->priv = gst_adaptive_demux_get_instance_private (demux);
  demux->priv->input_adapter = gst_adapter_new ();
  demux->realtime_clock = gst_adaptive_demux_clock_new ();

  demux->download_helper = downloadhelper_new (demux->realtime_clock);
  demux->priv->segment_seqnum = gst_util_seqnum_next ();
  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->instant_rate_multiplier = 1.0;

  GST_OBJECT_FLAG_SET (demux, GST_BIN_FLAG_STREAMS_AWARE);
  gst_bin_set_suppressed_flags (GST_BIN_CAST (demux),
      GST_ELEMENT_FLAG_SOURCE | GST_ELEMENT_FLAG_SINK);

  g_rec_mutex_init (&demux->priv->manifest_lock);

  demux->priv->scheduler_task = gst_adaptive_demux_loop_new ();

  g_mutex_init (&demux->priv->segment_lock);

  g_mutex_init (&demux->priv->tracks_lock);
  g_cond_init (&demux->priv->tracks_add);

  g_mutex_init (&demux->priv->buffering_lock);

  demux->priv->periods = g_queue_new ();

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  demux->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_sink_event));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_sink_chain));

  /* Properties */
  demux->bandwidth_target_ratio = DEFAULT_BANDWIDTH_TARGET_RATIO;
  demux->connection_speed = DEFAULT_CONNECTION_BITRATE;
  demux->min_bitrate = DEFAULT_MIN_BITRATE;
  demux->max_bitrate = DEFAULT_MAX_BITRATE;

  demux->max_buffering_time = DEFAULT_MAX_BUFFERING_TIME;
  demux->buffering_high_watermark_time = DEFAULT_BUFFERING_HIGH_WATERMARK_TIME;
  demux->buffering_low_watermark_time = DEFAULT_BUFFERING_LOW_WATERMARK_TIME;
  demux->buffering_high_watermark_fragments =
      DEFAULT_BUFFERING_HIGH_WATERMARK_FRAGMENTS;
  demux->buffering_low_watermark_fragments =
      DEFAULT_BUFFERING_LOW_WATERMARK_FRAGMENTS;

  demux->current_level_time_video = DEFAULT_CURRENT_LEVEL_TIME_VIDEO;
  demux->current_level_time_audio = DEFAULT_CURRENT_LEVEL_TIME_AUDIO;

  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  demux->priv->duration = GST_CLOCK_TIME_NONE;

  /* Output combiner */
  demux->priv->flowcombiner = gst_flow_combiner_new ();

  /* Output task */
  g_rec_mutex_init (&demux->priv->output_lock);
  demux->priv->output_task =
      gst_task_new ((GstTaskFunction) gst_adaptive_demux_output_loop, demux,
      NULL);
  gst_task_set_lock (demux->priv->output_task, &demux->priv->output_lock);
}

static void
gst_adaptive_demux_finalize (GObject * object)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (object);
  GstAdaptiveDemuxPrivate *priv = demux->priv;

  GST_DEBUG_OBJECT (object, "finalize");

  g_object_unref (priv->input_adapter);

  downloadhelper_free (demux->download_helper);

  g_rec_mutex_clear (&demux->priv->manifest_lock);
  g_mutex_clear (&demux->priv->segment_lock);

  g_mutex_clear (&demux->priv->buffering_lock);

  gst_adaptive_demux_loop_unref (demux->priv->scheduler_task);

  /* The input period is present after a reset, clear it now */
  if (demux->input_period)
    gst_adaptive_demux_period_unref (demux->input_period);

  if (demux->realtime_clock) {
    gst_adaptive_demux_clock_unref (demux->realtime_clock);
    demux->realtime_clock = NULL;
  }
  g_object_unref (priv->output_task);
  g_rec_mutex_clear (&priv->output_lock);

  gst_flow_combiner_free (priv->flowcombiner);

  g_queue_free (priv->periods);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_adaptive_demux_check_streams_aware (GstAdaptiveDemux * demux)
{
  gboolean ret = FALSE;
  GstObject *parent = gst_object_get_parent (GST_OBJECT (demux));

  if (parent) {
    ret = GST_OBJECT_FLAG_IS_SET (parent, GST_BIN_FLAG_STREAMS_AWARE);
    gst_object_unref (parent);
  }

  return ret;
}

static GstStateChangeReturn
gst_adaptive_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_adaptive_demux_check_streams_aware (demux)) {
        GST_ELEMENT_ERROR (demux, CORE, STATE_CHANGE,
            (_("Element requires a streams-aware context.")), (NULL));
        goto fail_out;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (g_atomic_int_compare_and_exchange (&demux->running, TRUE, FALSE))
        GST_DEBUG_OBJECT (demux, "demuxer has stopped running");

      gst_adaptive_demux_loop_stop (demux->priv->scheduler_task, TRUE);
      downloadhelper_stop (demux->download_helper);

      TRACKS_LOCK (demux);
      demux->priv->flushing = TRUE;
      g_cond_signal (&demux->priv->tracks_add);
      gst_task_stop (demux->priv->output_task);
      TRACKS_UNLOCK (demux);

      gst_task_join (demux->priv->output_task);

      gst_adaptive_demux_reset (demux);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adaptive_demux_reset (demux);

      gst_adaptive_demux_loop_start (demux->priv->scheduler_task);
      if (g_atomic_int_get (&demux->priv->have_manifest))
        gst_adaptive_demux_start_manifest_update_task (demux);
      if (g_atomic_int_compare_and_exchange (&demux->running, FALSE, TRUE))
        GST_DEBUG_OBJECT (demux, "demuxer has started running");
      /* gst_task_start (demux->priv->output_task); */
      break;
    default:
      break;
  }

  /* this must be run with the scheduler and output tasks stopped. */
  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Start download task */
      downloadhelper_start (demux->download_helper);
      break;
    default:
      break;
  }

fail_out:
  return result;
}

static void
gst_adaptive_demux_output_slot_free (GstAdaptiveDemux * demux,
    OutputSlot * slot)
{
  GstEvent *eos = gst_event_new_eos ();
  GST_DEBUG_OBJECT (slot->pad, "Releasing slot");

  /* FIXME: The slot might not have output any data, caps or segment yet */
  gst_event_set_seqnum (eos, demux->priv->segment_seqnum);
  gst_pad_push_event (slot->pad, eos);
  gst_pad_set_active (slot->pad, FALSE);
  gst_flow_combiner_remove_pad (demux->priv->flowcombiner, slot->pad);
  gst_element_remove_pad (GST_ELEMENT_CAST (demux), slot->pad);
  if (slot->track)
    gst_adaptive_demux_track_unref (slot->track);
  if (slot->pending_track)
    gst_adaptive_demux_track_unref (slot->pending_track);

  g_free (slot);
}

static OutputSlot *
gst_adaptive_demux_output_slot_new (GstAdaptiveDemux * demux,
    GstStreamType streamtype)
{
  OutputSlot *slot;
  GstPadTemplate *tmpl;
  gchar *name;

  switch (streamtype) {
    case GST_STREAM_TYPE_AUDIO:
      name = g_strdup_printf ("audio_%02u", demux->priv->n_audio_streams++);
      tmpl =
          gst_static_pad_template_get (&gst_adaptive_demux_audiosrc_template);
      break;
    case GST_STREAM_TYPE_VIDEO:
      name = g_strdup_printf ("video_%02u", demux->priv->n_video_streams++);
      tmpl =
          gst_static_pad_template_get (&gst_adaptive_demux_videosrc_template);
      break;
    case GST_STREAM_TYPE_TEXT:
      name =
          g_strdup_printf ("subtitle_%02u", demux->priv->n_subtitle_streams++);
      tmpl =
          gst_static_pad_template_get
          (&gst_adaptive_demux_subtitlesrc_template);
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }

  slot = g_new0 (OutputSlot, 1);
  slot->type = streamtype;
  slot->pushed_timed_data = FALSE;

  /* Create and activate new pads */
  slot->pad = gst_pad_new_from_template (tmpl, name);
  g_free (name);
  gst_object_unref (tmpl);

  gst_element_add_pad (GST_ELEMENT_CAST (demux), slot->pad);
  gst_flow_combiner_add_pad (demux->priv->flowcombiner, slot->pad);
  gst_pad_set_active (slot->pad, TRUE);

  gst_pad_set_query_function (slot->pad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_src_query));
  gst_pad_set_event_function (slot->pad,
      GST_DEBUG_FUNCPTR (gst_adaptive_demux_src_event));

  gst_pad_set_element_private (slot->pad, slot);

  GST_INFO_OBJECT (demux, "Created output slot %s:%s",
      GST_DEBUG_PAD_NAME (slot->pad));
  return slot;
}

static gboolean
gst_adaptive_demux_scheduler_unblock_fragment_downloads_cb (GstAdaptiveDemux *
    demux)
{
  GList *iter;

  GST_INFO_OBJECT (demux, "Unblocking streams' fragment downloads");
  demux->priv->streams_can_download_fragments = TRUE;

  iter = demux->input_period->streams;

  for (; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;
    gst_adaptive_demux2_stream_on_can_download_fragments (stream);
  }

  return FALSE;
}

/* must be called with the scheduler lock */
static void
gst_adaptive_demux_set_streams_can_download_fragments (GstAdaptiveDemux * demux,
    gboolean streams_can_download_fragments)
{
  if (streams_can_download_fragments) {
    gst_adaptive_demux_loop_call (demux->priv->scheduler_task, (GSourceFunc)
        gst_adaptive_demux_scheduler_unblock_fragment_downloads_cb, demux,
        NULL);
  } else {
    demux->priv->streams_can_download_fragments =
        streams_can_download_fragments;
  }

}

/* Called:
 * * After `process_manifest` or when a period starts
 * * Or when all tracks have been created
 *
 * Goes over tracks and creates the collection
 *
 * Returns TRUE if the collection was fully created.
 *
 * Must be called with MANIFEST_LOCK and TRACKS_LOCK taken.
 * */
static gboolean
gst_adaptive_demux_update_collection (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxPeriod * period)
{
  GstStreamCollection *collection;
  GList *iter;

  GST_DEBUG_OBJECT (demux, "tracks_changed : %d", period->tracks_changed);

  if (!period->tracks_changed) {
    GST_DEBUG_OBJECT (demux, "Tracks didn't change");
    return TRUE;
  }

  if (!period->tracks) {
    GST_WARNING_OBJECT (demux, "No tracks registered/present");
    return FALSE;
  }

  if (gst_adaptive_demux_period_has_pending_tracks (period)) {
    GST_DEBUG_OBJECT (demux,
        "Streams still have pending tracks, not creating/updating collection");
    return FALSE;
  }

  /* Update collection */
  collection = gst_stream_collection_new ("adaptivedemux");

  for (iter = period->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;

    GST_DEBUG_OBJECT (demux, "Adding '%s' to collection", track->stream_id);
    gst_stream_collection_add_stream (collection,
        gst_object_ref (track->stream_object));
  }

  if (period->collection)
    gst_object_unref (period->collection);
  period->collection = collection;

  return TRUE;
}

/*
 * Called for the output period:
 * * after `update_collection()` if the input period is the same as the output period
 * * When the output period changes
 *
 * Must be called with MANIFEST_LOCK and TRACKS_LOCK taken.
 */
static gboolean
gst_adaptive_demux_post_collection (GstAdaptiveDemux * demux)
{
  GstStreamCollection *collection;
  GstAdaptiveDemuxPeriod *period = demux->output_period;
  guint32 seqnum = g_atomic_int_get (&demux->priv->requested_selection_seqnum);

  g_return_val_if_fail (period, FALSE);
  if (!period->collection) {
    GST_DEBUG_OBJECT (demux, "No collection available yet");
    return TRUE;
  }

  collection = period->collection;

  GST_DEBUG_OBJECT (demux, "Posting collection for period %d",
      period->period_num);

  /* Post collection */
  TRACKS_UNLOCK (demux);
  GST_MANIFEST_UNLOCK (demux);

  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_stream_collection (GST_OBJECT (demux), collection));

  GST_MANIFEST_LOCK (demux);
  TRACKS_LOCK (demux);

  /* If no stream selection was handled, make a default selection */
  if (seqnum == g_atomic_int_get (&demux->priv->requested_selection_seqnum)) {
    gst_adaptive_demux_period_select_default_tracks (demux,
        demux->output_period);
  }

  /* Make sure the output task is running */
  if (gst_adaptive_demux2_is_running (demux)) {
    demux->priv->flushing = FALSE;
    GST_DEBUG_OBJECT (demux, "Starting the output task");
    gst_task_start (demux->priv->output_task);
  }

  return TRUE;
}

/* Called from the sinkpad's input thread with
 * the SCHEDULER lock held */
static gboolean
handle_incoming_manifest (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *demux_class;
  GstQuery *query;
  gboolean query_res;
  gboolean ret = TRUE;
  gsize available;
  GstBuffer *manifest_buffer;

  GST_MANIFEST_LOCK (demux);

  demux_class = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  available = gst_adapter_available (demux->priv->input_adapter);

  if (available == 0)
    goto eos_without_data;

  GST_DEBUG_OBJECT (demux, "Got EOS on the sink pad: manifest fetched");

  /* Need to get the URI to use it as a base to generate the fragment's
   * uris */
  query = gst_query_new_uri ();
  query_res = gst_pad_peer_query (demux->sinkpad, query);
  if (query_res) {
    gchar *uri, *redirect_uri;
    gboolean permanent;

    gst_query_parse_uri (query, &uri);
    gst_query_parse_uri_redirection (query, &redirect_uri);
    gst_query_parse_uri_redirection_permanent (query, &permanent);

    if (permanent && redirect_uri) {
      demux->manifest_uri = redirect_uri;
      demux->manifest_base_uri = NULL;
      g_free (uri);
    } else {
      demux->manifest_uri = uri;
      demux->manifest_base_uri = redirect_uri;
    }

    GST_DEBUG_OBJECT (demux, "Fetched manifest at URI: %s (base: %s)",
        demux->manifest_uri, GST_STR_NULL (demux->manifest_base_uri));

    if (!g_str_has_prefix (demux->manifest_uri, "data:")
        && !g_str_has_prefix (demux->manifest_uri, "http://")
        && !g_str_has_prefix (demux->manifest_uri, "https://")) {
      GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
          (_("Invalid manifest URI")),
          ("Manifest URI needs to use either data:, http:// or https://"));
      gst_query_unref (query);
      ret = FALSE;
      goto unlock_out;
    }
  } else {
    GST_WARNING_OBJECT (demux, "Upstream URI query failed.");
  }
  gst_query_unref (query);

  /* If somehow we didn't receive a stream-start with a group_id, pick one now */
  if (!demux->have_group_id) {
    demux->have_group_id = TRUE;
    demux->group_id = gst_util_group_id_next ();
  }

  /* Let the subclass parse the manifest */
  manifest_buffer =
      gst_adapter_take_buffer (demux->priv->input_adapter, available);
  ret = demux_class->process_manifest (demux, manifest_buffer);
  gst_buffer_unref (manifest_buffer);

  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux),
          gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
              "manifest-uri", G_TYPE_STRING,
              demux->manifest_uri, "uri", G_TYPE_STRING,
              demux->manifest_uri,
              "manifest-download-start", GST_TYPE_CLOCK_TIME,
              GST_CLOCK_TIME_NONE,
              "manifest-download-stop", GST_TYPE_CLOCK_TIME,
              gst_util_get_timestamp (), NULL)));

  if (!ret)
    goto invalid_manifest;

  /* Streams should have been added to the input period if the manifest parsing
   * succeeded */
  if (!demux->input_period->streams)
    goto no_streams;

  g_atomic_int_set (&demux->priv->have_manifest, TRUE);

  GST_DEBUG_OBJECT (demux, "Manifest was processed, setting ourselves up");
  /* Send duration message */
  if (!gst_adaptive_demux_is_live (demux)) {
    GstClockTime duration = demux_class->get_duration (demux);

    demux->priv->duration = duration;
    if (duration != GST_CLOCK_TIME_NONE) {
      GST_DEBUG_OBJECT (demux,
          "Sending duration message : %" GST_TIME_FORMAT,
          GST_TIME_ARGS (duration));
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_duration_changed (GST_OBJECT (demux)));
    } else {
      GST_DEBUG_OBJECT (demux,
          "media duration unknown, can not send the duration message");
    }
  }

  TRACKS_LOCK (demux);
  /* New streams/tracks will have been added to the input period */
  /* The input period has streams, make it the active output period */
  /* FIXME : Factorize this into a function to make a period active */
  demux->output_period = gst_adaptive_demux_period_ref (demux->input_period);
  ret = gst_adaptive_demux_update_collection (demux, demux->output_period) &&
      gst_adaptive_demux_post_collection (demux);
  TRACKS_UNLOCK (demux);

  gst_adaptive_demux_set_streams_can_download_fragments (demux, FALSE);
  gst_adaptive_demux_prepare_streams (demux,
      gst_adaptive_demux_is_live (demux));
  gst_adaptive_demux_set_streams_can_download_fragments (demux, TRUE);
  gst_adaptive_demux_start_tasks (demux);
  gst_adaptive_demux_start_manifest_update_task (demux);

unlock_out:
  GST_MANIFEST_UNLOCK (demux);

  return ret;

  /* ERRORS */
eos_without_data:
  {
    GST_WARNING_OBJECT (demux, "Received EOS without a manifest.");
    ret = FALSE;
    goto unlock_out;
  }

no_streams:
  {
    /* no streams */
    GST_WARNING_OBJECT (demux, "No streams created from manifest");
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
        (_("This file contains no playable streams.")),
        ("No known stream formats found at the Manifest"));
    ret = FALSE;
    goto unlock_out;
  }

invalid_manifest:
  {
    GST_MANIFEST_UNLOCK (demux);

    /* In most cases, this will happen if we set a wrong url in the
     * source element and we have received the 404 HTML response instead of
     * the manifest */
    GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid manifest."), (NULL));
    return FALSE;
  }
}

struct http_headers_collector
{
  GstAdaptiveDemux *demux;
  gchar **cookies;
};

static gboolean
gst_adaptive_demux_handle_upstream_http_header (GQuark field_id,
    const GValue * value, gpointer userdata)
{
  struct http_headers_collector *hdr_data = userdata;
  GstAdaptiveDemux *demux = hdr_data->demux;
  const gchar *field_name = g_quark_to_string (field_id);

  if (G_UNLIKELY (value == NULL))
    return TRUE;                /* This should not happen */

  if (g_ascii_strcasecmp (field_name, "User-Agent") == 0) {
    const gchar *user_agent = g_value_get_string (value);

    GST_INFO_OBJECT (demux, "User-Agent : %s", GST_STR_NULL (user_agent));
    downloadhelper_set_user_agent (demux->download_helper, user_agent);
  }

  if ((g_ascii_strcasecmp (field_name, "Cookie") == 0) ||
      g_ascii_strcasecmp (field_name, "Set-Cookie") == 0) {
    guint i = 0, prev_len = 0, total_len = 0;
    gchar **cookies = NULL;

    if (hdr_data->cookies != NULL)
      prev_len = g_strv_length (hdr_data->cookies);

    if (GST_VALUE_HOLDS_ARRAY (value)) {
      total_len = gst_value_array_get_size (value) + prev_len;
      cookies = (gchar **) g_malloc0 ((total_len + 1) * sizeof (gchar *));

      for (i = 0; i < gst_value_array_get_size (value); i++) {
        GST_INFO_OBJECT (demux, "%s : %s", g_quark_to_string (field_id),
            g_value_get_string (gst_value_array_get_value (value, i)));
        cookies[i] = g_value_dup_string (gst_value_array_get_value (value, i));
      }
    } else if (G_VALUE_HOLDS_STRING (value)) {
      total_len = 1 + prev_len;
      cookies = (gchar **) g_malloc0 ((total_len + 1) * sizeof (gchar *));

      GST_INFO_OBJECT (demux, "%s : %s", g_quark_to_string (field_id),
          g_value_get_string (value));
      cookies[0] = g_value_dup_string (value);
    } else {
      GST_WARNING_OBJECT (demux, "%s field is not string or array",
          g_quark_to_string (field_id));
    }

    if (cookies) {
      if (prev_len) {
        guint j;
        for (j = 0; j < prev_len; j++) {
          GST_DEBUG_OBJECT (demux,
              "Append existing cookie %s", hdr_data->cookies[j]);
          cookies[i + j] = g_strdup (hdr_data->cookies[j]);
        }
      }
      cookies[total_len] = NULL;

      g_strfreev (hdr_data->cookies);
      hdr_data->cookies = cookies;
    }
  }

  if (g_ascii_strcasecmp (field_name, "Referer") == 0) {
    const gchar *referer = g_value_get_string (value);
    GST_INFO_OBJECT (demux, "Referer : %s", GST_STR_NULL (referer));

    downloadhelper_set_referer (demux->download_helper, referer);
  }

  /* Date header can be used to estimate server offset */
  if (g_ascii_strcasecmp (field_name, "Date") == 0) {
    const gchar *http_date = g_value_get_string (value);

    if (http_date) {
      GstDateTime *datetime =
          gst_adaptive_demux_util_parse_http_head_date (http_date);

      if (datetime) {
        GDateTime *utc_now = gst_date_time_to_g_date_time (datetime);
        gchar *date_string = gst_date_time_to_iso8601_string (datetime);

        GST_INFO_OBJECT (demux,
            "HTTP response Date %s", GST_STR_NULL (date_string));
        g_free (date_string);

        gst_adaptive_demux_clock_set_utc_time (demux->realtime_clock, utc_now);

        g_date_time_unref (utc_now);
        gst_date_time_unref (datetime);
      }
    }
  }

  return TRUE;
}

static gboolean
gst_adaptive_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (parent);
  gboolean ret;

  switch (event->type) {
    case GST_EVENT_FLUSH_STOP:{
      GST_MANIFEST_LOCK (demux);

      gst_adaptive_demux_reset (demux);

      ret = gst_pad_event_default (pad, parent, event);

      GST_MANIFEST_UNLOCK (demux);

      return ret;
    }
    case GST_EVENT_EOS:
    {
      if (GST_ADAPTIVE_SCHEDULER_LOCK (demux)) {
        if (!handle_incoming_manifest (demux)) {
          GST_ADAPTIVE_SCHEDULER_UNLOCK (demux);
          return gst_pad_event_default (pad, parent, event);
        }
        GST_ADAPTIVE_SCHEDULER_UNLOCK (demux);
      } else {
        GST_ERROR_OBJECT (demux,
            "Failed to acquire scheduler to handle manifest");
        return gst_pad_event_default (pad, parent, event);
      }
      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_STREAM_START:
      if (gst_event_parse_group_id (event, &demux->group_id))
        demux->have_group_id = TRUE;
      else
        demux->have_group_id = FALSE;
      /* Swallow stream-start, we'll push our own */
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_SEGMENT:
      /* Swallow newsegments, we'll push our own */
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:{
      const GstStructure *structure = gst_event_get_structure (event);
      struct http_headers_collector c = { demux, NULL };

      if (gst_structure_has_name (structure, "http-headers")) {
        if (gst_structure_has_field (structure, "request-headers")) {
          GstStructure *req_headers = NULL;
          gst_structure_get (structure, "request-headers", GST_TYPE_STRUCTURE,
              &req_headers, NULL);
          if (req_headers) {
            gst_structure_foreach (req_headers,
                gst_adaptive_demux_handle_upstream_http_header, &c);
            gst_structure_free (req_headers);
          }
        }
        if (gst_structure_has_field (structure, "response-headers")) {
          GstStructure *res_headers = NULL;
          gst_structure_get (structure, "response-headers", GST_TYPE_STRUCTURE,
              &res_headers, NULL);
          if (res_headers) {
            gst_structure_foreach (res_headers,
                gst_adaptive_demux_handle_upstream_http_header, &c);
            gst_structure_free (res_headers);
          }
        }

        if (c.cookies)
          downloadhelper_set_cookies (demux->download_helper, c.cookies);
      }
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_adaptive_demux_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (parent);

  GST_MANIFEST_LOCK (demux);

  gst_adapter_push (demux->priv->input_adapter, buffer);

  GST_INFO_OBJECT (demux, "Received manifest buffer, total size is %i bytes",
      (gint) gst_adapter_available (demux->priv->input_adapter));

  GST_MANIFEST_UNLOCK (demux);
  return GST_FLOW_OK;
}


/* Called with TRACKS_LOCK taken */
static void
gst_adaptive_demux_period_reset_tracks (GstAdaptiveDemuxPeriod * period)
{
  GList *tmp;

  for (tmp = period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;

    gst_adaptive_demux_track_flush (track);
    if (gst_pad_is_active (track->sinkpad)) {
      gst_pad_set_active (track->sinkpad, FALSE);
      gst_pad_set_active (track->sinkpad, TRUE);
    }
  }
}

/* Resets all tracks to their initial state, ready to receive new data. */
static void
gst_adaptive_demux_reset_tracks (GstAdaptiveDemux * demux)
{
  TRACKS_LOCK (demux);
  g_queue_foreach (demux->priv->periods,
      (GFunc) gst_adaptive_demux_period_reset_tracks, NULL);
  TRACKS_UNLOCK (demux);
}

/* Subclasses will call this function to ensure that a new input period is
 * available to receive new streams and tracks */
gboolean
gst_adaptive_demux_start_new_period (GstAdaptiveDemux * demux)
{
  if (demux->input_period && !demux->input_period->prepared) {
    GST_DEBUG_OBJECT (demux, "Using existing input period");
    return TRUE;
  }

  if (demux->input_period) {
    GST_DEBUG_OBJECT (demux, "Marking that previous period has a next one");
    demux->input_period->has_next_period = TRUE;
  }
  GST_DEBUG_OBJECT (demux, "Setting up new period");

  demux->input_period = gst_adaptive_demux_period_new (demux);

  return TRUE;
}

/* must be called with manifest_lock taken */
static void
gst_adaptive_demux_reset (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GList *iter;

  gst_adaptive_demux_stop_tasks (demux, TRUE);

  if (klass->reset)
    klass->reset (demux);

  /* Disable and remove all outputs */
  GST_DEBUG_OBJECT (demux, "Disabling and removing all outputs");
  for (iter = demux->priv->outputs; iter; iter = iter->next) {
    gst_adaptive_demux_output_slot_free (demux, (OutputSlot *) iter->data);
  }
  g_list_free (demux->priv->outputs);
  demux->priv->outputs = NULL;

  g_queue_clear_full (demux->priv->periods,
      (GDestroyNotify) gst_adaptive_demux_period_unref);

  /* The output period always has an extra ref taken on it */
  if (demux->output_period)
    gst_adaptive_demux_period_unref (demux->output_period);
  demux->output_period = NULL;
  /* The input period doesn't have an extra ref taken on it */
  demux->input_period = NULL;

  gst_adaptive_demux_start_new_period (demux);

  g_free (demux->manifest_uri);
  g_free (demux->manifest_base_uri);
  demux->manifest_uri = NULL;
  demux->manifest_base_uri = NULL;

  gst_adapter_clear (demux->priv->input_adapter);
  g_atomic_int_set (&demux->priv->have_manifest, FALSE);

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  demux->instant_rate_multiplier = 1.0;

  demux->priv->duration = GST_CLOCK_TIME_NONE;

  demux->priv->percent = -1;
  demux->priv->is_buffering = TRUE;

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;
  demux->priv->segment_seqnum = gst_util_seqnum_next ();

  demux->priv->global_output_position = 0;

  demux->priv->n_audio_streams = 0;
  demux->priv->n_video_streams = 0;
  demux->priv->n_subtitle_streams = 0;

  gst_flow_combiner_reset (demux->priv->flowcombiner);
}

static gboolean
gst_adaptive_demux_send_event (GstElement * element, GstEvent * event)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (element);
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (demux, "Received event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      res = gst_adaptive_demux_handle_seek_event (demux, event);
      break;
    }
    case GST_EVENT_SELECT_STREAMS:
    {
      res = gst_adaptive_demux_handle_select_streams_event (demux, event);
      break;
    }
    default:
      res = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
      break;
  }
  return res;
}

/* MANIFEST_LOCK held. Find the stream that owns the given element */
static GstAdaptiveDemux2Stream *
find_stream_for_element_locked (GstAdaptiveDemux * demux, GstObject * o)
{
  GList *iter;

  /* We only look in the streams of the input period (i.e. with active streams) */
  for (iter = demux->input_period->streams; iter; iter = iter->next) {
    GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) iter->data;
    if (gst_object_has_as_ancestor (o, GST_OBJECT_CAST (stream->parsebin))) {
      return stream;
    }
  }

  return NULL;
}

static void
gst_adaptive_demux_handle_stream_collection_msg (GstAdaptiveDemux * demux,
    GstMessage * msg)
{
  GstAdaptiveDemux2Stream *stream;
  GstStreamCollection *collection = NULL;
  gboolean pending_tracks_activated = FALSE;

  GST_MANIFEST_LOCK (demux);

  stream = find_stream_for_element_locked (demux, GST_MESSAGE_SRC (msg));
  if (stream == NULL) {
    GST_WARNING_OBJECT (demux,
        "Failed to locate stream for collection message");
    goto beach;
  }

  gst_message_parse_stream_collection (msg, &collection);
  if (!collection)
    goto beach;

  TRACKS_LOCK (demux);

  if (!gst_adaptive_demux2_stream_handle_collection (stream, collection,
          &pending_tracks_activated)) {
    TRACKS_UNLOCK (demux);

    GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
        (_("Stream format can't be handled")),
        ("The streams provided by the multiplex are ambiguous"));
    goto beach;
  }

  if (pending_tracks_activated) {
    /* If pending tracks were handled, then update the demuxer collection */
    if (gst_adaptive_demux_update_collection (demux, demux->input_period) &&
        demux->input_period == demux->output_period) {
      gst_adaptive_demux_post_collection (demux);
    }

    /* If we discovered pending tracks and we no longer have any, we can ensure
     * selected tracks are started */
    if (!gst_adaptive_demux_period_has_pending_tracks (demux->input_period)) {
      GList *iter = demux->input_period->streams;
      for (; iter; iter = iter->next) {
        GstAdaptiveDemux2Stream *new_stream = iter->data;

        /* The stream that posted this collection was already started. If a
         * different stream is now selected, start it */
        if (stream != new_stream
            && gst_adaptive_demux2_stream_is_selected_locked (new_stream))
          gst_adaptive_demux2_stream_start (new_stream);
      }
    }
  }
  TRACKS_UNLOCK (demux);

beach:
  GST_MANIFEST_UNLOCK (demux);

  if (collection)
    gst_object_unref (collection);
  gst_message_unref (msg);
  msg = NULL;
}

static void
gst_adaptive_demux_handle_message (GstBin * bin, GstMessage * msg)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (bin);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_STREAM_COLLECTION:
    {
      gst_adaptive_demux_handle_stream_collection_msg (demux, msg);
      return;
    }
    case GST_MESSAGE_ERROR:{
      GstAdaptiveDemux2Stream *stream = NULL;
      GError *err = NULL;
      gchar *debug = NULL;
      gchar *new_error = NULL;
      const GstStructure *details = NULL;

      GST_MANIFEST_LOCK (demux);

      stream = find_stream_for_element_locked (demux, GST_MESSAGE_SRC (msg));
      if (stream == NULL) {
        GST_WARNING_OBJECT (demux,
            "Failed to locate stream for errored element");
        GST_MANIFEST_UNLOCK (demux);
        break;
      }

      gst_message_parse_error (msg, &err, &debug);

      GST_WARNING_OBJECT (demux,
          "Source posted error: %d:%d %s (%s)", err->domain, err->code,
          err->message, debug);

      if (debug)
        new_error = g_strdup_printf ("%s: %s\n", err->message, debug);
      if (new_error) {
        g_free (err->message);
        err->message = new_error;
      }

      gst_message_parse_error_details (msg, &details);
      if (details) {
        gst_structure_get_uint (details, "http-status-code",
            &stream->last_status_code);
      }

      /* error, but ask to retry */
      if (GST_ADAPTIVE_SCHEDULER_LOCK (demux)) {
        gst_adaptive_demux2_stream_parse_error (stream, err);
        GST_ADAPTIVE_SCHEDULER_UNLOCK (demux);
      }

      g_error_free (err);
      g_free (debug);

      GST_MANIFEST_UNLOCK (demux);

      gst_message_unref (msg);
      msg = NULL;
    }
      break;
    default:
      break;
  }

  if (msg)
    GST_BIN_CLASS (parent_class)->handle_message (bin, msg);
}

/* must be called with manifest_lock taken */
GstClockTime
gst_adaptive_demux_get_period_start_time (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass;

  klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->get_period_start_time == NULL)
    return 0;

  return klass->get_period_start_time (demux);
}

/* must be called with manifest_lock taken */
static gboolean
gst_adaptive_demux_prepare_streams (GstAdaptiveDemux * demux,
    gboolean first_and_live)
{
  GList *iter;
  GstClockTime period_start;
  GstClockTimeDiff min_stream_time = GST_CLOCK_STIME_NONE;
  GList *new_streams;

  g_return_val_if_fail (demux->input_period->streams, FALSE);
  g_assert (demux->input_period->prepared == FALSE);

  new_streams = demux->input_period->streams;

  if (!gst_adaptive_demux2_is_running (demux)) {
    GST_DEBUG_OBJECT (demux, "Not exposing pads due to shutdown");
    return TRUE;
  }

  GST_DEBUG_OBJECT (demux,
      "Preparing %d streams for period %d , first_and_live:%d",
      g_list_length (new_streams), demux->input_period->period_num,
      first_and_live);

  for (iter = new_streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;
    gboolean is_selected =
        gst_adaptive_demux2_stream_is_selected_locked (stream);

    GST_DEBUG_OBJECT (stream,
        "Preparing stream. Is selected: %d pending_tracks: %d", is_selected,
        stream->pending_tracks);

    stream->need_header = TRUE;
    stream->discont = TRUE;

    /* Grab the first stream time for live streams
     * * If the stream is selected
     * * Or it provides dynamic tracks (in which case we need to force an update)
     */
    if (first_and_live && (is_selected || stream->pending_tracks)) {
      /* TODO we only need the first timestamp, maybe create a simple function to
       * get the current PTS of a fragment ? */
      GST_DEBUG_OBJECT (stream, "Calling update_fragment_info");
      GstFlowReturn flow_ret =
          gst_adaptive_demux2_stream_update_fragment_info (stream);

      /* Handle fragment info waiting on BUSY */
      while (flow_ret == GST_ADAPTIVE_DEMUX_FLOW_BUSY) {
        if (!gst_adaptive_demux2_stream_wait_prepared (stream))
          break;
        flow_ret = gst_adaptive_demux2_stream_update_fragment_info (stream);
      }

      if (flow_ret != GST_FLOW_OK) {
        GST_WARNING_OBJECT (stream, "Could not update fragment info. flow: %s",
            gst_flow_get_name (flow_ret));
        continue;
      }

      GST_DEBUG_OBJECT (stream,
          "Got stream time %" GST_STIME_FORMAT,
          GST_STIME_ARGS (stream->fragment.stream_time));

      if (GST_CLOCK_STIME_IS_VALID (min_stream_time)) {
        min_stream_time = MIN (min_stream_time, stream->fragment.stream_time);
      } else {
        min_stream_time = stream->fragment.stream_time;
      }
    }
  }

  period_start = gst_adaptive_demux_get_period_start_time (demux);

  /* For live streams, the subclass is supposed to seek to the current fragment
   * and then tell us its stream time in stream->fragment.stream_time.  We now
   * also have to seek our demuxer segment to reflect this.
   *
   * FIXME: This needs some refactoring at some point.
   */
  if (first_and_live) {
    gst_segment_do_seek (&demux->segment, demux->segment.rate, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, min_stream_time + period_start,
        GST_SEEK_TYPE_NONE, -1, NULL);
  }

  GST_DEBUG_OBJECT (demux,
      "period_start:%" GST_TIME_FORMAT ", min_stream_time:%" GST_STIME_FORMAT
      " demux segment %" GST_SEGMENT_FORMAT,
      GST_TIME_ARGS (period_start), GST_STIME_ARGS (min_stream_time),
      &demux->segment);

  /* Synchronize stream start/current positions */
  if (min_stream_time == GST_CLOCK_STIME_NONE)
    min_stream_time = period_start;
  else
    min_stream_time += period_start;
  for (iter = new_streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;
    stream->start_position = stream->current_position = min_stream_time;
  }

  for (iter = new_streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;
    stream->compute_segment = TRUE;
    stream->first_and_live = first_and_live;
  }
  demux->priv->qos_earliest_time = GST_CLOCK_TIME_NONE;
  demux->input_period->prepared = TRUE;

  return TRUE;
}

static GstAdaptiveDemuxTrack *
find_track_for_stream_id (GstAdaptiveDemuxPeriod * period, gchar * stream_id)
{
  GList *tmp;

  for (tmp = period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;
    if (!g_strcmp0 (track->stream_id, stream_id))
      return track;
  }

  return NULL;
}

/* TRACKS_LOCK hold */
void
demux_update_buffering_locked (GstAdaptiveDemux * demux)
{
  GstClockTime min_level_time = GST_CLOCK_TIME_NONE;
  GstClockTime video_level_time = GST_CLOCK_TIME_NONE;
  GstClockTime audio_level_time = GST_CLOCK_TIME_NONE;
  GList *tmp;
  gint min_percent = -1, percent;
  gboolean all_eos = TRUE;

  /* Go over all active tracks of the output period and update level */

  /* Check that all tracks are above their respective low thresholds (different
   * tracks may have different fragment durations yielding different buffering
   * percentages) Overall buffering percent is the lowest. */
  for (tmp = demux->output_period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;

    GST_LOG_ID (track->id,
        "Checking track active:%d selected:%d eos:%d level:%"
        GST_TIME_FORMAT " buffering_threshold:%" GST_TIME_FORMAT,
        track->active, track->selected,
        track->eos, GST_TIME_ARGS (track->level_time),
        GST_TIME_ARGS (track->buffering_threshold));

    if (track->active && track->selected) {
      if (!track->eos) {
        gint cur_percent;

        all_eos = FALSE;
        if (min_level_time == GST_CLOCK_TIME_NONE) {
          min_level_time = track->level_time;
        } else if (track->level_time < min_level_time) {
          min_level_time = track->level_time;
        }

        if (track->type & GST_STREAM_TYPE_VIDEO
            && video_level_time > track->level_time)
          video_level_time = track->level_time;

        if (track->type & GST_STREAM_TYPE_AUDIO
            && audio_level_time > track->level_time)
          audio_level_time = track->level_time;

        if (track->level_time != GST_CLOCK_TIME_NONE
            && track->buffering_threshold != 0) {
          cur_percent =
              gst_util_uint64_scale (track->level_time, 100,
              track->buffering_threshold);
          if (min_percent < 0 || cur_percent < min_percent)
            min_percent = cur_percent;
        }
      }
    }
  }

  GST_DEBUG_OBJECT (demux,
      "Minimum time level %" GST_TIME_FORMAT " percent %d all_eos:%d",
      GST_TIME_ARGS (min_level_time), min_percent, all_eos);

  /* Update demuxer video/audio level properties */
  GST_OBJECT_LOCK (demux);
  demux->current_level_time_video = video_level_time;
  demux->current_level_time_audio = audio_level_time;
  GST_OBJECT_UNLOCK (demux);

  if (min_percent < 0 && !all_eos)
    return;

  if (min_percent > 100 || all_eos)
    percent = 100;
  else
    percent = MAX (0, min_percent);

  GST_LOG_OBJECT (demux, "percent : %d %%", percent);

  if (demux->priv->is_buffering) {
    if (percent >= 100)
      demux->priv->is_buffering = FALSE;
    if (demux->priv->percent != percent) {
      demux->priv->percent = percent;
      demux->priv->percent_changed = TRUE;
    }
  } else if (percent < 1) {
    demux->priv->is_buffering = TRUE;
    if (demux->priv->percent != percent) {
      demux->priv->percent = percent;
      demux->priv->percent_changed = TRUE;
    }
  }

  if (demux->priv->percent_changed)
    GST_DEBUG_OBJECT (demux, "Percent changed, %d %% is_buffering:%d", percent,
        demux->priv->is_buffering);
}

/* With TRACKS_LOCK held */
void
demux_post_buffering_locked (GstAdaptiveDemux * demux)
{
  gint percent;
  GstMessage *msg;

  if (!demux->priv->percent_changed)
    return;

  BUFFERING_LOCK (demux);
  percent = demux->priv->percent;
  msg = gst_message_new_buffering ((GstObject *) demux, percent);
  TRACKS_UNLOCK (demux);
  gst_element_post_message ((GstElement *) demux, msg);

  BUFFERING_UNLOCK (demux);
  TRACKS_LOCK (demux);
  if (percent == demux->priv->percent)
    demux->priv->percent_changed = FALSE;
}

/* MANIFEST_LOCK and TRACKS_LOCK hold */
static GstAdaptiveDemux2Stream *
find_stream_for_track_locked (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxTrack * track)
{
  GList *iter;

  for (iter = demux->output_period->streams; iter; iter = iter->next) {
    GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) iter->data;
    if (g_list_find (stream->tracks, track))
      return stream;
  }

  return NULL;
}

/* Called from seek handler
 *
 * This function is used when a (flushing) seek caused a new period to be activated.
 *
 * This will ensure that:
 * * the current output period is marked as finished (EOS)
 * * Any potential intermediate (non-input/non-output) periods are removed
 * * That the new input period is prepared and ready
 */
static void
gst_adaptive_demux_seek_to_input_period (GstAdaptiveDemux * demux)
{
  GList *iter;

  GST_DEBUG_OBJECT (demux,
      "Preparing new input period %u", demux->input_period->period_num);

  /* Prepare the new input period */
  gst_adaptive_demux_update_collection (demux, demux->input_period);

  /* Transfer the previous selection to the new input period */
  gst_adaptive_demux_period_transfer_selection (demux, demux->input_period,
      demux->output_period);
  gst_adaptive_demux_prepare_streams (demux, FALSE);

  /* Remove all periods except for the input (last) and output (first) period */
  while (demux->priv->periods->length > 2) {
    GstAdaptiveDemuxPeriod *period = g_queue_pop_nth (demux->priv->periods, 1);
    /* Mark all tracks of the removed period as not selected and EOS so they
     * will be skipped / ignored */
    for (iter = period->tracks; iter; iter = iter->next) {
      GstAdaptiveDemuxTrack *track = iter->data;
      track->selected = FALSE;
      track->eos = TRUE;
    }
    gst_adaptive_demux_period_unref (period);
  }

  /* Mark all tracks of the output period as EOS so that the output loop
   * will immediately move to the new period */
  for (iter = demux->output_period->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = iter->data;
    track->eos = TRUE;
  }

  /* Go over all slots, and clear any pending track */
  for (iter = demux->priv->outputs; iter; iter = iter->next) {
    OutputSlot *slot = (OutputSlot *) iter->data;

    if (slot->pending_track != NULL) {
      GST_DEBUG_OBJECT (demux,
          "Removing track '%s' as pending from output of current track '%s'",
          slot->pending_track->id, slot->track->id);
      gst_adaptive_demux_track_unref (slot->pending_track);
      slot->pending_track = NULL;
    }
  }
}

/* must be called with scheduler lock taken */
gboolean
gst_adaptive_demux_get_live_seek_range (GstAdaptiveDemux * demux,
    gint64 * range_start, gint64 * range_stop)
{
  GstAdaptiveDemuxClass *klass;

  klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  g_return_val_if_fail (klass->get_live_seek_range, FALSE);

  return klass->get_live_seek_range (demux, range_start, range_stop);
}

/* must be called from scheduler task */
gboolean
gst_adaptive_demux2_stream_in_live_seek_range (GstAdaptiveDemux * demux,
    GstAdaptiveDemux2Stream * stream)
{
  gint64 range_start, range_stop;
  if (gst_adaptive_demux_get_live_seek_range (demux, &range_start, &range_stop)) {
    GST_LOG_OBJECT (stream,
        "stream position %" GST_TIME_FORMAT "  live seek range %"
        GST_STIME_FORMAT " - %" GST_STIME_FORMAT,
        GST_TIME_ARGS (stream->current_position), GST_STIME_ARGS (range_start),
        GST_STIME_ARGS (range_stop));
    return (stream->current_position >= range_start
        && stream->current_position <= range_stop);
  }

  return FALSE;
}

/* must be called with manifest_lock taken */
static gboolean
gst_adaptive_demux_can_seek (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass;

  klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  if (gst_adaptive_demux_is_live (demux)) {
    return klass->get_live_seek_range != NULL;
  }

  return klass->seek != NULL;
}

static void
gst_adaptive_demux_setup_streams_for_restart (GstAdaptiveDemux * demux,
    GstSeekType start_type, GstSeekType stop_type)
{
  GList *iter;

  for (iter = demux->input_period->streams; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;

    /* Make sure the download loop clears and restarts on the next start,
     * which will recompute the stream segment */
    g_assert (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED ||
        stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART);
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART;
    stream->start_position = 0;

    if (demux->segment.rate > 0 && start_type != GST_SEEK_TYPE_NONE)
      stream->start_position = demux->segment.start;
    else if (demux->segment.rate < 0 && stop_type != GST_SEEK_TYPE_NONE)
      stream->start_position = demux->segment.stop;
  }
}

#define IS_SNAP_SEEK(f) (f & (GST_SEEK_FLAG_SNAP_BEFORE |	  \
                              GST_SEEK_FLAG_SNAP_AFTER |	  \
                              GST_SEEK_FLAG_SNAP_NEAREST |	  \
			      GST_SEEK_FLAG_TRICKMODE_KEY_UNITS | \
			      GST_SEEK_FLAG_KEY_UNIT))
#define REMOVE_SNAP_FLAGS(f) (f & ~(GST_SEEK_FLAG_SNAP_BEFORE | \
                              GST_SEEK_FLAG_SNAP_AFTER | \
                              GST_SEEK_FLAG_SNAP_NEAREST))

static gboolean
gst_adaptive_demux_handle_seek_event (GstAdaptiveDemux * demux,
    GstEvent * event)
{
  GstAdaptiveDemuxClass *demux_class = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  guint32 seqnum;
  gboolean update;
  gboolean ret = FALSE;
  GstSegment oldsegment;
  GstEvent *flush_event;

  GST_INFO_OBJECT (demux, "Received seek event");

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    GST_WARNING_OBJECT (demux,
        "Adaptive demuxers only support TIME-based seeking");
    gst_event_unref (event);
    return FALSE;
  }

  if (flags & GST_SEEK_FLAG_SEGMENT) {
    GST_FIXME_OBJECT (demux, "Handle segment seeks");
    gst_event_unref (event);
    return FALSE;
  }

  seqnum = gst_event_get_seqnum (event);

  if (!GST_ADAPTIVE_SCHEDULER_LOCK (demux)) {
    GST_LOG_OBJECT (demux, "Failed to acquire scheduler context");
    return FALSE;
  }

  if (flags & GST_SEEK_FLAG_INSTANT_RATE_CHANGE) {
    /* For instant rate seeks, reply directly and update
     * our segment so the new rate is reflected in any future
     * fragments */
    GstEvent *ev;
    gdouble rate_multiplier;

    /* instant rate change only supported if direction does not change. All
     * other requirements are already checked before creating the seek event
     * but let's double-check here to be sure */
    if ((demux->segment.rate > 0 && rate < 0) ||
        (demux->segment.rate < 0 && rate > 0) ||
        start_type != GST_SEEK_TYPE_NONE ||
        stop_type != GST_SEEK_TYPE_NONE || (flags & GST_SEEK_FLAG_FLUSH)) {
      GST_ERROR_OBJECT (demux,
          "Instant rate change seeks only supported in the "
          "same direction, without flushing and position change");
      goto unlock_return;
    }

    rate_multiplier = rate / demux->segment.rate;

    ev = gst_event_new_instant_rate_change (rate_multiplier,
        (GstSegmentFlags) flags);
    gst_event_set_seqnum (ev, seqnum);

    ret = gst_adaptive_demux_push_src_event (demux, ev);

    if (ret) {
      GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
      demux->instant_rate_multiplier = rate_multiplier;
      GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);
    }
    goto unlock_return;
  }

  if (!gst_adaptive_demux_can_seek (demux))
    goto unlock_return;

  /* We can only accept flushing seeks from this point onward */
  if (!(flags & GST_SEEK_FLAG_FLUSH)) {
    GST_ERROR_OBJECT (demux,
        "Non-flushing non-instant-rate seeks are not possible");
    goto unlock_return;
  }

  if (gst_adaptive_demux_is_live (demux)) {
    gint64 range_start, range_stop;
    gboolean changed = FALSE;
    gboolean start_valid = TRUE, stop_valid = TRUE;

    if (!gst_adaptive_demux_get_live_seek_range (demux, &range_start,
            &range_stop)) {
      GST_WARNING_OBJECT (demux, "Failure getting the live seek ranges");
      goto unlock_return;
    }

    GST_DEBUG_OBJECT (demux,
        "Live range is %" GST_STIME_FORMAT " %" GST_STIME_FORMAT,
        GST_STIME_ARGS (range_start), GST_STIME_ARGS (range_stop));

    /* Handle relative positioning for live streams (relative to the range_stop) */
    if (start_type == GST_SEEK_TYPE_END) {
      start = range_stop + start;
      start_type = GST_SEEK_TYPE_SET;
      changed = TRUE;
    }
    if (stop_type == GST_SEEK_TYPE_END) {
      stop = range_stop + stop;
      stop_type = GST_SEEK_TYPE_SET;
      changed = TRUE;
    }

    /* Adjust the requested start/stop position if it falls beyond the live
     * seek range.
     * The only case where we don't adjust is for the starting point of
     * an accurate seek (start if forward and stop if backwards)
     */
    if (start_type == GST_SEEK_TYPE_SET && start < range_start &&
        (rate < 0 || !(flags & GST_SEEK_FLAG_ACCURATE))) {
      GST_DEBUG_OBJECT (demux,
          "seek before live stream start, setting to range start: %"
          GST_TIME_FORMAT, GST_TIME_ARGS (range_start));
      start = range_start;
      changed = TRUE;
    }
    /* truncate stop position also if set */
    if (stop_type == GST_SEEK_TYPE_SET && stop > range_stop &&
        (rate > 0 || !(flags & GST_SEEK_FLAG_ACCURATE))) {
      GST_DEBUG_OBJECT (demux,
          "seek ending after live start, adjusting to: %"
          GST_TIME_FORMAT, GST_TIME_ARGS (range_stop));
      stop = range_stop;
      changed = TRUE;
    }

    if (start_type == GST_SEEK_TYPE_SET && GST_CLOCK_TIME_IS_VALID (start) &&
        (start < range_start || start > range_stop)) {
      GST_WARNING_OBJECT (demux,
          "Seek to invalid position start:%" GST_STIME_FORMAT
          " out of seekable range (%" GST_STIME_FORMAT " - %" GST_STIME_FORMAT
          ")", GST_STIME_ARGS (start), GST_STIME_ARGS (range_start),
          GST_STIME_ARGS (range_stop));
      start_valid = FALSE;
    }
    if (stop_type == GST_SEEK_TYPE_SET && GST_CLOCK_TIME_IS_VALID (stop) &&
        (stop < range_start || stop > range_stop)) {
      GST_WARNING_OBJECT (demux,
          "Seek to invalid position stop:%" GST_STIME_FORMAT
          " out of seekable range (%" GST_STIME_FORMAT " - %" GST_STIME_FORMAT
          ")", GST_STIME_ARGS (stop), GST_STIME_ARGS (range_start),
          GST_STIME_ARGS (range_stop));
      stop_valid = FALSE;
    }

    /* If the seek position is still outside of the seekable range, refuse the seek */
    if (!start_valid || !stop_valid)
      goto unlock_return;

    /* Re-create seek event with changed/updated values */
    if (changed) {
      gst_event_unref (event);
      event =
          gst_event_new_seek (rate, format, flags,
          start_type, start, stop_type, stop);
      gst_event_set_seqnum (event, seqnum);
    }
  }

  GST_DEBUG_OBJECT (demux, "seek event, %" GST_PTR_FORMAT, event);

  /* have a backup in case seek fails */
  gst_segment_copy_into (&demux->segment, &oldsegment);

  GST_DEBUG_OBJECT (demux, "sending flush start");
  flush_event = gst_event_new_flush_start ();
  gst_event_set_seqnum (flush_event, seqnum);

  gst_adaptive_demux_push_src_event (demux, flush_event);

  gst_adaptive_demux_stop_tasks (demux, FALSE);
  gst_adaptive_demux_reset_tracks (demux);

  GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);

  if (!IS_SNAP_SEEK (flags) && !(flags & GST_SEEK_FLAG_ACCURATE)) {
    /* If no accurate seeking was specified, we want to default to seeking to
     * the previous segment for efficient/fast playback. */
    flags |= GST_SEEK_FLAG_KEY_UNIT;
  }

  if (IS_SNAP_SEEK (flags)) {
    GstAdaptiveDemux2Stream *default_stream = NULL;
    GstAdaptiveDemux2Stream *stream = NULL;
    GList *iter;
    /*
     * Handle snap seeks as follows:
     * 1) do the snap seeking a (random) active stream
     * 1.1) If none are active yet (early-seek), pick a random default one
     * 2) use the final position on this stream to seek
     *    on the other streams to the same position
     *
     * We can't snap at all streams at the same time as they might end in
     * different positions, so just pick one and align all others to that
     * position.
     */

    /* Pick a random active stream on which to do the stream seek */
    for (iter = demux->output_period->streams; iter; iter = iter->next) {
      GstAdaptiveDemux2Stream *cand = iter->data;
      if (gst_adaptive_demux2_stream_is_selected_locked (cand)) {
        stream = cand;
        break;
      }
      if (default_stream == NULL
          && gst_adaptive_demux2_stream_is_default_locked (cand))
        default_stream = cand;
    }

    if (stream == NULL)
      stream = default_stream;

    if (stream) {
      GstClockTimeDiff ts;
      GstSeekFlags stream_seek_flags = flags;

      /* snap-seek on the chosen stream and then
       * use the resulting position to seek on all streams */
      if (rate >= 0) {
        if (start_type != GST_SEEK_TYPE_NONE)
          ts = start;
        else {
          ts = gst_segment_position_from_running_time (&demux->segment,
              GST_FORMAT_TIME, demux->priv->global_output_position);
          start_type = GST_SEEK_TYPE_SET;
        }
      } else {
        if (stop_type != GST_SEEK_TYPE_NONE)
          ts = stop;
        else {
          stop_type = GST_SEEK_TYPE_SET;
          ts = gst_segment_position_from_running_time (&demux->segment,
              GST_FORMAT_TIME, demux->priv->global_output_position);
        }
      }

      GstFlowReturn flow_ret =
          gst_adaptive_demux2_stream_seek (stream, rate >= 0, stream_seek_flags,
          ts, &ts);

      /* Handle fragment info waiting on BUSY */
      while (flow_ret == GST_ADAPTIVE_DEMUX_FLOW_BUSY) {
        if (!gst_adaptive_demux2_stream_wait_prepared (stream))
          break;
        flow_ret = gst_adaptive_demux2_stream_update_fragment_info (stream);
      }

      if (flow_ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (demux,
            "Seek on stream %" GST_PTR_FORMAT " failed with flow return %s",
            stream, gst_flow_get_name (flow_ret));
        GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);
        goto unlock_return;
      }
      /* replace event with a new one without snapping to seek on all streams */
      gst_event_unref (event);
      if (rate >= 0) {
        start = ts;
      } else {
        stop = ts;
      }
      event =
          gst_event_new_seek (rate, format, REMOVE_SNAP_FLAGS (flags),
          start_type, start, stop_type, stop);
      GST_DEBUG_OBJECT (demux, "Adapted snap seek to %" GST_PTR_FORMAT, event);
    }
  }

  ret = gst_segment_do_seek (&demux->segment, rate, format, flags, start_type,
      start, stop_type, stop, &update);

  if (ret) {
    GST_DEBUG_OBJECT (demux, "Calling subclass seek: %" GST_PTR_FORMAT, event);

    ret = demux_class->seek (demux, event);
  }

  if (!ret) {
    /* Is there anything else we can do if it fails? */
    gst_segment_copy_into (&oldsegment, &demux->segment);
  } else {
    demux->priv->segment_seqnum = seqnum;
  }
  GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

  /* Resetting flow combiner */
  gst_flow_combiner_reset (demux->priv->flowcombiner);

  GST_DEBUG_OBJECT (demux, "Sending flush stop on all pad");
  flush_event = gst_event_new_flush_stop (TRUE);
  gst_event_set_seqnum (flush_event, seqnum);
  gst_adaptive_demux_push_src_event (demux, flush_event);

  /* If the seek generated a new period, prepare it */
  if (!demux->input_period->prepared) {
    /* This can only happen on flushing seeks */
    g_assert (flags & GST_SEEK_FLAG_FLUSH);
    gst_adaptive_demux_seek_to_input_period (demux);
  }

  GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
  GST_DEBUG_OBJECT (demux, "Demuxer segment after seek: %" GST_SEGMENT_FORMAT,
      &demux->segment);
  gst_adaptive_demux_setup_streams_for_restart (demux, start_type, stop_type);
  demux->priv->qos_earliest_time = GST_CLOCK_TIME_NONE;

  /* Reset the global output position (running time) for when the output loop restarts */
  demux->priv->global_output_position = 0;

  /* After a flushing seek, any instant-rate override is undone */
  demux->instant_rate_multiplier = 1.0;

  GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

  /* Restart the demux */
  gst_adaptive_demux_set_streams_can_download_fragments (demux, TRUE);
  gst_adaptive_demux_start_tasks (demux);

unlock_return:
  GST_ADAPTIVE_SCHEDULER_UNLOCK (demux);
  gst_event_unref (event);

  return ret;
}

static gboolean
handle_stream_selection (GstAdaptiveDemux * demux, GList * streams,
    guint32 seqnum)
{
  gboolean selection_handled = TRUE;
  GList *iter;
  GList *tracks = NULL;

  if (!GST_ADAPTIVE_SCHEDULER_LOCK (demux))
    return FALSE;

  TRACKS_LOCK (demux);
  /* We can't do stream selection if we are migrating between periods */
  if (demux->input_period && demux->output_period != demux->input_period) {
    GST_WARNING_OBJECT (demux,
        "Stream selection while migrating between periods is not possible");
    TRACKS_UNLOCK (demux);
    return FALSE;
  }
  /* Validate the streams and fill:
   * tracks : list of tracks corresponding to requested streams
   */
  for (iter = streams; iter; iter = iter->next) {
    gchar *stream_id = (gchar *) iter->data;
    GstAdaptiveDemuxTrack *track;

    GST_DEBUG_OBJECT (demux, "Stream requested : %s", stream_id);
    track = find_track_for_stream_id (demux->output_period, stream_id);
    if (!track) {
      GST_WARNING_OBJECT (demux, "Unrecognized stream_id '%s'", stream_id);
      selection_handled = FALSE;
      goto select_streams_done;
    }
    tracks = g_list_append (tracks, track);
    GST_DEBUG_OBJECT (demux, "Track found, selected:%d", track->selected);
  }

  /* FIXME : ACTIVATING AND DEACTIVATING STREAMS SHOULD BE DONE FROM THE
   * SCHEDULING THREAD */

  /* FIXME: We want to iterate all streams, mark them as deselected,
   * then iterate tracks and mark any streams that have at least 1
   * active output track, then loop over all streams again and start/stop
   * them as needed */

  /* Go over all tracks present and (de)select based on current selection */
  for (iter = demux->output_period->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;

    if (track->selected && !g_list_find (tracks, track)) {
      GST_DEBUG_OBJECT (demux, "De-select track '%s' (active:%d)",
          track->stream_id, track->active);
      track->selected = FALSE;
      track->draining = TRUE;
    } else if (!track->selected && g_list_find (tracks, track)) {
      GST_DEBUG_OBJECT (demux, "Selecting track '%s'", track->stream_id);

      track->selected = TRUE;
    }
  }

  /* Start or stop streams based on the updated track selection */
  for (iter = demux->output_period->streams; iter; iter = iter->next) {
    GstAdaptiveDemux2Stream *stream = iter->data;
    GList *trackiter;

    gboolean is_running = gst_adaptive_demux2_stream_is_running (stream);
    gboolean should_be_running =
        gst_adaptive_demux2_stream_is_selected_locked (stream);

    if (!is_running && should_be_running) {
      GstClockTime output_running_ts = demux->priv->global_output_position;
      GstClockTime start_position;

      /* Calculate where we should start the stream, and then
       * start it. */
      GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);

      GST_DEBUG_OBJECT (stream, "(Re)starting stream. Output running ts %"
          GST_TIME_FORMAT " in demux segment %" GST_SEGMENT_FORMAT,
          GST_TIME_ARGS (output_running_ts), &demux->segment);

      start_position =
          gst_segment_position_from_running_time (&demux->segment,
          GST_FORMAT_TIME, output_running_ts);

      GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

      GST_DEBUG_OBJECT (demux, "Setting stream start position to %"
          GST_TIME_FORMAT, GST_TIME_ARGS (start_position));

      stream->current_position = stream->start_position = start_position;
      stream->compute_segment = TRUE;

      /* If output has already begun, ensure we seek this segment
       * to the correct restart position when the download loop begins */
      if (output_running_ts != 0)
        stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART;

      /* Activate track pads for this stream */
      for (trackiter = stream->tracks; trackiter; trackiter = trackiter->next) {
        GstAdaptiveDemuxTrack *track =
            (GstAdaptiveDemuxTrack *) trackiter->data;
        gst_pad_set_active (track->sinkpad, TRUE);
      }

      gst_adaptive_demux2_stream_start (stream);
    } else if (is_running && !should_be_running) {
      /* Stream should not be running and needs stopping */
      gst_adaptive_demux2_stream_stop (stream);

      /* Set all track sinkpads to inactive for this stream */
      for (trackiter = stream->tracks; trackiter; trackiter = trackiter->next) {
        GstAdaptiveDemuxTrack *track =
            (GstAdaptiveDemuxTrack *) trackiter->data;
        gst_pad_set_active (track->sinkpad, FALSE);
      }
    }
  }

  g_atomic_int_set (&demux->priv->requested_selection_seqnum, seqnum);

select_streams_done:
  demux_update_buffering_locked (demux);
  demux_post_buffering_locked (demux);

  TRACKS_UNLOCK (demux);
  GST_ADAPTIVE_SCHEDULER_UNLOCK (demux);

  if (tracks)
    g_list_free (tracks);
  return selection_handled;
}

static gboolean
gst_adaptive_demux_handle_select_streams_event (GstAdaptiveDemux * demux,
    GstEvent * event)
{
  GList *streams;
  gboolean selection_handled;

  if (GST_EVENT_SEQNUM (event) ==
      g_atomic_int_get (&demux->priv->requested_selection_seqnum)) {
    GST_DEBUG_OBJECT (demux, "Already handled/handling select-streams %d",
        GST_EVENT_SEQNUM (event));
    return TRUE;
  }

  gst_event_parse_select_streams (event, &streams);
  selection_handled =
      handle_stream_selection (demux, streams, GST_EVENT_SEQNUM (event));
  g_list_free_full (streams, g_free);

  gst_event_unref (event);
  return selection_handled;
}

static gboolean
gst_adaptive_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAdaptiveDemux *demux;

  demux = GST_ADAPTIVE_DEMUX_CAST (parent);

  switch (event->type) {
    case GST_EVENT_SEEK:
    {
      guint32 seqnum = gst_event_get_seqnum (event);
      if (seqnum == demux->priv->segment_seqnum) {
        GST_LOG_OBJECT (pad,
            "Drop duplicated SEEK event seqnum %" G_GUINT32_FORMAT, seqnum);
        gst_event_unref (event);
        return TRUE;
      }
      return gst_adaptive_demux_handle_seek_event (demux, event);
    }
    case GST_EVENT_LATENCY:{
      /* Upstream and our internal source are irrelevant
       * for latency, and we should not fail here to
       * configure the latency */
      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      GstClockTime earliest_time;

      gst_event_parse_qos (event, NULL, NULL, &diff, &timestamp);
      /* Only take into account lateness if late */
      if (diff > 0)
        earliest_time = timestamp + 2 * diff;
      else
        earliest_time = timestamp;

      GST_OBJECT_LOCK (demux);
      if (!GST_CLOCK_TIME_IS_VALID (demux->priv->qos_earliest_time) ||
          earliest_time > demux->priv->qos_earliest_time) {
        demux->priv->qos_earliest_time = earliest_time;
        GST_DEBUG_OBJECT (demux, "qos_earliest_time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->priv->qos_earliest_time));
      }
      GST_OBJECT_UNLOCK (demux);
      break;
    }
    case GST_EVENT_SELECT_STREAMS:
    {
      return gst_adaptive_demux_handle_select_streams_event (demux, event);
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_adaptive_demux_handle_query_seeking (GstAdaptiveDemux * demux,
    GstQuery * query)
{
  GstFormat fmt = GST_FORMAT_UNDEFINED;
  gint64 stop = -1;
  gint64 start = 0;
  gboolean ret = FALSE;

  if (!g_atomic_int_get (&demux->priv->have_manifest)) {
    GST_INFO_OBJECT (demux,
        "Don't have manifest yet, can't answer seeking query");
    return FALSE;               /* can't answer without manifest */
  }

  GST_MANIFEST_LOCK (demux);

  gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
  GST_INFO_OBJECT (demux, "Received GST_QUERY_SEEKING with format %d", fmt);
  if (fmt == GST_FORMAT_TIME) {
    GstClockTime duration;
    gboolean can_seek = gst_adaptive_demux_can_seek (demux);

    ret = TRUE;
    if (can_seek) {
      if (gst_adaptive_demux_is_live (demux)) {
        ret = gst_adaptive_demux_get_live_seek_range (demux, &start, &stop);

        if (!ret) {
          GST_MANIFEST_UNLOCK (demux);
          GST_INFO_OBJECT (demux, "can't answer seeking query");
          return FALSE;
        }
      } else {
        duration = demux->priv->duration;
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
          stop = duration;
      }
    }
    gst_query_set_seeking (query, fmt, can_seek, start, stop);
    GST_INFO_OBJECT (demux, "GST_QUERY_SEEKING returning with start : %"
        GST_TIME_FORMAT ", stop : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
  }
  GST_MANIFEST_UNLOCK (demux);
  return ret;
}

static gboolean
gst_adaptive_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (parent);
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  switch (query->type) {
    case GST_QUERY_DURATION:{
      GstFormat fmt;
      GstClockTime duration = GST_CLOCK_TIME_NONE;

      gst_query_parse_duration (query, &fmt, NULL);

      if (gst_adaptive_demux_is_live (demux)) {
        /* We are able to answer this query: the duration is unknown */
        gst_query_set_duration (query, fmt, -1);
        ret = TRUE;
        break;
      }

      if (fmt == GST_FORMAT_TIME
          && g_atomic_int_get (&demux->priv->have_manifest)) {

        GST_MANIFEST_LOCK (demux);
        duration = demux->priv->duration;
        GST_MANIFEST_UNLOCK (demux);

        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          ret = TRUE;
        }
      }

      GST_LOG_OBJECT (demux, "GST_QUERY_DURATION returns %s with duration %"
          GST_TIME_FORMAT, ret ? "TRUE" : "FALSE", GST_TIME_ARGS (duration));
      break;
    }
    case GST_QUERY_LATENCY:{
      gst_query_set_latency (query, FALSE, 0, -1);
      ret = TRUE;
      break;
    }
    case GST_QUERY_SEEKING:
      ret = gst_adaptive_demux_handle_query_seeking (demux, query);
      break;
    case GST_QUERY_URI:

      GST_MANIFEST_LOCK (demux);

      /* TODO HLS can answer this differently it seems */
      if (demux->manifest_uri) {
        /* FIXME: (hls) Do we answer with the variant playlist, with the current
         * playlist or the the uri of the last downlowaded fragment? */
        gst_query_set_uri (query, demux->manifest_uri);
        ret = TRUE;
      }

      GST_MANIFEST_UNLOCK (demux);
      break;
    case GST_QUERY_SELECTABLE:
      gst_query_set_selectable (query, TRUE);
      ret = TRUE;
      break;
    default:
      /* Don't forward queries upstream because of the special nature of this
       *  "demuxer", which relies on the upstream element only to be fed
       *  the Manifest
       */
      break;
  }

  return ret;
}

static gboolean
gst_adaptive_demux_query (GstElement * element, GstQuery * query)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX_CAST (element);

  GST_LOG_OBJECT (demux, "%" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_BUFFERING:
    {
      GstFormat format;
      gst_query_parse_buffering_range (query, &format, NULL, NULL, NULL);

      if (!demux->output_period) {
        if (format != GST_FORMAT_TIME) {
          GST_DEBUG_OBJECT (demux,
              "No period setup yet, can't answer non-TIME buffering queries");
          return FALSE;
        }

        GST_DEBUG_OBJECT (demux,
            "No period setup yet, but still answering buffering query");
        return TRUE;
      }
    }
    case GST_QUERY_SEEKING:
    {
      /* Source pads might not be present early on which would cause the default
       * element query handler to fail, yet we can answer this query */
      return gst_adaptive_demux_handle_query_seeking (demux, query);
    }
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->query (element, query);
}

gboolean
gst_adaptive_demux_handle_lost_sync (GstAdaptiveDemux * demux)
{
  GstEvent *seek;

  GST_WARNING_OBJECT (demux, "Lost synchronization, seeking back to live head");

  seek =
      gst_event_new_seek (1.0, GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, GST_SEEK_TYPE_END, 0,
      GST_SEEK_TYPE_NONE, 0);
  gst_adaptive_demux_handle_seek_event (demux, seek);
  return FALSE;
}


/* Called when the scheduler starts, to kick off manifest updates
 * and stream downloads */
static gboolean
gst_adaptive_demux_scheduler_start_cb (GstAdaptiveDemux * demux)
{
  GList *iter;

  GST_INFO_OBJECT (demux, "Starting streams' tasks");

  iter = demux->input_period->streams;

  for (; iter; iter = g_list_next (iter)) {
    GstAdaptiveDemux2Stream *stream = iter->data;

    /* If we need to process this stream to discover tracks *OR* it has any
     * tracks which are selected, start it now */
    if ((stream->pending_tracks == TRUE)
        || gst_adaptive_demux2_stream_is_selected_locked (stream))
      gst_adaptive_demux2_stream_start (stream);
  }

  return FALSE;
}

/* must be called with the scheduler lock */
static void
gst_adaptive_demux_start_tasks (GstAdaptiveDemux * demux)
{
  if (!gst_adaptive_demux2_is_running (demux)) {
    GST_DEBUG_OBJECT (demux, "Not starting tasks due to shutdown");
    return;
  }

  GST_DEBUG_OBJECT (demux, "Starting the SCHEDULER task");
  gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux_scheduler_start_cb, demux, NULL);

  TRACKS_LOCK (demux);
  demux->priv->flushing = FALSE;
  GST_DEBUG_OBJECT (demux, "Starting the output task");
  gst_task_start (demux->priv->output_task);
  TRACKS_UNLOCK (demux);
}

/* must be called with manifest_lock taken */
static void
gst_adaptive_demux_stop_manifest_update_task (GstAdaptiveDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "requesting stop of the manifest update task");
  demux->priv->manifest_updates_enabled = FALSE;
  if (demux->priv->manifest_updates_cb != 0) {
    gst_adaptive_demux_loop_cancel_call (demux->priv->scheduler_task,
        demux->priv->manifest_updates_cb);
    demux->priv->manifest_updates_cb = 0;
  }
}

static gboolean gst_adaptive_demux_updates_start_cb (GstAdaptiveDemux * demux);

/* must be called with manifest_lock taken */
static void
gst_adaptive_demux_start_manifest_update_task (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *demux_class = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  demux->priv->manifest_updates_enabled = TRUE;

  if (demux->priv->need_manual_manifest_update) {
    gst_adaptive_demux2_manual_manifest_update (demux);
    demux->priv->need_manual_manifest_update = FALSE;
  }

  if (gst_adaptive_demux_is_live (demux)) {
    /* Task to periodically update the manifest */
    if (demux_class->requires_periodical_playlist_update (demux)) {
      GST_DEBUG_OBJECT (demux, "requesting start of the manifest update task");
      if (demux->priv->manifest_updates_cb == 0) {
        demux->priv->manifest_updates_cb =
            gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
            (GSourceFunc) gst_adaptive_demux_updates_start_cb, demux, NULL);
      }
    }
  }
}

/* must be called with manifest_lock taken
 * This function will temporarily release manifest_lock in order to join the
 * download threads.
 * The api_lock will still protect it against other threads trying to modify
 * the demux element.
 */
static void
gst_adaptive_demux_stop_tasks (GstAdaptiveDemux * demux, gboolean stop_updates)
{
  GST_LOG_OBJECT (demux, "Stopping tasks");

  if (stop_updates)
    gst_adaptive_demux_stop_manifest_update_task (demux);

  TRACKS_LOCK (demux);
  if (demux->input_period)
    gst_adaptive_demux_period_stop_tasks (demux->input_period);

  demux->priv->flushing = TRUE;
  g_cond_signal (&demux->priv->tracks_add);
  gst_task_stop (demux->priv->output_task);
  TRACKS_UNLOCK (demux);

  gst_task_join (demux->priv->output_task);

  demux->priv->qos_earliest_time = GST_CLOCK_TIME_NONE;
}

/* must be called with manifest_lock taken */
static gboolean
gst_adaptive_demux_push_src_event (GstAdaptiveDemux * demux, GstEvent * event)
{
  GList *iter;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (demux, "event %" GST_PTR_FORMAT, event);

  TRACKS_LOCK (demux);
  for (iter = demux->priv->outputs; iter; iter = g_list_next (iter)) {
    OutputSlot *slot = (OutputSlot *) iter->data;
    gst_event_ref (event);
    GST_DEBUG_OBJECT (slot->pad, "Pushing event");
    ret = ret & gst_pad_push_event (slot->pad, event);
    if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP)
      slot->pushed_timed_data = FALSE;
  }
  TRACKS_UNLOCK (demux);
  gst_event_unref (event);
  return ret;
}

/* must be called with manifest_lock taken */
void
gst_adaptive_demux2_stream_set_caps (GstAdaptiveDemux2Stream * stream,
    GstCaps * caps)
{
  GST_DEBUG_OBJECT (stream,
      "setting new caps for stream %" GST_PTR_FORMAT, caps);
  gst_caps_replace (&stream->pending_caps, caps);
  gst_caps_unref (caps);
}

/* must be called with manifest_lock taken */
/* @tags: transfer full */
void
gst_adaptive_demux2_stream_set_tags (GstAdaptiveDemux2Stream * stream,
    GstTagList * tags)
{
  GST_DEBUG_OBJECT (stream,
      "setting new tags for stream %" GST_PTR_FORMAT, tags);
  gst_clear_tag_list (&stream->pending_tags);
  stream->pending_tags = tags;
}

/* must be called with manifest_lock taken */
void
gst_adaptive_demux2_stream_queue_event (GstAdaptiveDemux2Stream * stream,
    GstEvent * event)
{
  stream->pending_events = g_list_append (stream->pending_events, event);
}

static gboolean
gst_adaptive_demux_requires_periodical_playlist_update_default (GstAdaptiveDemux
    * demux)
{
  return TRUE;
}

/* Called when a stream needs waking after the manifest is updated */
void
gst_adaptive_demux2_stream_wants_manifest_update (GstAdaptiveDemux * demux)
{
  demux->priv->stream_waiting_for_manifest = TRUE;
}

static gboolean
gst_adaptive_demux_manifest_update_cb (GstAdaptiveDemux * demux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean schedule_again = TRUE;

  GST_MANIFEST_LOCK (demux);
  demux->priv->manifest_updates_cb = 0;

  /* Updating playlist only needed for live playlists */
  if (!gst_adaptive_demux_is_live (demux)) {
    GST_MANIFEST_UNLOCK (demux);
    return G_SOURCE_REMOVE;
  }

  GST_DEBUG_OBJECT (demux, "Updating playlist");
  ret = gst_adaptive_demux_update_manifest (demux);

  if (ret == GST_FLOW_EOS) {
    GST_MANIFEST_UNLOCK (demux);
    return G_SOURCE_REMOVE;
  }

  if (ret == GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Updated playlist successfully");
    demux->priv->update_failed_count = 0;

    /* Wake up download tasks */
    if (demux->priv->stream_waiting_for_manifest) {
      GList *iter;

      for (iter = demux->input_period->streams; iter; iter = g_list_next (iter)) {
        GstAdaptiveDemux2Stream *stream = iter->data;
        gst_adaptive_demux2_stream_on_manifest_update (stream);
      }
      demux->priv->stream_waiting_for_manifest = FALSE;
    }
  } else if (ret == GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC) {
    schedule_again = FALSE;
    gst_adaptive_demux_handle_lost_sync (demux);
  } else if (ret == GST_ADAPTIVE_DEMUX_FLOW_BUSY) {
    /* This is not an error, we'll just try again later */
    GST_LOG_OBJECT (demux, "Manifest update returned BUSY / ongoing");
  } else {
    demux->priv->update_failed_count++;

    if (demux->priv->update_failed_count <= DEFAULT_FAILED_COUNT) {
      GST_WARNING_OBJECT (demux, "Could not update the playlist, flow: %s",
          gst_flow_get_name (ret));
    } else {
      GST_ELEMENT_ERROR (demux, STREAM, FAILED,
          (_("Internal data stream error.")), ("Could not update playlist"));
      GST_DEBUG_OBJECT (demux, "Stopped manifest updates because of error");
      schedule_again = FALSE;
    }
  }

  if (schedule_again) {
    GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

    demux->priv->manifest_updates_cb =
        gst_adaptive_demux_loop_call_delayed (demux->priv->scheduler_task,
        klass->get_manifest_update_interval (demux) * GST_USECOND,
        (GSourceFunc) gst_adaptive_demux_manifest_update_cb, demux, NULL);
  }

  GST_MANIFEST_UNLOCK (demux);

  return G_SOURCE_REMOVE;
}

static gboolean
gst_adaptive_demux_updates_start_cb (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  /* Loop for updating of the playlist. This periodically checks if
   * the playlist is updated and does so, then signals the streaming
   * thread in case it can continue downloading now. */

  /* block until the next scheduled update or the signal to quit this thread */
  GST_DEBUG_OBJECT (demux, "Started updates task");
  demux->priv->manifest_updates_cb =
      gst_adaptive_demux_loop_call_delayed (demux->priv->scheduler_task,
      klass->get_manifest_update_interval (demux) * GST_USECOND,
      (GSourceFunc) gst_adaptive_demux_manifest_update_cb, demux, NULL);

  return G_SOURCE_REMOVE;
}

static OutputSlot *
find_replacement_slot_for_track (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxTrack * track)
{
  GList *tmp;

  for (tmp = demux->priv->outputs; tmp; tmp = tmp->next) {
    OutputSlot *slot = (OutputSlot *) tmp->data;
    /* Incompatible output type */
    if (slot->type != track->type)
      continue;

    /* Slot which is already assigned to this pending track */
    if (slot->pending_track == track)
      return slot;

    /* slot already used for another pending track */
    if (slot->pending_track != NULL)
      continue;

    /* Current output track is of the same type and is draining */
    if (slot->track && slot->track->draining)
      return slot;
  }

  return NULL;
}

/* TRACKS_LOCK taken */
static OutputSlot *
find_slot_for_track (GstAdaptiveDemux * demux, GstAdaptiveDemuxTrack * track)
{
  GList *tmp;

  for (tmp = demux->priv->outputs; tmp; tmp = tmp->next) {
    OutputSlot *slot = (OutputSlot *) tmp->data;

    if (slot->track == track)
      return slot;
  }

  return NULL;
}

/* TRACKS_LOCK held */
static GstMessage *
all_selected_tracks_are_active (GstAdaptiveDemux * demux, guint32 seqnum)
{
  GList *tmp;
  GstMessage *msg;

  for (tmp = demux->output_period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;

    if (track->selected && !track->active)
      return NULL;
  }

  /* All selected tracks are active, created message */
  msg =
      gst_message_new_streams_selected (GST_OBJECT (demux),
      demux->output_period->collection);
  GST_MESSAGE_SEQNUM (msg) = seqnum;
  for (tmp = demux->output_period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;
    if (track->active) {
      gst_message_streams_selected_add (msg, track->stream_object);
    }
  }

  return msg;
}

static void
gst_adaptive_demux_send_initial_events (GstAdaptiveDemux * demux,
    OutputSlot * slot)
{
  GstAdaptiveDemuxTrack *track = slot->track;
  GstEvent *event;

  /* Send EVENT_STREAM_START */
  event = gst_event_new_stream_start (track->stream_id);
  if (demux->have_group_id)
    gst_event_set_group_id (event, demux->group_id);
  gst_event_set_stream_flags (event, track->flags);
  gst_event_set_stream (event, track->stream_object);
  GST_DEBUG_OBJECT (demux, "Sending stream-start for track '%s'",
      track->stream_id);
  gst_pad_push_event (slot->pad, event);

  /* Send EVENT_STREAM_COLLECTION */
  event = gst_event_new_stream_collection (demux->output_period->collection);
  GST_DEBUG_OBJECT (demux, "Sending stream-collection for track '%s'",
      track->stream_id);
  gst_pad_push_event (slot->pad, event);

  /* Mark all sticky events for re-sending */
  gst_event_store_mark_all_undelivered (&track->sticky_events);
}

/*
 * Called with TRACKS_LOCK taken
 */
static void
check_and_handle_selection_update_locked (GstAdaptiveDemux * demux)
{
  GList *tmp;
  guint requested_selection_seqnum;
  GstMessage *msg;

  /* If requested_selection_seqnum != current_selection_seqnum, re-check all
     output slots vs active/draining tracks */
  requested_selection_seqnum =
      g_atomic_int_get (&demux->priv->requested_selection_seqnum);

  if (requested_selection_seqnum == demux->priv->current_selection_seqnum)
    return;

  GST_DEBUG_OBJECT (demux, "Selection changed, re-checking all output slots");

  /* Go over all slots, and if they have a pending track that's no longer
   * selected, clear it so the slot can be reused */
  for (tmp = demux->priv->outputs; tmp; tmp = tmp->next) {
    OutputSlot *slot = (OutputSlot *) tmp->data;

    if (slot->pending_track != NULL && !slot->pending_track->selected) {
      GST_DEBUG_OBJECT (demux,
          "Removing deselected track '%s' as pending from output of current track '%s'",
          slot->pending_track->id, slot->track->id);
      gst_adaptive_demux_track_unref (slot->pending_track);
      slot->pending_track = NULL;
    }
  }

  /* Go over all tracks and create/re-assign/remove slots */
  for (tmp = demux->output_period->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;

    if (track->selected) {
      OutputSlot *slot = find_slot_for_track (demux, track);

      /* 0. Track is selected and has a slot. Nothing to do */
      if (slot) {
        GST_DEBUG_OBJECT (demux, "Track '%s' is already being outputted",
            track->id);
        continue;
      }

      slot = find_replacement_slot_for_track (demux, track);
      if (slot) {
        /* 1. There is an existing slot of the same type which is currently
         *    draining, assign this track as a replacement for it */
        g_assert (slot->pending_track == NULL || slot->pending_track == track);
        if (slot->pending_track == NULL) {
          slot->pending_track = gst_adaptive_demux_track_ref (track);
          GST_DEBUG_ID (track->id,
              "Track will be used on output of track '%s' (period %u)",
              slot->track->id, slot->track->period_num);
        }
      } else {
        /* 2. There is no compatible replacement slot, create a new one */
        slot = gst_adaptive_demux_output_slot_new (demux, track->type);
        GST_DEBUG_OBJECT (demux, "Created slot for track '%s'", track->id);
        demux->priv->outputs = g_list_append (demux->priv->outputs, slot);

        track->update_next_segment = TRUE;

        slot->track = gst_adaptive_demux_track_ref (track);
        track->active = TRUE;
        gst_adaptive_demux_send_initial_events (demux, slot);
      }

      /* If we were draining this track, we no longer are */
      track->draining = FALSE;
    }
  }

  /* Finally check all slots have a current/pending track. If not remove it */
  for (tmp = demux->priv->outputs; tmp;) {
    OutputSlot *slot = (OutputSlot *) tmp->data;
    /* We should never has slots without target tracks */
    g_assert (slot->track);
    if (slot->track->draining && !slot->pending_track) {
      GstAdaptiveDemux2Stream *stream;

      GST_DEBUG_OBJECT (demux, "Output for track '%s' is no longer used",
          slot->track->id);
      slot->track->active = FALSE;

      /* If the stream feeding this track is stopped, flush and clear
       * the track now that it's going inactive. If the stream was not
       * found, it means we advanced past that period already (and the
       * stream was stopped and discarded) */
      stream = find_stream_for_track_locked (demux, slot->track);
      if (stream != NULL && !gst_adaptive_demux2_stream_is_running (stream))
        gst_adaptive_demux_track_flush (slot->track);

      tmp = demux->priv->outputs = g_list_remove (demux->priv->outputs, slot);
      gst_adaptive_demux_output_slot_free (demux, slot);
    } else
      tmp = tmp->next;
  }

  demux->priv->current_selection_seqnum = requested_selection_seqnum;
  msg = all_selected_tracks_are_active (demux, requested_selection_seqnum);
  if (msg) {
    TRACKS_UNLOCK (demux);
    GST_DEBUG_OBJECT (demux, "Posting streams-selected");
    gst_element_post_message (GST_ELEMENT_CAST (demux), msg);
    TRACKS_LOCK (demux);
  }
}

/* TRACKS_LOCK held */
static gboolean
gst_adaptive_demux_advance_output_period (GstAdaptiveDemux * demux)
{
  GList *iter;
  GstAdaptiveDemuxPeriod *previous_period;
  GstStreamCollection *collection;

  /* Grab the next period, should be demux->periods->next->data */
  previous_period = g_queue_pop_head (demux->priv->periods);

  /* Remove ref held by demux->output_period */
  gst_adaptive_demux_period_unref (previous_period);
  demux->output_period =
      gst_adaptive_demux_period_ref (g_queue_peek_head (demux->priv->periods));

  GST_DEBUG_OBJECT (demux, "Moved output to period %d",
      demux->output_period->period_num);

  /* We can now post the collection of the new period */
  collection = demux->output_period->collection;
  TRACKS_UNLOCK (demux);
  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_stream_collection (GST_OBJECT (demux), collection));
  TRACKS_LOCK (demux);

  /* Unselect all tracks of the previous period */
  for (iter = previous_period->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = iter->data;
    if (track->selected) {
      track->selected = FALSE;
      track->draining = TRUE;
    }
  }

  /* Force a selection re-check */
  g_atomic_int_inc (&demux->priv->requested_selection_seqnum);
  check_and_handle_selection_update_locked (demux);

  /* Remove the final ref on the previous period now that we have done the switch */
  gst_adaptive_demux_period_unref (previous_period);

  return TRUE;
}

/* Called with TRACKS_LOCK taken */
static void
handle_slot_pending_track_switch_locked (GstAdaptiveDemux * demux,
    OutputSlot * slot)
{
  GstAdaptiveDemuxTrack *track = slot->track;
  GstMessage *msg;
  gboolean pending_is_ready;
  GstAdaptiveDemux2Stream *stream;

  /* If we have a pending track for this slot, the current track should be
   * draining and no longer selected */
  g_assert (track->draining && !track->selected);

  /* If we're draining, check if the pending track has enough data *or* that
     we've already drained out entirely */
  pending_is_ready =
      (slot->pending_track->level_time >=
      slot->pending_track->buffering_threshold);
  pending_is_ready |= slot->pending_track->eos;

  if (!pending_is_ready && gst_queue_array_get_length (track->queue) > 0) {
    GST_DEBUG_OBJECT (demux,
        "Replacement track '%s' doesn't have enough data for switching yet",
        slot->pending_track->id);
    return;
  }

  GST_DEBUG_OBJECT (demux,
      "Pending replacement track has enough data, switching");
  track->active = FALSE;
  track->draining = FALSE;

  /* If the stream feeding this track is stopped, flush and clear
   * the track now that it's going inactive. If the stream was not
   * found, it means we advanced past that period already (and the
   * stream was stopped and discarded) */
  stream = find_stream_for_track_locked (demux, track);
  if (stream != NULL && !gst_adaptive_demux2_stream_is_running (stream))
    gst_adaptive_demux_track_flush (track);

  gst_adaptive_demux_track_unref (track);
  /* We steal the reference of pending_track */
  track = slot->track = slot->pending_track;
  slot->pending_track = NULL;
  slot->track->active = TRUE;

  /* Make sure the track segment will start at the current position */
  track->update_next_segment = TRUE;

  /* Send stream start and collection, and schedule sticky events */
  gst_adaptive_demux_send_initial_events (demux, slot);

  /* Can we emit the streams-selected message now ? */
  msg =
      all_selected_tracks_are_active (demux,
      g_atomic_int_get (&demux->priv->requested_selection_seqnum));
  if (msg) {
    TRACKS_UNLOCK (demux);
    GST_DEBUG_OBJECT (demux, "Posting streams-selected");
    gst_element_post_message (GST_ELEMENT_CAST (demux), msg);
    TRACKS_LOCK (demux);
  }

}

static void
gst_adaptive_demux_output_loop (GstAdaptiveDemux * demux)
{
  GList *tmp;
  GstClockTimeDiff global_output_position = GST_CLOCK_STIME_NONE;
  gboolean wait_for_data = FALSE;
  gboolean all_tracks_empty;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (demux, "enter");

  TRACKS_LOCK (demux);

  /* Check if stopping */
  if (demux->priv->flushing) {
    ret = GST_FLOW_FLUSHING;
    goto pause;
  }

  /* If the selection changed, handle it */
  check_and_handle_selection_update_locked (demux);

restart:
  ret = GST_FLOW_OK;
  global_output_position = GST_CLOCK_STIME_NONE;
  all_tracks_empty = TRUE;

  if (wait_for_data) {
    GST_DEBUG_OBJECT (demux, "Waiting for data");
    g_cond_wait (&demux->priv->tracks_add, &demux->priv->tracks_lock);
    GST_DEBUG_OBJECT (demux, "Done waiting for data");
    if (demux->priv->flushing) {
      ret = GST_FLOW_FLUSHING;
      goto pause;
    }
    wait_for_data = FALSE;
  }

  /* Grab/Recalculate current global output position
   * This is the minimum pending output position of all tracks used for output
   *
   * If there is a track which is empty and not EOS, wait for it to receive data
   * then recalculate global output position.
   *
   * This also pushes downstream all non-timed data that might be present.
   *
   * IF all tracks are EOS : stop task
   */
  GST_LOG_OBJECT (demux, "Calculating global output position of output slots");
  for (tmp = demux->priv->outputs; tmp; tmp = tmp->next) {
    OutputSlot *slot = (OutputSlot *) tmp->data;
    GstAdaptiveDemuxTrack *track;

    /* If there is a pending track, Check if it's time to switch to it */
    if (slot->pending_track)
      handle_slot_pending_track_switch_locked (demux, slot);

    track = slot->track;

    if (!track->active) {
      /* Note: Edward: I can't see in what cases we would end up with inactive
         tracks assigned to slots. */
      GST_ERROR_OBJECT (demux, "FIXME : Handle track switching");
      g_assert (track->active);
      continue;
    }

    if (track->next_position == GST_CLOCK_STIME_NONE) {
      gst_adaptive_demux_track_update_next_position (track);
    }

    GST_TRACE_ID (track->id,
        "Looking at track, next_position %" GST_STIME_FORMAT,
        GST_STIME_ARGS (track->next_position));

    if (track->next_position != GST_CLOCK_STIME_NONE) {
      if (global_output_position == GST_CLOCK_STIME_NONE)
        global_output_position = track->next_position;
      else
        global_output_position =
            MIN (global_output_position, track->next_position);
      track->waiting_add = FALSE;
      all_tracks_empty = FALSE;
    } else if (!track->eos) {
      GST_DEBUG_ID (track->id, "Need timed data");
      all_tracks_empty = FALSE;
      wait_for_data = track->waiting_add = TRUE;
    } else {
      GST_DEBUG_ID (track->id, "Track is EOS, not waiting for timed data");

      if (gst_queue_array_get_length (track->queue) > 0) {
        all_tracks_empty = FALSE;
      }
    }
  }

  if (wait_for_data)
    goto restart;

  if (all_tracks_empty && demux->output_period->has_next_period) {
    GST_DEBUG_OBJECT (demux, "Period %d is drained, switching to next period",
        demux->output_period->period_num);
    if (!gst_adaptive_demux_advance_output_period (demux)) {
      /* Failed to move to next period, error out */
      ret = GST_FLOW_ERROR;
      goto pause;
    }
    /* Restart the loop */
    goto restart;
  }

  GST_DEBUG_OBJECT (demux, "Outputting data for position %" GST_STIME_FORMAT,
      GST_STIME_ARGS (global_output_position));

  /* For each track:
   *
   * We know all active tracks have pending timed data
   * * while track next_position <= global output position
   *   * push pending data
   *   * Update track next_position
   *     * recalculate global output position
   *   * Pop next pending data from track and update pending position
   *
   */
  gboolean need_restart = FALSE;

  for (tmp = demux->priv->outputs; tmp; tmp = tmp->next) {
    OutputSlot *slot = (OutputSlot *) tmp->data;
    GstAdaptiveDemuxTrack *track = slot->track;

    GST_LOG_OBJECT (track->element,
        "active:%d draining:%d selected:%d next_position:%" GST_STIME_FORMAT
        " global_output_position:%" GST_STIME_FORMAT, track->active,
        track->draining, track->selected, GST_STIME_ARGS (track->next_position),
        GST_STIME_ARGS (global_output_position));

    if (!track->active)
      continue;

    while (global_output_position == GST_CLOCK_STIME_NONE
        || !slot->pushed_timed_data
        || ((track->next_position != GST_CLOCK_STIME_NONE)
            && track->next_position <= global_output_position)
        || ((track->next_position == GST_CLOCK_STIME_NONE) && track->eos)) {
      GstMiniObject *mo =
          gst_adaptive_demux_track_dequeue_data_locked (demux, track, TRUE);

      if (!mo) {
        GST_DEBUG_ID (track->id,
            "Track doesn't have any pending data (eos:%d pushed_timed_data:%d)",
            track->eos, slot->pushed_timed_data);
        /* This should only happen if the track is EOS, or exactly in between
         * the parser outputting segment/caps before buffers. */
        g_assert (track->eos || !slot->pushed_timed_data);

        /* If we drained the track, but there's a pending track on the slot
         * loop again to activate it */
        if (slot->pending_track) {
          GST_DEBUG_ID (track->id,
              "Track drained, but has a pending track to activate");
          goto restart;
        }
        break;
      }

      demux_update_buffering_locked (demux);
      demux_post_buffering_locked (demux);
      TRACKS_UNLOCK (demux);

      GST_DEBUG_ID (track->id, "Track dequeued %" GST_PTR_FORMAT, mo);

      if (GST_IS_EVENT (mo)) {
        GstEvent *event = (GstEvent *) mo;
        if (GST_EVENT_TYPE (event) == GST_EVENT_GAP) {
          slot->pushed_timed_data = TRUE;
        } else if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
          /* If there is a pending next period, don't send the EOS */
          if (demux->output_period->has_next_period) {
            GST_LOG_OBJECT (track->id, "Dropping EOS before next period");
            gst_event_store_mark_delivered (&track->sticky_events, event);
            gst_event_unref (event);
            event = NULL;
            /* We'll need to re-check if all tracks are empty again above */
            need_restart = TRUE;
          }
        }

        if (event != NULL) {
          gst_pad_push_event (slot->pad, gst_event_ref (event));

          if (GST_EVENT_IS_STICKY (event))
            gst_event_store_mark_delivered (&track->sticky_events, event);
          gst_event_unref (event);
        }
      } else if (GST_IS_BUFFER (mo)) {
        GstBuffer *buffer = (GstBuffer *) mo;

        if (track->output_discont) {
          if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
            buffer = gst_buffer_make_writable (buffer);
            GST_DEBUG_OBJECT (slot->pad,
                "track %s marking discont %" GST_PTR_FORMAT, track->id, buffer);
            GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
          }
          track->output_discont = FALSE;
        }
        slot->flow_ret = gst_pad_push (slot->pad, buffer);
        ret =
            gst_flow_combiner_update_pad_flow (demux->priv->flowcombiner,
            slot->pad, slot->flow_ret);
        GST_DEBUG_OBJECT (slot->pad,
            "track %s push returned %s (combined %s)",
            track->id,
            gst_flow_get_name (slot->flow_ret), gst_flow_get_name (ret));
        slot->pushed_timed_data = TRUE;
      } else {
        GST_ERROR ("Unhandled miniobject %" GST_PTR_FORMAT, mo);
      }

      TRACKS_LOCK (demux);
      gst_adaptive_demux_track_update_next_position (track);

      if (ret != GST_FLOW_OK)
        goto pause;
    }
  }

  /* Store global output position */
  if (global_output_position >= 0) {
    demux->priv->global_output_position = global_output_position;

    /* And see if any streams need to be woken for more input */
    gst_adaptive_demux_period_check_input_wakeup_locked (demux->input_period,
        global_output_position);
  }

  if (need_restart)
    goto restart;

  if (global_output_position == GST_CLOCK_STIME_NONE) {
    if (!demux->priv->flushing) {
      GST_DEBUG_OBJECT (demux,
          "Pausing output task after reaching NONE global_output_position");
      gst_task_pause (demux->priv->output_task);
    }
  }

  TRACKS_UNLOCK (demux);
  GST_DEBUG_OBJECT (demux, "leave");
  return;

pause:
  {
    GST_DEBUG_OBJECT (demux, "Pausing due to %s", gst_flow_get_name (ret));
    /* If the flushing flag is set, then the task is being
     * externally stopped, so don't go to pause(), otherwise we
     * should so we don't keep spinning */
    if (!demux->priv->flushing) {
      GST_DEBUG_OBJECT (demux, "Pausing task due to %s",
          gst_flow_get_name (ret));
      gst_task_pause (demux->priv->output_task);
    }

    TRACKS_UNLOCK (demux);

    if (ret == GST_FLOW_NOT_LINKED || ret <= GST_FLOW_EOS) {
      GstEvent *eos = gst_event_new_eos ();

      if (ret != GST_FLOW_EOS) {
        GST_ELEMENT_FLOW_ERROR (demux, ret);
      }

      GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
      if (demux->priv->segment_seqnum != GST_SEQNUM_INVALID)
        gst_event_set_seqnum (eos, demux->priv->segment_seqnum);
      GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

      gst_adaptive_demux_push_src_event (demux, eos);
    }

    return;
  }
}

/* must be called from the scheduler */
gboolean
gst_adaptive_demux_is_live (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);

  if (klass->is_live)
    return klass->is_live (demux);
  return FALSE;
}

const gchar *
gst_adaptive_demux_get_manifest_ref_uri (GstAdaptiveDemux * d)
{
  return d->manifest_base_uri ? d->manifest_base_uri : d->manifest_uri;
}

static void
handle_manifest_download_complete (DownloadRequest * request,
    DownloadRequestState state, GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstBuffer *buffer;
  GstFlowReturn result;

  g_free (demux->manifest_base_uri);
  g_free (demux->manifest_uri);

  if (request->redirect_permanent && request->redirect_uri) {
    demux->manifest_uri = g_strdup (request->redirect_uri);
    demux->manifest_base_uri = NULL;
  } else {
    demux->manifest_uri = g_strdup (request->uri);
    demux->manifest_base_uri = g_strdup (request->redirect_uri);
  }

  buffer = download_request_take_buffer (request);

  /* We should always have a buffer since this function is the non-error
   * callback for the download */
  g_assert (buffer);

  result = klass->update_manifest_data (demux, buffer);
  gst_buffer_unref (buffer);

  /* FIXME: Should the manifest uri vars be reverted to original
   * values if updating fails? */

  if (result == GST_FLOW_OK) {
    GstClockTime duration;
    /* Send an updated duration message */
    duration = klass->get_duration (demux);
    if (duration != GST_CLOCK_TIME_NONE) {
      GST_DEBUG_OBJECT (demux,
          "Sending duration message : %" GST_TIME_FORMAT,
          GST_TIME_ARGS (duration));
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_duration_changed (GST_OBJECT (demux)));
    } else {
      GST_DEBUG_OBJECT (demux,
          "Duration unknown, can not send the duration message");
    }

    /* If a manifest changes it's liveness or periodic updateness, we need
     * to start/stop the manifest update task appropriately */
    /* Keep this condition in sync with the one in
     * gst_adaptive_demux_start_manifest_update_task()
     */
    if (gst_adaptive_demux_is_live (demux) &&
        klass->requires_periodical_playlist_update (demux)) {
      gst_adaptive_demux_start_manifest_update_task (demux);
    } else {
      gst_adaptive_demux_stop_manifest_update_task (demux);
    }
  }
}

static void
handle_manifest_download_failure (DownloadRequest * request,
    DownloadRequestState state, GstAdaptiveDemux * demux)
{
  GST_FIXME_OBJECT (demux, "Manifest download failed.");
  /* Retry or error out here */
}

static GstFlowReturn
gst_adaptive_demux_update_manifest_default (GstAdaptiveDemux * demux)
{
  DownloadRequest *request;
  GstFlowReturn ret = GST_FLOW_OK;
  GError *error = NULL;

  request = download_request_new_uri (demux->manifest_uri);

  download_request_set_callbacks (request,
      (DownloadRequestEventCallback) handle_manifest_download_complete,
      (DownloadRequestEventCallback) handle_manifest_download_failure,
      NULL, NULL, demux);

  if (!downloadhelper_submit_request (demux->download_helper, NULL,
          DOWNLOAD_FLAG_COMPRESS | DOWNLOAD_FLAG_FORCE_REFRESH, request,
          &error)) {
    if (error) {
      GST_ELEMENT_WARNING (demux, RESOURCE, FAILED,
          ("Failed to download manifest: %s", error->message), (NULL));
      g_clear_error (&error);
    }
    ret = GST_FLOW_NOT_LINKED;
  }

  return ret;
}

/* must be called with manifest_lock taken */
GstFlowReturn
gst_adaptive_demux_update_manifest (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstFlowReturn ret;

  ret = klass->update_manifest (demux);

  return ret;
}

static gboolean
gst_adaptive_demux2_manual_manifest_update_cb (GstAdaptiveDemux * demux)
{
  GST_MANIFEST_LOCK (demux);
  gst_adaptive_demux_update_manifest (demux);
  GST_MANIFEST_UNLOCK (demux);

  return G_SOURCE_REMOVE;
}

/* called by a subclass that needs a callback to 'update_manifest'
 * done with with MANIFEST_LOCK held */
void
gst_adaptive_demux2_manual_manifest_update (GstAdaptiveDemux * demux)
{
  if (demux->priv->manifest_updates_cb != 0)
    return;                     /* Callback already pending */

  if (!demux->priv->manifest_updates_enabled) {
    GST_LOG_OBJECT (demux, "Marking manual manifest update pending");
    demux->priv->need_manual_manifest_update = TRUE;
    return;
  }

  demux->priv->manifest_updates_cb =
      gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_manual_manifest_update_cb, demux, NULL);
}

/* must be called with manifest_lock taken */
gboolean
gst_adaptive_demux_has_next_period (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  gboolean ret = FALSE;

  if (klass->has_next_period)
    ret = klass->has_next_period (demux);
  GST_DEBUG_OBJECT (demux, "Has next period: %d", ret);
  return ret;
}

/* must be called from the scheduler task */
void
gst_adaptive_demux_advance_period (GstAdaptiveDemux * demux)
{
  GstAdaptiveDemuxClass *klass = GST_ADAPTIVE_DEMUX_GET_CLASS (demux);
  GstAdaptiveDemuxPeriod *previous_period = demux->input_period;

  g_return_if_fail (klass->advance_period != NULL);

  GST_DEBUG_OBJECT (demux, "Advancing to next period");
  /* FIXME : no return value ? What if it fails ? */
  klass->advance_period (demux);

  if (previous_period == demux->input_period) {
    GST_ERROR_OBJECT (demux, "Advancing period failed");
    return;
  }

  /* Stop the previous period stream tasks */
  gst_adaptive_demux_period_stop_tasks (previous_period);

  gst_adaptive_demux_update_collection (demux, demux->input_period);
  /* Figure out a pre-emptive selection based on the output period selection */
  gst_adaptive_demux_period_transfer_selection (demux, demux->input_period,
      demux->output_period);

  gst_adaptive_demux_prepare_streams (demux, FALSE);
  gst_adaptive_demux_start_tasks (demux);
}

/**
 * gst_adaptive_demux_get_monotonic_time:
 * Returns: a monotonically increasing time, using the system realtime clock
 */
GstClockTime
gst_adaptive_demux2_get_monotonic_time (GstAdaptiveDemux * demux)
{
  g_return_val_if_fail (demux != NULL, GST_CLOCK_TIME_NONE);
  return gst_adaptive_demux_clock_get_time (demux->realtime_clock);
}

/**
 * gst_adaptive_demux_get_client_now_utc:
 * @demux: #GstAdaptiveDemux
 * Returns: the client's estimate of UTC
 *
 * Used to find the client's estimate of UTC, using the system realtime clock.
 */
GDateTime *
gst_adaptive_demux2_get_client_now_utc (GstAdaptiveDemux * demux)
{
  return gst_adaptive_demux_clock_get_now_utc (demux->realtime_clock);
}

/**
 * gst_adaptive_demux_is_running
 * @demux: #GstAdaptiveDemux
 * Returns: whether the demuxer is processing data
 *
 * Returns FALSE if shutdown has started (transitioning down from
 * PAUSED), otherwise TRUE.
 */
gboolean
gst_adaptive_demux2_is_running (GstAdaptiveDemux * demux)
{
  return g_atomic_int_get (&demux->running);
}

/**
 * gst_adaptive_demux_get_qos_earliest_time:
 *
 * Returns: The QOS earliest time
 *
 * Since: 1.20
 */
GstClockTime
gst_adaptive_demux2_get_qos_earliest_time (GstAdaptiveDemux * demux)
{
  GstClockTime earliest;

  GST_OBJECT_LOCK (demux);
  earliest = demux->priv->qos_earliest_time;
  GST_OBJECT_UNLOCK (demux);

  return earliest;
}

gboolean
gst_adaptive_demux2_add_stream (GstAdaptiveDemux * demux,
    GstAdaptiveDemux2Stream * stream)
{
  g_return_val_if_fail (demux && stream, FALSE);

  /* FIXME : Migrate to parent */
  g_return_val_if_fail (stream->demux == NULL, FALSE);

  GST_DEBUG_OBJECT (demux, "Adding stream %s", GST_OBJECT_NAME (stream));

  TRACKS_LOCK (demux);
  if (demux->input_period->prepared) {
    GST_ERROR_OBJECT (demux,
        "Attempted to add streams but no new period was created");
    TRACKS_UNLOCK (demux);
    return FALSE;
  }
  stream->demux = demux;

  /* Takes ownership of the stream and adds the tracks */
  if (!gst_adaptive_demux_period_add_stream (demux->input_period, stream)) {
    GST_ERROR_OBJECT (demux, "Failed to add stream to period");
    TRACKS_UNLOCK (demux);
    return FALSE;
  }

  TRACKS_UNLOCK (demux);
  return TRUE;
}

/* Return the current playback rate including any instant rate multiplier */
gdouble
gst_adaptive_demux_play_rate (GstAdaptiveDemux * demux)
{
  gdouble rate;
  GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
  rate = demux->segment.rate * demux->instant_rate_multiplier;
  GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);
  return rate;
}

GstAdaptiveDemuxLoop *
gst_adaptive_demux_get_loop (GstAdaptiveDemux * demux)
{
  return gst_adaptive_demux_loop_ref (demux->priv->scheduler_task);
}
