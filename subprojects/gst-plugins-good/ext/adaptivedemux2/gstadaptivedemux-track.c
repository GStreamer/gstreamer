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

#include "gstadaptivedemux.h"
#include "gstadaptivedemux-private.h"

GST_DEBUG_CATEGORY_EXTERN (adaptivedemux2_debug);
#define GST_CAT_DEFAULT adaptivedemux2_debug

/* TRACKS_LOCK held
 * Flushes all data in the track and resets it */
void
gst_adaptive_demux_track_flush (GstAdaptiveDemuxTrack * track)
{
  GST_DEBUG_ID (track->id, "Flushing track with %u queued items",
      gst_queue_array_get_length (track->queue));
  gst_queue_array_clear (track->queue);

  gst_event_store_flush (&track->sticky_events);

  gst_segment_init (&track->input_segment, GST_FORMAT_TIME);
  track->lowest_input_time = GST_CLOCK_STIME_NONE;
  track->input_time = 0;
  track->input_segment_seqnum = GST_SEQNUM_INVALID;

  gst_segment_init (&track->output_segment, GST_FORMAT_TIME);
  track->gap_position = track->gap_duration = GST_CLOCK_TIME_NONE;

  track->output_time = GST_CLOCK_STIME_NONE;
  track->next_position = GST_CLOCK_STIME_NONE;

  track->level_bytes = 0;
  track->level_time = 0;

  track->eos = FALSE;

  track->update_next_segment = FALSE;

  track->output_discont = FALSE;
}

