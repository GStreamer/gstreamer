/* GStreamer
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
 *    Base class for mixers and muxers. Implementers should at least implement
 *    the aggregate () vmethod.
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

GST_DEBUG_CATEGORY_STATIC (aggregator_debug);
#define GST_CAT_DEFAULT aggregator_debug

/* GstAggregatorPad definitions */
#define PAD_LOCK_EVENT(pad)   G_STMT_START {                            \
  GST_LOG_OBJECT (pad, "Taking EVENT lock from thread %p",              \
        g_thread_self());                                               \
  g_mutex_lock(&pad->priv->event_lock);                                 \
  GST_LOG_OBJECT (pad, "Took EVENT lock from thread %p",              \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_UNLOCK_EVENT(pad)  G_STMT_START {                           \
  GST_LOG_OBJECT (pad, "Releasing EVENT lock from thread %p",          \
        g_thread_self());                                               \
  g_mutex_unlock(&pad->priv->event_lock);                               \
  GST_LOG_OBJECT (pad, "Release EVENT lock from thread %p",          \
        g_thread_self());                                               \
  } G_STMT_END


#define PAD_WAIT_EVENT(pad)   G_STMT_START {                            \
  GST_LOG_OBJECT (pad, "Waiting for EVENT on thread %p",               \
        g_thread_self());                                               \
  g_cond_wait(&(((GstAggregatorPad* )pad)->priv->event_cond),       \
      &(pad->priv->event_lock));                                        \
  GST_LOG_OBJECT (pad, "DONE Waiting for EVENT on thread %p",               \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_BROADCAST_EVENT(pad) {                                          \
  GST_LOG_OBJECT (pad, "Signaling EVENT from thread %p",               \
        g_thread_self());                                                   \
  g_cond_broadcast(&(((GstAggregatorPad* )pad)->priv->event_cond)); \
  }

struct _GstAggregatorPadPrivate
{
  gboolean pending_flush_start;
  gboolean pending_flush_stop;
  gboolean pending_eos;
  gboolean flushing;

  GMutex event_lock;
  GCond event_cond;
};

static gboolean
_aggpad_flush (GstAggregatorPad * aggpad, GstAggregator * agg)
{
  GstAggregatorPadClass *klass = GST_AGGREGATOR_PAD_GET_CLASS (aggpad);

  aggpad->eos = FALSE;
  aggpad->priv->flushing = FALSE;

  if (klass->flush)
    return klass->flush (aggpad, agg);

  return TRUE;
}

/*************************************
 * GstAggregator implementation  *
 *************************************/
static GstElementClass *aggregator_parent_class = NULL;

#define MAIN_CONTEXT_LOCK(self) G_STMT_START {                       \
  GST_LOG_OBJECT (self, "Getting MAIN_CONTEXT_LOCK in thread %p",    \
        g_thread_self());                                            \
  g_mutex_lock(&((GstAggregator*)self)->priv->mcontext_lock);    \
  GST_LOG_OBJECT (self, "Got MAIN_CONTEXT_LOCK in thread %p",        \
        g_thread_self());                                            \
} G_STMT_END

#define MAIN_CONTEXT_UNLOCK(self) G_STMT_START {                     \
  g_mutex_unlock(&((GstAggregator*)self)->priv->mcontext_lock);  \
  GST_LOG_OBJECT (self, "Unlocked MAIN_CONTEXT_LOCK in thread %p",   \
        g_thread_self());                                            \
} G_STMT_END

struct _GstAggregatorPrivate
{
  gint padcount;

  GMainContext *mcontext;

  /* Our state is >= PAUSED */
  gboolean running;

  /* Ensure that when we remove all sources from the maincontext
   * we can not add any source, avoiding:
   * "g_source_attach: assertion '!SOURCE_DESTROYED (source)' failed" */
  GMutex mcontext_lock;
  GList *gsources;

  gint seqnum;
  gboolean send_stream_start;
  gboolean send_segment;
  gboolean flush_seeking;
  gboolean pending_flush_start;
  gboolean send_eos;
  GstFlowReturn flow_return;

  GstCaps *srccaps;

