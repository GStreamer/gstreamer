/* GStreamer aggregator base class
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
 *
 * gstaggregator.c:
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
 * SECTION: gstaggregator
 * @short_description: manages a set of pads with the purpose of
 * aggregating their buffers.
 * @see_also: gstcollectpads for historical reasons.
 *
 * Manages a set of pads with the purpose of aggregating their buffers.
 * Control is given to the subclass when all pads have data.
 * <itemizedlist>
 *  <listitem><para>
 *    Base class for mixers and muxers. Subclasses should at least implement
 *    the #GstAggregatorClass.aggregate() virtual method.
 *  </para></listitem>
 *  <listitem><para>
 *    When data is queued on all pads, tha aggregate vmethod is called.
 *  </para></listitem>
 *  <listitem><para>
 *    One can peek at the data on any given GstAggregatorPad with the
 *    gst_aggregator_pad_get_buffer () method, and take ownership of it
 *    with the gst_aggregator_pad_steal_buffer () method. When a buffer
 *    has been taken with steal_buffer (), a new buffer can be queued
 *    on that pad.
 *  </para></listitem>
 *  <listitem><para>
 *    If the subclass wishes to push a buffer downstream in its aggregate
 *    implementation, it should do so through the
 *    gst_aggregator_finish_buffer () method. This method will take care
 *    of sending and ordering mandatory events such as stream start, caps
 *    and segment.
 *  </para></listitem>
 *  <listitem><para>
 *    Same goes for EOS events, which should not be pushed directly by the
 *    subclass, it should instead return GST_FLOW_EOS in its aggregate
 *    implementation.
 *  </para></listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>             /* strlen */

#include "gstaggregator.h"


/*  Might become API */
static void gst_aggregator_merge_tags (GstAggregator * aggregator,
    const GstTagList * tags, GstTagMergeMode mode);
static void gst_aggregator_set_latency_property (GstAggregator * agg,
    gint64 latency);
static gint64 gst_aggregator_get_latency_property (GstAggregator * agg);


GST_DEBUG_CATEGORY_STATIC (aggregator_debug);
#define GST_CAT_DEFAULT aggregator_debug

/* GstAggregatorPad definitions */
#define PAD_LOCK(pad)   G_STMT_START {                            \
  GST_TRACE_OBJECT (pad, "Taking PAD lock from thread %p",            \
        g_thread_self());                                               \
  GST_OBJECT_LOCK (pad);                                                \
  GST_TRACE_OBJECT (pad, "Took PAD lock from thread %p",              \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_UNLOCK(pad)  G_STMT_START {                           \
  GST_TRACE_OBJECT (pad, "Releasing PAD lock from thread %p",         \
        g_thread_self());                                               \
  GST_OBJECT_UNLOCK (pad);                                              \
  GST_TRACE_OBJECT (pad, "Release PAD lock from thread %p",           \
        g_thread_self());                                               \
  } G_STMT_END


#define PAD_WAIT_EVENT(pad)   G_STMT_START {                            \
  GST_LOG_OBJECT (pad, "Waiting for EVENT on thread %p",                \
        g_thread_self());                                               \
  g_cond_wait(&(((GstAggregatorPad* )pad)->priv->event_cond),           \
      GST_OBJECT_GET_LOCK (pad));                                       \
  GST_LOG_OBJECT (pad, "DONE Waiting for EVENT on thread %p",           \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_BROADCAST_EVENT(pad) G_STMT_START {                        \
  GST_LOG_OBJECT (pad, "Signaling EVENT from thread %p",               \
        g_thread_self());                                              \
  g_cond_broadcast(&(((GstAggregatorPad* )pad)->priv->event_cond));    \
  } G_STMT_END

#define GST_AGGREGATOR_SETCAPS_LOCK(self)   G_STMT_START {        \
  GST_TRACE_OBJECT (self, "Taking SETCAPS lock from thread %p",   \
        g_thread_self());                                         \
  g_mutex_lock(&self->priv->setcaps_lock);                        \
  GST_TRACE_OBJECT (self, "Took SETCAPS lock from thread %p",     \
        g_thread_self());                                         \
  } G_STMT_END

#define GST_AGGREGATOR_SETCAPS_UNLOCK(self)   G_STMT_START {        \
  GST_TRACE_OBJECT (self, "Releasing SETCAPS lock from thread %p",  \
        g_thread_self());                                           \
  g_mutex_unlock(&self->priv->setcaps_lock);                        \
  GST_TRACE_OBJECT (self, "Took SETCAPS lock from thread %p",       \
        g_thread_self());                                           \
  } G_STMT_END

#define PAD_STREAM_LOCK(pad)   G_STMT_START {                           \
  GST_TRACE_OBJECT (pad, "Taking lock from thread %p",                  \
        g_thread_self());                                               \
  g_mutex_lock(&pad->priv->stream_lock);                                \
  GST_TRACE_OBJECT (pad, "Took lock from thread %p",                    \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_STREAM_UNLOCK(pad)  G_STMT_START {                          \
  GST_TRACE_OBJECT (pad, "Releasing lock from thread %p",               \
        g_thread_self());                                               \
  g_mutex_unlock(&pad->priv->stream_lock);                              \
  GST_TRACE_OBJECT (pad, "Release lock from thread %p",                 \
        g_thread_self());                                               \
  } G_STMT_END

#define SRC_STREAM_LOCK(self)   G_STMT_START {                             \
  GST_TRACE_OBJECT (self, "Taking src STREAM lock from thread %p",         \
        g_thread_self());                                                  \
  g_mutex_lock(&self->priv->src_lock);                                     \
  GST_TRACE_OBJECT (self, "Took src STREAM lock from thread %p",           \
        g_thread_self());                                                  \
  } G_STMT_END

#define SRC_STREAM_UNLOCK(self)  G_STMT_START {                            \
  GST_TRACE_OBJECT (self, "Releasing src STREAM lock from thread %p",      \
        g_thread_self());                                                  \
  g_mutex_unlock(&self->priv->src_lock);                                   \
  GST_TRACE_OBJECT (self, "Released src STREAM lock from thread %p",       \
        g_thread_self());                                                  \
  } G_STMT_END

#define SRC_STREAM_WAIT(self) G_STMT_START {                               \
  GST_LOG_OBJECT (self, "Waiting for src STREAM on thread %p",             \
        g_thread_self());                                                  \
  g_cond_wait(&(self->priv->src_cond), &(self->priv->src_lock));           \
  GST_LOG_OBJECT (self, "DONE Waiting for src STREAM on thread %p",        \
        g_thread_self());                                                  \
  } G_STMT_END

#define SRC_STREAM_BROADCAST(self) G_STMT_START {                 \
    GST_LOG_OBJECT (self, "Signaling src STREAM from thread %p",           \
        g_thread_self());                                                  \
    if (self->priv->aggregate_id)                                          \
      gst_clock_id_unschedule (self->priv->aggregate_id);                  \
    g_cond_broadcast(&(self->priv->src_cond));                             \
  } G_STMT_END

struct _GstAggregatorPadPrivate
{
  gboolean pending_flush_start;
  gboolean pending_flush_stop;
  gboolean pending_eos;
  gboolean flushing;

  GCond event_cond;

  GMutex stream_lock;
};

static gboolean
gst_aggregator_pad_flush (GstAggregatorPad * aggpad, GstAggregator * agg)
{
  GstAggregatorPadClass *klass = GST_AGGREGATOR_PAD_GET_CLASS (aggpad);

  PAD_LOCK (aggpad);
  aggpad->eos = FALSE;
  aggpad->priv->flushing = FALSE;
  PAD_UNLOCK (aggpad);

  if (klass->flush)
    return klass->flush (aggpad, agg);

  return TRUE;
}

/*************************************
 * GstAggregator implementation  *
 *************************************/
