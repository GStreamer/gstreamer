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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstadaptivedemux-stream.h"
#include "gstadaptivedemux-private.h"

#include <glib/gi18n-lib.h>
#include <gst/app/gstappsrc.h>

GST_DEBUG_CATEGORY_EXTERN (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

static void gst_adaptive_demux2_stream_finalize (GObject * object);
static void gst_adaptive_demux2_stream_error (GstAdaptiveDemux2Stream * stream);
static GstFlowReturn
gst_adaptive_demux2_stream_data_received_default (GstAdaptiveDemux2Stream *
    stream, GstBuffer * buffer);
static GstFlowReturn
gst_adaptive_demux2_stream_finish_fragment_default (GstAdaptiveDemux2Stream *
    stream);

guint64
gst_adaptive_demux2_stream_update_current_bitrate (GstAdaptiveDemux2Stream *
    stream);
static void gst_adaptive_demux2_stream_update_track_ids (GstAdaptiveDemux2Stream
    * stream);
static GstFlowReturn
gst_adaptive_demux2_stream_submit_request_default (GstAdaptiveDemux2Stream *
    stream, DownloadRequest * download_req);
static void
gst_adaptive_demux2_stream_start_default (GstAdaptiveDemux2Stream * stream);
static void
gst_adaptive_demux2_stream_stop_default (GstAdaptiveDemux2Stream * stream);

#define gst_adaptive_demux2_stream_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstAdaptiveDemux2Stream, gst_adaptive_demux2_stream,
    GST_TYPE_OBJECT);

static void
gst_adaptive_demux2_stream_class_init (GstAdaptiveDemux2StreamClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_adaptive_demux2_stream_finalize;

  klass->start = gst_adaptive_demux2_stream_start_default;
  klass->stop = gst_adaptive_demux2_stream_stop_default;
  klass->data_received = gst_adaptive_demux2_stream_data_received_default;
  klass->finish_fragment = gst_adaptive_demux2_stream_finish_fragment_default;
  klass->submit_request = gst_adaptive_demux2_stream_submit_request_default;
}

static GType tsdemux_type = 0;

static void
gst_adaptive_demux2_stream_init (GstAdaptiveDemux2Stream * stream)
{
  stream->download_request = download_request_new ();
  stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED;
  stream->last_ret = GST_FLOW_OK;
  stream->next_input_wakeup_time = GST_CLOCK_STIME_NONE;

  stream->recommended_buffering_threshold = GST_CLOCK_TIME_NONE;

  stream->fragment_bitrates =
      g_malloc0 (sizeof (guint64) * NUM_LOOKBACK_FRAGMENTS);

  stream->start_position = stream->current_position = GST_CLOCK_TIME_NONE;

  g_mutex_init (&stream->prepare_lock);
  g_cond_init (&stream->prepare_cond);

  gst_segment_init (&stream->parse_segment, GST_FORMAT_TIME);
}

/* must be called with manifest_lock taken.
 * It will temporarily drop the manifest_lock in order to join the task.
 * It will join only the old_streams (the demux->streams are joined by
 * gst_adaptive_demux_stop_tasks before gst_adaptive_demux2_stream_free is
 * called)
 */
static void
gst_adaptive_demux2_stream_finalize (GObject * object)
{
  GstAdaptiveDemux2Stream *stream = (GstAdaptiveDemux2Stream *) object;

  GST_LOG_OBJECT (object, "Finalizing");

  if (stream->download_request)
    download_request_unref (stream->download_request);

  g_clear_error (&stream->last_error);

  gst_adaptive_demux2_stream_fragment_clear (&stream->fragment);

  if (stream->pending_events) {
    g_list_free_full (stream->pending_events, (GDestroyNotify) gst_event_unref);
    stream->pending_events = NULL;
  }

  if (stream->parsebin_sink) {
    gst_object_unref (stream->parsebin_sink);
    stream->parsebin_sink = NULL;
  }

  if (stream->pad_added_id)
    g_signal_handler_disconnect (stream->parsebin, stream->pad_added_id);
  if (stream->pad_removed_id)
    g_signal_handler_disconnect (stream->parsebin, stream->pad_removed_id);

  if (stream->parsebin != NULL) {
    GST_LOG_OBJECT (stream, "Removing parsebin");
    gst_bin_remove (GST_BIN_CAST (stream->demux), stream->parsebin);
    gst_element_set_state (stream->parsebin, GST_STATE_NULL);
    gst_object_unref (stream->parsebin);
    stream->parsebin = NULL;
  }

  g_free (stream->fragment_bitrates);

  g_list_free_full (stream->tracks,
      (GDestroyNotify) gst_adaptive_demux_track_unref);

  if (stream->pending_caps)
    gst_caps_unref (stream->pending_caps);

  gst_clear_tag_list (&stream->pending_tags);
  g_clear_pointer (&stream->stream_collection, gst_object_unref);

  g_mutex_clear (&stream->prepare_lock);
  g_cond_clear (&stream->prepare_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_adaptive_demux2_stream_add_track:
 * @stream: A #GstAdaptiveDemux2Stream
 * @track: (transfer none): A #GstAdaptiveDemuxTrack to assign to the @stream
 *
 * This function is called when a subclass knows of a target @track that this
 * @stream can provide.
 */
gboolean
gst_adaptive_demux2_stream_add_track (GstAdaptiveDemux2Stream * stream,
    GstAdaptiveDemuxTrack * track)
{
  g_return_val_if_fail (track != NULL, FALSE);

  GST_DEBUG_OBJECT (stream, "track:%s", track->stream_id);
  if (g_list_find (stream->tracks, track)) {
    GST_DEBUG_OBJECT (stream,
        "track '%s' already handled by this stream", track->stream_id);
    return FALSE;
  }

  if (stream->demux->buffering_low_watermark_time)
    track->buffering_threshold = stream->demux->buffering_low_watermark_time;
  else if (GST_CLOCK_TIME_IS_VALID (stream->recommended_buffering_threshold))
    track->buffering_threshold =
        MIN (10 * GST_SECOND, stream->recommended_buffering_threshold);
  else {
    /* Using a starting default, can be overriden later in
     * ::update_stream_info() */
    GST_DEBUG_OBJECT (stream,
        "Setting default 10s buffering threshold on new track");
    track->buffering_threshold = 10 * GST_SECOND;
  }

  stream->tracks =
      g_list_append (stream->tracks, gst_adaptive_demux_track_ref (track));
  if (stream->demux) {
    g_assert (stream->period);
    gst_adaptive_demux_period_add_track (stream->period, track);
  }
  return TRUE;
}

static gboolean
gst_adaptive_demux2_stream_next_download (GstAdaptiveDemux2Stream * stream);
static gboolean
gst_adaptive_demux2_stream_load_a_fragment (GstAdaptiveDemux2Stream * stream);
static void
gst_adaptive_demux2_stream_handle_playlist_eos (GstAdaptiveDemux2Stream *
    stream);
static GstFlowReturn
gst_adaptive_demux2_stream_begin_download_uri (GstAdaptiveDemux2Stream * stream,
    const gchar * uri, gint64 start, gint64 end);

#ifndef GST_DISABLE_GST_DEBUG
static const char *
uritype (GstAdaptiveDemux2Stream * s)
{
  if (s->downloading_header)
    return "header";
  if (s->downloading_index)
    return "index";
  return "fragment";
}
#endif

/* Schedules another chunked download (returns TRUE) or FALSE if no more chunks */
static gboolean
schedule_another_chunk (GstAdaptiveDemux2Stream * stream)
{
  DownloadRequest *request = stream->download_request;
  GstFlowReturn ret;

  gchar *uri = request->uri;
  gint64 range_start = request->range_start;
  gint64 range_end = request->range_end;
  gint64 chunk_size;
  gint64 chunk_end;

  if (range_end == -1)
    return FALSE;               /* This was a request to the end, no more to load */

  /* The size of the request that just completed: */
  chunk_size = range_end + 1 - range_start;

  if (request->content_received < chunk_size)
    return FALSE;               /* Short read - we're done */

  /* Accumulate the data we just fetched, to figure out the next
   * request start position and update the target chunk size from
   * the updated stream fragment info */
  range_start += chunk_size;
  range_end = stream->fragment.range_end;
  chunk_size = stream->fragment.chunk_size;

  if (chunk_size == 0)
    return FALSE;               /* Sub-class doesn't want another chunk */

  /* HTTP ranges are inclusive for the end */
  if (chunk_size != -1) {
    chunk_end = range_start + chunk_size - 1;
    if (range_end != -1 && range_end < chunk_end)
      chunk_end = range_end;
  } else {
    chunk_end = range_end;
  }

  GST_DEBUG_OBJECT (stream,
      "Starting next chunk %s %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
      " chunk_size %" G_GINT64_FORMAT, uri, range_start, chunk_end, chunk_size);

  ret =
      gst_adaptive_demux2_stream_begin_download_uri (stream, uri,
      range_start, chunk_end);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (stream,
        "Stopping stream due to begin download failure - ret %s",
        gst_flow_get_name (ret));
    gst_adaptive_demux2_stream_stop (stream);
    return FALSE;
  }

  return TRUE;
}

static void
drain_inactive_tracks (GstAdaptiveDemux2Stream * stream)
{
  GList *iter;
  GstAdaptiveDemux *demux = stream->demux;

  TRACKS_LOCK (demux);
  for (iter = stream->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;
    if (!track->selected) {
      gst_adaptive_demux_track_drain_to (track,
          demux->priv->global_output_position);
    }
  }

  TRACKS_UNLOCK (demux);
}

/* Called to complete a download, either due to failure or completion
 * Should set up the next download if necessary */
static void
gst_adaptive_demux2_stream_finish_download (GstAdaptiveDemux2Stream *
    stream, GstFlowReturn ret, GError * err)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  GST_DEBUG_OBJECT (stream,
      "%s download finish: %d %s - err: %p", uritype (stream), ret,
      gst_flow_get_name (ret), err);

  stream->download_finished = TRUE;

  /* finish_fragment might call gst_adaptive_demux2_stream_advance_fragment,
   * which can look at the last_ret - so make sure it's stored before calling that.
   * Also, for not-linked or other errors passed in that are going to make
   * this stream stop, we'll need to store it */
  stream->last_ret = ret;

  if (err) {
    g_clear_error (&stream->last_error);
    stream->last_error = g_error_copy (err);
  }

  /* For actual errors, stop now, no need to call finish_fragment and get
   * confused if it returns a non-error status, but if EOS was passed in,
   * continue and check whether finish_fragment() says we've finished
   * the whole manifest or just this fragment */
  if (ret < 0 && ret != GST_FLOW_EOS) {
    GST_INFO_OBJECT (stream,
        "Stopping stream due to error ret %s", gst_flow_get_name (ret));
    gst_adaptive_demux2_stream_stop (stream);
    return;
  }

  /* Handle all the possible flow returns here: */
  if (ret == GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC) {
    /* We lost sync, seek back to live and return */
    GST_WARNING_OBJECT (stream, "Lost sync when downloading");
    gst_adaptive_demux_handle_lost_sync (stream->demux);
    return;
  } else if (ret == GST_ADAPTIVE_DEMUX_FLOW_END_OF_FRAGMENT) {
    /* The sub-class wants to stop the fragment immediately */
    stream->fragment.finished = TRUE;
    ret = klass->finish_fragment (stream);

    GST_DEBUG_OBJECT (stream, "finish_fragment ret %d %s", ret,
        gst_flow_get_name (ret));
  } else if (ret == GST_ADAPTIVE_DEMUX_FLOW_RESTART_FRAGMENT) {
    GST_DEBUG_OBJECT (stream, "Restarting download as requested");
    /* Just mark the fragment as finished */
    stream->fragment.finished = TRUE;
    ret = GST_FLOW_OK;
  } else if (!klass->need_another_chunk || stream->fragment.chunk_size == -1
      || !klass->need_another_chunk (stream)
      || stream->fragment.chunk_size == 0) {
    stream->fragment.finished = TRUE;
    ret = klass->finish_fragment (stream);

    GST_DEBUG_OBJECT (stream, "finish_fragment ret %d %s", ret,
        gst_flow_get_name (ret));
  } else if (stream->fragment.chunk_size != 0
      && schedule_another_chunk (stream)) {
    /* Another download has already begun, no need to queue anything below */
    return;
  }

  /* For HLS, we might be enqueueing data into tracks that aren't
   * selected. Drain those ones out */
  drain_inactive_tracks (stream);

  /* Now that we've called finish_fragment we can clear these flags the
   * sub-class might have checked */
  if (stream->downloading_header) {
    stream->need_header = FALSE;
    stream->downloading_header = FALSE;
  } else if (stream->downloading_index) {
    stream->need_index = FALSE;
    stream->downloading_index = FALSE;
    /* Restart the fragment again now that header + index were loaded
     * so that get_fragment_info() will be called again */
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT;
  } else {
    /* Finishing a fragment data download. Try for another */
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT;
  }

  /* if GST_FLOW_EOS was passed in that means this download is finished,
   * but it's the result returned from finish_fragment() we really care
   * about, as that tells us if the manifest has run out of fragments
   * to load */
  if (ret == GST_FLOW_EOS) {
    stream->last_ret = ret;

    gst_adaptive_demux2_stream_handle_playlist_eos (stream);
    return;
  }

  /* Now finally, if ret is anything other than success, we should stop this
   * stream */
  if (ret < 0) {
    GST_DEBUG_OBJECT (stream,
        "Stopping stream due to finish fragment ret %s",
        gst_flow_get_name (ret));
    gst_adaptive_demux2_stream_stop (stream);
    return;
  }

  /* Clear the last_ret marker before starting a fresh download */
  stream->last_ret = GST_FLOW_OK;

  GST_LOG_OBJECT (stream, "Scheduling next_download() call");
  stream->pending_cb_id =
      gst_adaptive_demux_loop_call (stream->demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_stream_next_download,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

/* Must be called from the scheduler context */
void
gst_adaptive_demux2_stream_parse_error (GstAdaptiveDemux2Stream * stream,
    GError * err)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING)
    return;

  downloadhelper_cancel_request (demux->download_helper,
      stream->download_request);

  /* cancellation is async, so recycle our download request to avoid races */
  download_request_unref (stream->download_request);
  stream->download_request = download_request_new ();

  gst_adaptive_demux2_stream_finish_download (stream, GST_FLOW_CUSTOM_ERROR,
      err);
}

