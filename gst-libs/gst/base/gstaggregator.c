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
 * @title: GstAggregator
 * @short_description: manages a set of pads with the purpose of
 * aggregating their buffers.
 * @see_also: gstcollectpads for historical reasons.
 *
 * Manages a set of pads with the purpose of aggregating their buffers.
 * Control is given to the subclass when all pads have data.
 *
 *  * Base class for mixers and muxers. Subclasses should at least implement
 *    the #GstAggregatorClass.aggregate() virtual method.
 *
 *  * When data is queued on all pads, tha aggregate vmethod is called.
 *
 *  * One can peek at the data on any given GstAggregatorPad with the
 *    gst_aggregator_pad_get_buffer () method, and take ownership of it
 *    with the gst_aggregator_pad_steal_buffer () method. When a buffer
 *    has been taken with steal_buffer (), a new buffer can be queued
 *    on that pad.
 *
 *  * If the subclass wishes to push a buffer downstream in its aggregate
 *    implementation, it should do so through the
 *    gst_aggregator_finish_buffer () method. This method will take care
 *    of sending and ordering mandatory events such as stream start, caps
 *    and segment.
 *
 *  * Same goes for EOS events, which should not be pushed directly by the
 *    subclass, it should instead return GST_FLOW_EOS in its aggregate
 *    implementation.
 *
 *  * Note that the aggregator logic regarding gap event handling is to turn
 *    these into gap buffers with matching PTS and duration. It will also
 *    flag these buffers with GST_BUFFER_FLAG_GAP and GST_BUFFER_FLAG_DROPPABLE
 *    to ease their identification and subsequent processing.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>             /* strlen */

#include "gstaggregator.h"

typedef enum
{
  GST_AGGREGATOR_START_TIME_SELECTION_ZERO,
  GST_AGGREGATOR_START_TIME_SELECTION_FIRST,
  GST_AGGREGATOR_START_TIME_SELECTION_SET
} GstAggregatorStartTimeSelection;

static GType
gst_aggregator_start_time_selection_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {GST_AGGREGATOR_START_TIME_SELECTION_ZERO,
          "Start at 0 running time (default)", "zero"},
      {GST_AGGREGATOR_START_TIME_SELECTION_FIRST,
          "Start at first observed input running time", "first"},
      {GST_AGGREGATOR_START_TIME_SELECTION_SET,
          "Set start time with start-time property", "set"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstAggregatorStartTimeSelection", values);
  }
  return gtype;
}

/*  Might become API */
static void gst_aggregator_merge_tags (GstAggregator * aggregator,
    const GstTagList * tags, GstTagMergeMode mode);
static void gst_aggregator_set_latency_property (GstAggregator * agg,
    gint64 latency);
static gint64 gst_aggregator_get_latency_property (GstAggregator * agg);


/* Locking order, locks in this element must always be taken in this order
 *
 * standard sink pad stream lock -> GST_PAD_STREAM_LOCK (aggpad)
 * Aggregator pad flush lock -> PAD_FLUSH_LOCK(aggpad)
 * standard src pad stream lock -> GST_PAD_STREAM_LOCK (srcpad)
 * Aggregator src lock -> SRC_LOCK(agg) w/ SRC_WAIT/BROADCAST
 * standard element object lock -> GST_OBJECT_LOCK(agg)
 * Aggregator pad lock -> PAD_LOCK (aggpad) w/ PAD_WAIT/BROADCAST_EVENT(aggpad)
 * standard src pad object lock -> GST_OBJECT_LOCK(srcpad)
 * standard sink pad object lock -> GST_OBJECT_LOCK(aggpad)
 */


static GstClockTime gst_aggregator_get_latency_unlocked (GstAggregator * self);

GST_DEBUG_CATEGORY_STATIC (aggregator_debug);
#define GST_CAT_DEFAULT aggregator_debug

/* GstAggregatorPad definitions */
#define PAD_LOCK(pad)   G_STMT_START {                                  \
  GST_TRACE_OBJECT (pad, "Taking PAD lock from thread %p",              \
        g_thread_self());                                               \
  g_mutex_lock(&pad->priv->lock);                                       \
  GST_TRACE_OBJECT (pad, "Took PAD lock from thread %p",                \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_UNLOCK(pad)  G_STMT_START {                                 \
  GST_TRACE_OBJECT (pad, "Releasing PAD lock from thread %p",           \
      g_thread_self());                                                 \
  g_mutex_unlock(&pad->priv->lock);                                     \
  GST_TRACE_OBJECT (pad, "Release PAD lock from thread %p",             \
        g_thread_self());                                               \
  } G_STMT_END


#define PAD_WAIT_EVENT(pad)   G_STMT_START {                            \
  GST_LOG_OBJECT (pad, "Waiting for buffer to be consumed thread %p",   \
        g_thread_self());                                               \
  g_cond_wait(&(((GstAggregatorPad* )pad)->priv->event_cond),           \
      (&((GstAggregatorPad*)pad)->priv->lock));                         \
  GST_LOG_OBJECT (pad, "DONE Waiting for buffer to be consumed on thread %p", \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_BROADCAST_EVENT(pad) G_STMT_START {                        \
  GST_LOG_OBJECT (pad, "Signaling buffer consumed from thread %p",     \
        g_thread_self());                                              \
  g_cond_broadcast(&(((GstAggregatorPad* )pad)->priv->event_cond));    \
  } G_STMT_END


#define PAD_FLUSH_LOCK(pad)     G_STMT_START {                          \
  GST_TRACE_OBJECT (pad, "Taking lock from thread %p",                  \
        g_thread_self());                                               \
  g_mutex_lock(&pad->priv->flush_lock);                                 \
  GST_TRACE_OBJECT (pad, "Took lock from thread %p",                    \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_FLUSH_UNLOCK(pad)   G_STMT_START {                          \
  GST_TRACE_OBJECT (pad, "Releasing lock from thread %p",               \
        g_thread_self());                                               \
  g_mutex_unlock(&pad->priv->flush_lock);                               \
  GST_TRACE_OBJECT (pad, "Release lock from thread %p",                 \
        g_thread_self());                                               \
  } G_STMT_END

#define SRC_LOCK(self)   G_STMT_START {                             \
  GST_TRACE_OBJECT (self, "Taking src lock from thread %p",         \
      g_thread_self());                                             \
  g_mutex_lock(&self->priv->src_lock);                              \
  GST_TRACE_OBJECT (self, "Took src lock from thread %p",           \
        g_thread_self());                                           \
  } G_STMT_END

#define SRC_UNLOCK(self)  G_STMT_START {                            \
  GST_TRACE_OBJECT (self, "Releasing src lock from thread %p",      \
        g_thread_self());                                           \
  g_mutex_unlock(&self->priv->src_lock);                            \
  GST_TRACE_OBJECT (self, "Released src lock from thread %p",       \
        g_thread_self());                                           \
  } G_STMT_END

#define SRC_WAIT(self) G_STMT_START {                               \
  GST_LOG_OBJECT (self, "Waiting for src on thread %p",             \
        g_thread_self());                                           \
  g_cond_wait(&(self->priv->src_cond), &(self->priv->src_lock));    \
  GST_LOG_OBJECT (self, "DONE Waiting for src on thread %p",        \
        g_thread_self());                                           \
  } G_STMT_END

