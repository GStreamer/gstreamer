/* GStreamer
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gststreamsynchronizer.h"

GST_DEBUG_CATEGORY_STATIC (stream_synchronizer_debug);
#define GST_CAT_DEFAULT stream_synchronizer_debug

#define GST_STREAM_SYNCHRONIZER_LOCK(obj) G_STMT_START {                   \
    GST_LOG_OBJECT (obj,                                                \
                    "locking from thread %p",                           \
                    g_thread_self ());                                  \
    g_mutex_lock (GST_STREAM_SYNCHRONIZER_CAST(obj)->lock);                \
    GST_LOG_OBJECT (obj,                                                \
                    "locked from thread %p",                            \
                    g_thread_self ());                                  \
} G_STMT_END

#define GST_STREAM_SYNCHRONIZER_UNLOCK(obj) G_STMT_START {                 \
    GST_LOG_OBJECT (obj,                                                \
                    "unlocking from thread %p",                         \
                    g_thread_self ());                                  \
    g_mutex_unlock (GST_STREAM_SYNCHRONIZER_CAST(obj)->lock);              \
} G_STMT_END

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

GST_BOILERPLATE (GstStreamSynchronizer, gst_stream_synchronizer,
    GstElement, GST_TYPE_ELEMENT);

typedef struct
{
  GstStreamSynchronizer *transform;
  guint stream_number;
  GstPad *srcpad;
  GstPad *sinkpad;
  GstSegment segment;

  gboolean wait;
  gboolean new_stream;
  gboolean drop_discont;

  gint64 running_time_diff;
} GstStream;

/* Must be called with lock! */
static GstPad *
gst_stream_get_other_pad (GstStream * stream, GstPad * pad)
{
  if (stream->sinkpad == pad)
    return gst_object_ref (stream->srcpad);
  else if (stream->srcpad == pad)
    return gst_object_ref (stream->sinkpad);

  return NULL;
}

static GstPad *
gst_stream_get_other_pad_from_pad (GstPad * pad)
{
  GstStreamSynchronizer *self =
      GST_STREAM_SYNCHRONIZER (gst_pad_get_parent (pad));
  GstStream *stream;
  GstPad *opad = NULL;

  GST_STREAM_SYNCHRONIZER_LOCK (self);
  stream = gst_pad_get_element_private (pad);
  if (!stream)
    goto out;

  opad = gst_stream_get_other_pad (stream, pad);

out:
  GST_STREAM_SYNCHRONIZER_UNLOCK (self);
  gst_object_unref (self);

  if (!opad)
    GST_WARNING_OBJECT (pad, "Trying to get other pad after releasing");

  return opad;
}

/* Generic pad functions */
static GstIterator *
gst_stream_synchronizer_iterate_internal_links (GstPad * pad)
{
  GstIterator *it = NULL;
  GstPad *opad;

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    it = gst_iterator_new_single (GST_TYPE_PAD, opad,
        (GstCopyFunction) gst_object_ref, (GFreeFunc) gst_object_unref);
    gst_object_unref (opad);
  }

  return it;
}

static gboolean
gst_stream_synchronizer_query (GstPad * pad, GstQuery * query)
{
  GstPad *opad;
  gboolean ret = FALSE;

  GST_LOG_OBJECT (pad, "Handling query %s", GST_QUERY_TYPE_NAME (query));

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_peer_query (opad, query);
    gst_object_unref (opad);
  }

  return ret;
}

static GstCaps *
gst_stream_synchronizer_getcaps (GstPad * pad)
{
  GstPad *opad;
  GstCaps *ret = NULL;

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_peer_get_caps (opad);
    gst_object_unref (opad);
  }

  if (ret == NULL)
    ret = gst_caps_new_any ();

  GST_LOG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_stream_synchronizer_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstPad *opad;
  gboolean ret = FALSE;

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_peer_accept_caps (opad, caps);
    gst_object_unref (opad);
  }

  GST_LOG_OBJECT (pad, "Caps%s accepted: %" GST_PTR_FORMAT, (ret ? "" : " not"),
      caps);

  return ret;
}