static gboolean
_track_sink_query_function (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAdaptiveDemuxTrack *track = gst_pad_get_element_private (pad);
  gboolean ret = FALSE;

  GST_DEBUG_ID (track->id, "query %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
      /* Should we intersect by track caps as a safety check ? */
      GST_DEBUG_ID (track->id, "We accept any caps on %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      gst_query_set_accept_caps_result (query, TRUE);
      ret = TRUE;
      break;
    default:
      break;
  }

  return ret;
}

/* Dequeue an item from the track queue for processing
 * TRACKS_LOCK hold */
static gboolean
track_dequeue_item_locked (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxTrack * track, TrackQueueItem * out_item)
{
  TrackQueueItem *item = gst_queue_array_peek_head_struct (track->queue);

  if (item == NULL)
    return FALSE;

  *out_item = *item;
  gst_queue_array_pop_head (track->queue);

  GST_LOG_ID (track->id,
      "item running_time %" GST_STIME_FORMAT " end %"
      GST_STIME_FORMAT,
      GST_STIME_ARGS (out_item->runningtime),
      GST_STIME_ARGS (out_item->runningtime_end));

  return TRUE;
}

static inline GstClockTimeDiff my_segment_to_running_time (GstSegment * segment,
    GstClockTime val);

/* Dequeue or generate a buffer/event from the track queue and update the buffering levels
 * TRACKS_LOCK hold */
GstMiniObject *
gst_adaptive_demux_track_dequeue_data_locked (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxTrack * track, gboolean check_sticky_events)
{
  GstMiniObject *res = NULL;
  gboolean is_pending_sticky = FALSE;
  GstEvent *event;
  GstClockTimeDiff running_time;
  GstClockTimeDiff running_time_buffering = GST_CLOCK_STIME_NONE;
  GstClockTimeDiff running_time_end;
  gsize item_size = 0;

  if (check_sticky_events) {
    /* If there are any sticky events to send, do that before anything else */
    event = gst_event_store_get_next_pending (&track->sticky_events);
    if (event != NULL) {
      res = (GstMiniObject *) event;
      running_time_buffering = running_time = running_time_end =
          GST_CLOCK_STIME_NONE;
      GST_DEBUG_ID (track->id,
          "dequeued pending sticky event %" GST_PTR_FORMAT, event);
      is_pending_sticky = TRUE;
      goto handle_event;
    }
  }

  do {
    TrackQueueItem item;

    /* If we're filling a gap, generate a gap event */
    if (track->gap_position != GST_CLOCK_TIME_NONE) {
      GstClockTime pos = track->gap_position;
      GstClockTime duration = track->gap_duration;

      if (duration > 100 * GST_MSECOND) {
        duration = 100 * GST_MSECOND;
        track->gap_position += duration;
        track->gap_duration -= duration;
      } else {
        /* Duration dropped below 100 ms, this is the last
         * gap of the sequence */
        track->gap_position = GST_CLOCK_TIME_NONE;
        track->gap_duration = GST_CLOCK_TIME_NONE;
      }

      res = (GstMiniObject *) gst_event_new_gap (pos, duration);
      if (track->output_segment.rate > 0.0) {
        running_time = my_segment_to_running_time (&track->output_segment, pos);
        running_time_buffering = running_time_end =
            my_segment_to_running_time (&track->output_segment, pos + duration);
      } else {
        running_time =
            my_segment_to_running_time (&track->output_segment, pos + duration);
        running_time_buffering = running_time_end =
            my_segment_to_running_time (&track->output_segment, pos);
      }
      item_size = 0;
      break;
    }

    /* Otherwise, try and pop something from the item queue */
    if (!track_dequeue_item_locked (demux, track, &item))
      return NULL;

    res = item.item;
    running_time = item.runningtime;
    running_time_end = item.runningtime_end;
    running_time_buffering = item.runningtime_buffering;
    item_size = item.size;

    /* Special case for a gap event, to drain them out little-by-little.
     * See if it can be output directly, otherwise set up to fill a gap and loop again */
    if (GST_IS_EVENT (res) && GST_EVENT_TYPE (res) == GST_EVENT_GAP
        && GST_CLOCK_STIME_IS_VALID (running_time)) {
      GstClockTime pos, duration;
      GstClockTime cstart, cstop;

      gst_event_parse_gap (GST_EVENT_CAST (res), &pos, &duration);

      /* Handle a track with no duration as 0 duration. This can only
       * happen if an element in parsebin emits such a gap event */
      if (duration == GST_CLOCK_TIME_NONE)
        duration = 0;

      /* We *can* end up with a gap outside of the segment range due to segment
       * base updating when (re)activating a track. In that case, just let the gap
       * event flow out normally.
       * Otherwise, this gap crosses into the segment, clip it to the ends and set up to fill the gap */
      if (!gst_segment_clip (&track->output_segment, GST_FORMAT_TIME, pos,
              pos + duration, &cstart, &cstop))
        break;

      pos = cstart;
      duration = cstop - cstart;

      GST_DEBUG_ID (track->id,
          "Starting gap for runningtime %" GST_STIME_FORMAT
          " - clipped position %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT,
          GST_STIME_ARGS (running_time),
          GST_TIME_ARGS (pos), GST_TIME_ARGS (duration));

      track->gap_position = pos;
      track->gap_duration = duration;

      gst_mini_object_unref (res);
      res = NULL;
      continue;
    }
  } while (res == NULL);

handle_event:
  if (GST_IS_EVENT (res)) {
    event = (GstEvent *) res;

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:
        gst_event_copy_segment (event, &track->output_segment);

        if (!GST_CLOCK_STIME_IS_VALID (track->output_time)) {
          if (track->output_segment.rate > 0.0) {
            track->output_time =
                my_segment_to_running_time (&track->output_segment,
                track->output_segment.start);
          } else {
            track->output_time =
                my_segment_to_running_time (&track->output_segment,
                track->output_segment.stop);
          }
        }

        if (track->update_next_segment) {
          GstClockTimeDiff global_output_position =
              demux->priv->global_output_position;

          GST_DEBUG_ID (track->id, "Override segment for running time %"
              GST_STIME_FORMAT " : %" GST_PTR_FORMAT,
              GST_STIME_ARGS (global_output_position), event);
          gst_event_unref (event);
          gst_segment_set_running_time (&track->output_segment, GST_FORMAT_TIME,
              global_output_position);

          event = gst_event_new_segment (&track->output_segment);
          gst_event_set_seqnum (event, track->demux->priv->segment_seqnum);

          res = (GstMiniObject *) event;
          running_time = global_output_position;

          track->update_next_segment = FALSE;

          /* Replace the stored sticky event with this one */
          is_pending_sticky = FALSE;
        }
        break;
      default:
        break;
    }

    /* Store any sticky event in the cache, unless this is already an event
     * from the pending sticky_events store */
    if (!is_pending_sticky && GST_EVENT_IS_STICKY (event)) {
      GST_DEBUG_ID (track->id, "Storing sticky event %" GST_PTR_FORMAT, event);
      gst_event_store_insert_event (&track->sticky_events, event, FALSE);
    }
  }

  /* Update track buffering levels */
  if (GST_CLOCK_STIME_IS_VALID (running_time_buffering)) {
    track->output_time = running_time_buffering;

    GST_LOG_ID (track->id,
        "buffering time:%" GST_STIME_FORMAT,
        GST_STIME_ARGS (running_time_buffering));

    gst_adaptive_demux_track_update_level_locked (track);
  } else {
    GST_LOG_ID (track->id, "popping untimed item %" GST_PTR_FORMAT, res);
  }

  track->level_bytes -= item_size;

  return res;
}

