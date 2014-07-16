/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gststats.c: tracing module that logs events
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
#  include "config.h"
#endif

#include "gststats.h"

#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_stats_debug);
#define GST_CAT_DEFAULT gst_stats_debug

static GQuark data_quark;
G_LOCK_DEFINE (_stats);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_stats_debug, "stats", 0, "stats tracer"); \
    data_quark = g_quark_from_static_string ("gststats:data");
#define gst_stats_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstStatsTracer, gst_stats_tracer, GST_TYPE_TRACER,
    _do_init);

typedef struct
{
  /* human readable pad name and details */
  gchar *name;
  guint index;
  GType type;
  GstPadDirection dir;
  /* buffer statistics */
  guint num_buffers;
  guint num_discont, num_gap, num_delta;
  guint min_size, max_size, avg_size;
  /* first and last activity on the pad, expected next_ts */
  GstClockTime first_ts, last_ts, next_ts;
  /* in which thread does it operate */
  gpointer thread_id;
} GstPadStats;

typedef struct
{
  /* human readable element name */
  gchar *name;
  guint index;
  GType type;
  /* buffer statistics */
  guint recv_buffers, sent_buffers;
  guint64 recv_bytes, sent_bytes;
  /* event, message statistics */
  guint num_events, num_messages, num_queries;
  /* first activity on the element */
  GstClockTime first_ts, last_ts;
  /* time spend in this element */
  GstClockTime treal;
  /* hierarchy */
  guint parent_ix;
} GstElementStats;

/* data helper */

static GstElementStats *
fill_element_stats (GstStatsTracer * self, GstElement * element)
{
  GstElementStats *stats = g_slice_new0 (GstElementStats);

  if (GST_IS_BIN (element)) {
    self->num_bins++;
  }
  stats->index = self->num_elements++;
  stats->type = G_OBJECT_TYPE (element);
  stats->first_ts = GST_CLOCK_TIME_NONE;
  stats->parent_ix = G_MAXUINT;
  return stats;
}

#if 0
static GstElementStats *
get_element_stats_by_id (GstStatsTracer * self, guint ix)
{
  return g_ptr_array_index (self->elements, ix);
}
#endif

static inline GstElementStats *
get_element_stats (GstStatsTracer * self, GstElement * element)
{
  GstElementStats *stats;

  G_LOCK (_stats);
  if (!(stats = g_object_get_qdata ((GObject *) element, data_quark))) {
    stats = fill_element_stats (self, element);
    g_object_set_qdata ((GObject *) element, data_quark, stats);
    if (self->elements->len <= stats->index)
      g_ptr_array_set_size (self->elements, stats->index + 1);
    g_ptr_array_index (self->elements, stats->index) = stats;
  }
  G_UNLOCK (_stats);
  if (G_UNLIKELY (stats->parent_ix == G_MAXUINT)) {
    GstElement *parent = GST_ELEMENT_PARENT (element);
    if (parent) {
      GstElementStats *parent_stats = get_element_stats (self, parent);
      stats->parent_ix = parent_stats->index;
    }
  }
  if (G_UNLIKELY (!stats->name)) {
    if (GST_OBJECT_NAME (element)) {
      stats->name = g_strdup (GST_OBJECT_NAME (element));
    }
  }
  return stats;
}

static void
free_element_stats (gpointer data)
{
  g_slice_free (GstElementStats, data);
}

static GstPadStats *
fill_pad_stats (GstStatsTracer * self, GstPad * pad)
{
  GstPadStats *stats = g_slice_new0 (GstPadStats);

  if (GST_IS_GHOST_PAD (pad)) {
    self->num_ghostpads++;
  }
  stats->index = self->num_pads++;
  stats->type = G_OBJECT_TYPE (pad);
  stats->dir = GST_PAD_DIRECTION (pad);
  stats->min_size = G_MAXUINT;
  stats->first_ts = stats->last_ts = stats->next_ts = GST_CLOCK_TIME_NONE;
  stats->thread_id = g_thread_self ();
  return stats;
}

