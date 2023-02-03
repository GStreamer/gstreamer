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

#ifndef _GST_ADAPTIVE_DEMUX_H_
#define _GST_ADAPTIVE_DEMUX_H_

#include <gst/gst.h>
#include <gst/base/gstqueuearray.h>
#include <gst/app/gstappsrc.h>
#include "downloadhelper.h"
#include "downloadrequest.h"

#include "gstadaptivedemuxutils.h"

#include "gstadaptivedemux-types.h"
#include "gstadaptivedemux-stream.h"

G_BEGIN_DECLS

/**
 * GST_ADAPTIVE_DEMUX_SINK_NAME:
 *
 * The name of the templates for the sink pad.
 */
#define GST_ADAPTIVE_DEMUX_SINK_NAME    "sink"

/**
 * GST_ADAPTIVE_DEMUX_SINK_PAD:
 * @obj: a #GstAdaptiveDemux
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define GST_ADAPTIVE_DEMUX_SINK_PAD(obj)        (((GstAdaptiveDemux *) (obj))->sinkpad)

#define GST_ADAPTIVE_DEMUX_IN_TRICKMODE_KEY_UNITS(obj) ((((GstAdaptiveDemux*)(obj))->segment.flags & GST_SEGMENT_FLAG_TRICKMODE_KEY_UNITS) == GST_SEGMENT_FLAG_TRICKMODE_KEY_UNITS)

/**
 * GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME:
 *
 * Name of the ELEMENT type messages posted by dashdemux with statistics.
 *
 * Since: 1.6
 */
#define GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME "adaptive-streaming-statistics"

#define GST_ELEMENT_ERROR_FROM_ERROR(el, msg, err) G_STMT_START { \
  gchar *__dbg = g_strdup_printf ("%s: %s", msg, err->message);         \
  GST_WARNING_OBJECT (el, "error: %s", __dbg);                          \
  gst_element_message_full (GST_ELEMENT(el), GST_MESSAGE_ERROR,         \
    err->domain, err->code,                                             \
    NULL, __dbg, __FILE__, GST_FUNCTION, __LINE__);                     \
  g_clear_error (&err); \
} G_STMT_END

/* DEPRECATED */
#define GST_ADAPTIVE_DEMUX_FLOW_END_OF_FRAGMENT GST_FLOW_CUSTOM_SUCCESS_1

/* Current fragment download should be aborted and restarted. The parent class
 * will call ::update_fragment_info() on the stream again to get the updated information.
 */
#define GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT GST_FLOW_CUSTOM_SUCCESS_2

/* The live stream has lost synchronization and the demuxer needs to be resetted */
#define GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC GST_FLOW_CUSTOM_SUCCESS_2 + 1

/* The stream sub-class is busy and can't supply information for
 * ::update_fragment_info() right now */
#define GST_ADAPTIVE_DEMUX_FLOW_BUSY (GST_FLOW_CUSTOM_SUCCESS_2 + 3)

typedef struct _GstAdaptiveDemuxPrivate GstAdaptiveDemuxPrivate;

struct _GstAdaptiveDemuxTrack
{
  gint ref_count;

  /* Demux */
  GstAdaptiveDemux *demux;

  /* Stream type */
  GstStreamType type;

  /* Stream flags */
  GstStreamFlags flags;

  /* Unique identifier (for naming and debugging) */
  gchar *id;

  /* Unique identifier */
  gchar *stream_id;

  /* Period number this track belongs
   * to (MAXINT if not assigned to a period yet)
   */
  guint period_num;

  /* Unique identifier of the internal stream produced
   * by parsebin for the Stream this track comes from */
  gchar *upstream_stream_id;

  /* Generic *elementary stream* caps */
  GstCaps *generic_caps;

  /* Generic metadata */
  GstTagList *tags;

  /* The stream object */
  GstStream *stream_object;

  /* If TRUE, this track should be filled */
  gboolean selected;

  /* If TRUE, this track is currently being outputted */
  gboolean active;

  /* If TRUE, it is no longer selected but still being outputted. */
  gboolean draining;

  /* FIXME : Replace by actual track element */
  GstElement *element;

  /* The level at which 100% buffering is achieved */
  GstClockTime buffering_threshold;

  /* The sinkpad receives parsed elementary stream */
  GstPad *sinkpad;

  /* The pending parsebin source pad (used in case streams from parsebin get updated) (ref taken) */
  GstPad *pending_srcpad;

  /* Data storage */
  GstQueueArray *queue;

  /* Sticky event storage for this track */
  GstEventStore sticky_events;

  /* ============== */
  /* Input tracking */

  /* The track received EOS */
  gboolean eos;

  /* Input segment and time (in running time) */
  GstSegment input_segment;
  GstClockTimeDiff input_time;
  GstClockTimeDiff lowest_input_time;
  guint64 input_segment_seqnum;

  /* ================= */
  /* Contents tracking */

  /* Current level of queue in bytes and time */
  guint64 level_bytes;
  GstClockTime level_time;

  /* =============== */
  /* Output tracking */

  /* Is the output thread waiting for data on this track ? */
  gboolean waiting_add;