#define SRC_BROADCAST(self) G_STMT_START {                          \
    GST_LOG_OBJECT (self, "Signaling src from thread %p",           \
        g_thread_self());                                           \
    if (self->priv->aggregate_id)                                   \
      gst_clock_id_unschedule (self->priv->aggregate_id);           \
    g_cond_broadcast(&(self->priv->src_cond));                      \
  } G_STMT_END

struct _GstAggregatorPadPrivate
{
  /* Following fields are protected by the PAD_LOCK */
  GstFlowReturn flow_return;
  gboolean pending_flush_start;
  gboolean pending_flush_stop;
  gboolean pending_eos;

  gboolean first_buffer;

  GQueue buffers;
  guint num_buffers;
  GstClockTime head_position;
  GstClockTime tail_position;
  GstClockTime head_time;
  GstClockTime tail_time;
  GstClockTime time_level;

  gboolean eos;

  GMutex lock;
  GCond event_cond;
  /* This lock prevents a flush start processing happening while
   * the chain function is also happening.
   */
  GMutex flush_lock;
};

static gboolean
gst_aggregator_pad_flush (GstAggregatorPad * aggpad, GstAggregator * agg)
{
  GstAggregatorPadClass *klass = GST_AGGREGATOR_PAD_GET_CLASS (aggpad);

  PAD_LOCK (aggpad);
  aggpad->priv->pending_eos = FALSE;
  aggpad->priv->eos = FALSE;
  aggpad->priv->flow_return = GST_FLOW_OK;
  GST_OBJECT_LOCK (aggpad);
  gst_segment_init (&aggpad->segment, GST_FORMAT_UNDEFINED);
  gst_segment_init (&aggpad->clip_segment, GST_FORMAT_UNDEFINED);
  GST_OBJECT_UNLOCK (aggpad);
  aggpad->priv->head_position = GST_CLOCK_TIME_NONE;
  aggpad->priv->tail_position = GST_CLOCK_TIME_NONE;
  aggpad->priv->head_time = GST_CLOCK_TIME_NONE;
  aggpad->priv->tail_time = GST_CLOCK_TIME_NONE;
  aggpad->priv->time_level = 0;
  PAD_UNLOCK (aggpad);

  if (klass->flush)
    return klass->flush (aggpad, agg);

  return TRUE;
}

/*************************************
 * GstAggregator implementation  *
 *************************************/
static GstElementClass *aggregator_parent_class = NULL;

/* All members are protected by the object lock unless otherwise noted */

struct _GstAggregatorPrivate
{
  gint max_padserial;

  /* Our state is >= PAUSED */
  gboolean running;             /* protected by src_lock */

  gint seqnum;
  gboolean send_stream_start;   /* protected by srcpad stream lock */
  gboolean send_segment;
  gboolean flush_seeking;
  gboolean pending_flush_start;
  gboolean send_eos;            /* protected by srcpad stream lock */

  GstCaps *srccaps;             /* protected by the srcpad stream lock */

  GstTagList *tags;
  gboolean tags_changed;

  gboolean peer_latency_live;   /* protected by src_lock */
  GstClockTime peer_latency_min;        /* protected by src_lock */
  GstClockTime peer_latency_max;        /* protected by src_lock */
  gboolean has_peer_latency;    /* protected by src_lock */

  GstClockTime sub_latency_min; /* protected by src_lock */
  GstClockTime sub_latency_max; /* protected by src_lock */

  /* aggregate */
  GstClockID aggregate_id;      /* protected by src_lock */
  GMutex src_lock;
  GCond src_cond;

  gboolean first_buffer;        /* protected by object lock */
  GstAggregatorStartTimeSelection start_time_selection;
  GstClockTime start_time;

  /* properties */
  gint64 latency;               /* protected by both src_lock and all pad locks */
};

typedef struct
{
  GstEvent *event;
  gboolean result;
  gboolean flush;
  gboolean only_to_active_pads;

  gboolean one_actually_seeked;
} EventData;

#define DEFAULT_LATENCY              0
#define DEFAULT_START_TIME_SELECTION GST_AGGREGATOR_START_TIME_SELECTION_ZERO
#define DEFAULT_START_TIME           (-1)

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_START_TIME_SELECTION,
  PROP_START_TIME,
  PROP_LAST
};

static GstFlowReturn gst_aggregator_pad_chain_internal (GstAggregator * self,
    GstAggregatorPad * aggpad, GstBuffer * buffer, gboolean head);

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
gst_aggregator_pad_queue_is_empty (GstAggregatorPad * pad)
{
  return (g_queue_peek_tail (&pad->priv->buffers) == NULL);
}

static gboolean
gst_aggregator_check_pads_ready (GstAggregator * self)
{
  GstAggregatorPad *pad;
  GList *l, *sinkpads;
  gboolean have_data = TRUE;

  GST_LOG_OBJECT (self, "checking pads");

  GST_OBJECT_LOCK (self);

  sinkpads = GST_ELEMENT_CAST (self)->sinkpads;
  if (sinkpads == NULL)
    goto no_sinkpads;

  for (l = sinkpads; l != NULL; l = l->next) {
    pad = l->data;

    PAD_LOCK (pad);

    if (gst_aggregator_pad_queue_is_empty (pad)) {
      if (!pad->priv->eos) {
        have_data = FALSE;

        /* If not live we need data on all pads, so leave the loop */
        if (!self->priv->peer_latency_live) {
          PAD_UNLOCK (pad);
          goto pad_not_ready;
        }
      }
    } else if (self->priv->peer_latency_live) {
      /* In live mode, having a single pad with buffers is enough to
       * generate a start time from it. In non-live mode all pads need
       * to have a buffer
       */
      self->priv->first_buffer = FALSE;
    }

    PAD_UNLOCK (pad);
  }

  if (!have_data)
    goto pad_not_ready;

  self->priv->first_buffer = FALSE;

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
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  gst_segment_init (&self->segment, GST_FORMAT_TIME);
  self->priv->first_buffer = TRUE;
  GST_OBJECT_UNLOCK (self);
}