  GstTagList *tags;
  gboolean tags_changed;
};

typedef struct
{
  GstEvent *event;
  gboolean result;
  gboolean flush;
} EventData;

/**
 * gst_aggregator_iterate_sinkpads:
 * @self: The #GstAggregator
 * @func: The function to call.
 * @user_data: The data to pass to @func.
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
        GstPad *pad;

        pad = g_value_get_object (&item);

        /* if already pushed, skip. FIXME, find something faster to tag pads */
        if (pad == NULL || g_list_find (seen_pads, pad)) {
          g_value_reset (&item);
          break;
        }

        GST_LOG_OBJECT (self, "calling function on pad %s:%s",
            GST_DEBUG_PAD_NAME (pad));
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

static inline gboolean
_check_all_pads_with_data_or_eos (GstAggregator * self,
    GstAggregatorPad * aggpad)
{
  if (aggpad->buffer || aggpad->eos) {
    return TRUE;
  }

  GST_LOG_OBJECT (aggpad, "Not ready to be aggregated");

  return FALSE;
}

/**
 * gst_aggregator_set_src_caps:
 * @self: The #GstAggregator
 * @caps: The #GstCaps to set later on the src pad.
 *
 * Sets the caps to be used on the src pad.
 */
void
gst_aggregator_set_src_caps (GstAggregator * self, GstCaps * caps)
{
  gst_caps_replace (&self->priv->srccaps, caps);
}

static void
_reset_flow_values (GstAggregator * self)
{
  self->priv->flow_return = GST_FLOW_FLUSHING;
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  gst_segment_init (&self->segment, GST_FORMAT_TIME);
}