static GstPadStats *
get_pad_stats (GstStatsTracer * self, GstPad * pad)
{
  GstPadStats *stats;

  if (!pad)
    return NULL;

  G_LOCK (_stats);
  if (!(stats = g_object_get_qdata ((GObject *) pad, data_quark))) {
    stats = fill_pad_stats (self, pad);
    g_object_set_qdata ((GObject *) pad, data_quark, stats);
    if (self->pads->len <= stats->index)
      g_ptr_array_set_size (self->pads, stats->index + 1);
    g_ptr_array_index (self->pads, stats->index) = stats;
  }
  G_UNLOCK (_stats);
  if (G_UNLIKELY (!stats->name)) {
    GstObject *parent = GST_OBJECT_PARENT (pad);
    /* yes we leak the names right now ... */
    if (GST_IS_ELEMENT (parent)) {
      /* pad is regular pad */
      get_element_stats (self, GST_ELEMENT_CAST (parent));
      if (GST_OBJECT_NAME (parent) && GST_OBJECT_NAME (pad)) {
        stats->name =
            g_strdup_printf ("%s_%s", GST_OBJECT_NAME (parent),
            GST_OBJECT_NAME (pad));
      }
    } else if (GST_IS_GHOST_PAD (parent)) {
      /* pad is proxy pad */
      get_pad_stats (self, GST_PAD_CAST (parent));
      if (GST_OBJECT_NAME (parent) && GST_OBJECT_NAME (pad)) {
        stats->name =
            g_strdup_printf ("%s_%s", GST_OBJECT_NAME (parent),
            GST_OBJECT_NAME (pad));
      }
    }
  }
  return stats;
}

static void
free_pad_stats (gpointer data)
{
  g_slice_free (GstPadStats, data);
}

static void
do_pad_stats (GstStatsTracer * self, GstPad * pad, GstPadStats * stats,
    GstBuffer * buffer, GstClockTime elapsed)
{
  guint size = gst_buffer_get_size (buffer);
  gulong avg_size;

  self->num_buffers++;
  /* size stats */
  avg_size = (((gulong) stats->avg_size * (gulong) stats->num_buffers) + size);
  stats->num_buffers++;
  stats->avg_size = (guint) (avg_size / stats->num_buffers);
  if (size < stats->min_size)
    stats->min_size = size;
  else if (size > stats->max_size)
    stats->max_size = size;
  /* time stats */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (stats->first_ts)))
    stats->first_ts = elapsed;
  stats->last_ts = elapsed;
  /* flag stats */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    stats->num_gap++;
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    stats->num_delta++;
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
    stats->num_discont++;
  /* TODO(ensonic): there is a bunch of new flags in 1.0 */

  /* update timestamps */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) +
      GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stats->next_ts =
        GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
  } else {
    stats->next_ts = GST_CLOCK_TIME_NONE;
  }
}

static void
do_transmission_stats (GstStatsTracer * self, GstPad * pad, GstBuffer * buf,
    GstClockTime elapsed)
{
  GstObject *parent = GST_OBJECT_PARENT (pad);
  GstElement *this =
      GST_ELEMENT_CAST (GST_IS_PAD (parent) ? GST_OBJECT_PARENT (parent) :
      parent);
  GstElementStats *this_stats = get_element_stats (self, this);
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElementStats *peer_stats;
  GSList *bin_i_stats = NULL, *bin_o_stats = NULL;
  GstPadStats *peer_pad_stats;

  if (!peer_pad)
    return;

  parent = GST_OBJECT_PARENT (peer_pad);
#ifdef _ENABLE_BLACK_MAGIC_
  /* walk the ghost pad chain downstream to get the real pad */
  /* if parent of peer_pad is a ghost-pad, then peer_pad is a proxy_pad */
  while (parent && GST_IS_GHOST_PAD (parent)) {
    peer_pad = GST_PAD_CAST (parent);
    /* if this is now the ghost pad, get the peer of this */
    get_pad_stats (self, peer_pad);
    if (parent = GST_OBJECT_PARENT (peer_pad)) {
      peer_stats = get_element_stats (self, GST_ELEMENT_CAST (parent));
      bin_o_stats = g_slist_prepend (bin_o_stats, peer_stats);
    }
    peer_pad = GST_PAD_PEER (GST_GHOST_PAD_CAST (peer_pad));
    parent = peer_pad ? GST_OBJECT_PARENT (peer_pad) : NULL;
  }
  /* walk the ghost pad chain upstream to get the real pad */
  /* if peer_pad is a ghost-pad, then parent is a bin adn it is the parent of
   * a proxy_pad */
  while (peer_pad && GST_IS_GHOST_PAD (peer_pad)) {
    get_pad_stats (self, peer_pad);
    peer_stats = get_element_stats (self, GST_ELEMENT_CAST (parent));
    bin_i_stats = g_slist_prepend (bin_i_stats, peer_stats);
    peer_pad = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (peer_pad));
    parent = peer_pad ? GST_OBJECT_PARENT (peer_pad) : NULL;
  }
#else
  if (parent && GST_IS_GHOST_PAD (parent)) {
    peer_pad = GST_PAD_CAST (parent);
    parent = GST_OBJECT_PARENT (peer_pad);
  }