/* srcpad functions */
static gboolean
gst_stream_synchronizer_src_event (GstPad * pad, GstEvent * event)
{
  GstStreamSynchronizer *self =
      GST_STREAM_SYNCHRONIZER (gst_pad_get_parent (pad));
  GstPad *opad;
  gboolean ret = FALSE;

  GST_LOG_OBJECT (pad, "Handling event %s: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event->structure);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gint64 running_time_diff;
      GstStream *stream;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);
      gst_event_unref (event);

      GST_STREAM_SYNCHRONIZER_LOCK (self);
      stream = gst_pad_get_element_private (pad);
      if (stream)
        running_time_diff = stream->running_time_diff;
      else
        running_time_diff = -1;
      GST_STREAM_SYNCHRONIZER_UNLOCK (self);

      if (running_time_diff == -1) {
        GST_WARNING_OBJECT (pad, "QOS event before group start");
        goto out;
      } else if (timestamp < running_time_diff) {
        GST_DEBUG_OBJECT (pad, "QOS event from previous group");
        goto out;
      }

      GST_LOG_OBJECT (pad,
          "Adjusting QOS event: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT " = %"
          GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
          GST_TIME_ARGS (running_time_diff),
          GST_TIME_ARGS (timestamp - running_time_diff));

      timestamp -= running_time_diff;

      /* That case is invalid for QoS events */
      if (diff < 0 && -diff > timestamp) {
        GST_DEBUG_OBJECT (pad, "QOS event from previous group");
        ret = TRUE;
        goto out;
      }

      event = gst_event_new_qos (proportion, diff, timestamp);
      break;
    }
    default:
      break;
  }

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_push_event (opad, event);
    gst_object_unref (opad);
  }

out:
  gst_object_unref (self);

  return ret;
}