static void
gst_adaptive_demux2_stream_prepare_segment (GstAdaptiveDemux2Stream * stream,
    gboolean first_and_live)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstClockTime period_start = gst_adaptive_demux_get_period_start_time (demux);
  GstClockTime offset =
      gst_adaptive_demux2_stream_get_presentation_offset (stream);

  /* FIXME: Add a helper function to retrieve the demuxer segment
   * using the SEGMENT_LOCK */
  stream->parse_segment = demux->segment;

  /* The demuxer segment is just built from seek events, but for each stream
   * we have to adjust segments according to the current period and the
   * stream specific presentation time offset.
   *
   * For each period, buffer timestamps start again from 0. Additionally the
   * buffer timestamps are shifted by the stream specific presentation time
   * offset, so the first buffer timestamp of a period is 0 + presentation
   * time offset. If the stream contains timestamps itself, this is also
   * supposed to be the presentation time stored inside the stream.
   *
   * The stream time over periods is supposed to be continuous, that is the
   * buffer timestamp 0 + presentation time offset should map to the start
   * time of the current period.
   *
   *
   * The adjustment of the stream segments as such works the following.
   *
   * If the demuxer segment start is bigger than the period start, this
   * means that we have to drop some media at the beginning of the current
   * period, e.g. because a seek into the middle of the period has
   * happened. The amount of media to drop is the difference between the
   * period start and the demuxer segment start, and as each period starts
   * again from 0, this difference is going to be the actual stream's
   * segment start. As all timestamps of the stream are shifted by the
   * presentation time offset, we will also have to move the segment start
   * by that offset.
   *
   * Likewise, the demuxer segment stop value is adjusted in the same
   * fashion.
   *
   * Now the running time and stream time at the stream's segment start has
   * to be the one that is stored inside the demuxer's segment, which means
   * that segment.base and segment.time have to be copied over (done just
   * above)
   *
   *
   * If the demuxer segment start is smaller than the period start time,
   * this means that the whole period is inside the segment. As each period
   * starts timestamps from 0, and additionally timestamps are shifted by
   * the presentation time offset, the stream's first timestamp (and as such
   * the stream's segment start) has to be the presentation time offset.
   * The stream time at the segment start is supposed to be the stream time
   * of the period start according to the demuxer segment, so the stream
   * segment's time would be set to that. The same goes for the stream
   * segment's base, which is supposed to be the running time of the period
   * start according to the demuxer's segment.
   *
   * The same logic applies for negative rates with the segment stop and
   * the period stop time (which gets clamped).
   *
   *
   * For the first case where not the complete period is inside the segment,
   * the segment time and base as calculated by the second case would be
   * equivalent.
   */
  GST_DEBUG_OBJECT (stream, "Using demux segment %" GST_SEGMENT_FORMAT,
      &stream->parse_segment);

  GST_DEBUG_OBJECT (demux,
      "period_start: %" GST_TIME_FORMAT " offset: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (period_start), GST_TIME_ARGS (offset));
  /* note for readers:
   * Since stream->parse_segment is initially a copy of demux->segment,
   * only the values that need updating are modified below. */
  if (first_and_live) {
    /* If first and live, demuxer did seek to the current position already */
    stream->parse_segment.start = demux->segment.start - period_start + offset;
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop))
      stream->parse_segment.stop = demux->segment.stop - period_start + offset;
    /* FIXME : Do we need to handle negative rates for this ? */
    stream->parse_segment.position = stream->parse_segment.start;
  } else if (demux->segment.start > period_start) {
    /* seek within a period */
    stream->parse_segment.start = demux->segment.start - period_start + offset;
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop))
      stream->parse_segment.stop = demux->segment.stop - period_start + offset;
    if (stream->parse_segment.rate >= 0)
      stream->parse_segment.position = offset;
    else
      stream->parse_segment.position = stream->parse_segment.stop;
  } else {
    stream->parse_segment.start = offset;
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop))
      stream->parse_segment.stop = demux->segment.stop - period_start + offset;
    if (stream->parse_segment.rate >= 0) {
      stream->parse_segment.position = offset;
      stream->parse_segment.base =
          gst_segment_to_running_time (&demux->segment, GST_FORMAT_TIME,
          period_start);
    } else {
      stream->parse_segment.position = stream->parse_segment.stop;
      stream->parse_segment.base =
          gst_segment_to_running_time (&demux->segment, GST_FORMAT_TIME,
          period_start + demux->segment.stop - demux->segment.start);
    }
    stream->parse_segment.time =
        gst_segment_to_stream_time (&demux->segment, GST_FORMAT_TIME,
        period_start);
  }

  stream->send_segment = TRUE;

  GST_DEBUG_OBJECT (stream, "Prepared segment %" GST_SEGMENT_FORMAT,
      &stream->parse_segment);
}

/* Segment lock hold */
static void
update_buffer_pts_and_demux_position_locked (GstAdaptiveDemux * demux,
    GstAdaptiveDemux2Stream * stream, GstBuffer * buffer)
{
  GstClockTimeDiff pos;

  GST_DEBUG_OBJECT (stream, "stream->fragment.stream_time %" GST_STIME_FORMAT,
      GST_STIME_ARGS (stream->fragment.stream_time));

  pos = stream->fragment.stream_time;

  if (GST_CLOCK_STIME_IS_VALID (pos)) {
    GstClockTime offset =
        gst_adaptive_demux2_stream_get_presentation_offset (stream);

    pos += offset;

    if (pos < 0) {
      GST_WARNING_OBJECT (stream, "Clamping segment and buffer position to 0");
      pos = 0;
    }

    GST_BUFFER_PTS (buffer) = pos;
  } else {
    GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG_OBJECT (stream, "Buffer/stream position is now: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));
}

/* Must be called from the scheduler context */
GstFlowReturn
gst_adaptive_demux2_stream_push_buffer (GstAdaptiveDemux2Stream * stream,
    GstBuffer * buffer)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean discont = FALSE;
  /* Pending events */
  GstEvent *pending_caps = NULL, *pending_segment = NULL, *pending_tags =
      NULL, *stream_start = NULL, *buffer_gap = NULL;
  GList *pending_events = NULL;

  if (stream->compute_segment) {
    gst_adaptive_demux2_stream_prepare_segment (stream, stream->first_and_live);
    stream->compute_segment = FALSE;
    stream->first_and_live = FALSE;
  }

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DROPPABLE)) {
    GST_DEBUG_OBJECT (stream, "Creating gap event for droppable buffer");
    buffer_gap =
        gst_event_new_gap (GST_BUFFER_PTS (buffer),
        GST_BUFFER_DURATION (buffer));
  }

  if (stream->first_fragment_buffer) {
    GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
    if (demux->segment.rate < 0)
      /* Set DISCONT flag for every first buffer in reverse playback mode
       * as each fragment for its own has to be reversed */
      discont = TRUE;
    update_buffer_pts_and_demux_position_locked (demux, stream, buffer);
    GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

    GST_LOG_OBJECT (stream, "Handling initial buffer %" GST_PTR_FORMAT, buffer);

    /* Do we need to inject STREAM_START and SEGMENT events ?
     *
     * This can happen when a stream is restarted, and also when switching to a
     * variant which needs a header (in which case downloading_header will be
     * TRUE)
     */
    if (G_UNLIKELY (stream->send_segment || stream->downloading_header)) {
      GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
      pending_segment = gst_event_new_segment (&stream->parse_segment);
      gst_event_set_seqnum (pending_segment, demux->priv->segment_seqnum);
      stream->send_segment = FALSE;
      GST_DEBUG_OBJECT (stream, "Sending %" GST_PTR_FORMAT, pending_segment);
      GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);
      stream_start = gst_event_new_stream_start ("bogus");
      if (demux->have_group_id)
        gst_event_set_group_id (stream_start, demux->group_id);
    }
  } else {
    GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  }
  stream->first_fragment_buffer = FALSE;

  if (stream->discont) {
    discont = TRUE;
    stream->discont = FALSE;
  }

  if (discont) {
    GST_DEBUG_OBJECT (stream, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  if (G_UNLIKELY (stream->pending_caps)) {
    pending_caps = gst_event_new_caps (stream->pending_caps);
    gst_caps_unref (stream->pending_caps);
    stream->pending_caps = NULL;
  }

  if (G_UNLIKELY (stream->pending_tags)) {
    GstTagList *tags = stream->pending_tags;

    stream->pending_tags = NULL;

    if (tags)
      pending_tags = gst_event_new_tag (tags);
  }
  if (G_UNLIKELY (stream->pending_events)) {
    pending_events = stream->pending_events;
    stream->pending_events = NULL;
  }

  /* Do not push events or buffers holding the manifest lock */
  if (G_UNLIKELY (stream_start)) {
    GST_DEBUG_OBJECT (stream,
        "Setting stream start: %" GST_PTR_FORMAT, stream_start);
    gst_pad_send_event (stream->parsebin_sink, stream_start);
  }
  if (G_UNLIKELY (pending_caps)) {
    GST_DEBUG_OBJECT (stream,
        "Setting pending caps: %" GST_PTR_FORMAT, pending_caps);
    gst_pad_send_event (stream->parsebin_sink, pending_caps);
  }
  if (G_UNLIKELY (pending_segment)) {
    GST_DEBUG_OBJECT (stream,
        "Sending pending seg: %" GST_PTR_FORMAT, pending_segment);
    gst_pad_send_event (stream->parsebin_sink, pending_segment);
  }
  if (G_UNLIKELY (pending_tags)) {
    GST_DEBUG_OBJECT (stream,
        "Sending pending tags: %" GST_PTR_FORMAT, pending_tags);
    gst_pad_send_event (stream->parsebin_sink, pending_tags);
  }
  while (pending_events != NULL) {
    GstEvent *event = pending_events->data;

    GST_DEBUG_OBJECT (stream, "Sending pending event: %" GST_PTR_FORMAT, event);
    if (!gst_pad_send_event (stream->parsebin_sink, event))
      GST_ERROR_OBJECT (stream, "Failed to send pending event");

    pending_events = g_list_delete_link (pending_events, pending_events);
  }

  GST_DEBUG_OBJECT (stream,
      "About to push buffer of size %" G_GSIZE_FORMAT " offset %"
      G_GUINT64_FORMAT, gst_buffer_get_size (buffer),
      GST_BUFFER_OFFSET (buffer));

  ret = gst_pad_chain (stream->parsebin_sink, buffer);

  if (buffer_gap) {
    GST_DEBUG_OBJECT (stream, "Sending %" GST_PTR_FORMAT, buffer_gap);
    gst_pad_send_event (stream->parsebin_sink, buffer_gap);
  }

  if (G_UNLIKELY (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED)) {
    GST_LOG_OBJECT (demux, "Stream was cancelled");
    return GST_FLOW_FLUSHING;
  }

  GST_LOG_OBJECT (stream, "Push result: %d %s", ret, gst_flow_get_name (ret));

  return ret;
}

static GstFlowReturn
gst_adaptive_demux2_stream_parse_buffer (GstAdaptiveDemux2Stream * stream,
    GstBuffer * buffer)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
  GstFlowReturn ret = GST_FLOW_OK;

  /* do not make any changes if the stream is cancelled */
  if (G_UNLIKELY (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED)) {
    GST_DEBUG_OBJECT (stream, "Stream was stopped. Aborting");
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

  /* starting_fragment is set to TRUE at the beginning of
   * _stream_download_fragment()
   * /!\ If there is a header/index being downloaded, then this will
   * be TRUE for the first one ... but FALSE for the remaining ones,
   * including the *actual* fragment ! */
  if (stream->starting_fragment) {
    stream->starting_fragment = FALSE;
    if (klass->start_fragment != NULL && !klass->start_fragment (stream))
      return GST_FLOW_ERROR;
  }

  stream->download_total_bytes += gst_buffer_get_size (buffer);

  GST_TRACE_OBJECT (stream,
      "Received %s buffer of size %" G_GSIZE_FORMAT, uritype (stream),
      gst_buffer_get_size (buffer));

  ret = klass->data_received (stream, buffer);

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (stream, "data_received returned %s",
        gst_flow_get_name (ret));

    if (ret == GST_FLOW_FLUSHING) {
      /* do not make any changes if the stream is cancelled */
      if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED) {
        GST_DEBUG_OBJECT (stream, "Stream was stopped. Aborting");
        return ret;
      }
    }

    if (ret < GST_FLOW_EOS) {
      GstEvent *eos = gst_event_new_eos ();
      GST_ELEMENT_FLOW_ERROR (demux, ret);

      GST_DEBUG_OBJECT (stream, "Pushing EOS to parser");

      /* TODO push this on all pads */
      gst_event_set_seqnum (eos, demux->priv->segment_seqnum);
      gst_pad_send_event (stream->parsebin_sink, eos);
      ret = GST_FLOW_ERROR;

      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_ERRORED;
    }
  }

  return ret;
}