static inline void
_push_mandatory_events (GstAggregator * self)
{
  GstAggregatorPrivate *priv = self->priv;

  if (g_atomic_int_get (&self->priv->send_stream_start)) {
    gchar s_id[32];

    GST_INFO_OBJECT (self, "pushing stream start");
    /* stream-start (FIXME: create id based on input ids) */
    g_snprintf (s_id, sizeof (s_id), "agg-%08x", g_random_int ());
    if (!gst_pad_push_event (self->srcpad, gst_event_new_stream_start (s_id))) {
      GST_WARNING_OBJECT (self->srcpad, "Sending stream start event failed");
    }
    g_atomic_int_set (&self->priv->send_stream_start, FALSE);
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

  if (g_atomic_int_get (&self->priv->send_segment)) {
    if (!g_atomic_int_get (&self->priv->flush_seeking)) {
      GstEvent *segev = gst_event_new_segment (&self->segment);

      if (!self->priv->seqnum)
        self->priv->seqnum = gst_event_get_seqnum (segev);
      else
        gst_event_set_seqnum (segev, self->priv->seqnum);

      GST_DEBUG_OBJECT (self, "pushing segment %" GST_PTR_FORMAT, segev);
      gst_pad_push_event (self->srcpad, segev);
      g_atomic_int_set (&self->priv->send_segment, FALSE);
    }
  }

  if (priv->tags && priv->tags_changed) {
    gst_pad_push_event (self->srcpad,
        gst_event_new_tag (gst_tag_list_ref (priv->tags)));
    priv->tags_changed = FALSE;
  }
}

/**
 * gst_aggregator_finish_buffer:
 * @self: The #GstAggregator
 * @buffer: the #GstBuffer to push.
 *
 * This method will take care of sending mandatory events before pushing
 * the provided buffer.
 */
GstFlowReturn
gst_aggregator_finish_buffer (GstAggregator * self, GstBuffer * buffer)
{
  _push_mandatory_events (self);

  if (!g_atomic_int_get (&self->priv->flush_seeking) &&
      gst_pad_is_active (self->srcpad)) {
    GST_TRACE_OBJECT (self, "pushing buffer %" GST_PTR_FORMAT, buffer);
    return gst_pad_push (self->srcpad, buffer);
  } else {
    GST_INFO_OBJECT (self, "Not pushing (active: %i, flushing: %i)",
        g_atomic_int_get (&self->priv->flush_seeking),
        gst_pad_is_active (self->srcpad));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static void
_push_eos (GstAggregator * self)
{
  GstEvent *event;
  _push_mandatory_events (self);

  self->priv->send_eos = FALSE;
  event = gst_event_new_eos ();
  gst_event_set_seqnum (event, self->priv->seqnum);
  gst_pad_push_event (self->srcpad, event);
}


static void
_destroy_gsource (GSource * source)
{
  g_source_destroy (source);
  g_source_unref (source);
}

static void
_remove_all_sources (GstAggregator * self)
{
  GstAggregatorPrivate *priv = self->priv;

  MAIN_CONTEXT_LOCK (self);
  g_list_free_full (priv->gsources, (GDestroyNotify) _destroy_gsource);
  priv->gsources = NULL;
  MAIN_CONTEXT_UNLOCK (self);
}

static gboolean
aggregate_func (GstAggregator * self)
{
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (self);

  GST_LOG_OBJECT (self, "Checking aggregate");
  while (priv->send_eos && gst_aggregator_iterate_sinkpads (self,
          (GstAggregatorPadForeachFunc) _check_all_pads_with_data_or_eos,
          NULL) && priv->running) {
    GST_TRACE_OBJECT (self, "Actually aggregating!");

    priv->flow_return = klass->aggregate (self);

    if (priv->flow_return == GST_FLOW_EOS) {
      g_main_context_wakeup (self->priv->mcontext);
      _remove_all_sources (self);
      _push_eos (self);
    }

    if (priv->flow_return == GST_FLOW_FLUSHING &&
        g_atomic_int_get (&priv->flush_seeking))
      priv->flow_return = GST_FLOW_OK;

    GST_LOG_OBJECT (self, "flow return is %s",
        gst_flow_get_name (priv->flow_return));

    if (priv->flow_return != GST_FLOW_OK)
      break;
  }

  return G_SOURCE_REMOVE;
}

static void
iterate_main_context_func (GstAggregator * self)
{
  if (self->priv->running == FALSE) {
    GST_DEBUG_OBJECT (self, "Not running anymore");

    return;
  }

  g_main_context_iteration (self->priv->mcontext, TRUE);
}

static gboolean
_start (GstAggregator * self)
{
  self->priv->running = TRUE;
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  self->priv->send_eos = TRUE;
  self->priv->srccaps = NULL;
  self->priv->flow_return = GST_FLOW_OK;

  return TRUE;
}

static gboolean
_check_pending_flush_stop (GstAggregatorPad * pad)
{
  return (!pad->priv->pending_flush_stop && !pad->priv->pending_flush_start);
}

static gboolean
_stop_srcpad_task (GstAggregator * self, GstEvent * flush_start)
{
  gboolean res = TRUE;

  GST_INFO_OBJECT (self, "%s srcpad task",
      flush_start ? "Pausing" : "Stopping");

  self->priv->running = FALSE;

  /*  Clean the stack of GSource set on the MainContext */
  g_main_context_wakeup (self->priv->mcontext);
  _remove_all_sources (self);
  if (flush_start) {
    res = gst_pad_push_event (self->srcpad, flush_start);
  }

  gst_pad_stop_task (self->srcpad);

  return res;
}

static void
_start_srcpad_task (GstAggregator * self)
{
  GST_INFO_OBJECT (self, "Starting srcpad task");

  self->priv->running = TRUE;
  gst_pad_start_task (GST_PAD (self->srcpad),
      (GstTaskFunction) iterate_main_context_func, self, NULL);
}

static inline void
_add_aggregate_gsource (GstAggregator * self)
{
  GSource *source;
  GstAggregatorPrivate *priv = self->priv;

  MAIN_CONTEXT_LOCK (self);
  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) aggregate_func, self, NULL);
  priv->gsources = g_list_prepend (priv->gsources, source);
  g_source_attach (source, priv->mcontext);
  MAIN_CONTEXT_UNLOCK (self);
}

static GstFlowReturn
_flush (GstAggregator * self)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Flushing everything");
  g_atomic_int_set (&priv->send_segment, TRUE);
  g_atomic_int_set (&priv->flush_seeking, FALSE);
  g_atomic_int_set (&priv->tags_changed, FALSE);
  if (klass->flush)
    ret = klass->flush (self);

  return ret;
}