static GstElementClass *aggregator_parent_class = NULL;

struct _GstAggregatorPrivate
{
  gint padcount;

  /* Our state is >= PAUSED */
  gboolean running;             /* protected by SRC_STREAM_LOCK */

  gint seqnum;
  gboolean send_stream_start;
  gboolean send_segment;
  gboolean flush_seeking;
  gboolean pending_flush_start;
  gboolean send_eos;
  GstFlowReturn flow_return;

  gboolean send_stream_start;   /* protected by srcpad stream lock */
  gboolean send_segment;        /* protected by object lock */
  gboolean flush_seeking;       /* protected by object lock */
  gboolean pending_flush_start; /* protected by object lock */
  gboolean send_eos;            /* protected by srcpad stream lock */
  GstFlowReturn flow_return;    /* protected by object lock */

  GstCaps *srccaps;

  GstTagList *tags;
  gboolean tags_changed;

  /* Lock to prevent two src setcaps from happening at the same time  */
  GMutex setcaps_lock;

  gboolean latency_live;
  GstClockTime latency_min;
  GstClockTime latency_max;

  GstClockTime sub_latency_min;
  GstClockTime sub_latency_max;

  /* aggregate */
  GstClockID aggregate_id;
  GMutex src_lock;
  GCond src_cond;

  /* properties */
  gint64 latency;
};

typedef struct
{
  GstEvent *event;
  gboolean result;
  gboolean flush;

  gboolean one_actually_seeked;
} EventData;

#define DEFAULT_LATENCY        0

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_LAST
};

/**
 * gst_aggregator_iterate_sinkpads:
 * @self: The #GstAggregator
 * @func: (scope call): The function to call.
 * @user_data: (closure): The data to pass to @func.
 *
 * Iterate the sinkpads of aggregator to call a function on them.
 *
 * This method guarantees that @func will be called only once for each
 * sink pad.
 */
gboolean
gst_aggregator_iterate_sinkpads (GstAggregator * self,
    GstAggregatorPadForeachFunc func, gpointer user_data)
{
  gboolean result = FALSE;
  GstIterator *iter;
  gboolean done = FALSE;
  GValue item = { 0, };
  GList *seen_pads = NULL;

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (self));

  if (!iter)
    goto no_iter;

  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
      {
        GstAggregatorPad *pad;

        pad = g_value_get_object (&item);

        /* if already pushed, skip. FIXME, find something faster to tag pads */
        if (pad == NULL || g_list_find (seen_pads, pad)) {
          g_value_reset (&item);
          break;
        }

        GST_LOG_OBJECT (pad, "calling function %s on pad",
            GST_DEBUG_FUNCPTR_NAME (func));

        result = func (self, pad, user_data);

        done = !result;

        seen_pads = g_list_prepend (seen_pads, pad);

        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR_OBJECT (self,
            "Could not iterate over internally linked pads");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

  if (seen_pads == NULL) {
    GST_DEBUG_OBJECT (self, "No pad seen");
    return FALSE;
  }

  g_list_free (seen_pads);

no_iter:
  return result;
}

static gboolean
gst_aggregator_check_pads_ready (GstAggregator * self)
{
  GstAggregatorPad *pad;
  GList *l, *sinkpads;

  GST_LOG_OBJECT (self, "checking pads");

  GST_OBJECT_LOCK (self);

  sinkpads = GST_ELEMENT_CAST (self)->sinkpads;
  if (sinkpads == NULL)
    goto no_sinkpads;

  for (l = sinkpads; l != NULL; l = l->next) {
    pad = l->data;

    PAD_LOCK (pad);
    if (pad->buffer == NULL && !pad->eos) {
      GST_OBJECT_UNLOCK (pad);
      goto pad_not_ready;
    }
    PAD_UNLOCK (pad);

  }

  GST_OBJECT_UNLOCK (self);
  GST_LOG_OBJECT (self, "pads are ready");
  return TRUE;

no_sinkpads:
  {
    GST_LOG_OBJECT (self, "pads not ready: no sink pads");
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }
pad_not_ready:
  {
    GST_LOG_OBJECT (pad, "pad not ready to be aggregated yet");
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }
}

static void
gst_aggregator_reset_flow_values (GstAggregator * self)
{
  GST_OBJECT_LOCK (self);
  self->priv->flow_return = GST_FLOW_FLUSHING;
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  gst_segment_init (&self->segment, GST_FORMAT_TIME);
  GST_OBJECT_UNLOCK (self);
}

static inline void
gst_aggregator_push_mandatory_events (GstAggregator * self)
{
  GstAggregatorPrivate *priv = self->priv;

  if (self->priv->send_stream_start) {
    gchar s_id[32];

    GST_INFO_OBJECT (self, "pushing stream start");
    /* stream-start (FIXME: create id based on input ids) */
    g_snprintf (s_id, sizeof (s_id), "agg-%08x", g_random_int ());
    if (!gst_pad_push_event (self->srcpad, gst_event_new_stream_start (s_id))) {
      GST_WARNING_OBJECT (self->srcpad, "Sending stream start event failed");
    }
    self->priv->send_stream_start = FALSE;
  }

  if (self->priv->srccaps) {

    GST_INFO_OBJECT (self, "pushing caps: %" GST_PTR_FORMAT,
        self->priv->srccaps);
    if (!gst_pad_push_event (self->srcpad,
            gst_event_new_caps (self->priv->srccaps))) {
      GST_WARNING_OBJECT (self->srcpad, "Sending caps event failed");
    }
    gst_caps_unref (self->priv->srccaps);
    self->priv->srccaps = NULL;
  }

  GST_OBJECT_LOCK (self);
  if (self->priv->send_segment && !self->priv->flush_seeking) {
    GstEvent *segev = gst_event_new_segment (&self->segment);

    if (!self->priv->seqnum)
      self->priv->seqnum = gst_event_get_seqnum (segev);
    else
      gst_event_set_seqnum (segev, self->priv->seqnum);
    self->priv->send_segment = FALSE;
    GST_OBJECT_UNLOCK (self);

    GST_DEBUG_OBJECT (self, "pushing segment %" GST_PTR_FORMAT, segev);
    gst_pad_push_event (self->srcpad, segev);
  } else {
    GST_OBJECT_UNLOCK (self);
  }

  if (priv->tags && priv->tags_changed) {
    gst_pad_push_event (self->srcpad,
        gst_event_new_tag (gst_tag_list_ref (priv->tags)));
    priv->tags_changed = FALSE;
  }
}

/**
 * gst_aggregator_set_src_caps:
 * @self: The #GstAggregator
 * @caps: The #GstCaps to set on the src pad.
 *
 * Sets the caps to be used on the src pad.
 */
void
gst_aggregator_set_src_caps (GstAggregator * self, GstCaps * caps)
{
  GST_AGGREGATOR_SETCAPS_LOCK (self);
  gst_caps_replace (&self->priv->srccaps, caps);
  gst_aggregator_push_mandatory_events (self);
  GST_AGGREGATOR_SETCAPS_UNLOCK (self);
}

/**
 * gst_aggregator_finish_buffer:
 * @self: The #GstAggregator
 * @buffer: (transfer full): the #GstBuffer to push.
 *
 * This method will push the provided output buffer downstream. If needed,
 * mandatory events such as stream-start, caps, and segment events will be
 * sent before pushing the buffer.
 */