/* Calculate the low and high download buffering watermarks
 * in time as MAX (low-watermark-time, low-watermark-fragments) and
 * MIN (high-watermark-time, high-watermark-fragments) respectively
 */
static void
calculate_track_thresholds (GstAdaptiveDemux * demux,
    GstAdaptiveDemux2Stream * stream,
    GstClockTime fragment_duration, GstClockTime * low_threshold,
    GstClockTime * high_threshold)
{
  GST_OBJECT_LOCK (demux);
  *low_threshold = demux->buffering_low_watermark_fragments * fragment_duration;
  if (*low_threshold == 0 ||
      (demux->buffering_low_watermark_time != 0
          && demux->buffering_low_watermark_time > *low_threshold)) {
    *low_threshold = demux->buffering_low_watermark_time;
  }

  if (*low_threshold == 0) {
    /* This implies both low level properties were 0, the default is 10s unless
     * the subclass has specified a recommended buffering threshold */
    *low_threshold = 10 * GST_SECOND;
    if (GST_CLOCK_TIME_IS_VALID (stream->recommended_buffering_threshold))
      *low_threshold =
          MIN (stream->recommended_buffering_threshold, *low_threshold);
  }

  *high_threshold =
      demux->buffering_high_watermark_fragments * fragment_duration;
  if (*high_threshold == 0 || (demux->buffering_high_watermark_time != 0
          && demux->buffering_high_watermark_time < *high_threshold)) {
    *high_threshold = demux->buffering_high_watermark_time;
  }

  /* Make sure the low and high thresholds are less than the maximum buffering
   * time */
  if (*high_threshold == 0 ||
      (demux->max_buffering_time != 0
          && demux->max_buffering_time < *high_threshold)) {
    *high_threshold = demux->max_buffering_time;
  }

  if (*low_threshold == 0 ||
      (demux->max_buffering_time != 0
          && demux->max_buffering_time < *low_threshold)) {
    *low_threshold = demux->max_buffering_time;
  }

  /* Make sure the high threshold is higher than (or equal to) the low threshold.
   * It's OK if they are the same, as the minimum download is 1 fragment */
  if (*high_threshold == 0 ||
      (*low_threshold != 0 && *low_threshold > *high_threshold)) {
    *high_threshold = *low_threshold;
  }

  GST_OBJECT_UNLOCK (demux);
}

#define ABSDIFF(a,b) ((a) < (b) ? (b) - (a) : (a) - (b))
static gboolean
gst_adaptive_demux2_stream_wait_for_output_space (GstAdaptiveDemux * demux,
    GstAdaptiveDemux2Stream * stream, GstClockTime fragment_duration)
{
  gboolean need_to_wait = TRUE;
  gboolean have_any_tracks = FALSE;
  gboolean have_active_tracks = FALSE;
  gboolean have_filled_inactive = FALSE;
  gboolean update_buffering = FALSE;

  GstClockTime low_threshold = 0, high_threshold = 0;
  GList *iter;

  calculate_track_thresholds (demux, stream, fragment_duration,
      &low_threshold, &high_threshold);
  GST_DEBUG_OBJECT (stream,
      "Thresholds low:%" GST_TIME_FORMAT " high:%" GST_TIME_FORMAT
      " recommended:%" GST_TIME_FORMAT, GST_TIME_ARGS (low_threshold),
      GST_TIME_ARGS (high_threshold),
      GST_TIME_ARGS (stream->recommended_buffering_threshold));

  /* If there are no tracks at all, don't wait. If there are no active
   * tracks, keep filling until at least one track is full. If there
   * are active tracks, require that they are all full */
  TRACKS_LOCK (demux);
  for (iter = stream->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;

    /* Update the buffering threshold if it changed by more than a second */
    if (ABSDIFF (low_threshold, track->buffering_threshold) > GST_SECOND) {
      GST_DEBUG_OBJECT (stream, "Updating threshold");
      /* The buffering threshold for this track changed, make sure to
       * re-check buffering status */
      update_buffering = TRUE;
      track->buffering_threshold = low_threshold;
    }

    have_any_tracks = TRUE;
    if (track->active)
      have_active_tracks = TRUE;

    if (track->level_time < high_threshold) {
      if (track->active) {
        need_to_wait = FALSE;
        GST_DEBUG_OBJECT (stream,
            "track %s has level %" GST_TIME_FORMAT
            " - needs more data (target %" GST_TIME_FORMAT
            ") (fragment duration %" GST_TIME_FORMAT ")",
            track->stream_id, GST_TIME_ARGS (track->level_time),
            GST_TIME_ARGS (high_threshold), GST_TIME_ARGS (fragment_duration));
        continue;
      }
    } else if (!track->active) {        /* track is over threshold and inactive */
      have_filled_inactive = TRUE;
    }

    GST_DEBUG_OBJECT (stream,
        "track %s active (%d) has level %" GST_TIME_FORMAT,
        track->stream_id, track->active, GST_TIME_ARGS (track->level_time));
  }

  /* If there are no tracks, don't wait (we might need data to create them),
   * or if there are active tracks that need more data to hit the threshold,
   * don't wait. Otherwise it means all active tracks are full and we should wait */
  if (!have_any_tracks) {
    GST_DEBUG_OBJECT (stream, "no tracks created yet - not waiting");
    need_to_wait = FALSE;
  } else if (!have_active_tracks && !have_filled_inactive) {
    GST_DEBUG_OBJECT (stream,
        "have only inactive tracks that need more data - not waiting");
    need_to_wait = FALSE;
  }

  if (need_to_wait) {
    stream->next_input_wakeup_time = GST_CLOCK_STIME_NONE;

    for (iter = stream->tracks; iter; iter = iter->next) {
      GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;

      GST_DEBUG_OBJECT (stream,
          "Waiting for queued data on track %s to drop below %"
          GST_TIME_FORMAT " (fragment duration %" GST_TIME_FORMAT ")",
          track->stream_id, GST_TIME_ARGS (high_threshold),
          GST_TIME_ARGS (fragment_duration));

      /* we want to get woken up when the global output position reaches
       * a point where the input is closer than "high_threshold" to needing
       * output, so we can put more data in */
      GstClockTimeDiff wakeup_time = track->input_time - high_threshold;

      if (stream->next_input_wakeup_time == GST_CLOCK_STIME_NONE ||
          wakeup_time < stream->next_input_wakeup_time) {
        stream->next_input_wakeup_time = wakeup_time;

        GST_DEBUG_OBJECT (stream,
            "Track %s level %" GST_TIME_FORMAT ". Input at position %"
            GST_TIME_FORMAT " next wakeup should be %" GST_TIME_FORMAT " now %"
            GST_TIME_FORMAT, track->stream_id,
            GST_TIME_ARGS (track->level_time),
            GST_TIME_ARGS (track->input_time), GST_TIME_ARGS (wakeup_time),
            GST_TIME_ARGS (demux->priv->global_output_position));
      }
    }

    if (stream->next_input_wakeup_time != GST_CLOCK_TIME_NONE) {
      GST_DEBUG_OBJECT (stream,
          "Next input wakeup time is now %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->next_input_wakeup_time));

      /* If this stream needs waking up sooner than any other current one,
       * update the period wakeup time, which is what the output loop
       * will check */
      GstAdaptiveDemuxPeriod *period = stream->period;
      if (period->next_input_wakeup_time == GST_CLOCK_STIME_NONE ||
          period->next_input_wakeup_time > stream->next_input_wakeup_time) {
        period->next_input_wakeup_time = stream->next_input_wakeup_time;
      }
    }
  }

  if (update_buffering) {
    demux_update_buffering_locked (demux);
    demux_post_buffering_locked (demux);
  }

  TRACKS_UNLOCK (demux);

  return need_to_wait;
}

static GstAdaptiveDemuxTrack *
match_parsebin_to_track (GstAdaptiveDemux2Stream * stream, GstPad * pad)
{
  GList *tmp;
  GstAdaptiveDemuxTrack *found_track = NULL, *first_matched_track = NULL;
  gint num_possible_tracks = 0;
  GstStream *gst_stream;
  const gchar *internal_stream_id;
  GstStreamType stream_type;

  gst_stream = gst_pad_get_stream (pad);

  /* FIXME: Edward: Added assertion because I don't see in what cases we would
   * end up with a pad from parsebin which wouldn't have an associated
   * GstStream. */
  g_assert (gst_stream);

  internal_stream_id = gst_stream_get_stream_id (gst_stream);
  stream_type = gst_stream_get_stream_type (gst_stream);

  GST_DEBUG_OBJECT (pad,
      "Trying to match pad from parsebin with internal streamid %s and stream %"
      GST_PTR_FORMAT, GST_STR_NULL (internal_stream_id), gst_stream);

  /* Try to match directly by the track's pending upstream_stream_id */
  for (tmp = stream->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) tmp->data;

    if (stream_type != GST_STREAM_TYPE_UNKNOWN && track->type != stream_type)
      continue;

    GST_DEBUG_OBJECT (pad, "track upstream_stream_id: %s",
        track->upstream_stream_id);

    if (first_matched_track == NULL)
      first_matched_track = track;
    num_possible_tracks++;

    /* If this track has a desired upstream stream id, match on it */
    if (track->upstream_stream_id == NULL ||
        g_strcmp0 (track->upstream_stream_id, internal_stream_id)) {
      /* This is not the track for this pad */
      continue;
    }

    /* Remove pending upstream id (we have matched it for the pending
     * stream_id) */
    g_free (track->upstream_stream_id);
    track->upstream_stream_id = NULL;
    found_track = track;
    break;
  }

  if (found_track == NULL) {
    /* If we arrive here, it means the stream is switching pads after
     * the stream has already started running */
    /* No track is currently waiting for this particular stream id -
     * try and match an existing linked track. If there's only 1 possible,
     * take it. */
    if (num_possible_tracks == 1 && first_matched_track != NULL) {
      GST_LOG_OBJECT (pad, "Only one possible track to link to");
      found_track = first_matched_track;
    }
  }

  if (found_track == NULL) {
    /* TODO: There are multiple possible tracks, need to match based
     * on language code and caps. Have you found a stream like this? */
    GST_FIXME_OBJECT (pad, "Need to match track based on caps and language");
  }

  if (found_track != NULL) {
    if (!gst_pad_is_linked (found_track->sinkpad)) {
      GST_LOG_OBJECT (pad, "Linking to track pad %" GST_PTR_FORMAT,
          found_track->sinkpad);

      if (gst_pad_link (pad, found_track->sinkpad) != GST_PAD_LINK_OK) {
        GST_ERROR_OBJECT (pad, "Couldn't connect to track sinkpad");
        /* FIXME : Do something if we can't link ? */
      }
    } else {
      /* Store pad as pending link */
      GST_LOG_OBJECT (pad,
          "Remembering pad to be linked when current pad is unlinked");
      g_assert (found_track->pending_srcpad == NULL);
      found_track->pending_srcpad = gst_object_ref (pad);
    }
  }

  if (gst_stream)
    gst_object_unref (gst_stream);

  return found_track;
}