void
gst_adaptive_demux_track_drain_to (GstAdaptiveDemuxTrack * track,
    GstClockTime drain_running_time)
{
  GstAdaptiveDemux *demux = track->demux;

  GST_DEBUG_ID (track->id,
      "draining to running time %" GST_STIME_FORMAT,
      GST_STIME_ARGS (drain_running_time));

  while (track->next_position == GST_CLOCK_STIME_NONE ||
      track->next_position < drain_running_time) {
    TrackQueueItem *item;
    GstMiniObject *next_mo = NULL;

    /* If we're in a gap, and the end time is after the target running time,
     * exit */
    if (track->gap_position != GST_CLOCK_TIME_NONE) {
      GstClockTimeDiff running_time_end;
      GstClockTimeDiff gap_end = track->gap_position;

      /* In reverse playback, the start of the gap is the highest
       * running time, so only add duration for forward play */
      if (track->output_segment.rate > 0)
        gap_end += track->gap_duration;

      running_time_end =
          my_segment_to_running_time (&track->output_segment, gap_end);

      if (running_time_end >= drain_running_time) {
        GST_DEBUG_ID (track->id,
            "drained to GAP with running time %" GST_STIME_FORMAT,
            GST_STIME_ARGS (running_time_end));
        return;
      }

      /* Otherwise this gap is complete, so skip it */
      track->gap_position = GST_CLOCK_STIME_NONE;
    }

    /* Otherwise check what's enqueued */
    item = gst_queue_array_peek_head_struct (track->queue);
    /* track is empty, we're done */
    if (item == NULL) {
      GST_DEBUG_ID (track->id, "Track completely drained");
      return;
    }

    /* If the item has a running time, and it's after the drain_running_time
     * we're done. */
    if (item->runningtime != GST_CLOCK_STIME_NONE
        && item->runningtime >= drain_running_time) {
      GST_DEBUG_ID (track->id, "Track drained to item %" GST_PTR_FORMAT
          " with running time %" GST_STIME_FORMAT,
          item->item, GST_STIME_ARGS (item->runningtime));
      return;
    }

    GST_DEBUG_ID (track->id, "discarding %" GST_PTR_FORMAT
        " with running time %" GST_STIME_FORMAT,
        item->item, GST_STIME_ARGS (item->runningtime));

    /* Dequeue the item and discard. Sticky events
     * will be collected by the dequeue function, gaps will be started.
     * If it's a buffer, mark the track as discont to get the flag set
     * on the next output buffer */
    next_mo =
        gst_adaptive_demux_track_dequeue_data_locked (demux, track, FALSE);
    if (GST_IS_BUFFER (next_mo)) {
      track->output_discont = TRUE;
    }
    gst_mini_object_unref (next_mo);
    gst_adaptive_demux_track_update_next_position (track);
  }

  GST_DEBUG_ID (track->id,
      "drained to running time %" GST_STIME_FORMAT,
      GST_STIME_ARGS (track->next_position));
}