GstFlowReturn
gst_aggregator_finish_buffer (GstAggregator * self, GstBuffer * buffer)
{
  gst_aggregator_push_mandatory_events (self);

  GST_OBJECT_LOCK (self);
  if (!self->priv->flush_seeking && gst_pad_is_active (self->srcpad)) {
    GST_TRACE_OBJECT (self, "pushing buffer %" GST_PTR_FORMAT, buffer);
    GST_OBJECT_UNLOCK (self);
    return gst_pad_push (self->srcpad, buffer);
  } else {
    GST_INFO_OBJECT (self, "Not pushing (active: %i, flushing: %i)",
        self->priv->flush_seeking, gst_pad_is_active (self->srcpad));
    GST_OBJECT_UNLOCK (self);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static void
gst_aggregator_push_eos (GstAggregator * self)
{
  GstEvent *event;
  gst_aggregator_push_mandatory_events (self);

  self->priv->send_eos = FALSE;
  event = gst_event_new_eos ();
  gst_event_set_seqnum (event, self->priv->seqnum);
  gst_pad_push_event (self->srcpad, event);
}

static GstClockTime
gst_aggregator_get_next_time (GstAggregator * self)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (self);

  if (klass->get_next_time)
    return klass->get_next_time (self);

  return GST_CLOCK_TIME_NONE;
}

/* called with the src STREAM lock */
static gboolean
gst_aggregator_wait_and_check (GstAggregator * self, gboolean * timeout)
{
  GstClockTime latency_max, latency_min;
  GstClockTime start;
  gboolean live, res;

  *timeout = FALSE;

  SRC_STREAM_LOCK (self);

  gst_aggregator_get_latency (self, &live, &latency_min, &latency_max);

  if (gst_aggregator_check_pads_ready (self)) {
    GST_DEBUG_OBJECT (self, "all pads have data");
    SRC_STREAM_UNLOCK (self);

    return TRUE;
  }

  /* Before waiting, check if we're actually still running */
  if (!self->priv->running || !self->priv->send_eos) {
    SRC_STREAM_UNLOCK (self);

    return FALSE;
  }

  start = gst_aggregator_get_next_time (self);

  if (!live || !GST_IS_CLOCK (GST_ELEMENT_CLOCK (self))
      || !GST_CLOCK_TIME_IS_VALID (start)) {
    /* We wake up here when something happened, and below
     * then check if we're ready now. If we return FALSE,
     * we will be directly called again.
     */
    SRC_STREAM_WAIT (self);
  } else {
    GstClockTime base_time, time;
    GstClock *clock;
    GstClockReturn status;
    GstClockTimeDiff jitter;

    GST_DEBUG_OBJECT (self, "got subclass start time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start));

    GST_OBJECT_LOCK (self);
    base_time = GST_ELEMENT_CAST (self)->base_time;
    clock = GST_ELEMENT_CLOCK (self);
    if (clock)
      gst_object_ref (clock);
    GST_OBJECT_UNLOCK (self);

    time = base_time + start;

    if (GST_CLOCK_TIME_IS_VALID (latency_min)) {
      time += latency_min;
    } else {
      time += self->priv->latency;
    }

    GST_DEBUG_OBJECT (self, "possibly waiting for clock to reach %"
        GST_TIME_FORMAT " (base %" GST_TIME_FORMAT " start %" GST_TIME_FORMAT
        " latency max %" GST_TIME_FORMAT " latency min %" GST_TIME_FORMAT
        " current %" GST_TIME_FORMAT ")", GST_TIME_ARGS (time),
        GST_TIME_ARGS (GST_ELEMENT_CAST (self)->base_time),
        GST_TIME_ARGS (start), GST_TIME_ARGS (latency_max),
        GST_TIME_ARGS (latency_min),
        GST_TIME_ARGS (gst_clock_get_time (clock)));

    self->priv->aggregate_id = gst_clock_new_single_shot_id (clock, time);
    gst_object_unref (clock);
    SRC_STREAM_UNLOCK (self);

    jitter = 0;
    status = gst_clock_id_wait (self->priv->aggregate_id, &jitter);

    SRC_STREAM_LOCK (self);
    if (self->priv->aggregate_id) {
      gst_clock_id_unref (self->priv->aggregate_id);
      self->priv->aggregate_id = NULL;
    }

    GST_DEBUG_OBJECT (self,
        "clock returned %d (jitter: %s%" GST_TIME_FORMAT ")",
        status, (jitter < 0 ? "-" : " "),
        GST_TIME_ARGS ((jitter < 0 ? -jitter : jitter)));

    /* we timed out */
    if (status == GST_CLOCK_OK || status == GST_CLOCK_EARLY) {
      SRC_STREAM_UNLOCK (self);
      *timeout = TRUE;
      return TRUE;
    }
  }

  res = gst_aggregator_check_pads_ready (self);
  SRC_STREAM_UNLOCK (self);

  return res;
}

static void
gst_aggregator_aggregate_func (GstAggregator * self)
{
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (self);
  gboolean timeout = FALSE;

  if (self->priv->running == FALSE) {
    GST_DEBUG_OBJECT (self, "Not running anymore");
    return;
  }

  GST_LOG_OBJECT (self, "Checking aggregate");
  while (priv->send_eos && priv->running) {
    GstFlowReturn flow_return;

    if (!gst_aggregator_wait_and_check (self, &timeout))
      continue;

    GST_TRACE_OBJECT (self, "Actually aggregating!");

    flow_return = klass->aggregate (self, timeout);

    GST_OBJECT_LOCK (self);
    if (flow_return == GST_FLOW_FLUSHING && priv->flush_seeking)
      priv->flow_return = GST_FLOW_OK;
    else
      priv->flow_return = flow_return;
    GST_OBJECT_UNLOCK (self);

    if (flow_return == GST_FLOW_EOS) {
      gst_aggregator_push_eos (self);
    }

    GST_LOG_OBJECT (self, "flow return is %s", gst_flow_get_name (flow_return));

    if (flow_return != GST_FLOW_OK)
      break;
  }
}

static gboolean
gst_aggregator_start (GstAggregator * self)
{
  GstAggregatorClass *klass;
  gboolean result;

  self->priv->running = TRUE;
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  self->priv->send_eos = TRUE;
  self->priv->srccaps = NULL;
  self->priv->flow_return = GST_FLOW_OK;

  klass = GST_AGGREGATOR_GET_CLASS (self);

  if (klass->start)
    result = klass->start (self);
  else
    result = TRUE;

  return result;
}

static gboolean
_check_pending_flush_stop (GstAggregatorPad * pad)
{
  return (!pad->priv->pending_flush_stop && !pad->priv->pending_flush_start);
}

static gboolean
gst_aggregator_stop_srcpad_task (GstAggregator * self, GstEvent * flush_start)
{
  gboolean res = TRUE;

  GST_INFO_OBJECT (self, "%s srcpad task",
      flush_start ? "Pausing" : "Stopping");

  SRC_STREAM_LOCK (self);
  self->priv->running = FALSE;
  SRC_STREAM_BROADCAST (self);
  SRC_STREAM_UNLOCK (self);

  if (flush_start) {
    res = gst_pad_push_event (self->srcpad, flush_start);
  }

  gst_pad_stop_task (self->srcpad);

  return res;
}

static void
gst_aggregator_start_srcpad_task (GstAggregator * self)
{
  GST_INFO_OBJECT (self, "Starting srcpad task");

  self->priv->running = TRUE;
  gst_pad_start_task (GST_PAD (self->srcpad),
      (GstTaskFunction) gst_aggregator_aggregate_func, self, NULL);
}

static GstFlowReturn
gst_aggregator_flush (GstAggregator * self)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Flushing everything");
  GST_OBJECT_LOCK (self);
  priv->send_segment = TRUE;
  priv->flush_seeking = FALSE;
  g_atomic_int_set (&priv->tags_changed, FALSE);
  GST_OBJECT_UNLOCK (self);
  if (klass->flush)
    ret = klass->flush (self);

  return ret;
}


/* Called with GstAggregator's object lock held */

static gboolean
gst_aggregator_all_flush_stop_received_locked (GstAggregator * self)
{
  GList *tmp;
  GstAggregatorPad *tmppad;

  for (tmp = GST_ELEMENT (self)->sinkpads; tmp; tmp = tmp->next) {
    tmppad = (GstAggregatorPad *) tmp->data;

    if (_check_pending_flush_stop (tmppad) == FALSE) {
      GST_DEBUG_OBJECT (tmppad, "Is not last %i -- %i",
          tmppad->priv->pending_flush_start, tmppad->priv->pending_flush_stop);
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_aggregator_flush_start (GstAggregator * self, GstAggregatorPad * aggpad,
    GstEvent * event)
{
  GstBuffer *tmpbuf;
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorPadPrivate *padpriv = aggpad->priv;

  g_atomic_int_set (&aggpad->priv->flushing, TRUE);
  /*  Remove pad buffer and wake up the streaming thread */
  tmpbuf = gst_aggregator_pad_steal_buffer (aggpad);
  gst_buffer_replace (&tmpbuf, NULL);
  PAD_STREAM_LOCK (aggpad);
  if (g_atomic_int_compare_and_exchange (&padpriv->pending_flush_start,
          TRUE, FALSE) == TRUE) {
    GST_DEBUG_OBJECT (aggpad, "Expecting FLUSH_STOP now");
    g_atomic_int_set (&padpriv->pending_flush_stop, TRUE);
  }

  GST_OBJECT_LOCK (self);
  if (priv->flush_seeking) {
    /* If flush_seeking we forward the first FLUSH_START */
    if (priv->pending_flush_start) {
      priv->pending_flush_start = FALSE;
      GST_OBJECT_UNLOCK (self);

      GST_INFO_OBJECT (self, "Flushing, pausing srcpad task");
      gst_aggregator_stop_srcpad_task (self, event);
      priv->flow_return = GST_FLOW_OK;

      GST_INFO_OBJECT (self, "Getting STREAM_LOCK while seeking");
      GST_PAD_STREAM_LOCK (self->srcpad);
      GST_LOG_OBJECT (self, "GOT STREAM_LOCK");
      event = NULL;
    } else {
      GST_OBJECT_UNLOCK (self);
      gst_event_unref (event);
    }
  } else {
    GST_OBJECT_UNLOCK (self);
    gst_event_unref (event);
  }
  PAD_STREAM_UNLOCK (aggpad);

  tmpbuf = gst_aggregator_pad_steal_buffer (aggpad);
  gst_buffer_replace (&tmpbuf, NULL);
}

/* GstAggregator vmethods default implementations */
static gboolean
gst_aggregator_default_sink_event (GstAggregator * self,
    GstAggregatorPad * aggpad, GstEvent * event)
{
  gboolean res = TRUE;
  GstPad *pad = GST_PAD (aggpad);
  GstAggregatorPrivate *priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      gst_aggregator_flush_start (self, aggpad, event);
      /* We forward only in one case: right after flush_seeking */
      event = NULL;
      goto eat;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (aggpad, "Got FLUSH_STOP");

      gst_aggregator_pad_flush (aggpad, self);
      GST_OBJECT_LOCK (self);
      if (priv->flush_seeking) {
        g_atomic_int_set (&aggpad->priv->pending_flush_stop, FALSE);
        if (gst_aggregator_all_flush_stop_received_locked (self)) {
          GST_OBJECT_UNLOCK (self);
          /* That means we received FLUSH_STOP/FLUSH_STOP on
           * all sinkpads -- Seeking is Done... sending FLUSH_STOP */
          gst_aggregator_flush (self);
          gst_pad_push_event (self->srcpad, event);
          event = NULL;
          SRC_STREAM_LOCK (self);
          priv->send_eos = TRUE;
          SRC_STREAM_BROADCAST (self);
          SRC_STREAM_UNLOCK (self);

          GST_INFO_OBJECT (self, "Releasing source pad STREAM_LOCK");
          GST_PAD_STREAM_UNLOCK (self->srcpad);
          gst_aggregator_start_srcpad_task (self);
        } else {
          GST_OBJECT_UNLOCK (self);
        }
      } else {
        GST_OBJECT_UNLOCK (self);
      }

      /* We never forward the event */
      goto eat;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (aggpad, "EOS");

      /* We still have a buffer, and we don't want the subclass to have to
       * check for it. Mark pending_eos, eos will be set when steal_buffer is
       * called
       */
      SRC_STREAM_LOCK (self);
      PAD_LOCK (aggpad);
      if (!aggpad->buffer) {
        aggpad->eos = TRUE;
      } else {
        aggpad->priv->pending_eos = TRUE;
      }
      PAD_UNLOCK (aggpad);

      SRC_STREAM_BROADCAST (self);
      SRC_STREAM_UNLOCK (self);
      goto eat;
    }
    case GST_EVENT_SEGMENT:
    {
      PAD_LOCK (aggpad);
      gst_event_copy_segment (event, &aggpad->segment);
      self->priv->seqnum = gst_event_get_seqnum (event);
      PAD_UNLOCK (aggpad);
      goto eat;
    }
    case GST_EVENT_STREAM_START:
    {
      goto eat;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *tags;

      gst_event_parse_tag (event, &tags);

      if (gst_tag_list_get_scope (tags) == GST_TAG_SCOPE_STREAM) {
        gst_aggregator_merge_tags (self, tags, GST_TAG_MERGE_REPLACE);
        gst_event_unref (event);
        event = NULL;
        goto eat;
      }
      break;
    }
    default:
    {
      break;
    }
  }

  GST_DEBUG_OBJECT (pad, "Forwarding event: %" GST_PTR_FORMAT, event);
  return gst_pad_event_default (pad, GST_OBJECT (self), event);

eat:
  GST_DEBUG_OBJECT (pad, "Eating event: %" GST_PTR_FORMAT, event);
  if (event)
    gst_event_unref (event);

  return res;
}

static inline gboolean
gst_aggregator_stop_pad (GstAggregator * self, GstAggregatorPad * pad,
    gpointer unused_udata)
{
  gst_aggregator_pad_flush (pad, self);

  return TRUE;
}

static gboolean
gst_aggregator_stop (GstAggregator * agg)
{
  GstAggregatorClass *klass;
  gboolean result;

  gst_aggregator_reset_flow_values (agg);

  gst_aggregator_iterate_sinkpads (agg, gst_aggregator_stop_pad, NULL);

  if (agg->priv->tags)
    gst_tag_list_unref (agg->priv->tags);
  agg->priv->tags = NULL;

  klass = GST_AGGREGATOR_GET_CLASS (agg);

  if (klass->stop)
    result = klass->stop (agg);
  else
    result = TRUE;

  return result;
}

/* GstElement vmethods implementations */
static GstStateChangeReturn
gst_aggregator_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAggregator *self = GST_AGGREGATOR (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_aggregator_start (self))
        goto error_start;
      break;
    default:
      break;
  }

  if ((ret =
          GST_ELEMENT_CLASS (aggregator_parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;


  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_aggregator_stop (self)) {
        /* What to do in this case? Error out? */
        GST_ERROR_OBJECT (self, "Subclass failed to stop.");
      }
      break;
    default:
      break;
  }

  return ret;

/* ERRORS */
failure:
  {
    GST_ERROR_OBJECT (element, "parent failed state change");
    return ret;
  }
error_start:
  {
    GST_ERROR_OBJECT (element, "Subclass failed to start");
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
gst_aggregator_release_pad (GstElement * element, GstPad * pad)
{
  GstAggregator *self = GST_AGGREGATOR (element);
  GstBuffer *tmpbuf;

  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  GST_INFO_OBJECT (pad, "Removing pad");

  SRC_STREAM_LOCK (self);
  g_atomic_int_set (&aggpad->priv->flushing, TRUE);
  tmpbuf = gst_aggregator_pad_steal_buffer (aggpad);
  gst_buffer_replace (&tmpbuf, NULL);
  gst_element_remove_pad (element, pad);

  SRC_STREAM_BROADCAST (self);
  SRC_STREAM_UNLOCK (self);
}

static GstPad *
gst_aggregator_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstAggregator *self;
  GstAggregatorPad *agg_pad;

  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstAggregatorPrivate *priv = GST_AGGREGATOR (element)->priv;

  self = GST_AGGREGATOR (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%u")) {
    gint serial = 0;
    gchar *name = NULL;

    GST_OBJECT_LOCK (element);
    if (req_name == NULL || strlen (req_name) < 6
        || !g_str_has_prefix (req_name, "sink_")) {
      /* no name given when requesting the pad, use next available int */
      priv->padcount++;
    } else {
      /* parse serial number from requested padname */
      serial = g_ascii_strtoull (&req_name[5], NULL, 10);
      if (serial >= priv->padcount)
        priv->padcount = serial;
    }

    name = g_strdup_printf ("sink_%u", priv->padcount);
    agg_pad = g_object_new (GST_AGGREGATOR_GET_CLASS (self)->sinkpads_type,
        "name", name, "direction", GST_PAD_SINK, "template", templ, NULL);
    g_free (name);

    GST_OBJECT_UNLOCK (element);

  } else {
    return NULL;
  }

  GST_DEBUG_OBJECT (element, "Adding pad %s", GST_PAD_NAME (agg_pad));

  if (priv->running)
    gst_pad_set_active (GST_PAD (agg_pad), TRUE);

  /* add the pad to the element */
  gst_element_add_pad (element, GST_PAD (agg_pad));

  return GST_PAD (agg_pad);
}

typedef struct
{
  GstClockTime min, max;
  gboolean live;
} LatencyData;

static gboolean
gst_aggregator_query_sink_latency_foreach (GstAggregator * self,
    GstAggregatorPad * pad, gpointer user_data)
{
  LatencyData *data = user_data;
  GstClockTime min, max;
  GstQuery *query;
  gboolean live, res;

  query = gst_query_new_latency ();
  res = gst_pad_peer_query (GST_PAD_CAST (pad), query);

  if (res) {
    gst_query_parse_latency (query, &live, &min, &max);

    GST_LOG_OBJECT (pad, "got latency live:%s min:%" G_GINT64_FORMAT
        " max:%" G_GINT64_FORMAT, live ? "true" : "false", min, max);

    if (min != GST_CLOCK_TIME_NONE && min > data->min)
      data->min = min;

    if (max != GST_CLOCK_TIME_NONE &&
        ((data->max != GST_CLOCK_TIME_NONE && max < data->max) ||
            (data->max == GST_CLOCK_TIME_NONE)))
      data->max = max;

    data->live |= live;
  }

  gst_query_unref (query);

  return TRUE;
}

/**
 * gst_aggregator_get_latency:
 * @self: a #GstAggregator
 * @live: (out) (allow-none): whether @self is live
 * @min_latency: (out) (allow-none): the configured minimum latency of @self
 * @max_latency: (out) (allow-none): the configured maximum latency of @self
 *
 * Retreives the latency values reported by @self in response to the latency
 * query.
 *
 * Typically only called by subclasses.
 */
void
gst_aggregator_get_latency (GstAggregator * self, gboolean * live,
    GstClockTime * min_latency, GstClockTime * max_latency)
{
  GstClockTime our_latency;
  GstClockTime min, max;

  g_return_if_fail (GST_IS_AGGREGATOR (self));

  /* latency_min is never GST_CLOCK_TIME_NONE by construction */
  min = self->priv->latency_min;
  max = self->priv->latency_max;

  min += self->priv->sub_latency_min;
  if (GST_CLOCK_TIME_IS_VALID (max)
      && GST_CLOCK_TIME_IS_VALID (self->priv->sub_latency_max))
    max += self->priv->sub_latency_max;

  our_latency = self->priv->latency;
  if (GST_CLOCK_TIME_IS_VALID (our_latency)) {
    min += our_latency;
    if (GST_CLOCK_TIME_IS_VALID (max))
      max += our_latency;
  }

  if (live)
    *live = self->priv->latency_live;
  if (min_latency)
    *min_latency = min;
  if (max_latency)
    *max_latency = max;
}

static gboolean
gst_aggregator_query_latency (GstAggregator * self, GstQuery * query)
{
  GstClockTime our_latency;
  LatencyData data;

  data.min = 0;
  data.max = GST_CLOCK_TIME_NONE;
  data.live = FALSE;

  /* query upstream's latency */
  SRC_STREAM_LOCK (self);
  gst_aggregator_iterate_sinkpads (self,
      gst_aggregator_query_sink_latency_foreach, &data);
  SRC_STREAM_UNLOCK (self);

  our_latency = self->priv->latency;

  if (data.live && GST_CLOCK_TIME_IS_VALID (our_latency) &&
      our_latency > data.max) {
    GST_ELEMENT_WARNING (self, CORE, NEGOTIATION,
        ("%s", "Latency too big"),
        ("The requested latency value is too big for the current pipeline.  "
            "Limiting to %" G_GINT64_FORMAT, data.max));
    self->priv->latency = data.max;
    /* FIXME: shouldn't we g_object_notify() the change here? */
  }

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (data.min))) {
    GST_WARNING_OBJECT (self, "Invalid minimum latency, using 0");
    data.min = 0;
  }

  if (G_UNLIKELY (data.min > data.max)) {
    GST_WARNING_OBJECT (self, "Minimum latency is greater than maximum latency "
        "(%" G_GINT64_FORMAT " > %" G_GINT64_FORMAT "). "
        "Clamping it at the maximum latency", data.min, data.max);
    data.min = data.max;
  }

  self->priv->latency_live = data.live;
  self->priv->latency_min = data.min;
  self->priv->latency_max = data.max;

  /* add our own */
  if (GST_CLOCK_TIME_IS_VALID (our_latency)) {
    if (GST_CLOCK_TIME_IS_VALID (data.min))
      data.min += our_latency;
    if (GST_CLOCK_TIME_IS_VALID (data.max))
      data.max += our_latency;
  }

  if (GST_CLOCK_TIME_IS_VALID (self->priv->sub_latency_min)
      && GST_CLOCK_TIME_IS_VALID (data.min))
    data.min += self->priv->sub_latency_min;
  if (GST_CLOCK_TIME_IS_VALID (self->priv->sub_latency_max)
      && GST_CLOCK_TIME_IS_VALID (data.max))
    data.max += self->priv->sub_latency_max;

  GST_DEBUG_OBJECT (self, "configured latency live:%s min:%" G_GINT64_FORMAT
      " max:%" G_GINT64_FORMAT, data.live ? "true" : "false", data.min,
      data.max);

  gst_query_set_latency (query, data.live, data.min, data.max);

  return TRUE;
}