static void
parsebin_pad_removed_cb (GstElement * parsebin, GstPad * pad,
    GstAdaptiveDemux2Stream * stream)
{
  GList *iter;
  GST_DEBUG_OBJECT (stream, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* Remove from pending source pad */
  TRACKS_LOCK (stream->demux);
  for (iter = stream->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = iter->data;
    if (track->pending_srcpad == pad) {
      gst_object_unref (track->pending_srcpad);
      track->pending_srcpad = NULL;
      break;
    }
  }
  TRACKS_UNLOCK (stream->demux);
}

static void
parsebin_pad_added_cb (GstElement * parsebin, GstPad * pad,
    GstAdaptiveDemux2Stream * stream)
{
  if (!GST_PAD_IS_SRC (pad))
    return;

  GST_DEBUG_OBJECT (stream, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (!match_parsebin_to_track (stream, pad))
    GST_WARNING_OBJECT (pad, "Found no track to handle pad");

  GST_DEBUG_OBJECT (stream->demux, "Done linking");
}

static void
parsebin_deep_element_added_cb (GstBin * parsebin, GstBin * unused,
    GstElement * element, GstAdaptiveDemux * demux)
{
  if (G_OBJECT_TYPE (element) == tsdemux_type) {
    GST_DEBUG_OBJECT (demux, "Overriding tsdemux ignore-pcr to TRUE");
    g_object_set (element, "ignore-pcr", TRUE, NULL);
  }
}

/* must be called with manifest_lock taken */
static gboolean
gst_adaptive_demux2_stream_create_parser (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (stream->parsebin == NULL) {
    GstEvent *event;

    GST_DEBUG_OBJECT (demux, "Setting up new parsing source");

    /* Workaround to detect if tsdemux is being used */
    if (tsdemux_type == 0) {
      GstElement *element = gst_element_factory_make ("tsdemux", NULL);
      if (element) {
        tsdemux_type = G_OBJECT_TYPE (element);
        gst_object_unref (element);
      }
    }

    stream->parsebin = gst_element_factory_make ("parsebin", NULL);
    if (tsdemux_type)
      g_signal_connect (stream->parsebin, "deep-element-added",
          (GCallback) parsebin_deep_element_added_cb, demux);
    gst_bin_add (GST_BIN_CAST (demux), gst_object_ref (stream->parsebin));
    stream->parsebin_sink =
        gst_element_get_static_pad (stream->parsebin, "sink");
    stream->pad_added_id = g_signal_connect (stream->parsebin, "pad-added",
        G_CALLBACK (parsebin_pad_added_cb), stream);
    stream->pad_removed_id = g_signal_connect (stream->parsebin, "pad-removed",
        G_CALLBACK (parsebin_pad_removed_cb), stream);

    event = gst_event_new_stream_start ("bogus");
    if (demux->have_group_id)
      gst_event_set_group_id (event, demux->group_id);

    gst_pad_send_event (stream->parsebin_sink, event);

    /* Not sure if these need to be outside the manifest lock: */
    gst_element_sync_state_with_parent (stream->parsebin);
    stream->last_status_code = 200;     /* default to OK */
  }
  return TRUE;
}

static void
on_download_cancellation (DownloadRequest * request, DownloadRequestState state,
    GstAdaptiveDemux2Stream * stream)
{
}

static void
on_download_error (DownloadRequest * request, DownloadRequestState state,
    GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  guint last_status_code = request->status_code;
  gboolean live;

  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING) {
    GST_DEBUG_OBJECT (stream, "Stream state changed to %d. Aborting",
        stream->state);
    return;
  }

  stream->download_active = FALSE;
  stream->last_status_code = last_status_code;

  live = gst_adaptive_demux_is_live (demux);

  GST_DEBUG_OBJECT (stream,
      "Download finished with error, request state %d http status %u, dc %d "
      "live %d retried %d",
      request->state, last_status_code,
      stream->download_error_count, live, stream->download_error_retry);

  if (!stream->download_error_retry && ((last_status_code / 100 == 4 && live)
          || last_status_code / 100 == 5)) {
    /* 4xx/5xx */
    /* if current position is before available start, switch to next */
    if (live) {
      gint64 range_start, range_stop;

      if (gst_adaptive_demux_get_live_seek_range (demux, &range_start,
              &range_stop)) {
        if (demux->segment.position < range_start) {
          /* This should advance into the valid playlist range */
          GST_DEBUG_OBJECT (stream, "Retrying once with next segment");
          stream->download_error_retry = TRUE;
          gst_adaptive_demux2_stream_finish_download (stream, GST_FLOW_OK,
              NULL);
          return;
        } else if (demux->segment.position > range_stop) {
          /* wait a bit to be in range */
          GstClockTime wait_time =
              gst_adaptive_demux2_stream_get_fragment_waiting_time (stream);
          if (wait_time > 0) {
            GST_DEBUG_OBJECT (stream,
                "Download waiting for %" GST_TIME_FORMAT,
                GST_TIME_ARGS (wait_time));
            g_assert (stream->pending_cb_id == 0);
            GST_LOG_OBJECT (stream,
                "Scheduling delayed load_a_fragment() call");
            stream->pending_cb_id =
                gst_adaptive_demux_loop_call_delayed (demux->
                priv->scheduler_task, wait_time,
                (GSourceFunc) gst_adaptive_demux2_stream_load_a_fragment,
                gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
            return;
          }
        } else {
          GST_LOG_OBJECT (stream,
              "Failed segment is inside the live range, retrying");
        }
      } else {
        GST_LOG_OBJECT (stream, "Could not get live seek range after error");
      }
    }

    if (stream->download_error_count >= MAX_DOWNLOAD_ERROR_COUNT) {
      /* looks like there is no way of knowing when a live stream has ended
       * Have to assume we are falling behind and cause a manifest reload */
      GST_DEBUG_OBJECT (stream, "Converting error of live stream to EOS");
      gst_adaptive_demux2_stream_handle_playlist_eos (stream);
      return;
    }
  } else if (!gst_adaptive_demux2_stream_has_next_fragment (stream)) {
    /* If this is the last fragment, consider failures EOS and not actual
     * errors. Due to rounding errors in the durations, the last fragment
     * might not actually exist */
    GST_DEBUG_OBJECT (stream, "Converting error for last fragment to EOS");
    gst_adaptive_demux2_stream_handle_playlist_eos (stream);
    return;
  } else {
    /* retry same segment */
    if (++stream->download_error_count > MAX_DOWNLOAD_ERROR_COUNT) {
      gst_adaptive_demux2_stream_error (stream);
      return;
    }
    goto again;
  }

again:
  /* wait a short time in case the server needs a bit to recover */
  GST_LOG_OBJECT (stream,
      "Scheduling delayed load_a_fragment() call to retry in 10 milliseconds");
  g_assert (stream->pending_cb_id == 0);
  stream->pending_cb_id = gst_adaptive_demux_loop_call_delayed (demux->priv->scheduler_task, 10 * GST_MSECOND,  /* Retry in 10 ms */
      (GSourceFunc) gst_adaptive_demux2_stream_load_a_fragment,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

static void
update_stream_bitrate (GstAdaptiveDemux2Stream * stream,
    DownloadRequest * request)
{
  GstClockTimeDiff last_download_duration;
  guint64 fragment_bytes_downloaded = request->content_received;

  /* The stream last_download time tracks the full download time for now */
  stream->last_download_time =
      GST_CLOCK_DIFF (request->download_request_time,
      request->download_end_time);

  /* Here we only track the time the data took to arrive and ignore request delay, so we can estimate bitrate */
  last_download_duration =
      GST_CLOCK_DIFF (request->download_start_time, request->download_end_time);

  /* If the entire response arrived in the first buffer
   * though, include the request time to get a valid
   * bitrate estimate */
  if (last_download_duration < 2 * stream->last_download_time)
    last_download_duration = stream->last_download_time;

  if (last_download_duration > 0) {
    stream->last_bitrate =
        gst_util_uint64_scale (fragment_bytes_downloaded,
        8 * GST_SECOND, last_download_duration);

    GST_DEBUG_OBJECT (stream,
        "Updated stream bitrate. %" G_GUINT64_FORMAT
        " bytes. download time %" GST_TIME_FORMAT " bitrate %"
        G_GUINT64_FORMAT " bps", fragment_bytes_downloaded,
        GST_TIME_ARGS (last_download_duration), stream->last_bitrate);
  }
}

static void
on_download_progress (DownloadRequest * request, DownloadRequestState state,
    GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstBuffer *buffer = download_request_take_buffer (request);

  if (buffer) {
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (stream,
        "Handling buffer of %" G_GSIZE_FORMAT
        " bytes of ongoing download progress - %" G_GUINT64_FORMAT " / %"
        G_GUINT64_FORMAT " bytes", gst_buffer_get_size (buffer),
        request->content_received, request->content_length);

    /* Drop the request lock when parsing data. That allows the DownloadHelper to
     * add more data while we're parsing (if more arrives) */
    download_request_unlock (request);
    ret = gst_adaptive_demux2_stream_parse_buffer (stream, buffer);
    download_request_lock (request);

    if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING)
      return;

    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (stream,
          "Buffer parsing returned: %d %s. Aborting download", ret,
          gst_flow_get_name (ret));

      if (!stream->downloading_header && !stream->downloading_index)
        update_stream_bitrate (stream, request);

      downloadhelper_cancel_request (demux->download_helper, request);

      /* cancellation is async, so recycle our download request to avoid races */
      download_request_unref (stream->download_request);
      stream->download_request = download_request_new ();

      gst_adaptive_demux2_stream_finish_download (stream, ret, NULL);
    }
  }
}

static void
on_download_complete (DownloadRequest * request, DownloadRequestState state,
    GstAdaptiveDemux2Stream * stream)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer;

  stream->download_active = FALSE;
  stream->download_error_retry = FALSE;

  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING) {
    GST_DEBUG_OBJECT (stream, "Stream state changed to %d. Aborting",
        stream->state);
    return;
  }

  GST_DEBUG_OBJECT (stream,
      "Stream %p %s download for %s is complete with state %d",
      stream, uritype (stream), request->uri, request->state);

  /* Update bitrate for fragment downloads */
  if (!stream->downloading_header && !stream->downloading_index)
    update_stream_bitrate (stream, request);

  buffer = download_request_take_buffer (request);
  if (buffer)
    ret = gst_adaptive_demux2_stream_parse_buffer (stream, buffer);

  GST_DEBUG_OBJECT (stream,
      "%s download finished: %s ret %d %s. Stream state %d", uritype (stream),
      request->uri, ret, gst_flow_get_name (ret), stream->state);

  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING)
    return;

  g_assert (stream->pending_cb_id == 0);
  gst_adaptive_demux2_stream_finish_download (stream, ret, NULL);
}

