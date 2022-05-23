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

G_BEGIN_DECLS

#define GST_TYPE_ADAPTIVE_DEMUX \
  (gst_adaptive_demux_ng_get_type())
#define GST_ADAPTIVE_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ADAPTIVE_DEMUX,GstAdaptiveDemux))
#define GST_ADAPTIVE_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ADAPTIVE_DEMUX,GstAdaptiveDemuxClass))
#define GST_ADAPTIVE_DEMUX_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_ADAPTIVE_DEMUX,GstAdaptiveDemuxClass))
#define GST_IS_ADAPTIVE_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ADAPTIVE_DEMUX))
#define GST_IS_ADAPTIVE_DEMUX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ADAPTIVE_DEMUX))
#define GST_ADAPTIVE_DEMUX_CAST(obj) ((GstAdaptiveDemux *)obj)

#define GST_TYPE_ADAPTIVE_DEMUX2_STREAM \
  (gst_adaptive_demux2_stream_get_type())
#define GST_ADAPTIVE_DEMUX2_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ADAPTIVE_DEMUX2_STREAM,GstAdaptiveDemux2Stream))
#define GST_ADAPTIVE_DEMUX2_STREAM_CAST(obj) ((GstAdaptiveDemux2Stream *)obj)

typedef struct _GstAdaptiveDemux2Stream GstAdaptiveDemux2Stream;
typedef GstObjectClass GstAdaptiveDemux2StreamClass;


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

#define GST_ADAPTIVE_DEMUX2_STREAM_NEED_HEADER(obj) (((GstAdaptiveDemux2Stream *) (obj))->need_header)

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
 * will call ::update_fragment_info() again to get the updated information.
 */
#define GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT GST_FLOW_CUSTOM_SUCCESS_2

/* The live stream has lost synchronization and the demuxer needs to be resetted */
#define GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC GST_FLOW_CUSTOM_ERROR_1

typedef enum _GstAdaptiveDemux2StreamState GstAdaptiveDemux2StreamState;

typedef struct _GstAdaptiveDemux2StreamFragment GstAdaptiveDemux2StreamFragment;
typedef struct _GstAdaptiveDemuxTrack GstAdaptiveDemuxTrack;
typedef struct _GstAdaptiveDemuxPeriod GstAdaptiveDemuxPeriod;
typedef struct _GstAdaptiveDemux GstAdaptiveDemux;
typedef struct _GstAdaptiveDemuxClass GstAdaptiveDemuxClass;
typedef struct _GstAdaptiveDemuxPrivate GstAdaptiveDemuxPrivate;

struct _GstAdaptiveDemux2StreamFragment
{
  /* The period-local stream time for the given fragment. */
  GstClockTimeDiff stream_time;
  GstClockTime duration;

  gchar *uri;
  gint64 range_start;
  gint64 range_end;

  /* when chunked downloading is used, may be be updated need_another_chunk() */
  gint chunk_size;

  /* when headers are needed */
  gchar *header_uri;
  gint64 header_range_start;
  gint64 header_range_end;

  /* when index is needed */
  gchar *index_uri;
  gint64 index_range_start;
  gint64 index_range_end;

  gboolean finished;
};

struct _GstAdaptiveDemuxTrack
{
  gint ref_count;

  /* Demux */
  GstAdaptiveDemux *demux;

  /* Stream type */
  GstStreamType type;

  /* Stream flags */
  GstStreamFlags flags;

  /* Unique identifier */
  gchar *stream_id;

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

  /* Level to wait until download can commence */
  GstClockTime waiting_del_level;

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

enum _GstAdaptiveDemux2StreamState {
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED, /* Stream was stopped */
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART, /* Stream stopped but needs restart logic */
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_LIVE,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_OUTPUT_SPACE,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_MANIFEST_UPDATE,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_ERRORED
};

struct _GstAdaptiveDemux2Stream
{
  GstObject object;

  /* FIXME : transition to gstobject->parent */
  GstAdaptiveDemux *demux;

  /* The period to which the stream belongs, set when adding the stream to the
   * demuxer */
  GstAdaptiveDemuxPeriod *period;
  
  /* The tracks this stream targets */
  GList *tracks;

  /* The internal parsebin, forward data to track */
  GstElement *parsebin;
  GstPad *parsebin_sink;

  gulong pad_added_id, pad_removed_id;
  
  GstSegment parse_segment;