static gboolean
_all_flush_stop_received (GstAggregator * self)
{
  GList *tmp;
  GstAggregatorPad *tmppad;

  GST_OBJECT_LOCK (self);
  for (tmp = GST_ELEMENT (self)->sinkpads; tmp; tmp = tmp->next) {
    tmppad = (GstAggregatorPad *) tmp->data;

    if (_check_pending_flush_stop (tmppad) == FALSE) {
      GST_DEBUG_OBJECT (tmppad, "Is not last %i -- %i",
          tmppad->priv->pending_flush_start, tmppad->priv->pending_flush_stop);
      GST_OBJECT_UNLOCK (self);
      return FALSE;
    }
  }
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/* GstAggregator vmethods default implementations */
static gboolean
_sink_event (GstAggregator * self, GstAggregatorPad * aggpad, GstEvent * event)
{
  gboolean res = TRUE;
  GstPad *pad = GST_PAD (aggpad);
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorPadPrivate *padpriv = aggpad->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      GstBuffer *tmpbuf;

      g_atomic_int_set (&aggpad->priv->flushing, TRUE);
      /*  Remove pad buffer and wake up the streaming thread */
      tmpbuf = gst_aggregator_pad_steal_buffer (aggpad);
      gst_buffer_replace (&tmpbuf, NULL);
      if (g_atomic_int_compare_and_exchange (&padpriv->pending_flush_start,
              TRUE, FALSE) == TRUE) {
        GST_DEBUG_OBJECT (aggpad, "Expecting FLUSH_STOP now");
        g_atomic_int_set (&padpriv->pending_flush_stop, TRUE);
      }

      if (g_atomic_int_get (&priv->flush_seeking)) {
        /* If flush_seeking we forward the first FLUSH_START */
        if (g_atomic_int_compare_and_exchange (&priv->pending_flush_start,
                TRUE, FALSE) == TRUE) {

          GST_DEBUG_OBJECT (self, "Flushing, pausing srcpad task");
          _stop_srcpad_task (self, event);
          priv->flow_return = GST_FLOW_OK;

          GST_INFO_OBJECT (self, "Getting STREAM_LOCK while seeking");
          GST_PAD_STREAM_LOCK (self->srcpad);
          GST_LOG_OBJECT (self, "GOT STREAM_LOCK");
          event = NULL;
          goto eat;
        }
      }

      /* We forward only in one case: right after flush_seeking */
      goto eat;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (aggpad, "Got FLUSH_STOP");

      _aggpad_flush (aggpad, self);
      if (g_atomic_int_get (&priv->flush_seeking)) {
        g_atomic_int_set (&aggpad->priv->pending_flush_stop, FALSE);

        if (g_atomic_int_get (&priv->flush_seeking)) {
          if (_all_flush_stop_received (self)) {
            /* That means we received FLUSH_STOP/FLUSH_STOP on
             * all sinkpads -- Seeking is Done... sending FLUSH_STOP */
            _flush (self);
            gst_pad_push_event (self->srcpad, event);
            priv->send_eos = TRUE;
            event = NULL;
            _add_aggregate_gsource (self);

            GST_INFO_OBJECT (self, "Releasing source pad STREAM_LOCK");
            GST_PAD_STREAM_UNLOCK (self->srcpad);
            _start_srcpad_task (self);
          }
        }
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
      PAD_LOCK_EVENT (aggpad);
      if (!aggpad->buffer) {
        aggpad->eos = TRUE;
      } else {
        aggpad->priv->pending_eos = TRUE;
      }
      PAD_UNLOCK_EVENT (aggpad);

      _add_aggregate_gsource (self);
      goto eat;
    }
    case GST_EVENT_SEGMENT:
    {
      PAD_LOCK_EVENT (aggpad);
      gst_event_copy_segment (event, &aggpad->segment);
      PAD_UNLOCK_EVENT (aggpad);
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

static gboolean
_flush_pad (GstAggregator * self, GstAggregatorPad * pad, gpointer unused_udata)
{
  _aggpad_flush (pad, self);

  return TRUE;
}

static gboolean
_stop (GstAggregator * agg)
{
  _reset_flow_values (agg);

  gst_aggregator_iterate_sinkpads (agg,
      (GstAggregatorPadForeachFunc) _flush_pad, NULL);

  return TRUE;
}

/* GstElement vmethods implementations */
static GstStateChangeReturn
_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAggregator *self = GST_AGGREGATOR (element);
  GstAggregatorClass *agg_class = GST_AGGREGATOR_GET_CLASS (self);


  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      agg_class->start (self);
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
      agg_class->stop (self);
      break;
    default:
      break;
  }

  return ret;

failure:
  {
    GST_ERROR_OBJECT (element, "parent failed state change");
    return ret;
  }
}

static void
_release_pad (GstElement * element, GstPad * pad)
{
  GstBuffer *tmpbuf;

  GstAggregator *self = GST_AGGREGATOR (element);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  GST_INFO_OBJECT (pad, "Removing pad");

  g_atomic_int_set (&aggpad->priv->flushing, TRUE);
  tmpbuf = gst_aggregator_pad_steal_buffer (aggpad);
  gst_buffer_replace (&tmpbuf, NULL);
  gst_element_remove_pad (element, pad);

  /* Something changed make sure we try to aggregate */
  _add_aggregate_gsource (self);
}

static GstPad *
_request_new_pad (GstElement * element,
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

static gboolean
_src_query (GstAggregator * self, GstQuery * query)
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
    default:
      break;
  }

  return gst_pad_query_default (self->srcpad, GST_OBJECT (self), query);

discard:
  return res;
}