/* sinkpad functions */
static gboolean
gst_stream_synchronizer_sink_event (GstPad * pad, GstEvent * event)
{
  GstStreamSynchronizer *self =
      GST_STREAM_SYNCHRONIZER (gst_pad_get_parent (pad));
  GstPad *opad;
  gboolean ret = FALSE;

  GST_LOG_OBJECT (pad, "Handling event %s: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event->structure);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SINK_MESSAGE:{
      GstMessage *message;

      gst_event_parse_sink_message (event, &message);
      if (message->structure
          && gst_structure_has_name (message->structure,
              "playbin2-stream-changed")) {
        GstStream *stream;

        GST_STREAM_SYNCHRONIZER_LOCK (self);
        stream = gst_pad_get_element_private (pad);
        if (stream) {
          GList *l;
          gboolean all_wait = TRUE;

          GST_DEBUG_OBJECT (pad, "Stream %d changed", stream->stream_number);

          stream->wait = TRUE;
          stream->new_stream = TRUE;

          for (l = self->streams; l; l = l->next) {
            GstStream *ostream = l->data;

            all_wait = all_wait && ostream->wait;
            if (!all_wait)
              break;
          }
          if (all_wait) {
            gint64 last_stop = 0;

            GST_DEBUG_OBJECT (self, "All streams have changed -- unblocking");

            for (l = self->streams; l; l = l->next) {
              GstStream *ostream = l->data;
              gint64 stop_running_time;
              gint64 last_stop_running_time;

              ostream->wait = FALSE;

              stop_running_time =
                  gst_segment_to_running_time (&ostream->segment,
                  GST_FORMAT_TIME, ostream->segment.stop);
              last_stop_running_time =
                  gst_segment_to_running_time (&ostream->segment,
                  GST_FORMAT_TIME, ostream->segment.last_stop);
              last_stop =
                  MAX (last_stop, MAX (stop_running_time,
                      last_stop_running_time));
            }
            last_stop = MAX (0, last_stop);
            self->group_start_time = MAX (self->group_start_time, last_stop);

            GST_DEBUG_OBJECT (self, "New group start time: %" GST_TIME_FORMAT,
                GST_TIME_ARGS (self->group_start_time));

            g_cond_broadcast (self->stream_finish_cond);
          }
        }
        GST_STREAM_SYNCHRONIZER_UNLOCK (self);
      }
      gst_message_unref (message);
      break;
    }
    case GST_EVENT_NEWSEGMENT:{
      GstStream *stream;
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event,
          &update, &rate, &applied_rate, &format, &start, &stop, &position);

      GST_STREAM_SYNCHRONIZER_LOCK (self);
      stream = gst_pad_get_element_private (pad);
      if (stream) {
        if (stream->wait) {
          GST_DEBUG_OBJECT (pad, "Stream %d is waiting", stream->stream_number);
          g_cond_wait (self->stream_finish_cond, self->lock);
          stream = gst_pad_get_element_private (pad);
          if (stream)
            stream->wait = FALSE;
        }
      }

      if (stream && format == GST_FORMAT_TIME) {
        if (stream->new_stream) {
          gint64 last_stop_running_time = 0;
          gint64 stop_running_time = 0;

          if (stream->segment.format == GST_FORMAT_TIME) {
            last_stop_running_time =
                gst_segment_to_running_time (&stream->segment, GST_FORMAT_TIME,
                stream->segment.last_stop);
            last_stop_running_time = MAX (last_stop_running_time, 0);
            stop_running_time =
                gst_segment_to_running_time (&stream->segment, GST_FORMAT_TIME,
                stream->segment.stop);
            stop_running_time = MAX (last_stop_running_time, 0);

            if (stop_running_time != last_stop_running_time) {
              GST_WARNING_OBJECT (pad,
                  "Gap between last_stop and segment stop: %" GST_TIME_FORMAT
                  " != %" GST_TIME_FORMAT, GST_TIME_ARGS (stop_running_time),
                  GST_TIME_ARGS (last_stop_running_time));
            }

            if (stop_running_time < last_stop_running_time) {
              GST_DEBUG_OBJECT (pad, "Updating stop position");
              gst_pad_push_event (stream->srcpad,
                  gst_event_new_new_segment_full (TRUE, stream->segment.rate,
                      stream->segment.applied_rate, GST_FORMAT_TIME,
                      stream->segment.start, stream->segment.last_stop,
                      stream->segment.time));
              gst_segment_set_newsegment_full (&stream->segment, TRUE,
                  stream->segment.rate, stream->segment.applied_rate,
                  GST_FORMAT_TIME, stream->segment.start,
                  stream->segment.last_stop, stream->segment.time);
            }
            stop_running_time = MAX (stop_running_time, last_stop_running_time);
            GST_DEBUG_OBJECT (pad,
                "Stop running time of last group: %" GST_TIME_FORMAT,
                GST_TIME_ARGS (stop_running_time));
          }
          stream->new_stream = FALSE;
          stream->drop_discont = TRUE;

          if (stop_running_time < self->group_start_time) {
            gint64 diff = self->group_start_time - stop_running_time;

            GST_DEBUG_OBJECT (pad,
                "Advancing running time for other streams by: %"
                GST_TIME_FORMAT, GST_TIME_ARGS (diff));
            gst_pad_push_event (stream->srcpad,
                gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
                    GST_FORMAT_TIME, 0, diff, 0));
            gst_segment_set_newsegment_full (&stream->segment, FALSE, 1.0, 1.0,
                GST_FORMAT_TIME, 0, diff, 0);
          }
        }

        GST_DEBUG_OBJECT (pad, "Segment was: %" GST_SEGMENT_FORMAT,
            &stream->segment);
        gst_segment_set_newsegment_full (&stream->segment, update, rate,
            applied_rate, format, start, stop, position);
        GST_DEBUG_OBJECT (pad, "Segment now is: %" GST_SEGMENT_FORMAT,
            &stream->segment);

        GST_DEBUG_OBJECT (pad, "Stream start running time: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (stream->segment.accum));
        stream->running_time_diff = stream->segment.accum;
      } else if (stream) {
        GST_ERROR_OBJECT (pad, "Non-TIME segment");
        gst_segment_init (&stream->segment, GST_FORMAT_UNDEFINED);
      }
      GST_STREAM_SYNCHRONIZER_UNLOCK (self);
      break;
    }
    case GST_EVENT_FLUSH_STOP:{
      GstStream *stream;

      GST_STREAM_SYNCHRONIZER_LOCK (self);
      stream = gst_pad_get_element_private (pad);
      if (stream) {
        GST_DEBUG_OBJECT (pad, "Resetting segment for stream %d",
            stream->stream_number);
        gst_segment_init (&stream->segment, GST_FORMAT_UNDEFINED);

        stream->wait = FALSE;
        stream->new_stream = FALSE;
        stream->drop_discont = FALSE;
      }
      GST_STREAM_SYNCHRONIZER_UNLOCK (self);
      break;
    }
    default:
      break;
  }

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_push_event (opad, event);
    gst_object_unref (opad);
  }

  gst_object_unref (self);

  return ret;
}