static inline void
gst_aggregator_push_mandatory_events (GstAggregator * self)
{
  GstAggregatorPrivate *priv = self->priv;
  GstEvent *segment = NULL;
  GstEvent *tags = NULL;

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
    segment = gst_event_new_segment (&self->segment);

    if (!self->priv->seqnum)
      self->priv->seqnum = gst_event_get_seqnum (segment);
    else
      gst_event_set_seqnum (segment, self->priv->seqnum);
    self->priv->send_segment = FALSE;

    GST_DEBUG_OBJECT (self, "pushing segment %" GST_PTR_FORMAT, segment);
  }

  if (priv->tags && priv->tags_changed && !self->priv->flush_seeking) {
    tags = gst_event_new_tag (gst_tag_list_ref (priv->tags));
    priv->tags_changed = FALSE;
  }
  GST_OBJECT_UNLOCK (self);

  if (segment)
    gst_pad_push_event (self->srcpad, segment);
  if (tags)
    gst_pad_push_event (self->srcpad, tags);

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
  GST_PAD_STREAM_LOCK (self->srcpad);
  gst_caps_replace (&self->priv->srccaps, caps);
  gst_aggregator_push_mandatory_events (self);
  GST_PAD_STREAM_UNLOCK (self->srcpad);
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

  event = gst_event_new_eos ();

  GST_OBJECT_LOCK (self);
  self->priv->send_eos = FALSE;
  gst_event_set_seqnum (event, self->priv->seqnum);
  GST_OBJECT_UNLOCK (self);

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