static gboolean
gst_aggregator_send_event (GstElement * element, GstEvent * event)
{
  GstAggregator *self = GST_AGGREGATOR (element);

  GST_STATE_LOCK (element);
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK &&
      GST_STATE (element) < GST_STATE_PAUSED) {
    gdouble rate;
    GstFormat fmt;
    GstSeekFlags flags;
    GstSeekType start_type, stop_type;
    gint64 start, stop;

    gst_event_parse_seek (event, &rate, &fmt, &flags, &start_type,
        &start, &stop_type, &stop);
    gst_segment_do_seek (&self->segment, rate, fmt, flags, start_type, start,
        stop_type, stop, NULL);

    self->priv->seqnum = gst_event_get_seqnum (event);
    GST_DEBUG_OBJECT (element, "Storing segment %" GST_PTR_FORMAT, event);
  }
  GST_STATE_UNLOCK (element);


  return GST_ELEMENT_CLASS (aggregator_parent_class)->send_event (element,
      event);
}

static gboolean
gst_aggregator_default_src_query (GstAggregator * self, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SEEKING:
    {
      GstFormat format;

      /* don't pass it along as some (file)sink might claim it does
       * whereas with a collectpads in between that will not likely work */
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      gst_query_set_seeking (query, format, FALSE, 0, -1);
      res = TRUE;

      goto discard;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean ret;

      ret = gst_aggregator_query_latency (self, query);
      /* Wake up the src thread again, due to changed latencies
       * or changed live-ness we might have to adjust if we wait
       * on a deadline at all and how long.
       * This is only to unschedule the clock id, we don't really care
       * about the GCond here.
       */
      SRC_STREAM_LOCK (self);
      SRC_STREAM_BROADCAST (self);
      SRC_STREAM_UNLOCK (self);
      return ret;
    }
    default:
      break;
  }

  return gst_pad_query_default (self->srcpad, GST_OBJECT (self), query);