static GstFlowReturn
gst_adaptive_demux2_stream_submit_request_default (GstAdaptiveDemux2Stream *
    stream, DownloadRequest * download_req)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (!downloadhelper_submit_request (demux->download_helper,
          NULL, DOWNLOAD_FLAG_NONE, download_req, NULL))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_adaptive_demux2_stream_submit_request (GstAdaptiveDemux2Stream * stream,
    DownloadRequest * download_req)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
  g_assert (klass->submit_request != NULL);
  return klass->submit_request (stream, download_req);
}

/* must be called from the scheduler context
 *
 * Will submit the request only, which will complete asynchronously
 */
static GstFlowReturn
gst_adaptive_demux2_stream_begin_download_uri (GstAdaptiveDemux2Stream * stream,
    const gchar * uri, gint64 start, gint64 end)
{
  DownloadRequest *request = stream->download_request;

  GST_DEBUG_OBJECT (stream,
      "Downloading %s uri: %s, range:%" G_GINT64_FORMAT " - %" G_GINT64_FORMAT,
      uritype (stream), uri, start, end);

  if (!gst_adaptive_demux2_stream_create_parser (stream))
    return GST_FLOW_ERROR;

  /* Configure our download request */
  download_request_set_uri (request, uri, start, end);

  if (stream->downloading_header || stream->downloading_index) {
    download_request_set_callbacks (request,
        (DownloadRequestEventCallback) on_download_complete,
        (DownloadRequestEventCallback) on_download_error,
        (DownloadRequestEventCallback) on_download_cancellation,
        (DownloadRequestEventCallback) NULL, stream);
  } else {
    download_request_set_callbacks (request,
        (DownloadRequestEventCallback) on_download_complete,
        (DownloadRequestEventCallback) on_download_error,
        (DownloadRequestEventCallback) on_download_cancellation,
        (DownloadRequestEventCallback) on_download_progress, stream);
  }


  stream->download_active = TRUE;
  GstFlowReturn ret =
      gst_adaptive_demux2_stream_submit_request (stream, request);
  if (ret != GST_FLOW_OK) {
    stream->download_active = FALSE;
  }

  return ret;
}

/* must be called from the scheduler context */
static GstFlowReturn
gst_adaptive_demux2_stream_download_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
  gchar *url = NULL;

  /* FIXME :  */
  /* THERE ARE THREE DIFFERENT VARIABLES FOR THE "BEGINNING" OF A FRAGMENT ! */
  if (stream->starting_fragment) {
    GST_DEBUG_OBJECT (stream, "Downloading %s%s%s",
        stream->fragment.uri ? "FRAGMENT " : "",
        stream->need_header && stream->fragment.header_uri ? "HEADER " : "",
        stream->need_index && stream->fragment.index_uri ? "INDEX" : "");

    if (stream->fragment.uri == NULL && stream->fragment.header_uri == NULL &&
        stream->fragment.index_uri == NULL)
      goto no_url_error;

    stream->first_fragment_buffer = TRUE;
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING;
  }

  if (stream->need_header && stream->fragment.header_uri != NULL) {

    /* Set the need_index flag when we start the header if we'll also need the index */
    stream->need_index = (stream->fragment.index_uri != NULL);

    GST_DEBUG_OBJECT (stream, "Fetching header %s %" G_GINT64_FORMAT "-%"
        G_GINT64_FORMAT, stream->fragment.header_uri,
        stream->fragment.header_range_start, stream->fragment.header_range_end);

    stream->downloading_header = TRUE;

    return gst_adaptive_demux2_stream_begin_download_uri (stream,
        stream->fragment.header_uri, stream->fragment.header_range_start,
        stream->fragment.header_range_end);
  }

  /* check if we have an index */
  if (stream->need_index && stream->fragment.index_uri != NULL) {
    GST_DEBUG_OBJECT (stream,
        "Fetching index %s %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT,
        stream->fragment.index_uri,
        stream->fragment.index_range_start, stream->fragment.index_range_end);

    stream->downloading_index = TRUE;

    return gst_adaptive_demux2_stream_begin_download_uri (stream,
        stream->fragment.index_uri, stream->fragment.index_range_start,
        stream->fragment.index_range_end);
  }

  url = stream->fragment.uri;
  GST_DEBUG_OBJECT (stream, "Got url '%s' for stream %p", url, stream);
  if (!url)
    return GST_FLOW_OK;

  /* Download the actual fragment, either in chunks or in one go */
  stream->first_fragment_buffer = TRUE;

  if (klass->need_another_chunk && klass->need_another_chunk (stream)
      && stream->fragment.chunk_size != 0) {
    /* Handle chunk downloading */
    gint64 range_start = stream->fragment.range_start;
    gint64 range_end = stream->fragment.range_end;
    gint chunk_size = stream->fragment.chunk_size;
    gint64 chunk_end;

    /* HTTP ranges are inclusive for the end */
    if (chunk_size != -1) {
      chunk_end = range_start + chunk_size - 1;
      if (range_end != -1 && range_end < chunk_end)
        chunk_end = range_end;
    } else {
      chunk_end = range_end;
    }

    GST_DEBUG_OBJECT (stream,
        "Starting chunked download %s %" G_GINT64_FORMAT "-%" G_GINT64_FORMAT,
        url, range_start, chunk_end);
    return gst_adaptive_demux2_stream_begin_download_uri (stream, url,
        range_start, chunk_end);
  }

  /* regular single chunk download */
  stream->fragment.chunk_size = 0;

  return gst_adaptive_demux2_stream_begin_download_uri (stream, url,
      stream->fragment.range_start, stream->fragment.range_end);

no_url_error:
  {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX,
        (_("Failed to get fragment URL.")),
        ("An error happened when getting fragment URL"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_adaptive_demux2_stream_push_event (GstAdaptiveDemux2Stream * stream,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstPad *pad;

  /* If there's a parsebin, push the event through it */
  if (stream->parsebin_sink != NULL) {
    pad = gst_object_ref (stream->parsebin_sink);
    GST_DEBUG_OBJECT (pad, "Pushing event %" GST_PTR_FORMAT, event);
    ret = gst_pad_send_event (pad, gst_event_ref (event));
    gst_object_unref (pad);
  }

  /* If the event is EOS, ensure that all tracks are EOS. This catches
   * the case where the parsebin hasn't parsed anything yet (we switched
   * to a never before used track right near EOS, or it didn't parse enough
   * to create pads and be able to send EOS through to the tracks.
   *
   * We don't need to care about any other events
   */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GList *iter;

    for (iter = stream->tracks; iter; iter = iter->next) {
      GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;
      ret &= gst_pad_send_event (track->sinkpad, gst_event_ref (event));
    }
  }

  gst_event_unref (event);
  return ret;
}

static void
gst_adaptive_demux2_stream_error (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstMessage *msg;
  GstStructure *details;

  details = gst_structure_new_empty ("details");
  gst_structure_set (details, "http-status-code", G_TYPE_UINT,
      stream->last_status_code, NULL);

  stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_ERRORED;

  if (stream->last_error) {
    gchar *debug = g_strdup_printf ("Error on stream %s",
        GST_OBJECT_NAME (stream));
    msg =
        gst_message_new_error_with_details (GST_OBJECT_CAST (demux),
        stream->last_error, debug, details);
    GST_ERROR_OBJECT (stream, "Download error: %s",
        stream->last_error->message);
    g_free (debug);
  } else {
    GError *err = g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND,
        _("Couldn't download fragments"));
    msg =
        gst_message_new_error_with_details (GST_OBJECT_CAST (demux), err,
        "Fragment downloading has failed consecutive times", details);
    g_error_free (err);
    GST_ERROR_OBJECT (stream,
        "Download error: Couldn't download fragments, too many failures");
  }

  gst_element_post_message (GST_ELEMENT_CAST (demux), msg);
}

/* Called when a stream reaches the end of a playback segment */
static void
gst_adaptive_demux2_stream_end_of_manifest (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  GstFlowReturn combined =
      gst_adaptive_demux_period_combine_stream_flows (demux->input_period);

  GST_DEBUG_OBJECT (stream, "Combined flow %s", gst_flow_get_name (combined));

  if (gst_adaptive_demux_has_next_period (demux)) {
    if (combined == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (stream, "Next period available, advancing");
      gst_adaptive_demux_advance_period (demux);
    } else {
      /* Ensure the 'has_next_period' flag is set on the period before
       * pushing EOS to the stream, so that the output loop knows not
       * to actually output the event */
      GST_DEBUG_OBJECT (stream, "Marking current period has a next one");
      demux->input_period->has_next_period = TRUE;
    }
  }

  if (demux->priv->outputs) {
    GstEvent *eos = gst_event_new_eos ();

    GST_DEBUG_OBJECT (stream, "Stream is EOS. Stopping.");
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS;

    gst_event_set_seqnum (eos, stream->demux->priv->segment_seqnum);
    gst_adaptive_demux2_stream_push_event (stream, eos);
  } else {
    GST_ERROR_OBJECT (demux, "Can't push EOS on non-exposed pad");
    gst_adaptive_demux2_stream_error (stream);
  }
}