static gboolean
event_forward_func (GstPad * pad, EventData * evdata)
{
  gboolean ret = TRUE;
  GstPad *peer = gst_pad_get_peer (pad);
  GstAggregatorPadPrivate *padpriv = GST_AGGREGATOR_PAD (pad)->priv;

  if (peer) {
    ret = gst_pad_send_event (peer, gst_event_ref (evdata->event));
    GST_DEBUG_OBJECT (pad, "return of event push is %d", ret);
    gst_object_unref (peer);
  }

  evdata->result &= ret;

  if (ret == FALSE) {
    if (GST_EVENT_TYPE (evdata->event) == GST_EVENT_SEEK)
      GST_ERROR_OBJECT (pad, "Event %" GST_PTR_FORMAT " failed", evdata->event);
    else
      GST_INFO_OBJECT (pad, "Event %" GST_PTR_FORMAT " failed", evdata->event);

    if (evdata->flush) {
      padpriv->pending_flush_start = FALSE;
      padpriv->pending_flush_stop = FALSE;
    }
  }

  /* Always send to all pads */
  return FALSE;
}

static gboolean
_set_flush_pending (GstAggregator * self, GstAggregatorPad * pad,
    gpointer udata)
{
  pad->priv->pending_flush_start = TRUE;
  pad->priv->pending_flush_stop = FALSE;

  return TRUE;
}

static gboolean
_forward_event_to_all_sinkpads (GstAggregator * self, GstEvent * event,
    gboolean flush)
{
  EventData evdata;

  evdata.event = event;
  evdata.result = TRUE;
  evdata.flush = flush;

  /* We first need to set all pads as flushing in a first pass
   * as flush_start flush_stop is sometimes sent synchronously
   * while we send the seek event */
  if (flush)
    gst_aggregator_iterate_sinkpads (self,
        (GstAggregatorPadForeachFunc) _set_flush_pending, NULL);
  gst_pad_forward (self->srcpad, (GstPadForwardFunction) event_forward_func,
      &evdata);

  gst_event_unref (event);

  return evdata.result;
}