static GstFlowReturn
gst_stream_synchronizer_sink_bufferalloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{
  GstPad *opad;
  GstFlowReturn ret = GST_FLOW_ERROR;

  GST_LOG_OBJECT (pad, "Allocating buffer: size=%u", size);

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_alloc_buffer (opad, offset, size, caps, buf);
    gst_object_unref (opad);
  }

  GST_LOG_OBJECT (pad, "Allocation: %s", gst_flow_get_name (ret));

  return ret;
}

static GstFlowReturn
gst_stream_synchronizer_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstStreamSynchronizer *self =
      GST_STREAM_SYNCHRONIZER (gst_pad_get_parent (pad));
  GstPad *opad;
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstStream *stream;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime timestamp_end = GST_CLOCK_TIME_NONE;

  GST_LOG_OBJECT (pad, "Handling buffer %p: size=%u, timestamp=%"
      GST_TIME_FORMAT " duration=%" GST_TIME_FORMAT
      " offset=%" G_GUINT64_FORMAT " offset_end=%" G_GUINT64_FORMAT,
      buffer, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_BUFFER_OFFSET (buffer), GST_BUFFER_OFFSET_END (buffer));

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer)
      && GST_BUFFER_DURATION_IS_VALID (buffer))
    timestamp_end = timestamp + GST_BUFFER_DURATION (buffer);

  GST_STREAM_SYNCHRONIZER_LOCK (self);
  stream = gst_pad_get_element_private (pad);

  if (stream && stream->drop_discont) {
    buffer = gst_buffer_make_metadata_writable (buffer);
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
    stream->drop_discont = FALSE;
  }

  if (stream && stream->segment.format == GST_FORMAT_TIME
      && GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_LOG_OBJECT (pad,
        "Updating last-stop from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stream->segment.last_stop), GST_TIME_ARGS (timestamp));
    gst_segment_set_last_stop (&stream->segment, GST_FORMAT_TIME, timestamp);
  }
  GST_STREAM_SYNCHRONIZER_UNLOCK (self);

  opad = gst_stream_get_other_pad_from_pad (pad);
  if (opad) {
    ret = gst_pad_push (opad, buffer);
    gst_object_unref (opad);
  }

  GST_LOG_OBJECT (pad, "Push returned: %s", gst_flow_get_name (ret));
  if (ret == GST_FLOW_OK) {
    GST_STREAM_SYNCHRONIZER_LOCK (self);
    stream = gst_pad_get_element_private (pad);
    if (stream && stream->segment.format == GST_FORMAT_TIME
        && GST_CLOCK_TIME_IS_VALID (timestamp_end)) {
      GST_LOG_OBJECT (pad,
          "Updating last-stop from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->segment.last_stop),
          GST_TIME_ARGS (timestamp_end));
      gst_segment_set_last_stop (&stream->segment, GST_FORMAT_TIME,
          timestamp_end);
    }
    GST_STREAM_SYNCHRONIZER_UNLOCK (self);
  }

  gst_object_unref (self);

  return ret;
}