static gboolean
gst_adaptive_demux2_stream_reload_manifest_cb (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  gboolean is_live = gst_adaptive_demux_is_live (demux);

  stream->pending_cb_id = 0;

  /* Refetch the playlist now after we waited */
  /* FIXME: Make this manifest update async and handle it on completion */
  if (!is_live && gst_adaptive_demux_update_manifest (demux) == GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "Updated the playlist");
  }

  /* We were called here from a timeout, so if the load function wants to loop
   * again, schedule an immediate callback but return G_SOURCE_REMOVE either
   * way */
  while (gst_adaptive_demux2_stream_next_download (stream));

  return G_SOURCE_REMOVE;
}

static gboolean
gst_adaptive_demux2_stream_on_output_space_available_cb (GstAdaptiveDemux2Stream
    * stream)
{
  /* If the state already moved on, the stream was stopped, or another track
   * already woke up and needed data */
  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_OUTPUT_SPACE)
    return G_SOURCE_REMOVE;

  GstAdaptiveDemux *demux = stream->demux;
  TRACKS_LOCK (demux);

  GList *iter;
  for (iter = stream->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = (GstAdaptiveDemuxTrack *) iter->data;

    /* We need to recompute the track's level_time value, as the
     * global output position may have advanced and reduced the
     * value, even without anything being dequeued yet */
    gst_adaptive_demux_track_update_level_locked (track);

    GST_DEBUG_OBJECT (stream, "track %s woken level %" GST_TIME_FORMAT
        " input position %" GST_TIME_FORMAT " at %" GST_TIME_FORMAT,
        track->stream_id, GST_TIME_ARGS (track->level_time),
        GST_TIME_ARGS (track->input_time),
        GST_TIME_ARGS (demux->priv->global_output_position));
  }
  TRACKS_UNLOCK (demux);

  while (gst_adaptive_demux2_stream_load_a_fragment (stream));

  return G_SOURCE_REMOVE;
}

void
gst_adaptive_demux2_stream_on_output_space_available (GstAdaptiveDemux2Stream *
    stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  stream->next_input_wakeup_time = GST_CLOCK_STIME_NONE;

  GST_LOG_OBJECT (stream, "Scheduling output_space_available() call");

  gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_stream_on_output_space_available_cb,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

void
gst_adaptive_demux2_stream_on_manifest_update (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_MANIFEST_UPDATE)
    return;

  g_assert (stream->pending_cb_id == 0);

  GST_LOG_OBJECT (stream, "Scheduling load_a_fragment() call");
  stream->pending_cb_id =
      gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_stream_load_a_fragment,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

void
gst_adaptive_demux2_stream_on_can_download_fragments (GstAdaptiveDemux2Stream *
    stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_BEFORE_DOWNLOAD)
    return;

  g_assert (stream->pending_cb_id == 0);

  GST_LOG_OBJECT (stream, "Scheduling load_a_fragment() call");
  stream->pending_cb_id =
      gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_stream_load_a_fragment,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

/*
 * Called by a subclass that has returned GST_ADAPTIVE_DEMUX_FLOW_BUSY
 * from update_fragment_info() to indicate that it is ready to continue
 * downloading now.
 *
 * Called from the scheduler task
 */
void
gst_adaptive_demux2_stream_mark_prepared (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  /* hlsdemux calls this method whenever a playlist is updated, so also
   * use it to wake up a stream that's waiting at the live edge */
  if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_MANIFEST_UPDATE) {
    gst_adaptive_demux2_stream_on_manifest_update (stream);
  }

  g_cond_broadcast (&stream->prepare_cond);
  if (stream->state != GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_PREPARE)
    return;

  g_assert (stream->pending_cb_id == 0);

  GST_LOG_OBJECT (stream, "Scheduling load_a_fragment() call");
  stream->pending_cb_id =
      gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_stream_load_a_fragment,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

/* Called by external threads (manifest input on sinkpad, and seek handling)
 * when it requires the stream to be prepared before they can continue
 * Must be held with the SCHEDULER lock held */
gboolean
gst_adaptive_demux2_stream_wait_prepared (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  g_mutex_lock (&stream->prepare_lock);
  GST_ADAPTIVE_SCHEDULER_UNLOCK (demux);
  g_cond_wait (&stream->prepare_cond, &stream->prepare_lock);
  g_mutex_unlock (&stream->prepare_lock);

  return GST_ADAPTIVE_SCHEDULER_LOCK (demux);
}

static void
gst_adaptive_demux2_stream_handle_playlist_eos (GstAdaptiveDemux2Stream *
    stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (gst_adaptive_demux_is_live (demux) && (demux->segment.rate == 1.0
          || gst_adaptive_demux2_stream_in_live_seek_range (demux, stream))) {

    if (!gst_adaptive_demux_has_next_period (demux)) {
      /* Wait only if we can ensure current manifest has been expired.
       * The meaning "we have next period" *WITH* EOS is that, current
       * period has been ended but we can continue to the next period */
      GST_DEBUG_OBJECT (stream,
          "Live playlist EOS - waiting for manifest update");
      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_MANIFEST_UPDATE;
      /* Clear the stream last_ret EOS state, since we're not actually EOS */
      if (stream->last_ret == GST_FLOW_EOS)
        stream->last_ret = GST_FLOW_OK;
      gst_adaptive_demux2_stream_wants_manifest_update (demux);
      return;
    }
  }

  gst_adaptive_demux2_stream_end_of_manifest (stream);
}

static gboolean
gst_adaptive_demux2_stream_load_a_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  gboolean live = gst_adaptive_demux_is_live (demux);
  GstFlowReturn ret = GST_FLOW_OK;

  stream->pending_cb_id = 0;

  GST_LOG_OBJECT (stream, "entering, state = %d.", stream->state);

  switch (stream->state) {
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART:
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT:
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_PREPARE:
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_LIVE:
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_OUTPUT_SPACE:
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_MANIFEST_UPDATE:
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_BEFORE_DOWNLOAD:
      /* Get information about the fragment to download */
      GST_DEBUG_OBJECT (demux, "Calling update_fragment_info");
      ret = gst_adaptive_demux2_stream_update_fragment_info (stream);
      GST_DEBUG_OBJECT (stream,
          "Fragment info update result: %d %s", ret, gst_flow_get_name (ret));

      if (ret == GST_FLOW_OK) {
        /* Wake anyone that's waiting for this stream to get prepared */
        if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_PREPARE)
          g_cond_broadcast (&stream->prepare_cond);
        stream->starting_fragment = TRUE;
      }
      break;
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_DOWNLOADING:
      break;
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS:
      GST_ERROR_OBJECT (stream,
          "Unexpected stream state EOS. The stream should not be running now.");
      return FALSE;
    case GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED:
      /* The stream was stopped. Just finish up */
      return FALSE;
    default:
      GST_ERROR_OBJECT (stream, "Unexpected stream state %d", stream->state);
      g_assert_not_reached ();
      break;
  }

  if (ret == GST_ADAPTIVE_DEMUX_FLOW_BUSY) {
    GST_LOG_OBJECT (stream,
        "Sub-class returned BUSY flow return. Waiting in PREPARE state");
    /* Need to take the prepare lock specifically when switching
     * to WAITING_PREPARE state, to avoid a race in _wait_prepared();
     */
    g_mutex_lock (&stream->prepare_lock);
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_PREPARE;
    g_mutex_unlock (&stream->prepare_lock);
    return FALSE;
  }

  if (ret == GST_FLOW_OK) {
    /* Wait for room in the output tracks */
    if (gst_adaptive_demux2_stream_wait_for_output_space (demux, stream,
            stream->fragment.duration)) {
      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_OUTPUT_SPACE;
      return FALSE;
    }
  }

  if (ret == GST_FLOW_OK) {
    /* wait for live fragments to be available */
    if (live) {
      GstClockTime wait_time =
          gst_adaptive_demux2_stream_get_fragment_waiting_time (stream);
      if (wait_time > 0) {
        GST_DEBUG_OBJECT (stream,
            "Download waiting for %" GST_TIME_FORMAT,
            GST_TIME_ARGS (wait_time));

        stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_LIVE;

        GST_LOG_OBJECT (stream, "Scheduling delayed load_a_fragment() call");
        g_assert (stream->pending_cb_id == 0);
        stream->pending_cb_id =
            gst_adaptive_demux_loop_call_delayed (demux->priv->scheduler_task,
            wait_time, (GSourceFunc) gst_adaptive_demux2_stream_load_a_fragment,
            gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
        return FALSE;
      }
    }
  }

  if (ret == GST_FLOW_OK) {
    if (!demux->priv->streams_can_download_fragments) {
      GST_LOG_OBJECT (stream, "Waiting for fragment downloads to be unblocked");
      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_WAITING_BEFORE_DOWNLOAD;
      return FALSE;
    }
  }

  /* Cast to int avoids a compiler warning that
   * GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC is not in the GstFlowReturn enum */
  switch ((int) ret) {
    case GST_FLOW_OK:
      /* all is good, let's go */
      if (gst_adaptive_demux2_stream_download_fragment (stream) != GST_FLOW_OK) {
        GST_ERROR_OBJECT (demux,
            "Failed to begin fragment download for stream %p", stream);
        return FALSE;
      }
      break;
    case GST_FLOW_EOS:
      GST_DEBUG_OBJECT (stream, "EOS, checking to stop download loop");
      stream->last_ret = ret;
      gst_adaptive_demux2_stream_handle_playlist_eos (stream);
      return FALSE;
    case GST_ADAPTIVE_DEMUX_FLOW_LOST_SYNC:
      GST_DEBUG_OBJECT (stream, "Lost sync, asking reset to current position");
      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED;
      g_cond_broadcast (&stream->prepare_cond);
      gst_adaptive_demux_handle_lost_sync (demux);
      return FALSE;
    case GST_FLOW_NOT_LINKED:
    {
      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS;

      if (gst_adaptive_demux_period_combine_stream_flows (demux->input_period)
          == GST_FLOW_NOT_LINKED) {
        GST_ELEMENT_FLOW_ERROR (demux, ret);
      }
    }
      break;

    case GST_FLOW_FLUSHING:
      /* Flushing is normal, the target track might have been unselected */
      GST_DEBUG_OBJECT (stream, "Got flushing return. Stopping callback.");
      return FALSE;
    default:
      if (ret <= GST_FLOW_ERROR) {
        GST_WARNING_OBJECT (demux, "Error while downloading fragment");
        if (++stream->download_error_count > MAX_DOWNLOAD_ERROR_COUNT) {
          gst_adaptive_demux2_stream_error (stream);
          return FALSE;
        }

        g_clear_error (&stream->last_error);

        /* First try to update the playlist for non-live playlists
         * in case the URIs have changed in the meantime. But only
         * try it the first time, after that we're going to wait a
         * a bit to not flood the server */
        if (stream->download_error_count == 1
            && !gst_adaptive_demux_is_live (demux)) {
          /* TODO hlsdemux had more options to this function (boolean and err) */
          if (gst_adaptive_demux_update_manifest (demux) == GST_FLOW_OK) {
            /* Retry immediately, the playlist actually has changed */
            GST_DEBUG_OBJECT (demux, "Updated the playlist");
            return TRUE;
          }
        }

        /* Wait half the fragment duration before retrying */
        GST_LOG_OBJECT (stream, "Scheduling delayed reload_manifest_cb() call");
        g_assert (stream->pending_cb_id == 0);
        stream->pending_cb_id =
            gst_adaptive_demux_loop_call_delayed (demux->priv->scheduler_task,
            stream->fragment.duration / 2,
            (GSourceFunc) gst_adaptive_demux2_stream_reload_manifest_cb,
            gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
        return FALSE;
      }
      break;
  }

  return FALSE;
}

static gboolean
gst_adaptive_demux2_stream_next_download (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;
  gboolean end_of_manifest = FALSE;

  GST_LOG_OBJECT (stream, "Looking for next download");

  /* Restarting download, figure out new position
   * FIXME : Move this to a separate function ? */
  if (G_UNLIKELY (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART)) {
    GstClockTimeDiff stream_time = 0;

    GST_DEBUG_OBJECT (stream, "Activating stream after restart");

    if (stream->parsebin_sink != NULL) {
      /* If the parsebin already exists, we need to clear it out (if it doesn't,
       * this is the first time we've used this stream, so it's all good) */
      gst_adaptive_demux2_stream_push_event (stream,
          gst_event_new_flush_start ());
      gst_adaptive_demux2_stream_push_event (stream,
          gst_event_new_flush_stop (FALSE));
    }

    GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
    stream_time = stream->start_position;

    GST_DEBUG_OBJECT (stream, "Restarting stream at "
        "stream position %" GST_STIME_FORMAT, GST_STIME_ARGS (stream_time));

    if (GST_CLOCK_STIME_IS_VALID (stream_time)) {
      /* TODO check return */
      gst_adaptive_demux2_stream_seek (stream, demux->segment.rate >= 0,
          0, stream_time, &stream_time);
      stream->current_position = stream->start_position;

      GST_DEBUG_OBJECT (stream,
          "stream_time after restart seek: %" GST_STIME_FORMAT
          " position %" GST_STIME_FORMAT, GST_STIME_ARGS (stream_time),
          GST_STIME_ARGS (stream->current_position));
    }

    /* Trigger (re)computation of the parsebin input segment */
    stream->compute_segment = TRUE;

    GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

    stream->discont = TRUE;
    stream->need_header = TRUE;
    stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT;
  }

  /* Check if we're done with our segment */
  GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
  if (demux->segment.rate > 0) {
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop)
        && stream->current_position >= demux->segment.stop) {
      end_of_manifest = TRUE;
    }
  } else {
    if (GST_CLOCK_TIME_IS_VALID (demux->segment.start)
        && stream->current_position <= demux->segment.start) {
      end_of_manifest = TRUE;
    }
  }
  GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

  if (end_of_manifest) {
    gst_adaptive_demux2_stream_end_of_manifest (stream);
    return FALSE;
  }
  return gst_adaptive_demux2_stream_load_a_fragment (stream);
}

