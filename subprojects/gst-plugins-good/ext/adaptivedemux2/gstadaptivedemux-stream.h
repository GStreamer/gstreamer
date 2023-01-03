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
#ifndef _GST_ADAPTIVE_DEMUX_STREAM_H_
#define _GST_ADAPTIVE_DEMUX_STREAM_H_

#include <gst/gst.h>
#include "gstadaptivedemux-types.h"
#include "downloadrequest.h"

G_BEGIN_DECLS

#define GST_TYPE_ADAPTIVE_DEMUX2_STREAM \
  (gst_adaptive_demux2_stream_get_type())
#define GST_ADAPTIVE_DEMUX2_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ADAPTIVE_DEMUX2_STREAM,GstAdaptiveDemux2Stream))
#define GST_ADAPTIVE_DEMUX2_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ADAPTIVE_DEMUX2_STREAM,GstAdaptiveDemux2StreamClass))
#define GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_ADAPTIVE_DEMUX2_STREAM,GstAdaptiveDemux2StreamClass))
#define GST_IS_ADAPTIVE_DEMUX2_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ADAPTIVE_DEMUX2_STREAM))
#define GST_IS_ADAPTIVE_DEMUX2_STREAM_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ADAPTIVE_DEMUX2_STREAM))
#define GST_ADAPTIVE_DEMUX2_STREAM_CAST(obj) ((GstAdaptiveDemux2Stream *)obj)


#define GST_ADAPTIVE_DEMUX2_STREAM_NEED_HEADER(obj) (((GstAdaptiveDemux2Stream *) (obj))->need_header)

typedef enum _GstAdaptiveDemux2StreamState GstAdaptiveDemux2StreamState;

typedef struct _GstAdaptiveDemux2StreamFragment GstAdaptiveDemux2StreamFragment;

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

enum _GstAdaptiveDemux2StreamState {
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED, /* Stream was stopped */
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART, /* Stream stopped but needs restart logic */
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_PREPARE, /* Sub-class is busy and can't update_fragment_info() yet */
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_LIVE,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_OUTPUT_SPACE,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_MANIFEST_UPDATE,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_BEFORE_DOWNLOAD, /* Ready, but not allowed to download yet */
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS,
  GST_ADAPTIVE_DEMUX2_STREAM_STATE_ERRORED
};

struct _GstAdaptiveDemux2StreamClass
{
  GstObjectClass parent_class;

  /**
   * update_fragment_info:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Requests the stream to set the information about the current fragment to its
   * current fragment struct
   *
   * Returns: #GST_FLOW_OK in success, #GST_FLOW_ERROR on error, #GST_FLOW_EOS
   *          if there is no fragment, or the custom GST_ADAPTIVE_DEMUX_FLOW_BUSY
   *          if the sub-class is still preparing.
   */
  GstFlowReturn (*update_fragment_info) (GstAdaptiveDemux2Stream * stream);

  /**
   * submit_request:
   * @stream: #GstAdaptiveDemux2Stream
   * @download_req: #DownloadRequest
   *
   * Requests the stream submit the provided download request for processing,
   * either through the DownloadHelper (default), or through some sub-class
   * mechanism
   *
   * Returns: #GST_FLOW_OK in success, #GST_FLOW_ERROR on error
   */
  GstFlowReturn (*submit_request) (GstAdaptiveDemux2Stream * stream, DownloadRequest * download_req);

  /**
   * finish_fragment:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Notifies the subclass that a fragment download was finished.
   * It can be used to cleanup internal state after a fragment and
   * also push any pending data before moving to the next fragment.
   */
  GstFlowReturn (*finish_fragment) (GstAdaptiveDemux2Stream * stream);

  /**
   * data_received:
   * @stream: #GstAdaptiveDemux2Stream
   * @buffer: #GstBuffer
   *
   * Notifies the subclass that a fragment chunk was downloaded. The subclass
   * can look at the data and modify/push data as desired.
   *
   * Returns: #GST_FLOW_OK if successful, #GST_FLOW_ERROR in case of error.
   */
  GstFlowReturn (*data_received) (GstAdaptiveDemux2Stream * stream, GstBuffer * buffer);

  gboolean      (*has_next_fragment)  (GstAdaptiveDemux2Stream * stream);
  GstFlowReturn (*advance_fragment) (GstAdaptiveDemux2Stream * stream);

  GstFlowReturn (*stream_seek)     (GstAdaptiveDemux2Stream * stream,
				    gboolean                 forward,
				    GstSeekFlags             flags,
				    GstClockTimeDiff         target_ts,
				    GstClockTimeDiff       * final_ts);