/* GstElement vfuncs */
static GstPad *
gst_stream_synchronizer_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * name)
{
  GstStreamSynchronizer *self = GST_STREAM_SYNCHRONIZER (element);
  GstStream *stream;
  gchar *tmp;

  GST_STREAM_SYNCHRONIZER_LOCK (self);
  GST_DEBUG_OBJECT (self, "Requesting new pad for stream %d",
      self->current_stream_number);

  stream = g_slice_new0 (GstStream);
  stream->transform = self;
  stream->stream_number = self->current_stream_number;

  tmp = g_strdup_printf ("sink_%d", self->current_stream_number);
  stream->sinkpad = gst_pad_new_from_static_template (&sinktemplate, tmp);
  g_free (tmp);
  gst_pad_set_element_private (stream->sinkpad, stream);
  gst_pad_set_iterate_internal_links_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_iterate_internal_links));
  gst_pad_set_query_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_query));
  gst_pad_set_getcaps_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_getcaps));
  gst_pad_set_acceptcaps_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_acceptcaps));
  gst_pad_set_event_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_sink_event));
  gst_pad_set_chain_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_sink_chain));
  gst_pad_set_bufferalloc_function (stream->sinkpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_sink_bufferalloc));

  tmp = g_strdup_printf ("src_%d", self->current_stream_number);
  stream->srcpad = gst_pad_new_from_static_template (&srctemplate, tmp);
  g_free (tmp);
  gst_pad_set_element_private (stream->srcpad, stream);
  gst_pad_set_iterate_internal_links_function (stream->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_iterate_internal_links));
  gst_pad_set_query_function (stream->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_query));
  gst_pad_set_getcaps_function (stream->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_getcaps));
  gst_pad_set_acceptcaps_function (stream->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_acceptcaps));
  gst_pad_set_event_function (stream->srcpad,
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_src_event));

  gst_segment_init (&stream->segment, GST_FORMAT_UNDEFINED);

  self->streams = g_list_prepend (self->streams, stream);
  self->current_stream_number++;
  GST_STREAM_SYNCHRONIZER_UNLOCK (self);

  /* Add pads and activate unless we're going to NULL */
  g_static_rec_mutex_lock (GST_STATE_GET_LOCK (self));
  if (GST_STATE_TARGET (self) != GST_STATE_NULL) {
    gst_pad_set_active (stream->srcpad, TRUE);
    gst_pad_set_active (stream->sinkpad, TRUE);
  }
  gst_element_add_pad (GST_ELEMENT_CAST (self), stream->srcpad);
  gst_element_add_pad (GST_ELEMENT_CAST (self), stream->sinkpad);
  g_static_rec_mutex_unlock (GST_STATE_GET_LOCK (self));

  return stream->sinkpad;
}