discard:
  return res;
}

static gboolean
gst_aggregator_event_forward_func (GstPad * pad, gpointer user_data)
{
  EventData *evdata = user_data;
  gboolean ret = TRUE;
  GstPad *peer = gst_pad_get_peer (pad);
  GstAggregatorPadPrivate *padpriv = GST_AGGREGATOR_PAD (pad)->priv;

  if (peer) {
    ret = gst_pad_send_event (peer, gst_event_ref (evdata->event));
    GST_DEBUG_OBJECT (pad, "return of event push is %d", ret);
    gst_object_unref (peer);
  }

  if (ret == FALSE) {
    if (GST_EVENT_TYPE (evdata->event) == GST_EVENT_SEEK)
      GST_ERROR_OBJECT (pad, "Event %" GST_PTR_FORMAT " failed", evdata->event);

    if (GST_EVENT_TYPE (evdata->event) == GST_EVENT_SEEK) {
      GstQuery *seeking = gst_query_new_seeking (GST_FORMAT_TIME);

      if (gst_pad_query (peer, seeking)) {
        gboolean seekable;

        gst_query_parse_seeking (seeking, NULL, &seekable, NULL, NULL);

        if (seekable == FALSE) {
          GST_INFO_OBJECT (pad,
              "Source not seekable, We failed but it does not matter!");

          ret = TRUE;
        }
      } else {
        GST_ERROR_OBJECT (pad, "Query seeking FAILED");
      }

      gst_query_unref (seeking);
    }

    if (evdata->flush) {
      padpriv->pending_flush_start = FALSE;
      padpriv->pending_flush_stop = FALSE;
    }
  } else {
    evdata->one_actually_seeked = TRUE;
  }

  evdata->result &= ret;

  /* Always send to all pads */
  return FALSE;
}