static inline GstClockTimeDiff
my_segment_to_running_time (GstSegment * segment, GstClockTime val)
{
  GstClockTimeDiff res = GST_CLOCK_STIME_NONE;

  if (GST_CLOCK_TIME_IS_VALID (val)) {
    gboolean sign =
        gst_segment_to_running_time_full (segment, GST_FORMAT_TIME, val, &val);
    if (sign > 0)
      res = val;
    else if (sign < 0)
      res = -val;
  }
  return res;
}

/* Queues an item on a track queue and updates the buffering levels
 * TRACKS_LOCK hold */
static void
track_queue_data_locked (GstAdaptiveDemux * demux,
    GstAdaptiveDemuxTrack * track, GstMiniObject * object, gsize size,
    GstClockTime timestamp, GstClockTime duration, gboolean is_discont)
{
  TrackQueueItem item;

  item.item = object;
  item.size = size;
  item.runningtime = GST_CLOCK_STIME_NONE;
  item.runningtime_end = GST_CLOCK_STIME_NONE;
  item.runningtime_buffering = GST_CLOCK_STIME_NONE;

  if (timestamp != GST_CLOCK_TIME_NONE) {
    GstClockTimeDiff input_time;

    /* Set the running time of the item */
    input_time = item.runningtime_end = item.runningtime =
        my_segment_to_running_time (&track->input_segment, timestamp);

    /* Update segment position (include duration if valid) */
    track->input_segment.position = timestamp;

    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      if (track->input_segment.rate > 0.0) {
        /* Forward playback, add duration onto our position and update
         * the input time to match */
        track->input_segment.position += duration;
        item.runningtime_end = input_time =
            my_segment_to_running_time (&track->input_segment,
            track->input_segment.position);
      } else {
        /* Otherwise, the end of the buffer has the smaller running time and
         * we need to change the item.runningtime, but input_time and runningtime_end
         * are already set to the larger running time */
        item.runningtime = my_segment_to_running_time (&track->input_segment,
            timestamp + duration);
      }
    }

    /* Update track input time and level */
    if (!GST_CLOCK_STIME_IS_VALID (track->lowest_input_time))
      track->lowest_input_time = track->input_time;

    if (track->input_segment.rate > 0.0) {
      if (input_time > track->input_time) {
        track->input_time = input_time;
      }
    } else {
      /* In reverse playback, we track input time differently, to do buffering
       * across the reversed GOPs. Each GOP arrives in reverse order, with
       * running time moving backward, then jumping forward at the start of
       * each GOP. At each point, we want the input time to be the lowest
       * running time of the previous GOP. Therefore, we track input times
       * into a different variable, and transfer it across when a discont buffer
       * arrives */
      if (is_discont) {
        track->input_time = track->lowest_input_time;
        track->lowest_input_time = input_time;
      } else if (input_time < track->lowest_input_time) {
        track->lowest_input_time = input_time;
      }
    }

    /* Store the maximum running time we've seen as
     * this item's "buffering running time" */
    item.runningtime_buffering = track->input_time;

    /* Configure the track output time if nothing was dequeued yet,
     * so buffering level is updated correctly */
    if (!GST_CLOCK_STIME_IS_VALID (track->output_time)) {
      track->output_time = track->lowest_input_time;
      GST_LOG_ID (track->id,
          "setting output_time = lowest input_time = %"
          GST_STIME_FORMAT, GST_STIME_ARGS (track->output_time));
    }

    gst_adaptive_demux_track_update_level_locked (track);
  }

  GST_LOG_ID (track->id,
      "item running_time :%" GST_STIME_FORMAT " end :%"
      GST_STIME_FORMAT, GST_STIME_ARGS (item.runningtime),
      GST_STIME_ARGS (item.runningtime_end));

  track->level_bytes += size;
  gst_queue_array_push_tail_struct (track->queue, &item);

  /* If we were waiting for this track to add something, notify output thread */
  /* FIXME: This should be in adaptive demux */
  if (track->waiting_add) {
    g_cond_signal (&demux->priv->tracks_add);
  }
}