/**
 * gst_adaptive_demux2_stream_start:
 * @stream: a #GstAdaptiveDemux2Stream
 *
 * Start the given @stream. Can be called by subclasses that previously
 * returned %FALSE in `GstAdaptiveDemux2Stream::start()`, or from
 * the demuxer when a stream should start downloading.
 */
void
gst_adaptive_demux2_stream_start (GstAdaptiveDemux2Stream * stream)
{
  g_return_if_fail (stream && stream->demux);

  if (stream->pending_cb_id != 0 || stream->download_active) {
    /* There is already something active / pending on this stream */
    GST_LOG_OBJECT (stream, "Stream already running");
    return;
  }

  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  klass->start (stream);
}

static void
gst_adaptive_demux2_stream_start_default (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS) {
    GST_LOG_OBJECT (stream, "Stream is EOS already");
    return;
  }

  if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED ||
      stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART) {
    GST_LOG_OBJECT (stream, "Activating stream. Current state %d",
        stream->state);
    stream->last_ret = GST_FLOW_OK;

    if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED)
      stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_START_FRAGMENT;
  }

  GST_LOG_OBJECT (stream, "Scheduling next_download() call");
  stream->pending_cb_id =
      gst_adaptive_demux_loop_call (demux->priv->scheduler_task,
      (GSourceFunc) gst_adaptive_demux2_stream_next_download,
      gst_object_ref (stream), (GDestroyNotify) gst_object_unref);
}

void
gst_adaptive_demux2_stream_stop (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  klass->stop (stream);
}

static void
gst_adaptive_demux2_stream_stop_default (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux *demux = stream->demux;

  GST_DEBUG_OBJECT (stream, "Stopping stream (from state %d)", stream->state);
  stream->state = GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED;
  g_cond_broadcast (&stream->prepare_cond);

  if (stream->pending_cb_id != 0) {
    gst_adaptive_demux_loop_cancel_call (demux->priv->scheduler_task,
        stream->pending_cb_id);
    stream->pending_cb_id = 0;
  }

  /* Cancel and drop the existing download request */
  downloadhelper_cancel_request (demux->download_helper,
      stream->download_request);
  download_request_unref (stream->download_request);
  stream->downloading_header = stream->downloading_index = FALSE;
  stream->download_request = download_request_new ();
  stream->download_active = FALSE;
  stream->download_error_retry = FALSE;
  stream->download_error_count = 0;

  stream->next_input_wakeup_time = GST_CLOCK_STIME_NONE;
}

gboolean
gst_adaptive_demux2_stream_is_running (GstAdaptiveDemux2Stream * stream)
{
  if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_STOPPED)
    return FALSE;
  if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_RESTART)
    return FALSE;
  if (stream->state == GST_ADAPTIVE_DEMUX2_STREAM_STATE_EOS)
    return FALSE;
  return TRUE;
}

/* Returns TRUE if the stream has at least one selected track.
 * Must be called with the TRACKS_LOCK held */
gboolean
gst_adaptive_demux2_stream_is_selected_locked (GstAdaptiveDemux2Stream * stream)
{
  GList *tmp;

  for (tmp = stream->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = tmp->data;
    if (track->selected)
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_adaptive_demux2_stream_is_default_locked (GstAdaptiveDemux2Stream * stream)
{
  GList *tmp;

  for (tmp = stream->tracks; tmp; tmp = tmp->next) {
    GstAdaptiveDemuxTrack *track = tmp->data;
    if (track->flags & GST_STREAM_FLAG_SELECT)
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_adaptive_demux2_stream_is_selected:
 * @stream: A #GstAdaptiveDemux2Stream
 *
 * Returns: %TRUE if any of the tracks targetted by @stream is selected
 */
gboolean
gst_adaptive_demux2_stream_is_selected (GstAdaptiveDemux2Stream * stream)
{
  gboolean ret;

  g_return_val_if_fail (stream && stream->demux, FALSE);

  TRACKS_LOCK (stream->demux);
  ret = gst_adaptive_demux2_stream_is_selected_locked (stream);
  TRACKS_UNLOCK (stream->demux);

  return ret;
}

/* Called from the scheduler task */
GstClockTime
gst_adaptive_demux2_stream_get_presentation_offset (GstAdaptiveDemux2Stream *
    stream)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  if (klass->get_presentation_offset == NULL)
    return 0;

  return klass->get_presentation_offset (stream);
}

GstFlowReturn
gst_adaptive_demux2_stream_update_fragment_info (GstAdaptiveDemux2Stream *
    stream)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
  GstFlowReturn ret;

  g_return_val_if_fail (klass->update_fragment_info != NULL, GST_FLOW_ERROR);

  /* Make sure the sub-class will update bitrate, or else
   * we will later */
  stream->fragment.finished = FALSE;

  GST_LOG_OBJECT (stream, "position %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->current_position));

  ret = klass->update_fragment_info (stream);

  GST_LOG_OBJECT (stream, "ret:%s uri:%s",
      gst_flow_get_name (ret), stream->fragment.uri);
  if (ret == GST_FLOW_OK) {
    GST_LOG_OBJECT (stream,
        "stream_time %" GST_STIME_FORMAT " duration:%" GST_TIME_FORMAT,
        GST_STIME_ARGS (stream->fragment.stream_time),
        GST_TIME_ARGS (stream->fragment.duration));
    GST_LOG_OBJECT (stream,
        "range start:%" G_GINT64_FORMAT " end:%" G_GINT64_FORMAT,
        stream->fragment.range_start, stream->fragment.range_end);
  }

  return ret;
}

static GstFlowReturn
gst_adaptive_demux2_stream_data_received_default (GstAdaptiveDemux2Stream *
    stream, GstBuffer * buffer)
{
  return gst_adaptive_demux2_stream_push_buffer (stream, buffer);
}

static GstFlowReturn
gst_adaptive_demux2_stream_finish_fragment_default (GstAdaptiveDemux2Stream *
    stream)
{
  /* No need to advance, this isn't a real fragment */
  if (G_UNLIKELY (stream->downloading_header || stream->downloading_index))
    return GST_FLOW_OK;

  return gst_adaptive_demux2_stream_advance_fragment (stream,
      stream->fragment.duration);
}

/* must be called from the scheduler */
gboolean
gst_adaptive_demux2_stream_has_next_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
  gboolean ret = TRUE;

  if (klass->has_next_fragment)
    ret = klass->has_next_fragment (stream);

  return ret;
}

/* must be called from the scheduler */
GstFlowReturn
gst_adaptive_demux2_stream_seek (GstAdaptiveDemux2Stream * stream,
    gboolean forward, GstSeekFlags flags,
    GstClockTimeDiff ts, GstClockTimeDiff * final_ts)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  if (klass->stream_seek)
    return klass->stream_seek (stream, forward, flags, ts, final_ts);
  return GST_FLOW_ERROR;
}

static gboolean
gst_adaptive_demux2_stream_select_bitrate (GstAdaptiveDemux *
    demux, GstAdaptiveDemux2Stream * stream, guint64 bitrate)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  if (klass->select_bitrate)
    return klass->select_bitrate (stream, bitrate);
  return FALSE;
}

GstClockTime
gst_adaptive_demux2_stream_get_fragment_waiting_time (GstAdaptiveDemux2Stream *
    stream)
{
  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);

  if (klass->get_fragment_waiting_time)
    return klass->get_fragment_waiting_time (stream);
  return 0;
}

/* must be called from the scheduler */
/* Called from: the ::finish_fragment() handlers when an *actual* fragment is
 * done
 *
 * @duration: Is the duration of the advancement starting from
 * stream->current_position which might not be the fragment duration after a
 * seek.
 */
GstFlowReturn
gst_adaptive_demux2_stream_advance_fragment (GstAdaptiveDemux2Stream * stream,
    GstClockTime duration)
{
  if (stream->last_ret != GST_FLOW_OK)
    return stream->last_ret;

  GstAdaptiveDemux2StreamClass *klass =
      GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
  GstAdaptiveDemux *demux = stream->demux;
  GstFlowReturn ret = GST_FLOW_OK;

  g_assert (klass->advance_fragment != NULL);

  GST_LOG_OBJECT (stream,
      "stream_time %" GST_STIME_FORMAT " duration:%" GST_TIME_FORMAT,
      GST_STIME_ARGS (stream->fragment.stream_time), GST_TIME_ARGS (duration));

  stream->download_error_count = 0;
  g_clear_error (&stream->last_error);

#if 0
  /* FIXME - url has no indication of byte ranges for subsegments */
  /* FIXME: Reenable statistics sending? */
  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux),
          gst_structure_new (GST_ADAPTIVE_DEMUX_STATISTICS_MESSAGE_NAME,
              "manifest-uri", G_TYPE_STRING,
              demux->manifest_uri, "uri", G_TYPE_STRING,
              stream->fragment.uri, "fragment-start-time",
              GST_TYPE_CLOCK_TIME, stream->download_start_time,
              "fragment-stop-time", GST_TYPE_CLOCK_TIME,
              gst_util_get_timestamp (), "fragment-size", G_TYPE_UINT64,
              stream->download_total_bytes, "fragment-download-time",
              GST_TYPE_CLOCK_TIME, stream->last_download_time, NULL)));