static gboolean
gst_aggregator_wait_and_check (GstAggregator * self, gboolean * timeout)
{
  GstClockTime latency;
  GstClockTime start;
  gboolean res;

  *timeout = FALSE;

  SRC_LOCK (self);

  latency = gst_aggregator_get_latency_unlocked (self);

  if (gst_aggregator_check_pads_ready (self)) {
    GST_DEBUG_OBJECT (self, "all pads have data");
    SRC_UNLOCK (self);

    return TRUE;
  }

  /* Before waiting, check if we're actually still running */
  if (!self->priv->running || !self->priv->send_eos) {
    SRC_UNLOCK (self);

    return FALSE;
  }

  start = gst_aggregator_get_next_time (self);

  /* If we're not live, or if we use the running time
   * of the first buffer as start time, we wait until
   * all pads have buffers.
   * Otherwise (i.e. if we are live!), we wait on the clock
   * and if a pad does not have a buffer in time we ignore
   * that pad.
   */
  GST_OBJECT_LOCK (self);
  if (!GST_CLOCK_TIME_IS_VALID (latency) ||
      !GST_IS_CLOCK (GST_ELEMENT_CLOCK (self)) ||
      !GST_CLOCK_TIME_IS_VALID (start) ||
      (self->priv->first_buffer
          && self->priv->start_time_selection ==
          GST_AGGREGATOR_START_TIME_SELECTION_FIRST)) {
    /* We wake up here when something happened, and below
     * then check if we're ready now. If we return FALSE,
     * we will be directly called again.
     */
    GST_OBJECT_UNLOCK (self);
    SRC_WAIT (self);
  } else {
    GstClockTime base_time, time;
    GstClock *clock;
    GstClockReturn status;
    GstClockTimeDiff jitter;

    GST_DEBUG_OBJECT (self, "got subclass start time: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start));

    base_time = GST_ELEMENT_CAST (self)->base_time;
    clock = gst_object_ref (GST_ELEMENT_CLOCK (self));
    GST_OBJECT_UNLOCK (self);

    time = base_time + start;
    time += latency;

    GST_DEBUG_OBJECT (self, "possibly waiting for clock to reach %"
        GST_TIME_FORMAT " (base %" GST_TIME_FORMAT " start %" GST_TIME_FORMAT
        " latency %" GST_TIME_FORMAT " current %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (time),
        GST_TIME_ARGS (base_time),
        GST_TIME_ARGS (start), GST_TIME_ARGS (latency),
        GST_TIME_ARGS (gst_clock_get_time (clock)));

    self->priv->aggregate_id = gst_clock_new_single_shot_id (clock, time);
    gst_object_unref (clock);
    SRC_UNLOCK (self);

    jitter = 0;
    status = gst_clock_id_wait (self->priv->aggregate_id, &jitter);

    SRC_LOCK (self);
    if (self->priv->aggregate_id) {
      gst_clock_id_unref (self->priv->aggregate_id);
      self->priv->aggregate_id = NULL;
    }

    GST_DEBUG_OBJECT (self,
        "clock returned %d (jitter: %" GST_STIME_FORMAT ")",
        status, GST_STIME_ARGS (jitter));

    /* we timed out */
    if (status == GST_CLOCK_OK || status == GST_CLOCK_EARLY) {
      SRC_UNLOCK (self);
      *timeout = TRUE;
      return TRUE;
    }
  }

  res = gst_aggregator_check_pads_ready (self);
  SRC_UNLOCK (self);

  return res;
}

static gboolean
check_events (GstAggregator * self, GstAggregatorPad * pad, gpointer user_data)
{
  GstEvent *event = NULL;
  GstAggregatorClass *klass = NULL;
  gboolean *processed_event = user_data;

  do {
    event = NULL;

    PAD_LOCK (pad);
    if (gst_aggregator_pad_queue_is_empty (pad) && pad->priv->pending_eos) {
      pad->priv->pending_eos = FALSE;
      pad->priv->eos = TRUE;
    }
    if (GST_IS_EVENT (g_queue_peek_tail (&pad->priv->buffers))) {
      event = g_queue_pop_tail (&pad->priv->buffers);
      PAD_BROADCAST_EVENT (pad);
    }
    PAD_UNLOCK (pad);
    if (event) {
      if (processed_event)
        *processed_event = TRUE;
      if (klass == NULL)
        klass = GST_AGGREGATOR_GET_CLASS (self);

      GST_LOG_OBJECT (pad, "Processing %" GST_PTR_FORMAT, event);
      klass->sink_event (self, pad, event);
    }
  } while (event != NULL);

  return TRUE;
}

static void
gst_aggregator_pad_set_flushing (GstAggregatorPad * aggpad,
    GstFlowReturn flow_return, gboolean full)
{
  GList *item;

  PAD_LOCK (aggpad);
  if (flow_return == GST_FLOW_NOT_LINKED)
    aggpad->priv->flow_return = MIN (flow_return, aggpad->priv->flow_return);
  else
    aggpad->priv->flow_return = flow_return;

  item = g_queue_peek_head_link (&aggpad->priv->buffers);
  while (item) {
    GList *next = item->next;

    /* In partial flush, we do like the pad, we get rid of non-sticky events
     * and EOS/SEGMENT.
     */
    if (full || GST_IS_BUFFER (item->data) ||
        GST_EVENT_TYPE (item->data) == GST_EVENT_EOS ||
        GST_EVENT_TYPE (item->data) == GST_EVENT_SEGMENT ||
        !GST_EVENT_IS_STICKY (item->data)) {
      gst_mini_object_unref (item->data);
      g_queue_delete_link (&aggpad->priv->buffers, item);
    }
    item = next;
  }
  aggpad->priv->num_buffers = 0;

  PAD_BROADCAST_EVENT (aggpad);
  PAD_UNLOCK (aggpad);
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
    gboolean processed_event = FALSE;

    gst_aggregator_iterate_sinkpads (self, check_events, NULL);

    if (!gst_aggregator_wait_and_check (self, &timeout))
      continue;

    gst_aggregator_iterate_sinkpads (self, check_events, &processed_event);
    if (processed_event)
      continue;

    GST_TRACE_OBJECT (self, "Actually aggregating!");
    flow_return = klass->aggregate (self, timeout);

    GST_OBJECT_LOCK (self);
    if (flow_return == GST_FLOW_FLUSHING && priv->flush_seeking) {
      /* We don't want to set the pads to flushing, but we want to
       * stop the thread, so just break here */
      GST_OBJECT_UNLOCK (self);
      break;
    }
    GST_OBJECT_UNLOCK (self);

    if (flow_return == GST_FLOW_EOS || flow_return == GST_FLOW_ERROR) {
      gst_aggregator_push_eos (self);
    }

    GST_LOG_OBJECT (self, "flow return is %s", gst_flow_get_name (flow_return));

    if (flow_return != GST_FLOW_OK) {
      GList *item;

      GST_OBJECT_LOCK (self);
      for (item = GST_ELEMENT (self)->sinkpads; item; item = item->next) {
        GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (item->data);

        gst_aggregator_pad_set_flushing (aggpad, flow_return, TRUE);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
  }

  /* Pause the task here, the only ways to get here are:
   * 1) We're stopping, in which case the task is stopped anyway
   * 2) We got a flow error above, in which case it might take
   *    some time to forward the flow return upstream and we
   *    would otherwise call the task function over and over
   *    again without doing anything
   */
  gst_pad_pause_task (self->srcpad);
}

static gboolean
gst_aggregator_start (GstAggregator * self)
{
  GstAggregatorClass *klass;
  gboolean result;

  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  self->priv->send_eos = TRUE;
  self->priv->srccaps = NULL;

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
  gboolean res;

  PAD_LOCK (pad);
  res = (!pad->priv->pending_flush_stop && !pad->priv->pending_flush_start);
  PAD_UNLOCK (pad);

  return res;
}

static gboolean
gst_aggregator_stop_srcpad_task (GstAggregator * self, GstEvent * flush_start)
{
  gboolean res = TRUE;

  GST_INFO_OBJECT (self, "%s srcpad task",
      flush_start ? "Pausing" : "Stopping");

  SRC_LOCK (self);
  self->priv->running = FALSE;
  SRC_BROADCAST (self);
  SRC_UNLOCK (self);

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
  priv->tags_changed = FALSE;
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
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorPadPrivate *padpriv = aggpad->priv;

  gst_aggregator_pad_set_flushing (aggpad, GST_FLOW_FLUSHING, FALSE);

  PAD_FLUSH_LOCK (aggpad);
  PAD_LOCK (aggpad);
  if (padpriv->pending_flush_start) {
    GST_DEBUG_OBJECT (aggpad, "Expecting FLUSH_STOP now");

    padpriv->pending_flush_start = FALSE;
    padpriv->pending_flush_stop = TRUE;
  }
  PAD_UNLOCK (aggpad);

  GST_OBJECT_LOCK (self);
  if (priv->flush_seeking) {
    /* If flush_seeking we forward the first FLUSH_START */
    if (priv->pending_flush_start) {
      priv->pending_flush_start = FALSE;
      GST_OBJECT_UNLOCK (self);

      GST_INFO_OBJECT (self, "Flushing, pausing srcpad task");
      gst_aggregator_stop_srcpad_task (self, event);

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
  PAD_FLUSH_UNLOCK (aggpad);
}

/* Must be called with the the PAD_LOCK held */
static void
update_time_level (GstAggregatorPad * aggpad, gboolean head)
{
  if (head) {
    if (GST_CLOCK_TIME_IS_VALID (aggpad->priv->head_position) &&
        aggpad->clip_segment.format == GST_FORMAT_TIME)
      aggpad->priv->head_time =
          gst_segment_to_running_time (&aggpad->clip_segment,
          GST_FORMAT_TIME, aggpad->priv->head_position);
    else
      aggpad->priv->head_time = GST_CLOCK_TIME_NONE;
  } else {
    if (GST_CLOCK_TIME_IS_VALID (aggpad->priv->tail_position) &&
        aggpad->segment.format == GST_FORMAT_TIME)
      aggpad->priv->tail_time =
          gst_segment_to_running_time (&aggpad->segment,
          GST_FORMAT_TIME, aggpad->priv->tail_position);
    else
      aggpad->priv->tail_time = aggpad->priv->head_time;
  }

  if (aggpad->priv->head_time == GST_CLOCK_TIME_NONE ||
      aggpad->priv->tail_time == GST_CLOCK_TIME_NONE) {
    aggpad->priv->time_level = 0;
    return;
  }

  if (aggpad->priv->tail_time > aggpad->priv->head_time)
    aggpad->priv->time_level = 0;
  else
    aggpad->priv->time_level = aggpad->priv->head_time -
        aggpad->priv->tail_time;
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
          SRC_LOCK (self);
          priv->send_eos = TRUE;
          SRC_BROADCAST (self);
          SRC_UNLOCK (self);

          GST_INFO_OBJECT (self, "Releasing source pad STREAM_LOCK");
          GST_PAD_STREAM_UNLOCK (self->srcpad);
          gst_aggregator_start_srcpad_task (self);
        } else {
          GST_OBJECT_UNLOCK (self);
        }
      } else {
        GST_OBJECT_UNLOCK (self);
      }

      aggpad->priv->first_buffer = TRUE;

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
      SRC_LOCK (self);
      PAD_LOCK (aggpad);
      if (gst_aggregator_pad_queue_is_empty (aggpad)) {
        aggpad->priv->eos = TRUE;
      } else {
        aggpad->priv->pending_eos = TRUE;
      }
      PAD_UNLOCK (aggpad);

      SRC_BROADCAST (self);
      SRC_UNLOCK (self);
      goto eat;
    }
    case GST_EVENT_SEGMENT:
    {
      PAD_LOCK (aggpad);
      GST_OBJECT_LOCK (aggpad);
      gst_event_copy_segment (event, &aggpad->segment);
      update_time_level (aggpad, FALSE);
      GST_OBJECT_UNLOCK (aggpad);
      PAD_UNLOCK (aggpad);

      GST_OBJECT_LOCK (self);
      self->priv->seqnum = gst_event_get_seqnum (event);
      GST_OBJECT_UNLOCK (self);
      goto eat;
    }
    case GST_EVENT_STREAM_START:
    {
      goto eat;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime pts, endpts;
      GstClockTime duration;
      GstBuffer *gapbuf;

      gst_event_parse_gap (event, &pts, &duration);
      gapbuf = gst_buffer_new ();

      if (GST_CLOCK_TIME_IS_VALID (duration))
        endpts = pts + duration;
      else
        endpts = GST_CLOCK_TIME_NONE;

      GST_OBJECT_LOCK (aggpad);
      res = gst_segment_clip (&aggpad->segment, GST_FORMAT_TIME, pts, endpts,
          &pts, &endpts);
      GST_OBJECT_UNLOCK (aggpad);

      if (!res) {
        GST_WARNING_OBJECT (self, "GAP event outside segment, dropping");
        goto eat;
      }

      if (GST_CLOCK_TIME_IS_VALID (endpts) && GST_CLOCK_TIME_IS_VALID (pts))
        duration = endpts - pts;
      else
        duration = GST_CLOCK_TIME_NONE;

      GST_BUFFER_PTS (gapbuf) = pts;
      GST_BUFFER_DURATION (gapbuf) = duration;
      GST_BUFFER_FLAG_SET (gapbuf, GST_BUFFER_FLAG_GAP);
      GST_BUFFER_FLAG_SET (gapbuf, GST_BUFFER_FLAG_DROPPABLE);

      if (gst_aggregator_pad_chain_internal (self, aggpad, gapbuf, FALSE) !=
          GST_FLOW_OK) {
        GST_WARNING_OBJECT (self, "Failed to chain gap buffer");
        res = FALSE;
      }

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

  klass = GST_AGGREGATOR_GET_CLASS (agg);

  if (klass->stop)
    result = klass->stop (agg);
  else
    result = TRUE;

  agg->priv->has_peer_latency = FALSE;
  agg->priv->peer_latency_live = FALSE;
  agg->priv->peer_latency_min = agg->priv->peer_latency_max = FALSE;

  if (agg->priv->tags)
    gst_tag_list_unref (agg->priv->tags);
  agg->priv->tags = NULL;

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
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  GST_INFO_OBJECT (pad, "Removing pad");

  SRC_LOCK (self);
  gst_aggregator_pad_set_flushing (aggpad, GST_FLOW_FLUSHING, TRUE);
  gst_element_remove_pad (element, pad);

  self->priv->has_peer_latency = FALSE;
  SRC_BROADCAST (self);
  SRC_UNLOCK (self);
}

static GstAggregatorPad *
gst_aggregator_default_create_new_pad (GstAggregator * self,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstAggregatorPad *agg_pad;
  GstAggregatorPrivate *priv = self->priv;
  gint serial = 0;
  gchar *name = NULL;

  if (templ->direction != GST_PAD_SINK ||
      g_strcmp0 (templ->name_template, "sink_%u") != 0)
    goto not_sink;

  GST_OBJECT_LOCK (self);
  if (req_name == NULL || strlen (req_name) < 6
      || !g_str_has_prefix (req_name, "sink_")) {
    /* no name given when requesting the pad, use next available int */
    serial = ++priv->max_padserial;
  } else {
    /* parse serial number from requested padname */
    serial = g_ascii_strtoull (&req_name[5], NULL, 10);
    if (serial > priv->max_padserial)
      priv->max_padserial = serial;
  }

  name = g_strdup_printf ("sink_%u", serial);
  agg_pad = g_object_new (GST_AGGREGATOR_GET_CLASS (self)->sinkpads_type,
      "name", name, "direction", GST_PAD_SINK, "template", templ, NULL);
  g_free (name);

  GST_OBJECT_UNLOCK (self);

  return agg_pad;

  /* errors */
not_sink:
  {
    GST_WARNING_OBJECT (self, "request new pad that is not a SINK pad\n");
    return NULL;
  }
}

static GstPad *
gst_aggregator_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstAggregator *self;
  GstAggregatorPad *agg_pad;
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (element);
  GstAggregatorPrivate *priv = GST_AGGREGATOR (element)->priv;

  self = GST_AGGREGATOR (element);

  agg_pad = klass->create_new_pad (self, templ, req_name, caps);
  if (!agg_pad) {
    GST_ERROR_OBJECT (element, "Couldn't create new pad");
    return NULL;
  }

  GST_DEBUG_OBJECT (element, "Adding pad %s", GST_PAD_NAME (agg_pad));
  self->priv->has_peer_latency = FALSE;

  if (priv->running)
    gst_pad_set_active (GST_PAD (agg_pad), TRUE);

  /* add the pad to the element */
  gst_element_add_pad (element, GST_PAD (agg_pad));

  return GST_PAD (agg_pad);
}

/* Must be called with SRC_LOCK held */

static gboolean
gst_aggregator_query_latency_unlocked (GstAggregator * self, GstQuery * query)
{
  gboolean query_ret, live;
  GstClockTime our_latency, min, max;

  query_ret = gst_pad_query_default (self->srcpad, GST_OBJECT (self), query);

  if (!query_ret) {
    GST_WARNING_OBJECT (self, "Latency query failed");
    return FALSE;
  }

  gst_query_parse_latency (query, &live, &min, &max);

  our_latency = self->priv->latency;

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (min))) {
    GST_ERROR_OBJECT (self, "Invalid minimum latency %" GST_TIME_FORMAT
        ". Please file a bug at " PACKAGE_BUGREPORT ".", GST_TIME_ARGS (min));
    return FALSE;
  }

  if (min > max && GST_CLOCK_TIME_IS_VALID (max)) {
    GST_ELEMENT_WARNING (self, CORE, CLOCK, (NULL),
        ("Impossible to configure latency: max %" GST_TIME_FORMAT " < min %"
            GST_TIME_FORMAT ". Add queues or other buffering elements.",
            GST_TIME_ARGS (max), GST_TIME_ARGS (min)));
    return FALSE;
  }

  self->priv->peer_latency_live = live;
  self->priv->peer_latency_min = min;
  self->priv->peer_latency_max = max;
  self->priv->has_peer_latency = TRUE;

  /* add our own */
  min += our_latency;
  min += self->priv->sub_latency_min;
  if (GST_CLOCK_TIME_IS_VALID (self->priv->sub_latency_max)
      && GST_CLOCK_TIME_IS_VALID (max))
    max += self->priv->sub_latency_max + our_latency;
  else
    max = GST_CLOCK_TIME_NONE;

  SRC_BROADCAST (self);

  GST_DEBUG_OBJECT (self, "configured latency live:%s min:%" G_GINT64_FORMAT
      " max:%" G_GINT64_FORMAT, live ? "true" : "false", min, max);

  gst_query_set_latency (query, live, min, max);

  return query_ret;
}