static EventData
gst_aggregator_forward_event_to_all_sinkpads (GstAggregator * self,
    GstEvent * event, gboolean flush)
{
  EventData evdata;

  evdata.event = event;
  evdata.result = TRUE;
  evdata.flush = flush;
  evdata.one_actually_seeked = FALSE;

  /* We first need to set all pads as flushing in a first pass
   * as flush_start flush_stop is sometimes sent synchronously
   * while we send the seek event */
  if (flush) {
    GList *l;

    GST_OBJECT_LOCK (self);
    for (l = GST_ELEMENT_CAST (self)->sinkpads; l != NULL; l = l->next) {
      GstAggregatorPad *pad = l->data;

      pad->priv->pending_flush_start = TRUE;
      pad->priv->pending_flush_stop = FALSE;
    }
    GST_OBJECT_UNLOCK (self);
  }

  gst_pad_forward (self->srcpad, gst_aggregator_event_forward_func, &evdata);

  gst_event_unref (event);

  return evdata;
}

static gboolean
gst_aggregator_do_seek (GstAggregator * self, GstEvent * event)
{
  gdouble rate;
  GstFormat fmt;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gboolean flush;
  EventData evdata;
  GstAggregatorPrivate *priv = self->priv;

  gst_event_parse_seek (event, &rate, &fmt, &flags, &start_type,
      &start, &stop_type, &stop);

  GST_INFO_OBJECT (self, "starting SEEK");

  flush = flags & GST_SEEK_FLAG_FLUSH;

  GST_OBJECT_LOCK (self);
  if (flush) {
    priv->pending_flush_start = TRUE;
    priv->flush_seeking = TRUE;
  }

  gst_segment_do_seek (&self->segment, rate, fmt, flags, start_type, start,
      stop_type, stop, NULL);
  GST_OBJECT_UNLOCK (self);

  /* forward the seek upstream */
  evdata = gst_aggregator_forward_event_to_all_sinkpads (self, event, flush);
  event = NULL;

  if (!evdata.result || !evdata.one_actually_seeked) {
    GST_OBJECT_LOCK (self);
    priv->flush_seeking = FALSE;
    priv->pending_flush_start = FALSE;
    GST_OBJECT_UNLOCK (self);
  }

  GST_INFO_OBJECT (self, "seek done, result: %d", evdata.result);

  return evdata.result;
}

static gboolean
gst_aggregator_default_src_event (GstAggregator * self, GstEvent * event)
{
  EventData evdata;
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gst_event_ref (event);
      res = gst_aggregator_do_seek (self, event);
      gst_event_unref (event);
      event = NULL;
      goto done;
    }
    case GST_EVENT_NAVIGATION:
    {
      /* navigation is rather pointless. */
      res = FALSE;
      gst_event_unref (event);
      goto done;
    }
    default:
    {
      break;
    }
  }

  evdata = gst_aggregator_forward_event_to_all_sinkpads (self, event, FALSE);
  res = evdata.result;

done:
  return res;
}

static gboolean
gst_aggregator_src_pad_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  return klass->src_event (GST_AGGREGATOR (parent), event);
}

static gboolean
gst_aggregator_src_pad_query_func (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  return klass->src_query (GST_AGGREGATOR (parent), query);
}

static gboolean
gst_aggregator_src_pad_activate_mode_func (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstAggregator *self = GST_AGGREGATOR (parent);
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  if (klass->src_activate) {
    if (klass->src_activate (self, mode, active) == FALSE) {
      return FALSE;
    }
  }

  if (active == TRUE) {
    switch (mode) {
      case GST_PAD_MODE_PUSH:
      {
        GST_INFO_OBJECT (pad, "Activating pad!");
        gst_aggregator_start_srcpad_task (self);
        return TRUE;
      }
      default:
      {
        GST_ERROR_OBJECT (pad, "Only supported mode is PUSH");
        return FALSE;
      }
    }
  }

  /* deactivating */
  GST_INFO_OBJECT (self, "Deactivating srcpad");
  gst_aggregator_stop_srcpad_task (self, FALSE);

  return TRUE;
}

static gboolean
gst_aggregator_default_sink_query (GstAggregator * self,
    GstAggregatorPad * aggpad, GstQuery * query)
{
  GstPad *pad = GST_PAD (aggpad);

  return gst_pad_query_default (pad, GST_OBJECT (self), query);
}

static void
gst_aggregator_finalize (GObject * object)
{
  GstAggregator *self = (GstAggregator *) object;

  g_mutex_clear (&self->priv->setcaps_lock);
  g_mutex_clear (&self->priv->src_lock);
  g_cond_clear (&self->priv->src_cond);

  G_OBJECT_CLASS (aggregator_parent_class)->finalize (object);
}

/*
 * gst_aggregator_set_latency_property:
 * @agg: a #GstAggregator
 * @latency: the new latency value.
 *
 * Sets the new latency value to @latency. This value is used to limit the
 * amount of time a pad waits for data to appear before considering the pad
 * as unresponsive.
 */