#endif

  /* Don't update to the end of the segment if in reverse playback */
  GST_ADAPTIVE_DEMUX_SEGMENT_LOCK (demux);
  if (GST_CLOCK_TIME_IS_VALID (duration) && demux->segment.rate > 0) {
    stream->parse_segment.position += duration;
    stream->current_position += duration;

    GST_DEBUG_OBJECT (stream,
        "stream position now %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stream->current_position));
  }
  GST_ADAPTIVE_DEMUX_SEGMENT_UNLOCK (demux);

  /* When advancing with a non 1.0 rate on live streams, we need to check
   * the live seeking range again to make sure we can still advance to
   * that position */
  if (demux->segment.rate != 1.0 && gst_adaptive_demux_is_live (demux)) {
    if (!gst_adaptive_demux2_stream_in_live_seek_range (demux, stream))
      ret = GST_FLOW_EOS;
    else
      ret = klass->advance_fragment (stream);
  } else if (gst_adaptive_demux_is_live (demux)
      || gst_adaptive_demux2_stream_has_next_fragment (stream)) {
    ret = klass->advance_fragment (stream);
  } else {
    ret = GST_FLOW_EOS;
  }

  stream->download_start_time =
      GST_TIME_AS_USECONDS (gst_adaptive_demux2_get_monotonic_time (demux));

  /* Always check if we need to switch bitrate on OK, or when live
   * (it's normal to have EOS on advancing in live when we hit the
   * end of the manifest) */
  if (ret == GST_FLOW_OK || gst_adaptive_demux_is_live (demux)) {
    GST_DEBUG_OBJECT (stream, "checking if stream requires bitrate change");
    if (gst_adaptive_demux2_stream_select_bitrate (demux, stream,
            gst_adaptive_demux2_stream_update_current_bitrate (stream))) {
      GST_DEBUG_OBJECT (stream, "Bitrate changed. Returning FLOW_SWITCH");
      stream->need_header = TRUE;
      ret = (GstFlowReturn) GST_ADAPTIVE_DEMUX_FLOW_SWITCH;
    }
  }

  stream->last_ret = ret;
  return stream->last_ret;
}

/* TRACKS_LOCK held */
static GstAdaptiveDemuxTrack *
gst_adaptive_demux2_stream_find_track_of_type (GstAdaptiveDemux2Stream * stream,
    GstStreamType stream_type)
{
  GList *iter;

  for (iter = stream->tracks; iter; iter = iter->next) {
    GstAdaptiveDemuxTrack *track = iter->data;

    if (track->type == stream_type)
      return track;
  }

  return NULL;
}

/* TRACKS lock held */
static void
gst_adaptive_demux2_stream_update_track_ids (GstAdaptiveDemux2Stream * stream)
{
  guint i;

  GST_DEBUG_OBJECT (stream, "Updating track information from collection");

  for (i = 0; i < gst_stream_collection_get_size (stream->stream_collection);
      i++) {
    GstStream *gst_stream =
        gst_stream_collection_get_stream (stream->stream_collection, i);
    GstStreamType stream_type = gst_stream_get_stream_type (gst_stream);
    GstAdaptiveDemuxTrack *track;

    if (stream_type == GST_STREAM_TYPE_UNKNOWN)
      continue;
    track = gst_adaptive_demux2_stream_find_track_of_type (stream, stream_type);
    if (!track) {
      GST_DEBUG_OBJECT (stream,
          "We don't have an existing track to handle stream %" GST_PTR_FORMAT,
          gst_stream);
      continue;
    }

    if (track->upstream_stream_id)
      g_free (track->upstream_stream_id);
    track->upstream_stream_id =
        g_strdup (gst_stream_get_stream_id (gst_stream));
  }

}

static gboolean
tags_have_language_info (GstTagList * tags)
{
  const gchar *language = NULL;

  if (tags == NULL)
    return FALSE;

  if (gst_tag_list_peek_string_index (tags, GST_TAG_LANGUAGE_CODE, 0,
          &language))
    return TRUE;
  if (gst_tag_list_peek_string_index (tags, GST_TAG_LANGUAGE_NAME, 0,
          &language))
    return TRUE;

  return FALSE;
}

static gboolean
can_handle_collection (GstAdaptiveDemux2Stream * stream,
    GstStreamCollection * collection)
{
  guint i;
  guint nb_audio, nb_video, nb_text;
  gboolean have_audio_languages = TRUE;
  gboolean have_text_languages = TRUE;

  nb_audio = nb_video = nb_text = 0;

  for (i = 0; i < gst_stream_collection_get_size (collection); i++) {
    GstStream *gst_stream = gst_stream_collection_get_stream (collection, i);
    GstTagList *tags = gst_stream_get_tags (gst_stream);

    GST_DEBUG_OBJECT (stream,
        "Internal collection stream #%d %" GST_PTR_FORMAT, i, gst_stream);
    switch (gst_stream_get_stream_type (gst_stream)) {
      case GST_STREAM_TYPE_AUDIO:
        have_audio_languages &= tags_have_language_info (tags);
        nb_audio++;
        break;
      case GST_STREAM_TYPE_VIDEO:
        nb_video++;
        break;
      case GST_STREAM_TYPE_TEXT:
        have_text_languages &= tags_have_language_info (tags);
        nb_text++;
        break;
      default:
        break;
    }
    if (tags)
      gst_tag_list_unref (tags);
  }

  /* Check that we either have at most 1 of each track type, or that
   * we have language tags for each to tell which is which */
  if (nb_video > 1 ||
      (nb_audio > 1 && !have_audio_languages) ||
      (nb_text > 1 && !have_text_languages)) {
    GST_WARNING
        ("Collection can't be handled (nb_audio:%d, nb_video:%d, nb_text:%d)",
        nb_audio, nb_video, nb_text);
    return FALSE;
  }

  return TRUE;
}

/* Called from the demuxer when it receives a GstStreamCollection on the bus
 * for this stream. */
/* TRACKS lock held */
gboolean
gst_adaptive_demux2_stream_handle_collection (GstAdaptiveDemux2Stream * stream,
    GstStreamCollection * collection, gboolean * had_pending_tracks)
{
  g_assert (had_pending_tracks != NULL);

  /* Check whether the collection is "sane" or not.
   *
   * In the context of adaptive streaming, we can only handle multiplexed
   * content where the output sub-streams can be matched reliably to the various
   * tracks. That is, only a single stream of each type, or if there are
   * multiple audio/subtitle tracks, they can be differentiated by language
   * (and possibly in the future by codec).
   */
  if (!can_handle_collection (stream, collection)) {
    return FALSE;
  }

  /* Store the collection on the stream */
  gst_object_replace ((GstObject **) & stream->stream_collection,
      (GstObject *) collection);

  /* If stream is marked as having pending_tracks, ask the subclass to
   * handle that and create the tracks now */
  if (stream->pending_tracks) {
    GstAdaptiveDemux2StreamClass *klass =
        GST_ADAPTIVE_DEMUX2_STREAM_GET_CLASS (stream);
    g_assert (klass->create_tracks);
    klass->create_tracks (stream);
    stream->pending_tracks = FALSE;
    *had_pending_tracks = TRUE;
  } else {
    g_assert (stream->tracks);

    /* Now we should have assigned tracks, match them to the
     * collection and update the pending upstream stream_id
     * for each of them based on the collection information. */
    gst_adaptive_demux2_stream_update_track_ids (stream);
  }

  return TRUE;
}

static guint64
_update_average_bitrate (GstAdaptiveDemux2Stream * stream, guint64 new_bitrate)
{
  gint index = stream->moving_index % NUM_LOOKBACK_FRAGMENTS;

  stream->moving_bitrate -= stream->fragment_bitrates[index];
  stream->fragment_bitrates[index] = new_bitrate;
  stream->moving_bitrate += new_bitrate;

  stream->moving_index += 1;

  if (stream->moving_index > NUM_LOOKBACK_FRAGMENTS)
    return stream->moving_bitrate / NUM_LOOKBACK_FRAGMENTS;
  return stream->moving_bitrate / stream->moving_index;
}

guint64
gst_adaptive_demux2_stream_update_current_bitrate (GstAdaptiveDemux2Stream *
    stream)
{
  guint64 average_bitrate;
  guint64 fragment_bitrate;
  guint connection_speed, min_bitrate, max_bitrate, target_download_rate;

  fragment_bitrate = stream->last_bitrate;
  GST_DEBUG_OBJECT (stream, "Download bitrate is : %" G_GUINT64_FORMAT " bps",
      fragment_bitrate);

  average_bitrate = _update_average_bitrate (stream, fragment_bitrate);

  GST_INFO_OBJECT (stream,
      "last fragment bitrate was %" G_GUINT64_FORMAT, fragment_bitrate);
  GST_INFO_OBJECT (stream,
      "Last %u fragments average bitrate is %" G_GUINT64_FORMAT,
      NUM_LOOKBACK_FRAGMENTS, average_bitrate);

  /* Conservative approach, make sure we don't upgrade too fast */
  stream->current_download_rate = MIN (average_bitrate, fragment_bitrate);

  /* For the video stream, update the demuxer reported download
   * rate. FIXME: Move all bandwidth estimation to the
   * download helper and make it the demuxer's responsibility
   * to select the right set of things to download within
   * that bandwidth */
  GstAdaptiveDemux *demux = stream->demux;
  GST_OBJECT_LOCK (demux);

  /* If this is stream containing our video, update the overall demuxer
   * reported bitrate and notify, to give the application a
   * chance to choose a new connection-bitrate */
  if ((stream->stream_type & GST_STREAM_TYPE_VIDEO) != 0) {
    demux->current_download_rate = stream->current_download_rate;
    GST_OBJECT_UNLOCK (demux);
    g_object_notify (G_OBJECT (demux), "current-bandwidth");
    GST_OBJECT_LOCK (demux);
  }

  connection_speed = demux->connection_speed;
  min_bitrate = demux->min_bitrate;
  max_bitrate = demux->max_bitrate;
  GST_OBJECT_UNLOCK (demux);

  if (connection_speed) {
    GST_LOG_OBJECT (stream, "connection-speed is set to %u kbps, using it",
        connection_speed / 1000);
    return connection_speed;
  }

  /* No explicit connection_speed, so choose the new variant to use as a
   * fraction of the measured download rate */
  target_download_rate =
      CLAMP (stream->current_download_rate, 0,
      G_MAXUINT) * (gdouble) demux->bandwidth_target_ratio;

  GST_DEBUG_OBJECT (stream, "Bitrate after target ratio limit (%0.2f): %u",
      demux->bandwidth_target_ratio, target_download_rate);

#if 0
  /* Debugging code, modulate the bitrate every few fragments */
  {
    static guint ctr = 0;
    if (ctr % 3 == 0) {
      GST_INFO_OBJECT (stream, "Halving reported bitrate for debugging");
      target_download_rate /= 2;
    }
    ctr++;
  }
#endif

  if (min_bitrate > 0 && target_download_rate < min_bitrate) {
    target_download_rate = min_bitrate;
    GST_LOG_OBJECT (stream, "Bitrate adjusted due to min-bitrate : %u bits/s",
        min_bitrate);
  }

  if (max_bitrate > 0 && target_download_rate > max_bitrate) {
    target_download_rate = max_bitrate;
    GST_LOG_OBJECT (stream, "Bitrate adjusted due to max-bitrate : %u bits/s",
        max_bitrate);
  }

  GST_DEBUG_OBJECT (stream, "Returning target download rate of %u bps",
      target_download_rate);

  return target_download_rate;
}

void
gst_adaptive_demux2_stream_fragment_clear (GstAdaptiveDemux2StreamFragment * f)
{
  g_free (f->uri);
  f->uri = NULL;
  f->range_start = 0;
  f->range_end = -1;

  g_free (f->header_uri);
  f->header_uri = NULL;
  f->header_range_start = 0;
  f->header_range_end = -1;

  g_free (f->index_uri);
  f->index_uri = NULL;
  f->index_range_start = 0;
  f->index_range_end = -1;

  f->stream_time = GST_CLOCK_STIME_NONE;
  f->duration = GST_CLOCK_TIME_NONE;
  f->finished = FALSE;
}