static gboolean
_do_seek (GstAggregator * self, GstEvent * event)
{
  gdouble rate;
  GstFormat fmt;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gboolean flush;
  gboolean res;
  GstAggregatorPrivate *priv = self->priv;

  gst_event_parse_seek (event, &rate, &fmt, &flags, &start_type,
      &start, &stop_type, &stop);

  GST_INFO_OBJECT (self, "starting SEEK");

  flush = flags & GST_SEEK_FLAG_FLUSH;

  if (flush) {
    g_atomic_int_set (&priv->pending_flush_start, TRUE);
    g_atomic_int_set (&priv->flush_seeking, TRUE);
  }

  gst_segment_do_seek (&self->segment, rate, fmt, flags, start_type, start,
      stop_type, stop, NULL);

  /* forward the seek upstream */
  res = _forward_event_to_all_sinkpads (self, event, flush);
  event = NULL;

  if (!res) {
    g_atomic_int_set (&priv->flush_seeking, FALSE);
    g_atomic_int_set (&priv->pending_flush_start, FALSE);
  }

  GST_INFO_OBJECT (self, "seek done, result: %d", res);

  return res;
}

static gboolean
_src_event (GstAggregator * self, GstEvent * event)
{
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gst_event_ref (event);
      res = _do_seek (self, event);
      if (res)
        self->priv->seqnum = gst_event_get_seqnum (event);
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

  return _forward_event_to_all_sinkpads (self, event, FALSE);

done:
  return res;
}

static gboolean
src_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  return klass->src_event (GST_AGGREGATOR (parent), event);
}

static gboolean
src_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  return klass->src_query (GST_AGGREGATOR (parent), query);
}

static gboolean
src_activate_mode (GstPad * pad,
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
        _start_srcpad_task (self);
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
  _stop_srcpad_task (self, FALSE);

  return TRUE;
}

static gboolean
_sink_query (GstAggregator * self, GstAggregatorPad * aggpad, GstQuery * query)
{
  GstPad *pad = GST_PAD (aggpad);

  return gst_pad_query_default (pad, GST_OBJECT (self), query);
}

static void
gst_aggregator_finalize (GObject * object)
{
  GstAggregator *self = (GstAggregator *) object;

  g_mutex_clear (&self->priv->mcontext_lock);

  G_OBJECT_CLASS (aggregator_parent_class)->finalize (object);
}

static void
gst_aggregator_dispose (GObject * object)
{
  GstAggregator *self = (GstAggregator *) object;

  G_OBJECT_CLASS (aggregator_parent_class)->dispose (object);

  g_main_context_unref (self->priv->mcontext);
  _remove_all_sources (self);
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
  klass->start = _start;
  klass->stop = _stop;

  klass->sink_event = _sink_event;
  klass->sink_query = _sink_query;

  klass->src_event = _src_event;
  klass->src_query = _src_query;

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (_change_state);

  gobject_class->finalize = gst_aggregator_finalize;
  gobject_class->dispose = gst_aggregator_dispose;
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
  _reset_flow_values (self);

  priv->mcontext = g_main_context_new ();
  self->srcpad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) src_event_func));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) src_query_func));
  gst_pad_set_activatemode_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadActivateModeFunction) src_activate_mode));

  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  g_mutex_init (&self->priv->mcontext_lock);
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
_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstBuffer *actual_buf = buffer;
  GstAggregator *self = GST_AGGREGATOR (object);
  GstAggregatorPrivate *priv = self->priv;
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);
  GstAggregatorClass *aggclass = GST_AGGREGATOR_GET_CLASS (object);

  GST_DEBUG_OBJECT (aggpad, "Start chaining a buffer %" GST_PTR_FORMAT, buffer);

  if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE)
    goto flushing;

  if (g_atomic_int_get (&aggpad->priv->pending_eos) == TRUE)
    goto eos;

  PAD_LOCK_EVENT (aggpad);
  if (aggpad->buffer) {
    GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
    PAD_WAIT_EVENT (aggpad);
  }
  PAD_UNLOCK_EVENT (aggpad);

  if (g_atomic_int_get (&aggpad->priv->flushing) == TRUE)
    goto flushing;


  if (aggclass->clip) {
    aggclass->clip (self, aggpad, buffer, &actual_buf);
  }

  PAD_LOCK_EVENT (aggpad);
  if (aggpad->buffer)
    gst_buffer_unref (aggpad->buffer);
  aggpad->buffer = actual_buf;
  PAD_UNLOCK_EVENT (aggpad);

  _add_aggregate_gsource (self);

  GST_DEBUG_OBJECT (aggpad, "Done chaining");

  return priv->flow_return;