static GstFlowReturn
_track_sink_chain_function (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstAdaptiveDemuxTrack *track = gst_pad_get_element_private (pad);
  GstAdaptiveDemux *demux = track->demux;
  GstClockTime ts;

  GST_DEBUG_ID (track->id, "buffer %" GST_PTR_FORMAT, buffer);

  TRACKS_LOCK (demux);

  /* Discard buffers that are received outside of a valid segment. This can
   * happen if a flushing seek (which resets the track segment seqnums) was
   * received but the stream is still providing buffers before returning.
   */
  if (track->input_segment_seqnum == GST_SEQNUM_INVALID) {
    GST_DEBUG_OBJECT (pad,
        "Dropping buffer because we do not have a valid input segment");
    gst_buffer_unref (buffer);
    TRACKS_UNLOCK (demux);
    return GST_FLOW_OK;
  }

  ts = GST_BUFFER_DTS_OR_PTS (buffer);

  /* Buffers coming out of parsebin *should* always be timestamped (it's the
   * goal of parsebin after all). The tracks will use that (converted to
   * running-time) in order to track position and buffering levels.
   *
   * Unfortunately there are valid cases were the parsers won't be able to
   * timestamp all frames (due to the underlying formats or muxing). For those
   * cases, we use the last incoming timestamp (via the track input GstSegment
   * position):
   *
   * * If buffers were previously received, that segment position will
   *   correspond to the last timestamped-buffer PTS/DTS
   *
   * * If *no* buffers were previously received, the segment position *should*
   *   correspond to the valid initial position (in buffer timestamps). If not
   *   set, we need to bail out.
   */
  if (!GST_CLOCK_TIME_IS_VALID (ts)) {
    if (GST_CLOCK_TIME_IS_VALID (track->input_segment.position)) {
      GST_WARNING_ID (track->id,
          "buffer doesn't have any pts or dts, using segment position (%"
          GST_TIME_FORMAT ")", GST_TIME_ARGS (track->input_segment.position));
      ts = track->input_segment.position;
    } else {
      GST_ERROR_ID (track->id, "initial buffer doesn't have any pts or dts !");
      gst_buffer_unref (buffer);
      TRACKS_UNLOCK (demux);
      return GST_FLOW_ERROR;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (track->input_segment.position) &&
      ts > track->input_segment.position &&
      ts > track->input_segment.start &&
      ts - track->input_segment.position > 100 * GST_MSECOND) {
    GstClockTime duration = ts - track->input_segment.position;
    GstEvent *gap = gst_event_new_gap (track->input_segment.position, duration);
    /* Insert gap event to ensure coherent interleave */
    GST_DEBUG_ID (track->id,
        "Inserting gap for %" GST_TIME_FORMAT " vs %" GST_TIME_FORMAT,
        GST_TIME_ARGS (ts), GST_TIME_ARGS (track->input_segment.position));
    track_queue_data_locked (demux, track, (GstMiniObject *) gap, 0,
        track->input_segment.position, duration, FALSE);
  }

  track_queue_data_locked (demux, track, (GstMiniObject *) buffer,
      gst_buffer_get_size (buffer), ts, GST_BUFFER_DURATION (buffer),
      GST_BUFFER_IS_DISCONT (buffer));

  /* Recalculate buffering */
  demux_update_buffering_locked (demux);
  demux_post_buffering_locked (demux);
  /* UNLOCK */
  TRACKS_UNLOCK (demux);

  return GST_FLOW_OK;
}

static gboolean
_track_sink_event_function (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAdaptiveDemuxTrack *track = gst_pad_get_element_private (pad);
  GstAdaptiveDemux *demux = track->demux;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime duration = GST_CLOCK_TIME_NONE;
  gboolean drop = FALSE;
  gboolean is_discont = FALSE;

  GST_DEBUG_ID (track->id, "event %" GST_PTR_FORMAT, event);

  TRACKS_LOCK (demux);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_COLLECTION:
    {
      /* Replace upstream collection with demux collection */
      GST_DEBUG_ID (track->id, "Dropping stream-collection, we send our own");
      drop = TRUE;
      break;
    }
    case GST_EVENT_STREAM_START:
    {
      GST_DEBUG_ID (track->id, "Dropping stream-start, we send our own");
      if (track->eos) {
        gint i, len;
        /* Find and drop latest EOS if present */
        len = gst_queue_array_get_length (track->queue);
        for (i = len - 1; i >= 0; i--) {
          TrackQueueItem *item =
              gst_queue_array_peek_nth_struct (track->queue, i);
          if (GST_IS_EVENT (item->item)
              && GST_EVENT_TYPE (item->item) == GST_EVENT_EOS) {
            TrackQueueItem sub;
            GST_DEBUG_ID (track->id,
                "Removing previously received EOS (pos:%d)", i);
            if (gst_queue_array_drop_struct (track->queue, i, &sub))
              gst_mini_object_unref (sub.item);
            break;
          }
        }
        track->eos = FALSE;
      }
      drop = TRUE;
      break;
    }
    case GST_EVENT_EOS:
    {
      if (track->pending_srcpad != NULL) {
        GST_DEBUG_ID (track->id,
            "Dropping EOS because we have a pending pad switch");
        drop = TRUE;
      } else {
        track->eos = TRUE;
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_FLUSH_START:
    {
      /* Drop flush events */
      drop = TRUE;
      break;
    }
    default:
      break;
  }

  if (drop || !GST_EVENT_IS_SERIALIZED (event)) {
    GST_DEBUG_ID (track->id, "dropping event %s", GST_EVENT_TYPE_NAME (event));
    gst_event_unref (event);
    TRACKS_UNLOCK (demux);
    /* Silently "accept" them */
    return TRUE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      guint64 seg_seqnum = gst_event_get_seqnum (event);

      if (track->input_segment_seqnum == seg_seqnum) {
        GST_DEBUG_ID (track->id, "Ignoring duplicate segment");
        gst_event_unref (event);
        TRACKS_UNLOCK (demux);

        return TRUE;
      }

      if (seg_seqnum != demux->priv->segment_seqnum) {
        GST_DEBUG_OBJECT (pad, "Ignoring non-current segment");
        gst_event_unref (event);
        TRACKS_UNLOCK (demux);

        return TRUE;
      }

      track->input_segment_seqnum = seg_seqnum;
      gst_event_copy_segment (event, &track->input_segment);
      if (track->input_segment.rate >= 0)
        track->input_segment.position = track->input_segment.start;
      else
        track->input_segment.position = track->input_segment.stop;
      GST_DEBUG_ID (track->id, "stored segment %" GST_SEGMENT_FORMAT,
          &track->input_segment);
      timestamp = track->input_segment.position;
      is_discont = TRUE;

      break;
    }
    case GST_EVENT_GAP:
    {
      gst_event_parse_gap (event, &timestamp, &duration);

      if (!GST_CLOCK_TIME_IS_VALID (timestamp)) {
        GST_DEBUG_ID (track->id, "Dropping gap event with invalid timestamp");
        goto drop_ok;
      }

      break;
    }
    default:
      break;
  }

  track_queue_data_locked (demux, track, (GstMiniObject *) event, 0,
      timestamp, duration, is_discont);

  /* Recalculate buffering */
  demux_update_buffering_locked (demux);
  demux_post_buffering_locked (demux);

  TRACKS_UNLOCK (demux);

  return TRUE;

  /* errors */
drop_ok:
  {
    gst_event_unref (event);
    TRACKS_UNLOCK (demux);
    return TRUE;
  }
}

static void
track_sinkpad_unlinked_cb (GstPad * sinkpad, GstPad * parsebin_srcpad,
    GstAdaptiveDemuxTrack * track)
{
  GST_DEBUG_OBJECT (sinkpad, "Got unlinked from %s:%s",
      GST_DEBUG_PAD_NAME (parsebin_srcpad));

  if (track->pending_srcpad) {
    GST_DEBUG_OBJECT (sinkpad, "linking to pending pad %s:%s",
        GST_DEBUG_PAD_NAME (track->pending_srcpad));

    if (gst_pad_link (track->pending_srcpad, sinkpad) != GST_PAD_LINK_OK) {
      GST_ERROR_OBJECT (sinkpad, "could not link pending pad !");
    }
    gst_object_unref (track->pending_srcpad);
    track->pending_srcpad = NULL;
  }
}

/* TRACKS_LOCK held
 * Call this to update the track next_position with timed data  */
void
gst_adaptive_demux_track_update_next_position (GstAdaptiveDemuxTrack * track)
{
  guint i, len;

  /* If filling a gap, the next position is the gap position */
  if (track->gap_position != GST_CLOCK_TIME_NONE) {
    track->next_position =
        my_segment_to_running_time (&track->output_segment,
        track->gap_position);
    return;
  }

  len = gst_queue_array_get_length (track->queue);
  for (i = 0; i < len; i++) {
    TrackQueueItem *item = gst_queue_array_peek_nth_struct (track->queue, i);

    if (item->runningtime != GST_CLOCK_STIME_NONE) {
      GST_DEBUG_ID (track->id,
          "next position %" GST_STIME_FORMAT,
          GST_STIME_ARGS (item->runningtime));
      track->next_position = item->runningtime;
      return;
    }
  }
  track->next_position = GST_CLOCK_STIME_NONE;

  GST_DEBUG_ID (track->id, "Track doesn't have any pending timed data");
}

/* TRACKS_LOCK held. Recomputes the level_time for the track */
void
gst_adaptive_demux_track_update_level_locked (GstAdaptiveDemuxTrack * track)
{
  GstAdaptiveDemux *demux = track->demux;
  GstClockTimeDiff output_time;

  if (GST_CLOCK_STIME_IS_VALID (track->output_time))
    output_time = MAX (track->output_time, demux->priv->global_output_position);
  else
    output_time = MIN (track->input_time, demux->priv->global_output_position);

  if (track->input_time >= output_time)
    track->level_time = track->input_time - output_time;
  else
    track->level_time = 0;

  GST_LOG_ID (track->id,
      "input_time:%" GST_STIME_FORMAT " output_time:%"
      GST_STIME_FORMAT " level:%" GST_TIME_FORMAT,
      GST_STIME_ARGS (track->input_time),
      GST_STIME_ARGS (track->output_time), GST_TIME_ARGS (track->level_time));
}

static void
_demux_track_free (GstAdaptiveDemuxTrack * track)
{
  GST_DEBUG_ID (track->id, "freeing track");

  g_free (track->stream_id);
  g_free (track->upstream_stream_id);
  g_free (track->id);

  if (track->pending_srcpad)
    gst_object_unref (track->pending_srcpad);

  if (track->generic_caps)
    gst_caps_unref (track->generic_caps);
  gst_object_unref (track->stream_object);
  if (track->tags)
    gst_tag_list_unref (track->tags);
  gst_queue_array_free (track->queue);

  gst_event_store_deinit (&track->sticky_events);

  if (track->element != NULL) {
    gst_element_set_state (track->element, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (track->demux), track->element);
  }

  g_free (track);
}

GstAdaptiveDemuxTrack *
gst_adaptive_demux_track_ref (GstAdaptiveDemuxTrack * track)
{
  g_return_val_if_fail (track != NULL, NULL);
  GST_TRACE_ID (track->id, "%d -> %d", track->ref_count, track->ref_count + 1);
  g_atomic_int_inc (&track->ref_count);

  return track;
}

void
gst_adaptive_demux_track_unref (GstAdaptiveDemuxTrack * track)
{
  g_return_if_fail (track != NULL);

  GST_TRACE_ID (track->id, "%d -> %d", track->ref_count, track->ref_count - 1);
  if (g_atomic_int_dec_and_test (&track->ref_count)) {
    _demux_track_free (track);
  }
}

static void
_track_queue_item_clear (TrackQueueItem * item)
{
  if (item->item) {
    gst_mini_object_unref ((GstMiniObject *) item->item);
    item->item = NULL;
  }
}

/* Internal function which actually adds the elements to the demuxer */
gboolean
gst_adaptive_demux_track_add_elements (GstAdaptiveDemuxTrack * track,
    guint period_num)
{
  GstAdaptiveDemux *demux = track->demux;
  gchar *tmpid;
  guint i, len;

  /* Store the period number for debugging output */
  track->period_num = period_num;

  tmpid = g_strdup_printf ("%s-period%d", track->id, period_num);
  g_free (track->id);
  track->id = tmpid;
  len = strlen (track->id);
  for (i = 0; i < len; i++)
    if (track->id[i] == ' ')
      track->id[i] = '_';
  track->element = gst_bin_new (track->id);

  track->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  g_signal_connect (track->sinkpad, "unlinked",
      (GCallback) track_sinkpad_unlinked_cb, track);
  gst_element_add_pad (GST_ELEMENT_CAST (track->element), track->sinkpad);
  gst_pad_set_element_private (track->sinkpad, track);
  gst_pad_set_chain_function (track->sinkpad, _track_sink_chain_function);
  gst_pad_set_event_function (track->sinkpad, _track_sink_event_function);
  gst_pad_set_query_function (track->sinkpad, _track_sink_query_function);

  if (!gst_bin_add (GST_BIN_CAST (demux), track->element)) {
    track->element = NULL;
    return FALSE;
  }

  gst_element_sync_state_with_parent (track->element);
  return TRUE;
}

/**
 * gst_adaptive_demux_track_new:
 * @demux: a #GstAdaptiveDemux
 * @type: a #GstStreamType
 * @flags: a #GstStreamFlags
 * @stream_id: (transfer none): The stream id for the new track
 * @caps: (transfer full): The caps for the track
 * @tags: (allow-none) (transfer full): The tags for the track
 *
 * Create and register a new #GstAdaptiveDemuxTrack
 *
 * Returns: (transfer none) The new track
 */
GstAdaptiveDemuxTrack *
gst_adaptive_demux_track_new (GstAdaptiveDemux * demux,
    GstStreamType type,
    GstStreamFlags flags, gchar * stream_id, GstCaps * caps, GstTagList * tags)
{
  GstAdaptiveDemuxTrack *track;

  g_return_val_if_fail (stream_id != NULL, NULL);
  g_return_val_if_fail (type && type != GST_STREAM_TYPE_UNKNOWN, NULL);


  GST_DEBUG_OBJECT (demux, "type:%s stream_id:%s caps:%" GST_PTR_FORMAT,
      gst_stream_type_get_name (type), stream_id, caps);

  track = g_new0 (GstAdaptiveDemuxTrack, 1);
  g_atomic_int_set (&track->ref_count, 1);
  track->demux = demux;
  track->type = type;
  track->flags = flags;
  track->stream_id =
      gst_element_decorate_stream_id (GST_ELEMENT (demux), stream_id);
  track->id = g_strdup_printf ("track-%s", stream_id);
  track->period_num = (guint) (-1);
  track->generic_caps = caps;
  track->stream_object = gst_stream_new (track->stream_id, caps, type, flags);
  if (tags) {
    gst_stream_set_tags (track->stream_object, tags);
    track->tags = tags;
  }

  track->selected = FALSE;
  track->active = FALSE;
  track->draining = FALSE;

  track->queue = gst_queue_array_new_for_struct (sizeof (TrackQueueItem), 50);
  gst_queue_array_set_clear_func (track->queue,
      (GDestroyNotify) _track_queue_item_clear);

  gst_event_store_init (&track->sticky_events);

  track->waiting_add = TRUE;

  /* We have no fragment duration yet, so the buffering threshold is just the
   * low watermark in time for now */
  GST_OBJECT_LOCK (demux);
  track->buffering_threshold = demux->buffering_low_watermark_time;
  GST_OBJECT_UNLOCK (demux);

  gst_segment_init (&track->input_segment, GST_FORMAT_TIME);
  track->input_time = 0;
  track->input_segment_seqnum = GST_SEQNUM_INVALID;

  gst_segment_init (&track->output_segment, GST_FORMAT_TIME);
  track->gap_position = track->gap_duration = GST_CLOCK_TIME_NONE;

  track->output_time = GST_CLOCK_STIME_NONE;
  track->next_position = GST_CLOCK_STIME_NONE;

  track->update_next_segment = FALSE;

  track->level_bytes = 0;
  track->level_time = 0;

  return track;
}