  /**
   * start:
   * @stream: a #GstAdaptiveDemux2Stream
   *
   * Called to start downloading a @stream, sub-classes should chain up to the default
   * implementation. Sub-classes can return %FALSE if more
   * information is required before the stream can be started. In that case, sub-classes
   * will have to call gst_adaptive_demux2_stream_start() again when the stream should
   * be started.
   */
  void       (*start) (GstAdaptiveDemux2Stream *stream);

  /**
   * stop:
   * @stream: a #GstAdaptiveDemux2Stream
   *
   * Called to stop downloading a @stream, sub-classes should chain up to the default
   * implementation.
   */
  void       (*stop) (GstAdaptiveDemux2Stream *stream);


  /**
   * create_tracks:
   * @stream: A #GstAdaptiveDemux2Stream
   *
   * Called whenever the base class collected a @collection on a @stream which has
   * pending tracks to be created. Subclasses should override this if they
   * create streams without tracks.
   *
   * * create the various tracks by analyzing the @stream stream_collection
   * * Set the track upstream_stream_id to the corresponding stream_id from the collection
   */
  void  (*create_tracks) (GstAdaptiveDemux2Stream *stream);

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
   * select_bitrate:
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
  gboolean      (*select_bitrate) (GstAdaptiveDemux2Stream * stream, guint64 bitrate);

  /**
   * get_fragment_waiting_time:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * For live streams, requests how much time should be waited before starting
   * to download the fragment. This is useful to avoid downloading a fragment that
   * isn't available yet.
   *
   * Returns: The waiting time in as a #GstClockTime
   */
  GstClockTime (*get_fragment_waiting_time) (GstAdaptiveDemux2Stream * stream);

  /**
   * start_fragment:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Notifies the subclass that the given stream is starting the download
   * of a new fragment. Can be used to reset/init internal state that is
   * needed before each fragment, like decryption engines.
   *
   * Returns: %TRUE if successful.
   */
  gboolean      (*start_fragment) (GstAdaptiveDemux2Stream * stream);

  /**
   * get_presentation_offset:
   * @stream: #GstAdaptiveDemux2Stream
   *
   * Gets the delay to apply to @stream.
   *
   * Return: a #GstClockTime representing the (positive) time offset to apply to
   * @stream.
   */
  GstClockTime (*get_presentation_offset) (GstAdaptiveDemux2Stream *stream);
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

  GMutex prepare_lock;
  GCond prepare_cond;

  /* The (global output) time at which this stream should be woken
   * to download more input */
  GstClockTimeDiff next_input_wakeup_time;

  guint last_status_code;

  gboolean pending_tracks; /* if we need to discover tracks dynamically for this stream */
  gboolean download_finished;

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

  gboolean download_error_retry;
  guint download_error_count;

  /* Last collection provided by parsebin */
  GstStreamCollection *stream_collection;

  /* OR'd set of stream types in this stream */
  GstStreamType stream_type;

  /* The buffering threshold recommended by the subclass */
  GstClockTime recommended_buffering_threshold;
};

GType    gst_adaptive_demux2_stream_get_type (void);

void gst_adaptive_demux2_stream_start (GstAdaptiveDemux2Stream * stream);

void gst_adaptive_demux2_stream_queue_event (GstAdaptiveDemux2Stream * stream,
					     GstEvent * event);

gboolean gst_adaptive_demux2_stream_is_selected (GstAdaptiveDemux2Stream *stream);
gboolean gst_adaptive_demux2_stream_is_running (GstAdaptiveDemux2Stream * stream);

void gst_adaptive_demux2_stream_set_caps (GstAdaptiveDemux2Stream * stream,
                                         GstCaps * caps);

void gst_adaptive_demux2_stream_set_tags (GstAdaptiveDemux2Stream * stream,
                                         GstTagList * tags);

GstFlowReturn gst_adaptive_demux2_stream_push_buffer (GstAdaptiveDemux2Stream * stream,
						      GstBuffer * buffer);

GstFlowReturn gst_adaptive_demux2_stream_advance_fragment (GstAdaptiveDemux2Stream * stream,
							   GstClockTime duration);

gboolean gst_adaptive_demux2_stream_handle_collection (GstAdaptiveDemux2Stream *stream,
    GstStreamCollection *collection, gboolean *had_pending_tracks);

void gst_adaptive_demux2_stream_mark_prepared(GstAdaptiveDemux2Stream *stream);
gboolean gst_adaptive_demux2_stream_wait_prepared(GstAdaptiveDemux2Stream *stream);

void gst_adaptive_demux2_stream_fragment_clear (GstAdaptiveDemux2StreamFragment * f);

G_END_DECLS

#endif