flushing:

  gst_buffer_unref (buffer);
  GST_DEBUG_OBJECT (aggpad, "We are flushing");

  return GST_FLOW_FLUSHING;

eos:

  gst_buffer_unref (buffer);
  GST_DEBUG_OBJECT (pad, "We are EOS already...");

  return GST_FLOW_EOS;
}

static gboolean
pad_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  return klass->sink_query (GST_AGGREGATOR (parent),
      GST_AGGREGATOR_PAD (pad), query);
}

static gboolean
pad_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAggregatorClass *klass = GST_AGGREGATOR_GET_CLASS (parent);

  return klass->sink_event (GST_AGGREGATOR (parent),
      GST_AGGREGATOR_PAD (pad), event);
}

static gboolean
pad_activate_mode_func (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  if (active == FALSE) {
    PAD_LOCK_EVENT (aggpad);
    g_atomic_int_set (&aggpad->priv->flushing, TRUE);
    gst_buffer_replace (&aggpad->buffer, NULL);
    PAD_BROADCAST_EVENT (aggpad);
    PAD_UNLOCK_EVENT (aggpad);
  } else {
    g_atomic_int_set (&aggpad->priv->flushing, FALSE);
    PAD_LOCK_EVENT (aggpad);
    PAD_BROADCAST_EVENT (aggpad);
    PAD_UNLOCK_EVENT (aggpad);
  }

  return TRUE;
}

/***********************************
 * GstAggregatorPad implementation  *
 ************************************/
static GstPadClass *aggregator_pad_parent_class = NULL;
G_DEFINE_TYPE (GstAggregatorPad, gst_aggregator_pad, GST_TYPE_PAD);

static void
_pad_constructed (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadChainFunction) _chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) pad_event_func));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) pad_query_func));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadActivateModeFunction) pad_activate_mode_func));
}

static void
gst_aggregator_pad_finalize (GObject * object)
{
  GstAggregatorPad *pad = (GstAggregatorPad *) object;

  g_mutex_clear (&pad->priv->event_lock);
  g_cond_clear (&pad->priv->event_cond);

  G_OBJECT_CLASS (aggregator_pad_parent_class)->finalize (object);
}

static void
gst_aggregator_pad_dispose (GObject * object)
{
  GstAggregatorPad *pad = (GstAggregatorPad *) object;
  GstBuffer *buf;

  buf = gst_aggregator_pad_steal_buffer (pad);
  if (buf)
    gst_buffer_unref (buf);

  G_OBJECT_CLASS (aggregator_pad_parent_class)->dispose (object);
}

static void
gst_aggregator_pad_class_init (GstAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  aggregator_pad_parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (GstAggregatorPadPrivate));

  gobject_class->constructed = GST_DEBUG_FUNCPTR (_pad_constructed);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_aggregator_pad_finalize);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_aggregator_pad_dispose);
}

static void
gst_aggregator_pad_init (GstAggregatorPad * pad)
{
  pad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pad, GST_TYPE_AGGREGATOR_PAD,
      GstAggregatorPadPrivate);

  pad->buffer = NULL;
  g_mutex_init (&pad->priv->event_lock);
  g_cond_init (&pad->priv->event_cond);

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

  PAD_LOCK_EVENT (pad);
  if (pad->buffer) {
    GST_TRACE_OBJECT (pad, "Consuming buffer");
    buffer = pad->buffer;
    pad->buffer = NULL;
    if (pad->priv->pending_eos) {
      pad->priv->pending_eos = FALSE;
      pad->eos = TRUE;
    }
    PAD_BROADCAST_EVENT (pad);
    GST_DEBUG_OBJECT (pad, "Consummed: %" GST_PTR_FORMAT, buffer);
  }
  PAD_UNLOCK_EVENT (pad);

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

  PAD_LOCK_EVENT (pad);
  if (pad->buffer)
    buffer = gst_buffer_ref (pad->buffer);
  PAD_UNLOCK_EVENT (pad);

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