  /* If TRUE, the next pending GstSegment running time should be updated to the
   * time stored in update_next_segment_run_ts */
  gboolean update_next_segment;

  /* Output segment and time (in running time) */
  GstSegment output_segment;
  GstClockTimeDiff output_time;

  /* Track position and duration for emitting gap
   * events */
  GstClockTime gap_position;
  GstClockTime gap_duration;

  /* Next running time position pending in queue */
  GstClockTimeDiff next_position;

  /* If the next output buffer should be marked discont */
  gboolean output_discont;
};

/**
 * GstAdaptiveDemuxPeriod:
 *
 * The opaque #GstAdaptiveDemuxPeriod data structure. */
struct _GstAdaptiveDemuxPeriod
{
  gint ref_count;

  GstAdaptiveDemux *demux;
  
  /* TRUE if the streams of this period were prepared and can be started */
  gboolean prepared;


  /* TRUE if there is another period after this one */
  gboolean has_next_period;
  
  /* An increasing unique identifier for the period.
   *
   * Note: unrelated to dash period id (which can be identical across
   * periods) */
  guint period_num;
  
  /* The list of GstAdaptiveDemux2Stream (ref hold) */
  GList *streams;

  /* Current collection */
  GstStreamCollection *collection;

  /* List of available GstAdaptiveDemuxTrack (ref hold) */
  GList *tracks;

  /* Whether tracks were changed and need re-matching against outputs */
  gboolean tracks_changed;

  /* The time at which to wake up input streams for more
   * data - the earliest of all waiting input stream thresholds,
   * or GST_CLOCK_STIME_NONE if noone is waiting */
  GstClockTimeDiff next_input_wakeup_time;
};

/**
 * GstAdaptiveDemux:
 *
 * The opaque #GstAdaptiveDemux data structure.
 */
struct _GstAdaptiveDemux
{
  /*< private >*/
  GstBin     bin;

  gint running;

  /*< protected >*/
  GstPad         *sinkpad;

  DownloadHelper *download_helper;

  /* Protected by TRACKS_LOCK */
  /* Period used for output */
  GstAdaptiveDemuxPeriod *output_period;

  /* Period used for input */
  GstAdaptiveDemuxPeriod *input_period;
  
  GstSegment segment;
  gdouble instant_rate_multiplier; /* 1.0 by default, or from instant-rate seek */

  gchar *manifest_uri;
  gchar *manifest_base_uri;

  /* Properties */
  gfloat bandwidth_target_ratio; /* ratio of the available bitrate to use */
  guint connection_speed; /* Available / bandwidth to use set by the application */
  guint min_bitrate; /* Minimum bitrate to choose */
  guint max_bitrate; /* Maximum bitrate to choose */

  guint current_download_rate; /* Current estimate of download bitrate */

  /* Buffering levels */
  GstClockTime max_buffering_time;
  GstClockTime buffering_high_watermark_time;
  GstClockTime buffering_low_watermark_time;
  gdouble buffering_high_watermark_fragments;
  gdouble buffering_low_watermark_fragments;

  /* video/audio buffer level as minimum of the appropriate streams */
  GstClockTime current_level_time_video;
  GstClockTime current_level_time_audio;

  gboolean have_group_id;
  guint group_id;

  guint next_stream_id;

  /* Realtime clock */
  GstAdaptiveDemuxClock *realtime_clock;

  /* < private > */
  GstAdaptiveDemuxPrivate *priv;
};

/**
 * GstAdaptiveDemuxClass:
 *
 */
struct _GstAdaptiveDemuxClass
{
  /*< private >*/
  GstBinClass bin_class;

  /*< public >*/

  /**
   * process_manifest: Parse the manifest
   * @demux: #GstAdaptiveDemux
   * @manifest: the manifest to be parsed
   *
   * Parse the manifest and add the created streams using
   * gst_adaptive_demux2_stream_new()
   *
   * Returns: %TRUE if successful
   */
  gboolean      (*process_manifest) (GstAdaptiveDemux * demux, GstBuffer * manifest);

  /**
   * get_manifest_update_interval:
   * @demux: #GstAdaptiveDemux
   *
   * Used during live streaming, the subclass should return the interval
   * between successive manifest updates
   *
   * Returns: the update interval in microseconds
   */
  gint64        (*get_manifest_update_interval) (GstAdaptiveDemux * demux);

  /**
   * update_manifest:
   * @demux: #GstAdaptiveDemux
   *
   * During live streaming, this will be called for the subclass to update its
   * manifest with the new version. By default it fetches the manifest URI
   * and passes it to GstAdaptiveDemux::update_manifest_data().
   *
   * Returns: #GST_FLOW_OK is all succeeded, #GST_FLOW_EOS if the stream ended
   *          or #GST_FLOW_ERROR if an error happened
   */
  GstFlowReturn (*update_manifest) (GstAdaptiveDemux * demux);