  /* TRUE if the current stream GstSegment should be sent downstream */
  gboolean send_segment;
  /* TRUE if the stream GstSegment requires recalculation (from demuxer
     segment) */
  gboolean compute_segment;
  /* first_and_live applies to compute_segment */
  gboolean first_and_live;

  /* When restarting, what is the target position (in demux segment) to
   * begin at */
  GstClockTime start_position;

  /* Track the current position (in demux segment) of the current fragment */
  GstClockTime current_position;

  GstCaps *pending_caps;
  GstTagList *pending_tags;

  GList *pending_events;

  GstFlowReturn last_ret;
  GError *last_error;

  gboolean discont;

  /* download tooling */
  gboolean need_header;
  gboolean need_index;

  gboolean downloading_header;
  gboolean downloading_index;

  /* persistent, reused download request for fragment data */
  DownloadRequest *download_request;

  GstAdaptiveDemux2StreamState state;
  guint pending_cb_id;
  gboolean download_active;

  guint last_status_code;

  gboolean pending_tracks; /* if we need to discover tracks dynamically for this stream */
  gboolean download_finished;
  gboolean cancelled;
  gboolean replaced; /* replaced in a bitrate switch (used with cancelled) */

  gboolean starting_fragment;
  gboolean first_fragment_buffer;
  gint64 download_start_time;
  gint64 download_total_bytes;
  gint64 download_end_offset;
  guint64 current_download_rate;

  /* bitrate of the previous fragment (pre-queue2) */
  guint64 last_bitrate;

  /* Total last download time, from request to completion */
  GstClockTime last_download_time;

  /* Average for the last fragments */
  guint64 moving_bitrate;
  guint moving_index;
  guint64 *fragment_bitrates;

  GstAdaptiveDemux2StreamFragment fragment;

  guint download_error_count;

  /* Last collection provided by parsebin */
  GstStreamCollection *stream_collection;

  /* OR'd set of stream types in this stream */
  GstStreamType stream_type;
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


  /* TRUE if the period no longer receives any data (i.e. it is closed) */
  gboolean closed;
  
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

  GstFlowReturn (*stream_seek)     (GstAdaptiveDemux2Stream * stream,
				    gboolean                 forward,
				    GstSeekFlags             flags,
				    GstClockTimeDiff         target_ts,
				    GstClockTimeDiff       * final_ts);
  gboolean      (*stream_has_next_fragment)  (GstAdaptiveDemux2Stream * stream);
  GstFlowReturn (*stream_advance_fragment) (GstAdaptiveDemux2Stream * stream);

  /**
   * stream_can_start:
   * @demux: The #GstAdaptiveDemux
   * @stream: a #GstAdaptiveDemux2Stream
   *
   * Called before starting a @stream. sub-classes can return %FALSE if more
   * information is required before it can be started. Sub-classes will have to
   * call gst_adaptive_demux2_stream_start() when the stream should be started.
   */
  gboolean      (*stream_can_start) (GstAdaptiveDemux *demux,
				     GstAdaptiveDemux2Stream *stream);

  /**
   * stream_update_tracks:
   * @demux: The #GstAdaptiveDemux
   * @stream: A #GstAdaptiveDemux2Stream
   *
   * Called whenever the base class collected a @collection on a @stream which has
   * pending tracks to be created. Subclasses should override this if they
   * create streams without tracks.
   *
   * * create the various tracks by analyzing the @stream stream_collection
   * * Set the track upstream_stream_id to the corresponding stream_id from the collection
   */
  void          (*stream_update_tracks) (GstAdaptiveDemux *demux,
					 GstAdaptiveDemux2Stream *stream);
  /**
   * need_another_chunk:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * If chunked downloading is used (chunk_size != 0) this is called once a
   * chunk is finished to decide whether more has to be downloaded or not.
   * May update chunk_size to a different value
   */
  gboolean      (*need_another_chunk) (GstAdaptiveDemux2Stream * stream);