static void
gst_aggregator_set_latency_property (GstAggregator * self, gint64 latency)
{
  gboolean changed;

  g_return_if_fail (GST_IS_AGGREGATOR (self));

  GST_OBJECT_LOCK (self);

  if (self->priv->latency_live && self->priv->latency_max != 0 &&
      GST_CLOCK_TIME_IS_VALID (latency) && latency > self->priv->latency_max) {
    GST_ELEMENT_WARNING (self, CORE, NEGOTIATION,
        ("%s", "Latency too big"),
        ("The requested latency value is too big for the latency in the "
            "current pipeline.  Limiting to %" G_GINT64_FORMAT,
            self->priv->latency_max));
    latency = self->priv->latency_max;
  }

  changed = (self->priv->latency != latency);
  self->priv->latency = latency;
  GST_OBJECT_UNLOCK (self);

  if (changed)
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_message_new_latency (GST_OBJECT_CAST (self)));
}

/*
 * gst_aggregator_get_latency_property:
 * @agg: a #GstAggregator
 *
 * Gets the latency value. See gst_aggregator_set_latency for
 * more details.
 *
 * Returns: The time in nanoseconds to wait for data to arrive on a sink pad 
 * before a pad is deemed unresponsive. A value of -1 means an
 * unlimited time.
 */
static gint64
gst_aggregator_get_latency_property (GstAggregator * agg)
{
  gint64 res;

  g_return_val_if_fail (GST_IS_AGGREGATOR (agg), -1);

  GST_OBJECT_LOCK (agg);
  res = agg->priv->latency;
  GST_OBJECT_UNLOCK (agg);

  return res;
}

static void
gst_aggregator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAggregator *agg = GST_AGGREGATOR (object);

  switch (prop_id) {
    case PROP_LATENCY:
      gst_aggregator_set_latency_property (agg, g_value_get_int64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_aggregator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAggregator *agg = GST_AGGREGATOR (object);

  switch (prop_id) {
    case PROP_LATENCY:
      g_value_set_int64 (value, gst_aggregator_get_latency_property (agg));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GObject vmethods implementations */
static void
gst_aggregator_class_init (GstAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  aggregator_parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (GstAggregatorPrivate));

  GST_DEBUG_CATEGORY_INIT (aggregator_debug, "aggregator",
      GST_DEBUG_FG_MAGENTA, "GstAggregator");

  klass->sinkpads_type = GST_TYPE_AGGREGATOR_PAD;

  klass->sink_event = gst_aggregator_default_sink_event;
  klass->sink_query = gst_aggregator_default_sink_query;

  klass->src_event = gst_aggregator_default_src_event;
  klass->src_query = gst_aggregator_default_src_query;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_request_new_pad);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_aggregator_send_event);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_aggregator_change_state);

  gobject_class->set_property = gst_aggregator_set_property;
  gobject_class->get_property = gst_aggregator_get_property;
  gobject_class->finalize = gst_aggregator_finalize;

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_int64 ("latency", "Buffer latency",
          "Additional latency in live mode to allow upstream "
          "to take longer to produce buffers for the current "
          "position", 0,
          (G_MAXLONG == G_MAXINT64) ? G_MAXINT64 : (G_MAXLONG * GST_SECOND - 1),
          DEFAULT_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_REGISTER_FUNCPTR (gst_aggregator_stop_pad);
  GST_DEBUG_REGISTER_FUNCPTR (gst_aggregator_query_sink_latency_foreach);
}

static void
gst_aggregator_init (GstAggregator * self, GstAggregatorClass * klass)
{
  GstPadTemplate *pad_template;
  GstAggregatorPrivate *priv;

  g_return_if_fail (klass->aggregate != NULL);

  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_AGGREGATOR,
      GstAggregatorPrivate);

  priv = self->priv;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  priv->padcount = -1;
  priv->tags_changed = FALSE;

  self->priv->latency_live = FALSE;
  self->priv->latency_min = self->priv->sub_latency_min = 0;
  self->priv->latency_max = self->priv->sub_latency_max = GST_CLOCK_TIME_NONE;
  gst_aggregator_reset_flow_values (self);

  self->srcpad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_aggregator_src_pad_event_func));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_aggregator_src_pad_query_func));
  gst_pad_set_activatemode_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_aggregator_src_pad_activate_mode_func));

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->priv->latency = DEFAULT_LATENCY;

  g_mutex_init (&self->priv->setcaps_lock);
  g_mutex_init (&self->priv->src_lock);
  g_cond_init (&self->priv->src_cond);
}

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_aggregator_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstAggregatorClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_aggregator_class_init,
      NULL,
      NULL,
      sizeof (GstAggregator),
      0,
      (GInstanceInitFunc) gst_aggregator_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAggregator", &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static GstFlowReturn
gst_aggregator_pad_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstBuffer *actual_buf = buffer;
  GstAggregator *self = GST_AGGREGATOR (object);
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAggregatorClass *aggclass = GST_AGGREGATOR_GET_CLASS (object);
  GstFlowReturn flow_return;

  GST_DEBUG_OBJECT (aggpad, "Start chaining a buffer %" GST_PTR_FORMAT, buffer);

  PAD_STREAM_LOCK (aggpad);

  if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE)
    goto flushing;

  if (g_atomic_int_get (&aggpad->priv->pending_eos) == TRUE)
    goto eos;

  PAD_LOCK (aggpad);

  while (aggpad->buffer && g_atomic_int_get (&aggpad->priv->flushing) == FALSE) {
    GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
    PAD_WAIT_EVENT (aggpad);
  }
  PAD_UNLOCK (aggpad);

  if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE)
    goto flushing;

  if (aggclass->clip) {
    aggclass->clip (self, aggpad, buffer, &actual_buf);
  }

  SRC_STREAM_LOCK (self);
  PAD_LOCK (aggpad);
  if (aggpad->buffer)
    gst_buffer_unref (aggpad->buffer);
  aggpad->buffer = actual_buf;
  PAD_UNLOCK (aggpad);
  PAD_STREAM_UNLOCK (aggpad);

  if (gst_aggregator_check_pads_ready (self))
    SRC_STREAM_BROADCAST (self);
  SRC_STREAM_UNLOCK (self);

  GST_DEBUG_OBJECT (aggpad, "Done chaining");

  GST_OBJECT_LOCK (self);
  flow_return = priv->flow_return;
  GST_OBJECT_UNLOCK (self);

  return flow_return;

flushing:
  PAD_STREAM_UNLOCK (aggpad);

  gst_buffer_unref (buffer);
  GST_DEBUG_OBJECT (aggpad, "We are flushing");

  return GST_FLOW_FLUSHING;

eos:
  PAD_STREAM_UNLOCK (aggpad);

  gst_buffer_unref (buffer);
  GST_DEBUG_OBJECT (pad, "We are EOS already...");

  return GST_FLOW_EOS;
}

static gboolean
gst_aggregator_pad_query_func (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  if (GST_QUERY_IS_SERIALIZED (query)) {
    PAD_LOCK (aggpad);

    if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE) {
      PAD_UNLOCK (aggpad);
      goto flushing;
    }

    while (aggpad->buffer
        && g_atomic_int_get (&aggpad->priv->flushing) == FALSE) {
      GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
      PAD_WAIT_EVENT (aggpad);
    }
    PAD_UNLOCK (aggpad);

    if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE)
      goto flushing;
  }

  return klass->sink_query (GST_AGGREGATOR (parent),
      GST_AGGREGATOR_PAD (pad), query);

flushing:
  GST_DEBUG_OBJECT (aggpad, "Pad is flushing, dropping query");
  return FALSE;
}