/*
 * MUST be called with the src_lock held.
 *
 * See  gst_aggregator_get_latency() for doc
 */
static GstClockTime
gst_aggregator_get_latency_unlocked (GstAggregator * self)
{
  GstClockTime latency;

  g_return_val_if_fail (GST_IS_AGGREGATOR (self), 0);

  if (!self->priv->has_peer_latency) {
    GstQuery *query = gst_query_new_latency ();
    gboolean ret;

    ret = gst_aggregator_query_latency_unlocked (self, query);
    gst_query_unref (query);
    if (!ret)
      return GST_CLOCK_TIME_NONE;
  }

  if (!self->priv->has_peer_latency || !self->priv->peer_latency_live)
    return GST_CLOCK_TIME_NONE;

  /* latency_min is never GST_CLOCK_TIME_NONE by construction */
  latency = self->priv->peer_latency_min;

  /* add our own */
  latency += self->priv->latency;
  latency += self->priv->sub_latency_min;

  return latency;
}

/**
 * gst_aggregator_get_latency:
 * @self: a #GstAggregator
 *
 * Retrieves the latency values reported by @self in response to the latency
 * query, or %GST_CLOCK_TIME_NONE if there is not live source connected and the element
 * will not wait for the clock.
 *
 * Typically only called by subclasses.
 *
 * Returns: The latency or %GST_CLOCK_TIME_NONE if the element does not sync
 */