  /**
   * stream_update_fragment_info:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Requests the stream to set the information about the current fragment to its
   * current fragment struct
   *
   * Returns: #GST_FLOW_OK in success, #GST_FLOW_ERROR on error and #GST_FLOW_EOS
   *          if there is no fragment.
   */
  GstFlowReturn (*stream_update_fragment_info) (GstAdaptiveDemux2Stream * stream);
  /**
   * stream_select_bitrate:
   * @stream: #GstAdaptiveDemux2Stream
   * @bitrate: the bitrate to select (in bytes per second)
   *
   * The stream should try to select the bitrate that is the greater, but not
   * greater than the requested bitrate. If it needs a codec change it should
   * create the new stream using gst_adaptive_demux2_stream_new(). If it only
   * needs a caps change it should set the new caps using
   * gst_adaptive_demux2_stream_set_caps().
   *
   * Returns: %TRUE if the stream changed bitrate, %FALSE otherwise
   */
  gboolean      (*stream_select_bitrate) (GstAdaptiveDemux2Stream * stream, guint64 bitrate);
  /**
   * stream_get_fragment_waiting_time:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * For live streams, requests how much time should be waited before starting
   * to download the fragment. This is useful to avoid downloading a fragment that
   * isn't available yet.
   *
   * Returns: The waiting time in as a #GstClockTime
   */
  GstClockTime (*stream_get_fragment_waiting_time) (GstAdaptiveDemux2Stream * stream);

  /**
   * start_fragment:
   * @demux: #GstAdaptiveDemux
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Notifies the subclass that the given stream is starting the download
   * of a new fragment. Can be used to reset/init internal state that is
   * needed before each fragment, like decryption engines.
   *
   * Returns: %TRUE if successful.
   */
  gboolean      (*start_fragment) (GstAdaptiveDemux * demux, GstAdaptiveDemux2Stream * stream);
  /**
   * finish_fragment:
   * @demux: #GstAdaptiveDemux
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Notifies the subclass that a fragment download was finished.
   * It can be used to cleanup internal state after a fragment and
   * also push any pending data before moving to the next fragment.
   */
  GstFlowReturn (*finish_fragment) (GstAdaptiveDemux * demux, GstAdaptiveDemux2Stream * stream);
  /**
   * data_received:
   * @demux: #GstAdaptiveDemux
   * @stream: #GstAdaptiveDemux2Stream
   * @buffer: #GstBuffer
   *
   * Notifies the subclass that a fragment chunk was downloaded. The subclass
   * can look at the data and modify/push data as desired.
   *
   * Returns: #GST_FLOW_OK if successful, #GST_FLOW_ERROR in case of error.
   */
  GstFlowReturn (*data_received) (GstAdaptiveDemux * demux, GstAdaptiveDemux2Stream * stream, GstBuffer * buffer);

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
   * get_presentation_offset:
   * @demux: #GstAdaptiveDemux
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Gets the delay to apply to @stream.
   *
   * Return: a #GstClockTime representing the (positive) time offset to apply to
   * @stream.
   */
  GstClockTime (*get_presentation_offset) (GstAdaptiveDemux *demux, GstAdaptiveDemux2Stream *stream);

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

GType    gst_adaptive_demux2_stream_get_type (void);

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


void gst_adaptive_demux2_stream_set_caps (GstAdaptiveDemux2Stream * stream,
                                         GstCaps * caps);

void gst_adaptive_demux2_stream_set_tags (GstAdaptiveDemux2Stream * stream,
                                         GstTagList * tags);

void gst_adaptive_demux2_stream_fragment_clear (GstAdaptiveDemux2StreamFragment * f);

GstFlowReturn gst_adaptive_demux2_stream_push_buffer (GstAdaptiveDemux2Stream * stream,
						      GstBuffer * buffer);

GstFlowReturn gst_adaptive_demux2_stream_advance_fragment (GstAdaptiveDemux * demux,
							   GstAdaptiveDemux2Stream * stream,
							   GstClockTime duration);

gboolean gst_adaptive_demux_start_new_period (GstAdaptiveDemux * demux);

void
gst_adaptive_demux2_stream_start (GstAdaptiveDemux2Stream * stream);

void gst_adaptive_demux2_stream_queue_event (GstAdaptiveDemux2Stream * stream,
					     GstEvent * event);

gboolean gst_adaptive_demux2_stream_is_selected (GstAdaptiveDemux2Stream *stream);
gboolean gst_adaptive_demux2_stream_is_running (GstAdaptiveDemux2Stream * stream);

GstClockTime gst_adaptive_demux2_get_monotonic_time (GstAdaptiveDemux * demux);

GDateTime *gst_adaptive_demux2_get_client_now_utc (GstAdaptiveDemux * demux);

gboolean gst_adaptive_demux2_is_running (GstAdaptiveDemux * demux);

GstClockTime gst_adaptive_demux2_get_qos_earliest_time (GstAdaptiveDemux *demux);

GstCaps * gst_codec_utils_caps_from_iso_rfc6831 (gchar * codec);

gdouble gst_adaptive_demux_play_rate (GstAdaptiveDemux *demux);

G_END_DECLS

#endif