static gboolean
gst_aggregator_pad_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  if (GST_EVENT_IS_SERIALIZED (event) && GST_EVENT_TYPE (event) != GST_EVENT_EOS
      && GST_EVENT_TYPE (event) != GST_EVENT_SEGMENT_DONE) {
    PAD_LOCK (aggpad);

    if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE
        && GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_STOP) {
      PAD_UNLOCK (aggpad);
      goto flushing;
    }

    while (aggpad->buffer
        && g_atomic_int_get (&aggpad->priv->flushing) == FALSE) {
      GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
      PAD_WAIT_EVENT (aggpad);
    }
    PAD_UNLOCK (aggpad);

    if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE
        && GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_STOP)
      goto flushing;
  }

  return klass->sink_event (GST_AGGREGATOR (parent),
      GST_AGGREGATOR_PAD (pad), event);

flushing:
  GST_DEBUG_OBJECT (aggpad, "Pad is flushing, dropping event");
  if (GST_EVENT_IS_STICKY (event))
    gst_pad_store_sticky_event (pad, event);
  gst_event_unref (event);
  return FALSE;
}

static gboolean
gst_aggregator_pad_activate_mode_func (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  if (active == FALSE) {
    PAD_LOCK (aggpad);
    g_atomic_int_set (&aggpad->priv->flushing, TRUE);
    gst_buffer_replace (&aggpad->buffer, NULL);
    PAD_BROADCAST_EVENT (aggpad);
    PAD_UNLOCK (aggpad);
  } else {
    PAD_LOCK (aggpad);
    g_atomic_int_set (&aggpad->priv->flushing, FALSE);
    PAD_BROADCAST_EVENT (aggpad);
    PAD_UNLOCK (aggpad);
  }

  return TRUE;
}

/***********************************
 * GstAggregatorPad implementation  *
 ************************************/
G_DEFINE_TYPE (GstAggregatorPad, gst_aggregator_pad, GST_TYPE_PAD);

static void
gst_aggregator_pad_constructed (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_aggregator_pad_chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_aggregator_pad_event_func));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_aggregator_pad_query_func));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR (gst_aggregator_pad_activate_mode_func));
}

static void
gst_aggregator_pad_finalize (GObject * object)
{
  GstAggregatorPad *pad = (GstAggregatorPad *) object;

  g_cond_clear (&pad->priv->event_cond);
  g_mutex_clear (&pad->priv->stream_lock);

  G_OBJECT_CLASS (gst_aggregator_pad_parent_class)->finalize (object);
}

static void
gst_aggregator_pad_dispose (GObject * object)
{
  GstAggregatorPad *pad = (GstAggregatorPad *) object;
  GstBuffer *buf;

  buf = gst_aggregator_pad_steal_buffer (pad);
  if (buf)
    gst_buffer_unref (buf);

  G_OBJECT_CLASS (gst_aggregator_pad_parent_class)->dispose (object);
}

static void
gst_aggregator_pad_class_init (GstAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstAggregatorPadPrivate));

  gobject_class->constructed = gst_aggregator_pad_constructed;
  gobject_class->finalize = gst_aggregator_pad_finalize;
  gobject_class->dispose = gst_aggregator_pad_dispose;
}

static void
gst_aggregator_pad_init (GstAggregatorPad * pad)
{
  pad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pad, GST_TYPE_AGGREGATOR_PAD,
      GstAggregatorPadPrivate);

  pad->buffer = NULL;
  g_cond_init (&pad->priv->event_cond);

  g_mutex_init (&pad->priv->stream_lock);
}

/**
 * gst_aggregator_pad_steal_buffer_unlocked:
 * @pad: the pad to get buffer from
 *
 * Steal the ref to the buffer currently queued in @pad.
 *
 * MUST be called with the pad's object lock held.
 *
 * Returns: (transfer full): The buffer in @pad or NULL if no buffer was
 *   queued. You should unref the buffer after usage.
 */
GstBuffer *
gst_aggregator_pad_steal_buffer_unlocked (GstAggregatorPad * pad)
{
  GstBuffer *buffer = NULL;

  if (pad->buffer) {
    GST_TRACE_OBJECT (pad, "Consuming buffer");
    buffer = pad->buffer;
    pad->buffer = NULL;
    if (pad->priv->pending_eos) {
      pad->priv->pending_eos = FALSE;
      pad->eos = TRUE;
    }
    PAD_BROADCAST_EVENT (pad);
    GST_DEBUG_OBJECT (pad, "Consumed: %" GST_PTR_FORMAT, buffer);
  }

  return buffer;
}

/**
 * gst_aggregator_pad_steal_buffer:
 * @pad: the pad to get buffer from
 *
 * Steal the ref to the buffer currently queued in @pad.
 *
 * Returns: (transfer full): The buffer in @pad or NULL if no buffer was
 *   queued. You should unref the buffer after usage.
 */
GstBuffer *
gst_aggregator_pad_steal_buffer (GstAggregatorPad * pad)
{
  GstBuffer *buffer = NULL;

  PAD_LOCK (pad);
  buffer = gst_aggregator_pad_steal_buffer_unlocked (pad);
  PAD_UNLOCK (pad);

  return buffer;
}

/**
 * gst_aggregator_pad_get_buffer:
 * @pad: the pad to get buffer from
 *
 * Returns: (transfer full): A reference to the buffer in @pad or
 * NULL if no buffer was queued. You should unref the buffer after
 * usage.
 */
GstBuffer *
gst_aggregator_pad_get_buffer (GstAggregatorPad * pad)
{
  GstBuffer *buffer = NULL;

  PAD_LOCK (pad);
  if (pad->buffer)
    buffer = gst_buffer_ref (pad->buffer);
  PAD_UNLOCK (pad);

  return buffer;
}

/**
 * gst_aggregator_merge_tags:
 * @self: a #GstAggregator
 * @tags: a #GstTagList to merge
 * @mode: the #GstTagMergeMode to use
 *
 * Adds tags to so-called pending tags, which will be processed
 * before pushing out data downstream.
 *
 * Note that this is provided for convenience, and the subclass is
 * not required to use this and can still do tag handling on its own.
 *
 * MT safe.
 */
void
gst_aggregator_merge_tags (GstAggregator * self,
    const GstTagList * tags, GstTagMergeMode mode)
{
  GstTagList *otags;

  g_return_if_fail (GST_IS_AGGREGATOR (self));
  g_return_if_fail (tags == NULL || GST_IS_TAG_LIST (tags));

  /* FIXME Check if we can use OBJECT lock here! */
  GST_OBJECT_LOCK (self);
  if (tags)
    GST_DEBUG_OBJECT (self, "merging tags %" GST_PTR_FORMAT, tags);
  otags = self->priv->tags;
  self->priv->tags = gst_tag_list_merge (self->priv->tags, tags, mode);
  if (otags)
    gst_tag_list_unref (otags);
  self->priv->tags_changed = TRUE;
  GST_OBJECT_UNLOCK (self);
}

/**
 * gst_aggregator_set_latency:
 * @self: a #GstAggregator
 * @min_latency: minimum latency
 * @max_latency: maximum latency
 *
 * Lets #GstAggregator sub-classes tell the baseclass what their internal
 * latency is. Will also post a LATENCY message on the bus so the pipeline
 * can reconfigure its global latency.
 */
void
gst_aggregator_set_latency (GstAggregator * self,
    GstClockTime min_latency, GstClockTime max_latency)
{
  g_return_if_fail (GST_IS_AGGREGATOR (self));
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (min_latency));
  g_return_if_fail (max_latency >= min_latency);

  GST_OBJECT_LOCK (self);
  self->priv->sub_latency_min = min_latency;
  self->priv->sub_latency_max = max_latency;
  GST_OBJECT_UNLOCK (self);

  gst_element_post_message (GST_ELEMENT_CAST (self),
      gst_message_new_latency (GST_OBJECT_CAST (self)));
}