#endif

  if (!parent) {
    fprintf (stderr,
        "%" GST_TIME_FORMAT " transmission on unparented target pad%s_%s\n",
        GST_TIME_ARGS (elapsed), GST_DEBUG_PAD_NAME (peer_pad));
    return;
  }
  peer_stats = get_element_stats (self, GST_ELEMENT_CAST (parent));

  peer_pad_stats = get_pad_stats (self, peer_pad);
  do_pad_stats (self, peer_pad, peer_pad_stats, buf, elapsed);

  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
    /* push */
    this_stats->sent_buffers++;
    peer_stats->recv_buffers++;
    this_stats->sent_bytes += gst_buffer_get_size (buf);
    peer_stats->recv_bytes += gst_buffer_get_size (buf);
    /* time stats */
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (this_stats->first_ts))) {
      this_stats->first_ts = elapsed;
      //printf("%" GST_TIME_FORMAT " %s pushes on %s\n",GST_TIME_ARGS(elapsed),this_stats->name,peer_stats->name);
    }
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (peer_stats->first_ts))) {
      peer_stats->first_ts = elapsed + 1;
      //printf("%" GST_TIME_FORMAT " %s is beeing pushed from %s\n",GST_TIME_ARGS(elapsed),peer_stats->name,this_stats->name);
    }
#ifdef _ENABLE_BLACK_MAGIC_
    for (node = bin_o_stats; node; node = g_slist_next (node)) {
      peer_stats = node->data;
      peer_stats->sent_buffers++;
      peer_stats->sent_bytes += gst_buffer_get_size (buf);
    }
    for (node = bin_i_stats; node; node = g_slist_next (node)) {
      peer_stats = node->data;
      peer_stats->recv_buffers++;
      peer_stats->recv_bytes += gst_buffer_get_size (buf);
    }
#endif
  } else {
    /* pull */
    peer_stats->sent_buffers++;
    this_stats->recv_buffers++;
    peer_stats->sent_bytes += gst_buffer_get_size (buf);
    this_stats->recv_bytes += gst_buffer_get_size (buf);
    /* time stats */
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (this_stats->first_ts))) {
      this_stats->first_ts = elapsed + 1;
      //printf("%" GST_TIME_FORMAT " %s pulls from %s\n",GST_TIME_ARGS(elapsed),this_stats->name,peer_stats->name);
    }
    if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (peer_stats->first_ts))) {
      peer_stats->first_ts = elapsed;
      //printf("%" GST_TIME_FORMAT " %s is beeing pulled from %s\n",GST_TIME_ARGS(elapsed),peer_stats->name,this_stats->name);
    }
#ifdef _ENABLE_BLACK_MAGIC_
    for (node = bin_i_stats; node; node = g_slist_next (node)) {
      peer_stats = node->data;
      peer_stats->sent_buffers++;
      peer_stats->sent_bytes += gst_buffer_get_size (buf);
    }
    for (node = bin_o_stats; node; node = g_slist_next (node)) {
      peer_stats = node->data;
      peer_stats->recv_buffers++;
      peer_stats->recv_bytes += gst_buffer_get_size (buf);
    }
#endif
  }
  g_slist_free (bin_o_stats);
  g_slist_free (bin_i_stats);
}