  /**
   * update_manifest_data:
   * @demux: #GstAdaptiveDemux
   * @buf: Downloaded manifest data
   *
   * During live streaming, this will be called for the subclass to update its
   * manifest with the new version
   *
   * Returns: #GST_FLOW_OK is all succeeded, #GST_FLOW_EOS if the stream ended
   *          or #GST_FLOW_ERROR if an error happened
   */
  GstFlowReturn (*update_manifest_data) (GstAdaptiveDemux * demux, GstBuffer * buf);

  gboolean      (*is_live)          (GstAdaptiveDemux * demux);
  GstClockTime  (*get_duration)     (GstAdaptiveDemux * demux);

  /**
   * reset:
   * @demux: #GstAdaptiveDemux
   *
   * Reset the internal state of the subclass, getting ready to restart with
   * a new stream afterwards
   */
  void          (*reset)            (GstAdaptiveDemux * demux);

  /**
   * seek:
   * @demux: #GstAdaptiveDemux
   * @seek: a seek #GstEvent
   *
   * The demuxer should seek on all its streams to the specified position
   * in the seek event
   *
   * Returns: %TRUE if successful
   */
  gboolean      (*seek)             (GstAdaptiveDemux * demux, GstEvent * seek);

  /**
   * has_next_period:
   * @demux: #GstAdaptiveDemux
   *
   * Checks if there is a next period following the current one.
   * DASH can have multiple medias chained in its manifest, when one finishes
   * this function is called to verify if there is a new period to be played
   * in sequence.
   *
   * Returns: %TRUE if there is another period
   */
  gboolean      (*has_next_period)  (GstAdaptiveDemux * demux);
  /**
   * advance_period:
   * @demux: #GstAdaptiveDemux
   *
   * Advances the manifest to the next period. New streams should be created
   * using gst_adaptive_demux2_stream_new().
   */
  void          (*advance_period)  (GstAdaptiveDemux * demux);

  /**
   * get_live_seek_range:
   * @demux: #GstAdaptiveDemux
   * @start: pointer to put the start position allowed to seek to
   * @stop: pointer to put the stop position allowed to seek to
   *
   * Gets the allowed seek start and stop positions for the current live stream
   *
   * Return: %TRUE if successful
   */
  gboolean (*get_live_seek_range) (GstAdaptiveDemux * demux, gint64 * start, gint64 * stop);

  /**
   * get_period_start_time:
   * @demux: #GstAdaptiveDemux
   *
   * Gets the start time of the current period. Timestamps are resetting to 0
   * after each period but we have to maintain a continuous stream and running
   * time so need to know the start time of the current period.
   *
   * Return: a #GstClockTime representing the start time of the currently
   * selected period.
   */
  GstClockTime (*get_period_start_time) (GstAdaptiveDemux *demux);

  /**
   * requires_periodical_playlist_update:
   * @demux: #GstAdaptiveDemux
   *
   * Some adaptive streaming protocols allow the client to download
   * the playlist once and build up the fragment list based on the
   * current fragment metadata. For those protocols the demuxer
   * doesn't need to periodically refresh the playlist. This vfunc
   * is relevant only for live playback scenarios.
   *
   * Return: %TRUE if the playlist needs to be refreshed periodically by the demuxer.
   */
  gboolean (*requires_periodical_playlist_update) (GstAdaptiveDemux * demux);
};

GType    gst_adaptive_demux_ng_get_type (void);

gboolean gst_adaptive_demux2_add_stream (GstAdaptiveDemux *demux,
					 GstAdaptiveDemux2Stream *stream);

gboolean gst_adaptive_demux2_stream_add_track (GstAdaptiveDemux2Stream *stream,
					       GstAdaptiveDemuxTrack *track);

GstAdaptiveDemuxTrack *gst_adaptive_demux_track_new (GstAdaptiveDemux *demux,
						     GstStreamType type,
						     GstStreamFlags flags,
						     gchar *stream_id,
						     GstCaps *caps,
						     GstTagList *tags);
GstAdaptiveDemuxTrack *gst_adaptive_demux_track_ref (GstAdaptiveDemuxTrack *track);
void                   gst_adaptive_demux_track_unref (GstAdaptiveDemuxTrack *track);

const gchar *gst_adaptive_demux_get_manifest_ref_uri (GstAdaptiveDemux * demux);

gboolean gst_adaptive_demux_start_new_period (GstAdaptiveDemux * demux);

GstClockTime gst_adaptive_demux2_get_monotonic_time (GstAdaptiveDemux * demux);
GDateTime *gst_adaptive_demux2_get_client_now_utc (GstAdaptiveDemux * demux);

gboolean gst_adaptive_demux2_is_running (GstAdaptiveDemux * demux);

GstClockTime gst_adaptive_demux2_get_qos_earliest_time (GstAdaptiveDemux *demux);

GstCaps * gst_codec_utils_caps_from_iso_rfc6831 (gchar * codec);

gdouble gst_adaptive_demux_play_rate (GstAdaptiveDemux *demux);

void gst_adaptive_demux2_manual_manifest_update (GstAdaptiveDemux * demux);
GstAdaptiveDemuxLoop *gst_adaptive_demux_get_loop (GstAdaptiveDemux *demux);

G_END_DECLS

#endif