GstClockTime
gst_aggregator_get_latency (GstAggregator * self)
{
  GstClockTime ret;

  SRC_LOCK (self);
  ret = gst_aggregator_get_latency_unlocked (self);
  SRC_UNLOCK (self);

  return ret;
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

    GST_OBJECT_LOCK (self);
    gst_segment_do_seek (&self->segment, rate, fmt, flags, start_type, start,
        stop_type, stop, NULL);
    self->priv->seqnum = gst_event_get_seqnum (event);
    self->priv->first_buffer = FALSE;
    GST_OBJECT_UNLOCK (self);

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

      break;
    }
    case GST_QUERY_LATENCY:
      SRC_LOCK (self);
      res = gst_aggregator_query_latency_unlocked (self, query);
      SRC_UNLOCK (self);
      break;
    default:
      return gst_pad_query_default (self->srcpad, GST_OBJECT (self), query);
  }

  return res;
}

static gboolean
gst_aggregator_event_forward_func (GstPad * pad, gpointer user_data)
{
  EventData *evdata = user_data;
  gboolean ret = TRUE;
  GstPad *peer = gst_pad_get_peer (pad);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  if (peer) {
    if (evdata->only_to_active_pads && aggpad->priv->first_buffer) {
      GST_DEBUG_OBJECT (pad, "not sending event to inactive pad");
      ret = TRUE;
    } else {
      ret = gst_pad_send_event (peer, gst_event_ref (evdata->event));
      GST_DEBUG_OBJECT (pad, "return of event push is %d", ret);
      gst_object_unref (peer);
    }
  }

  if (ret == FALSE) {
    if (GST_EVENT_TYPE (evdata->event) == GST_EVENT_SEEK) {
      GstQuery *seeking = gst_query_new_seeking (GST_FORMAT_TIME);

      GST_DEBUG_OBJECT (pad, "Event %" GST_PTR_FORMAT " failed", evdata->event);

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
      PAD_LOCK (aggpad);
      aggpad->priv->pending_flush_start = FALSE;
      aggpad->priv->pending_flush_stop = FALSE;
      PAD_UNLOCK (aggpad);
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
    GstEvent * event, gboolean flush, gboolean only_to_active_pads)
{
  EventData evdata;

  evdata.event = event;
  evdata.result = TRUE;
  evdata.flush = flush;
  evdata.one_actually_seeked = FALSE;
  evdata.only_to_active_pads = only_to_active_pads;

  /* We first need to set all pads as flushing in a first pass
   * as flush_start flush_stop is sometimes sent synchronously
   * while we send the seek event */
  if (flush) {
    GList *l;

    GST_OBJECT_LOCK (self);
    for (l = GST_ELEMENT_CAST (self)->sinkpads; l != NULL; l = l->next) {
      GstAggregatorPad *pad = l->data;

      PAD_LOCK (pad);
      pad->priv->pending_flush_start = TRUE;
      pad->priv->pending_flush_stop = FALSE;
      PAD_UNLOCK (pad);
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

  /* Seeking sets a position */
  self->priv->first_buffer = FALSE;
  GST_OBJECT_UNLOCK (self);

  /* forward the seek upstream */
  evdata =
      gst_aggregator_forward_event_to_all_sinkpads (self, event, flush, FALSE);
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

  /* Don't forward QOS events to pads that had no active buffer yet. Otherwise
   * they will receive a QOS event that has earliest_time=0 (because we can't
   * have negative timestamps), and consider their buffer as too late */
  evdata =
      gst_aggregator_forward_event_to_all_sinkpads (self, event, FALSE,
      GST_EVENT_TYPE (event) == GST_EVENT_QOS);
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

  g_mutex_clear (&self->priv->src_lock);
  g_cond_clear (&self->priv->src_cond);

  G_OBJECT_CLASS (aggregator_parent_class)->finalize (object);
}

/*
 * gst_aggregator_set_latency_property:
 * @agg: a #GstAggregator
 * @latency: the new latency value (in nanoseconds).
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
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (latency));

  SRC_LOCK (self);
  changed = (self->priv->latency != latency);

  if (changed) {
    GList *item;

    GST_OBJECT_LOCK (self);
    /* First lock all the pads */
    for (item = GST_ELEMENT_CAST (self)->sinkpads; item; item = item->next) {
      GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (item->data);
      PAD_LOCK (aggpad);
    }

    self->priv->latency = latency;

    SRC_BROADCAST (self);

    /* Now wake up the pads */
    for (item = GST_ELEMENT_CAST (self)->sinkpads; item; item = item->next) {
      GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (item->data);
      PAD_BROADCAST_EVENT (aggpad);
      PAD_UNLOCK (aggpad);
    }
    GST_OBJECT_UNLOCK (self);
  }

  SRC_UNLOCK (self);

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
    case PROP_START_TIME_SELECTION:
      agg->priv->start_time_selection = g_value_get_enum (value);
      break;
    case PROP_START_TIME:
      agg->priv->start_time = g_value_get_uint64 (value);
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
    case PROP_START_TIME_SELECTION:
      g_value_set_enum (value, agg->priv->start_time_selection);
      break;
    case PROP_START_TIME:
      g_value_set_uint64 (value, agg->priv->start_time);
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

  klass->create_new_pad = gst_aggregator_default_create_new_pad;

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
          "position (in nanoseconds)", 0,
          (G_MAXLONG == G_MAXINT64) ? G_MAXINT64 : (G_MAXLONG * GST_SECOND - 1),
          DEFAULT_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_START_TIME_SELECTION,
      g_param_spec_enum ("start-time-selection", "Start Time Selection",
          "Decides which start time is output",
          gst_aggregator_start_time_selection_get_type (),
          DEFAULT_START_TIME_SELECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_START_TIME,
      g_param_spec_uint64 ("start-time", "Start Time",
          "Start time to use if start-time-selection=set", 0,
          G_MAXUINT64,
          DEFAULT_START_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_REGISTER_FUNCPTR (gst_aggregator_stop_pad);
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

  priv->max_padserial = -1;
  priv->tags_changed = FALSE;

  self->priv->peer_latency_live = FALSE;
  self->priv->peer_latency_min = self->priv->sub_latency_min = 0;
  self->priv->peer_latency_max = self->priv->sub_latency_max = 0;
  self->priv->has_peer_latency = FALSE;
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
  self->priv->start_time_selection = DEFAULT_START_TIME_SELECTION;
  self->priv->start_time = DEFAULT_START_TIME;

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

/* Must be called with SRC lock and PAD lock held */
static gboolean
gst_aggregator_pad_has_space (GstAggregator * self, GstAggregatorPad * aggpad)
{
  /* Empty queue always has space */
  if (g_queue_get_length (&aggpad->priv->buffers) == 0)
    return TRUE;

  /* We also want at least two buffers, one is being processed and one is ready
   * for the next iteration when we operate in live mode. */
  if (self->priv->peer_latency_live && aggpad->priv->num_buffers < 2)
    return TRUE;

  /* zero latency, if there is a buffer, it's full */
  if (self->priv->latency == 0)
    return FALSE;

  /* Allow no more buffers than the latency */
  return (aggpad->priv->time_level <= self->priv->latency);
}

/* Must be called with the PAD_LOCK held */
static void
apply_buffer (GstAggregatorPad * aggpad, GstBuffer * buffer, gboolean head)
{
  GstClockTime timestamp;

  if (GST_BUFFER_DTS_IS_VALID (buffer))
    timestamp = GST_BUFFER_DTS (buffer);
  else
    timestamp = GST_BUFFER_PTS (buffer);

  if (timestamp == GST_CLOCK_TIME_NONE) {
    if (head)
      timestamp = aggpad->priv->head_position;
    else
      timestamp = aggpad->priv->tail_position;
  }

  /* add duration */
  if (GST_BUFFER_DURATION_IS_VALID (buffer))
    timestamp += GST_BUFFER_DURATION (buffer);

  if (head)
    aggpad->priv->head_position = timestamp;
  else
    aggpad->priv->tail_position = timestamp;

  update_time_level (aggpad, head);
}

static GstFlowReturn
gst_aggregator_pad_chain_internal (GstAggregator * self,
    GstAggregatorPad * aggpad, GstBuffer * buffer, gboolean head)
{
  GstBuffer *actual_buf = buffer;
  GstAggregatorClass *aggclass = GST_AGGREGATOR_GET_CLASS (self);
  GstFlowReturn flow_return;
  GstClockTime buf_pts;

  GST_DEBUG_OBJECT (aggpad, "Start chaining a buffer %" GST_PTR_FORMAT, buffer);

  PAD_FLUSH_LOCK (aggpad);

  PAD_LOCK (aggpad);
  flow_return = aggpad->priv->flow_return;
  if (flow_return != GST_FLOW_OK)
    goto flushing;

  if (aggpad->priv->pending_eos == TRUE)
    goto eos;

  PAD_UNLOCK (aggpad);

  if (aggclass->clip && head) {
    aggclass->clip (self, aggpad, buffer, &actual_buf);
  }

  if (actual_buf == NULL) {
    GST_LOG_OBJECT (actual_buf, "Buffer dropped by clip function");
    goto done;
  }

  buf_pts = GST_BUFFER_PTS (actual_buf);

  aggpad->priv->first_buffer = FALSE;

  for (;;) {
    SRC_LOCK (self);
    GST_OBJECT_LOCK (self);
    PAD_LOCK (aggpad);
    if (gst_aggregator_pad_has_space (self, aggpad)
        && aggpad->priv->flow_return == GST_FLOW_OK) {
      if (head)
        g_queue_push_head (&aggpad->priv->buffers, actual_buf);
      else
        g_queue_push_tail (&aggpad->priv->buffers, actual_buf);
      apply_buffer (aggpad, actual_buf, head);
      aggpad->priv->num_buffers++;
      actual_buf = buffer = NULL;
      SRC_BROADCAST (self);
      break;
    }

    flow_return = aggpad->priv->flow_return;
    if (flow_return != GST_FLOW_OK) {
      GST_OBJECT_UNLOCK (self);
      SRC_UNLOCK (self);
      goto flushing;
    }
    GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
    GST_OBJECT_UNLOCK (self);
    SRC_UNLOCK (self);
    PAD_WAIT_EVENT (aggpad);

    PAD_UNLOCK (aggpad);
  }

  if (self->priv->first_buffer) {
    GstClockTime start_time;

    switch (self->priv->start_time_selection) {
      case GST_AGGREGATOR_START_TIME_SELECTION_ZERO:
      default:
        start_time = 0;
        break;
      case GST_AGGREGATOR_START_TIME_SELECTION_FIRST:
        GST_OBJECT_LOCK (aggpad);
        if (aggpad->segment.format == GST_FORMAT_TIME) {
          start_time = buf_pts;
          if (start_time != -1) {
            start_time = MAX (start_time, aggpad->segment.start);
            start_time =
                gst_segment_to_running_time (&aggpad->segment, GST_FORMAT_TIME,
                start_time);
          }
        } else {
          start_time = 0;
          GST_WARNING_OBJECT (aggpad,
              "Ignoring request of selecting the first start time "
              "as the segment is a %s segment instead of a time segment",
              gst_format_get_name (aggpad->segment.format));
        }
        GST_OBJECT_UNLOCK (aggpad);
        break;
      case GST_AGGREGATOR_START_TIME_SELECTION_SET:
        start_time = self->priv->start_time;
        if (start_time == -1)
          start_time = 0;
        break;
    }

    if (start_time != -1) {
      if (self->segment.position == -1)
        self->segment.position = start_time;
      else
        self->segment.position = MIN (start_time, self->segment.position);

      GST_DEBUG_OBJECT (self, "Selecting start time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start_time));
    }
  }

  PAD_UNLOCK (aggpad);
  GST_OBJECT_UNLOCK (self);
  SRC_UNLOCK (self);

done:

  PAD_FLUSH_UNLOCK (aggpad);

  GST_DEBUG_OBJECT (aggpad, "Done chaining");

  return flow_return;

flushing:
  PAD_UNLOCK (aggpad);
  PAD_FLUSH_UNLOCK (aggpad);

  GST_DEBUG_OBJECT (aggpad, "Pad is %s, dropping buffer",
      gst_flow_get_name (flow_return));
  if (buffer)
    gst_buffer_unref (buffer);

  return flow_return;

eos:
  PAD_UNLOCK (aggpad);
  PAD_FLUSH_UNLOCK (aggpad);

  gst_buffer_unref (buffer);
  GST_DEBUG_OBJECT (aggpad, "We are EOS already...");

  return GST_FLOW_EOS;
}

static GstFlowReturn
gst_aggregator_pad_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  return gst_aggregator_pad_chain_internal (GST_AGGREGATOR_CAST (object),
      GST_AGGREGATOR_PAD_CAST (pad), buffer, TRUE);
}

static gboolean
gst_aggregator_pad_query_func (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  if (GST_QUERY_IS_SERIALIZED (query)) {
    PAD_LOCK (aggpad);

    while (!gst_aggregator_pad_queue_is_empty (aggpad)
        && aggpad->priv->flow_return == GST_FLOW_OK) {
      GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
      PAD_WAIT_EVENT (aggpad);
    }

    if (aggpad->priv->flow_return != GST_FLOW_OK)
      goto flushing;

    PAD_UNLOCK (aggpad);
  }

  return klass->sink_query (GST_AGGREGATOR (parent),
      GST_AGGREGATOR_PAD (pad), query);

flushing:
  GST_DEBUG_OBJECT (aggpad, "Pad is %s, dropping query",
      gst_flow_get_name (aggpad->priv->flow_return));
  PAD_UNLOCK (aggpad);
  return FALSE;
}

static GstFlowReturn
gst_aggregator_pad_event_func (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAggregator *self = GST_AGGREGATOR (parent);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  if (GST_EVENT_IS_SERIALIZED (event) && GST_EVENT_TYPE (event) != GST_EVENT_EOS
      /* && GST_EVENT_TYPE (event) != GST_EVENT_SEGMENT_DONE */ ) {
    SRC_LOCK (self);
    PAD_LOCK (aggpad);

    if (aggpad->priv->flow_return != GST_FLOW_OK
        && GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_STOP) {
      ret = aggpad->priv->flow_return;
      goto flushing;
    }

    if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
      GST_OBJECT_LOCK (aggpad);
      gst_event_copy_segment (event, &aggpad->clip_segment);
      aggpad->priv->head_position = aggpad->clip_segment.position;
      update_time_level (aggpad, TRUE);
      GST_OBJECT_UNLOCK (aggpad);
    }

    if (!gst_aggregator_pad_queue_is_empty (aggpad) &&
        GST_EVENT_TYPE (event) != GST_EVENT_FLUSH_STOP) {
      GST_DEBUG_OBJECT (aggpad, "Store event in queue: %" GST_PTR_FORMAT,
          event);
      g_queue_push_head (&aggpad->priv->buffers, event);
      event = NULL;
      SRC_BROADCAST (self);
    }
    PAD_UNLOCK (aggpad);
    SRC_UNLOCK (self);
  }

  if (event) {
    gboolean is_caps = (GST_EVENT_TYPE (event) == GST_EVENT_CAPS);

    if (!klass->sink_event (self, aggpad, event)) {
      /* Copied from GstPad to convert boolean to a GstFlowReturn in
       * the event handling func */
      ret = is_caps ? GST_FLOW_NOT_NEGOTIATED : GST_FLOW_ERROR;
    }
  }

  return ret;

flushing:
  GST_DEBUG_OBJECT (aggpad, "Pad is %s, dropping event",
      gst_flow_get_name (aggpad->priv->flow_return));
  PAD_UNLOCK (aggpad);
  SRC_UNLOCK (self);
  if (GST_EVENT_IS_STICKY (event))
    gst_pad_store_sticky_event (pad, event);
  gst_event_unref (event);

  return ret;
}

static gboolean
gst_aggregator_pad_activate_mode_func (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstAggregator *self = GST_AGGREGATOR (parent);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  if (active == FALSE) {
    SRC_LOCK (self);
    gst_aggregator_pad_set_flushing (aggpad, GST_FLOW_FLUSHING, TRUE);
    SRC_BROADCAST (self);
    SRC_UNLOCK (self);
  } else {
    PAD_LOCK (aggpad);
    aggpad->priv->flow_return = GST_FLOW_OK;
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
  gst_pad_set_event_full_function_full (pad,
      GST_DEBUG_FUNCPTR (gst_aggregator_pad_event_func),
      NULL, NULL);
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
  g_mutex_clear (&pad->priv->flush_lock);
  g_mutex_clear (&pad->priv->lock);

  G_OBJECT_CLASS (gst_aggregator_pad_parent_class)->finalize (object);
}

static void
gst_aggregator_pad_dispose (GObject * object)
{
  GstAggregatorPad *pad = (GstAggregatorPad *) object;

  gst_aggregator_pad_set_flushing (pad, GST_FLOW_FLUSHING, TRUE);

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

  g_queue_init (&pad->priv->buffers);
  g_cond_init (&pad->priv->event_cond);

  g_mutex_init (&pad->priv->flush_lock);
  g_mutex_init (&pad->priv->lock);

  pad->priv->first_buffer = TRUE;
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
  if (GST_IS_BUFFER (g_queue_peek_tail (&pad->priv->buffers)))
    buffer = g_queue_pop_tail (&pad->priv->buffers);

  if (buffer) {
    apply_buffer (pad, buffer, FALSE);
    pad->priv->num_buffers--;
    GST_TRACE_OBJECT (pad, "Consuming buffer");
    if (gst_aggregator_pad_queue_is_empty (pad) && pad->priv->pending_eos) {
      pad->priv->pending_eos = FALSE;
      pad->priv->eos = TRUE;
    }
    PAD_BROADCAST_EVENT (pad);
    GST_DEBUG_OBJECT (pad, "Consumed: %" GST_PTR_FORMAT, buffer);
  }
  PAD_UNLOCK (pad);

  return buffer;
}

/**
 * gst_aggregator_pad_drop_buffer:
 * @pad: the pad where to drop any pending buffer
 *
 * Drop the buffer currently queued in @pad.
 *
 * Returns: TRUE if there was a buffer queued in @pad, or FALSE if not.
 */
gboolean
gst_aggregator_pad_drop_buffer (GstAggregatorPad * pad)
{
  GstBuffer *buf;

  buf = gst_aggregator_pad_steal_buffer (pad);

  if (buf == NULL)
    return FALSE;

  gst_buffer_unref (buf);
  return TRUE;
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
  buffer = g_queue_peek_tail (&pad->priv->buffers);
  /* The tail should always be a buffer, because if it is an event,
   * it will be consumed immeditaly in gst_aggregator_steal_buffer */

  if (GST_IS_BUFFER (buffer))
    gst_buffer_ref (buffer);
  else
    buffer = NULL;
  PAD_UNLOCK (pad);

  return buffer;
}

gboolean
gst_aggregator_pad_is_eos (GstAggregatorPad * pad)
{
  gboolean is_eos;

  PAD_LOCK (pad);
  is_eos = pad->priv->eos;
  PAD_UNLOCK (pad);

  return is_eos;
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
  gboolean changed = FALSE;

  g_return_if_fail (GST_IS_AGGREGATOR (self));
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (min_latency));
  g_return_if_fail (max_latency >= min_latency);

  SRC_LOCK (self);
  if (self->priv->sub_latency_min != min_latency) {
    self->priv->sub_latency_min = min_latency;
    changed = TRUE;
  }
  if (self->priv->sub_latency_max != max_latency) {
    self->priv->sub_latency_max = max_latency;
    changed = TRUE;
  }

  if (changed)
    SRC_BROADCAST (self);
  SRC_UNLOCK (self);

  if (changed) {
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_message_new_latency (GST_OBJECT_CAST (self)));
  }
}