static void
do_element_stats (GstStatsTracer * self, GstPad * pad, GstClockTime elapsed1,
    GstClockTime elapsed2)
{
  GstClockTimeDiff elapsed = GST_CLOCK_DIFF (elapsed1, elapsed2);
  GstObject *parent = GST_OBJECT_PARENT (pad);
  GstElement *this =
      GST_ELEMENT_CAST (GST_IS_PAD (parent) ? GST_OBJECT_PARENT (parent) :
      parent);
  GstElementStats *this_stats = get_element_stats (self, this);
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElementStats *peer_stats;

  if (!peer_pad)
    return;

  /* walk the ghost pad chain downstream to get the real pad */
  /* if parent of peer_pad is a ghost-pad, then peer_pad is a proxy_pad */
  parent = GST_OBJECT_PARENT (peer_pad);
  if (parent && GST_IS_GHOST_PAD (parent)) {
    peer_pad = GST_PAD_CAST (parent);
    /* if this is now the ghost pad, get the peer of this */
    get_pad_stats (self, peer_pad);
    if ((parent = GST_OBJECT_PARENT (peer_pad))) {
      get_element_stats (self, GST_ELEMENT_CAST (parent));
    }
    peer_pad = GST_PAD_PEER (GST_GHOST_PAD_CAST (peer_pad));
    parent = peer_pad ? GST_OBJECT_PARENT (peer_pad) : NULL;
  }
  /* walk the ghost pad chain upstream to get the real pad */
  /* if peer_pad is a ghost-pad, then parent is a bin and it is the parent of
   * a proxy_pad */
  while (peer_pad && GST_IS_GHOST_PAD (peer_pad)) {
    get_pad_stats (self, peer_pad);
    get_element_stats (self, GST_ELEMENT_CAST (parent));
    peer_pad = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (peer_pad));
    parent = peer_pad ? GST_OBJECT_PARENT (peer_pad) : NULL;
  }

  if (!parent) {
    GstPadStats *pad_stats = get_pad_stats (self, pad);

    printf ("%" GST_TIME_FORMAT
        " transmission on unparented target pad %s -> %s_%s\n",
        GST_TIME_ARGS (elapsed), pad_stats->name,
        GST_DEBUG_PAD_NAME (peer_pad));
    return;
  }
  peer_stats = get_element_stats (self, GST_ELEMENT_CAST (parent));

  /* we'd like to gather time spend in each element, but this does not make too
   * much sense yet
   * pure push/pull-based:
   *   - the time spend in the push/pull_range is accounted for the peer and
   *     removed from the current element
   *   - this works for chains
   *   - drawback is sink elements that block to sync have a high time usage
   *     - we could rerun the ests with sync=false
   * both:
   *   - a.g. demuxers both push and pull. thus we subtract time for the pull
   *     and the push operations, but never add anything.
   *   - can we start a counter after push/pull in such elements and add then
   *     time to the element upon next pad activity?
   */
#if 1
  /* this does not make sense for demuxers */
  this_stats->treal -= elapsed;
  peer_stats->treal += elapsed;
#else
  /* this creates several >100% figures */
  this_stats->treal += GST_CLOCK_DIFF (this_stats->last_ts, elapsed2) - elapsed;
  peer_stats->treal += elapsed;
  this_stats->last_ts = elapsed2;
  peer_stats->last_ts = elapsed2;
#endif
}

/* FIXME: this looks a bit weired, check that we really want to do this
 *
 * in: a normal pad
 * out: the element
 *
 * in: a proxy pad
 * out: the element that contains the peer of the proxy
 *
 * in: a ghost pad
 * out: the bin owning the ghostpad
 */
static GstObject *
get_real_pad_parent (GstPad * pad)
{
  GstObject *parent = GST_OBJECT_PARENT (pad);

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD (parent)) {
    pad = GST_PAD_CAST (parent);
    parent = GST_OBJECT_PARENT (pad);
  }
  /* if pad is a ghost-pad, then parent is a bin and it is the parent of a
   * proxy_pad */
  while (parent && GST_IS_GHOST_PAD (pad)) {
    pad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
    parent = pad ? GST_OBJECT_PARENT (pad) : NULL;
  }
  return parent;
}

/* tracer class */

static void gst_stats_tracer_finalize (GObject * obj);
static void gst_stats_tracer_invoke (GstTracer * obj, GstTracerHookId id,
    GstTracerMessageId mid, va_list var_args);

static void
gst_stats_tracer_class_init (GstStatsTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstTracerClass *gst_tracer_class = GST_TRACER_CLASS (klass);

  gobject_class->finalize = gst_stats_tracer_finalize;

  gst_tracer_class->invoke = gst_stats_tracer_invoke;
}

static void
gst_stats_tracer_init (GstStatsTracer * self)
{
  g_object_set (self, "mask",
      GST_TRACER_HOOK_BUFFERS | GST_TRACER_HOOK_EVENTS |
      GST_TRACER_HOOK_MESSAGES | GST_TRACER_HOOK_QUERIES, NULL);
  self->elements = g_ptr_array_new_with_free_func (free_element_stats);
  self->pads = g_ptr_array_new_with_free_func (free_pad_stats);
}

static void
do_push_buffer_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstBuffer *buffer = va_arg (var_args, GstBuffer *);
  GstPadStats *stats = get_pad_stats (self, pad);

  do_pad_stats (self, pad, stats, buffer, ts);
  do_transmission_stats (self, pad, buffer, ts);
}

static void
do_push_buffer_post (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstPadStats *stats = get_pad_stats (self, pad);

  do_element_stats (self, pad, stats->last_ts, ts);
}

typedef struct
{
  GstStatsTracer *self;
  GstPad *pad;
  GstPadStats *stats;
  guint64 ts;
} DoPushBufferListArgs;