/* Must be called with lock! */
static void
gst_stream_synchronizer_release_stream (GstStreamSynchronizer * self,
    GstStream * stream)
{
  GList *l;

  GST_DEBUG_OBJECT (self, "Releasing stream %d", stream->stream_number);

  for (l = self->streams; l; l = l->next) {
    if (l->data == stream) {
      self->streams = g_list_delete_link (self->streams, l);
      break;
    }
  }
  g_assert (l != NULL);

  gst_pad_set_element_private (stream->srcpad, NULL);
  gst_pad_set_element_private (stream->sinkpad, NULL);
  gst_pad_set_active (stream->srcpad, FALSE);
  gst_pad_set_active (stream->sinkpad, FALSE);
  gst_element_remove_pad (GST_ELEMENT_CAST (self), stream->srcpad);
  gst_element_remove_pad (GST_ELEMENT_CAST (self), stream->sinkpad);

  if (stream->segment.format == GST_FORMAT_TIME) {
    gint64 stop_running_time;
    gint64 last_stop_running_time;

    stop_running_time =
        gst_segment_to_running_time (&stream->segment, GST_FORMAT_TIME,
        stream->segment.stop);
    last_stop_running_time =
        gst_segment_to_running_time (&stream->segment, GST_FORMAT_TIME,
        stream->segment.last_stop);
    stop_running_time = MAX (stop_running_time, last_stop_running_time);

    GST_DEBUG_OBJECT (stream->sinkpad,
        "Stop running time was: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stop_running_time));

    self->group_start_time = MAX (self->group_start_time, stop_running_time);
  }

  g_slice_free (GstStream, stream);
}

static void
gst_stream_synchronizer_release_pad (GstElement * element, GstPad * pad)
{
  GstStreamSynchronizer *self = GST_STREAM_SYNCHRONIZER (element);
  GstStream *stream;

  GST_STREAM_SYNCHRONIZER_LOCK (self);
  stream = gst_pad_get_element_private (pad);
  if (stream) {
    g_assert (stream->sinkpad == pad);

    gst_stream_synchronizer_release_stream (self, stream);
  }
  GST_STREAM_SYNCHRONIZER_UNLOCK (self);
}

static GstStateChangeReturn
gst_stream_synchronizer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStreamSynchronizer *self = GST_STREAM_SYNCHRONIZER (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_DEBUG_OBJECT (self, "State change NULL->READY");
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (self, "State change READY->PAUSED");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_DEBUG_OBJECT (self, "State change PAUSED->PLAYING");
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:{
      GST_DEBUG_OBJECT (self, "State change READY->NULL");

      GST_STREAM_SYNCHRONIZER_LOCK (self);
      g_cond_broadcast (self->stream_finish_cond);
      while (self->streams)
        gst_stream_synchronizer_release_stream (self, self->streams->data);
      self->current_stream_number = 0;
      GST_STREAM_SYNCHRONIZER_UNLOCK (self);
      break;
    }
    default:
      break;
  }

  {
    GstStateChangeReturn bret;

    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    GST_DEBUG_OBJECT (self, "Base class state changed returned: %d", bret);
    if (G_UNLIKELY (bret == GST_STATE_CHANGE_FAILURE))
      return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT (self, "State change PLAYING->PAUSED");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      GList *l;

      GST_DEBUG_OBJECT (self, "State change PAUSED->READY");
      self->group_start_time = 0;

      GST_STREAM_SYNCHRONIZER_LOCK (self);
      for (l = self->streams; l; l = l->next) {
        GstStream *stream = l->data;

        gst_segment_init (&stream->segment, GST_FORMAT_UNDEFINED);
        stream->wait = FALSE;
        stream->new_stream = FALSE;
        stream->drop_discont = FALSE;
      }
      GST_STREAM_SYNCHRONIZER_UNLOCK (self);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:{
      GST_DEBUG_OBJECT (self, "State change READY->NULL");
      break;
    }
    default:
      break;
  }

  return ret;
}

/* GObject vfuncs */
static void
gst_stream_synchronizer_finalize (GObject * object)
{
  GstStreamSynchronizer *self = GST_STREAM_SYNCHRONIZER (object);

  if (self->lock) {
    g_mutex_free (self->lock);
    self->lock = NULL;
  }

  if (self->stream_finish_cond) {
    g_cond_free (self->stream_finish_cond);
    self->stream_finish_cond = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* GObject type initialization */
static void
gst_stream_synchronizer_init (GstStreamSynchronizer * self,
    GstStreamSynchronizerClass * klass)
{
  self->lock = g_mutex_new ();
  self->stream_finish_cond = g_cond_new ();
}

static void
gst_stream_synchronizer_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details_simple (gstelement_class,
      "Stream Synchronizer", "Generic",
      "Synchronizes a group of streams to have equal durations and starting points",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_stream_synchronizer_class_init (GstStreamSynchronizerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (stream_synchronizer_debug,
      "streamsynchronizer", 0, "Stream Synchronizer");

  gobject_class->finalize = gst_stream_synchronizer_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_change_state);
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_stream_synchronizer_release_pad);
}