static gboolean
do_push_buffer_list_item (GstBuffer ** buffer, guint idx, gpointer user_data)
{
  DoPushBufferListArgs *args = (DoPushBufferListArgs *) user_data;

  do_pad_stats (args->self, args->pad, args->stats, *buffer, args->ts);
  do_transmission_stats (args->self, args->pad, *buffer, args->ts);
  return TRUE;
}

static void
do_push_buffer_list_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstBufferList *list = va_arg (var_args, GstBufferList *);
  GstPadStats *stats = get_pad_stats (self, pad);
  DoPushBufferListArgs args = { self, pad, stats, ts };

  gst_buffer_list_foreach (list, do_push_buffer_list_item, &args);
}

static void
do_push_buffer_list_post (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstPadStats *stats = get_pad_stats (self, pad);

  do_element_stats (self, pad, stats->last_ts, ts);
}

static void
do_pull_range_list_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstPadStats *stats = get_pad_stats (self, pad);
  stats->last_ts = ts;
}

static void
do_pull_range_list_post (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstBuffer *buffer = va_arg (var_args, GstBuffer *);
  GstPadStats *stats = get_pad_stats (self, pad);
  guint64 last_ts = stats->last_ts;

  if (buffer != NULL) {
    do_pad_stats (self, pad, stats, buffer, ts);
    do_transmission_stats (self, pad, buffer, ts);
  }
  do_element_stats (self, pad, last_ts, ts);
}

static void
do_push_event_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstObject *parent;
  GstElement *elem;

  parent = get_real_pad_parent (pad);
  if ((elem = GST_ELEMENT (parent))) {
    GstElementStats *stats = get_element_stats (self, elem);
    get_pad_stats (self, pad);

    stats->last_ts = ts;
    stats->num_events++;
    self->num_events++;
  }
}

static void
do_push_event_post (GstStatsTracer * self, va_list var_args)
{
#if 0
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
#endif
}

static void
do_post_message_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstElement *elem = va_arg (var_args, GstElement *);
  GstElementStats *stats = get_element_stats (self, elem);

  stats->last_ts = ts;
  stats->num_messages++;
  self->num_messages++;
}

static void
do_post_message_post (GstStatsTracer * self, va_list var_args)
{
#if 0
  guint64 ts = va_arg (var_args, guint64);
  GstElement *elem = va_arg (var_args, GstElement *);
#endif
}

static void
do_query_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstElement *elem = va_arg (var_args, GstElement *);
  GstElementStats *stats = get_element_stats (self, elem);

  stats->last_ts = ts;
  stats->num_queries++;
  self->num_queries++;
}

static void
do_query_post (GstStatsTracer * self, va_list var_args)
{
#if 0
  guint64 ts = va_arg (var_args, guint64);
  GstElement *elem = va_arg (var_args, GstElement *);
#endif
}

static void
gst_stats_tracer_invoke (GstTracer * obj, GstTracerHookId hid,
    GstTracerMessageId mid, va_list var_args)
{
  GstStatsTracer *self = GST_STATS_TRACER_CAST (obj);

  switch (mid) {
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_PRE:
      do_push_buffer_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_POST:
      do_push_buffer_post (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_PRE:
      do_push_buffer_list_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_POST:
      do_push_buffer_list_post (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_PRE:
      do_pull_range_list_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_POST:
      do_pull_range_list_post (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_PRE:
      do_push_event_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_POST:
      do_push_event_post (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_PRE:
      do_post_message_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_POST:
      do_post_message_post (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_PRE:
      do_query_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_POST:
      do_query_post (self, var_args);
      break;
    default:
      break;
  }
}

static void
gst_stats_tracer_finalize (GObject * obj)
{
  GstStatsTracer *self = GST_STATS_TRACER_CAST (obj);

  /* print overall stats */
  puts ("\nOverall Statistics:");
  printf ("Number of Elements: %u\n", self->num_elements - self->num_bins);
  printf ("Number of Bins: %u\n", self->num_bins);
  printf ("Number of Pads: %u\n", self->num_pads - self->num_ghostpads);
  printf ("Number of GhostPads: %u\n", self->num_ghostpads);
  printf ("Number of Buffers passed: %" G_GUINT64_FORMAT "\n",
      self->num_buffers);
  printf ("Number of Events sent: %" G_GUINT64_FORMAT "\n", self->num_events);
  printf ("Number of Message sent: %" G_GUINT64_FORMAT "\n",
      self->num_messages);
  printf ("Number of Queries sent: %" G_GUINT64_FORMAT "\n", self->num_queries);
  //printf("Time: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (self->last_ts));

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}
