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
/**
 * SECTION:gststats
 * @short_description: log event stats
 *
 * A tracing module that builds usage statistic for elements and pads. 
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
  /* we can't rely on the address to be unique over time */
  guint index;
  /* for pre + post */
  GstClockTime last_ts;
  /* hierarchy */
  guint parent_ix;
} GstPadStats;

typedef struct
{
  /* we can't rely on the address to be unique over time */
  guint index;
  /* for pre + post */
  GstClockTime last_ts;
  /* time spend in this element */
  GstClockTime treal;
  /* hierarchy */
  guint parent_ix;
} GstElementStats;

/* data helper */

static GstElementStats no_elem_stats = { 0, };

static GstElementStats *
fill_element_stats (GstStatsTracer * self, GstElement * element)
{
  GstElementStats *stats = g_slice_new0 (GstElementStats);

  stats->index = self->num_elements++;
  stats->parent_ix = G_MAXUINT;
  return stats;
}

static void
log_new_element_stats (GstElementStats * stats, GstElement * element)
{
  gst_tracer_log_trace (gst_structure_new ("new-element",
          "ix", G_TYPE_UINT, stats->index,
          "parent-ix", G_TYPE_UINT, stats->parent_ix,
          "name", G_TYPE_STRING, GST_OBJECT_NAME (element),
          "type", G_TYPE_STRING, G_OBJECT_TYPE_NAME (element),
          "is-bin", G_TYPE_BOOLEAN, GST_IS_BIN (element), NULL));
}

static inline GstElementStats *
get_element_stats (GstStatsTracer * self, GstElement * element)
{
  GstElementStats *stats;
  gboolean is_new = FALSE;

  if (!element) {
    no_elem_stats.index = G_MAXUINT;
    return &no_elem_stats;
  }

  G_LOCK (_stats);
  if (!(stats = g_object_get_qdata ((GObject *) element, data_quark))) {
    stats = fill_element_stats (self, element);
    g_object_set_qdata ((GObject *) element, data_quark, stats);
    if (self->elements->len <= stats->index)
      g_ptr_array_set_size (self->elements, stats->index + 1);
    g_ptr_array_index (self->elements, stats->index) = stats;
    is_new = TRUE;
  }
  G_UNLOCK (_stats);
  if (G_UNLIKELY (stats->parent_ix == G_MAXUINT)) {
    GstElement *parent = GST_ELEMENT_PARENT (element);
    if (parent) {
      GstElementStats *parent_stats = get_element_stats (self, parent);
      stats->parent_ix = parent_stats->index;
    }
  }
  if (G_UNLIKELY (is_new)) {
    log_new_element_stats (stats, element);
  }
  return stats;
}

static void
free_element_stats (gpointer data)
{
  g_slice_free (GstElementStats, data);
}

/*
 * Get the element/bin owning the pad. 
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
/* TODO(ensonic): gst_pad_get_parent_element() would not work here, should we
 * add this as new api, e.g. gst_pad_find_parent_element();
 */
static GstElement *
get_real_pad_parent (GstPad * pad)
{
  GstObject *parent;

  if (!pad)
    return NULL;

  parent = GST_OBJECT_PARENT (pad);

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD (parent)) {
    pad = GST_PAD_CAST (parent);
    parent = GST_OBJECT_PARENT (pad);
  }
  return GST_ELEMENT_CAST (parent);
}

static GstPadStats no_pad_stats = { 0, };

static GstPadStats *
fill_pad_stats (GstStatsTracer * self, GstPad * pad)
{
  GstPadStats *stats = g_slice_new0 (GstPadStats);

  stats->index = self->num_pads++;
  stats->parent_ix = G_MAXUINT;

  return stats;
}

static void
log_new_pad_stats (GstPadStats * stats, GstPad * pad)
{
  gst_tracer_log_trace (gst_structure_new ("new-pad",
          "ix", G_TYPE_UINT, stats->index,
          "parent-ix", G_TYPE_UINT, stats->parent_ix,
          "name", G_TYPE_STRING, GST_OBJECT_NAME (pad),
          "type", G_TYPE_STRING, G_OBJECT_TYPE_NAME (pad),
          "is-ghostpad", G_TYPE_BOOLEAN, GST_IS_GHOST_PAD (pad),
          "pad-direction", GST_TYPE_PAD_DIRECTION, GST_PAD_DIRECTION (pad),
          "thread-id", G_TYPE_UINT, GPOINTER_TO_UINT (g_thread_self ()), NULL));
}

static GstPadStats *
get_pad_stats (GstStatsTracer * self, GstPad * pad)
{
  GstPadStats *stats;
  gboolean is_new = FALSE;

  if (!pad) {
    no_pad_stats.index = G_MAXUINT;
    return &no_pad_stats;
  }

  G_LOCK (_stats);
  if (!(stats = g_object_get_qdata ((GObject *) pad, data_quark))) {
    stats = fill_pad_stats (self, pad);
    g_object_set_qdata ((GObject *) pad, data_quark, stats);
    if (self->pads->len <= stats->index)
      g_ptr_array_set_size (self->pads, stats->index + 1);
    g_ptr_array_index (self->pads, stats->index) = stats;
    is_new = TRUE;
  }
  G_UNLOCK (_stats);
  if (G_UNLIKELY (stats->parent_ix == G_MAXUINT)) {
    GstElement *elem = get_real_pad_parent (pad);
    if (elem) {
      GstElementStats *elem_stats = get_element_stats (self, elem);

      stats->parent_ix = elem_stats->index;
    }
  }
  if (G_UNLIKELY (is_new)) {
    log_new_pad_stats (stats, pad);
  }
  return stats;
}

static void
free_pad_stats (gpointer data)
{
  g_slice_free (GstPadStats, data);
}

static void
do_buffer_stats (GstStatsTracer * self, GstPad * this_pad,
    GstPadStats * this_pad_stats, GstPad * that_pad,
    GstPadStats * that_pad_stats, GstBuffer * buf, GstClockTime elapsed)
{
  GstElement *this_elem = get_real_pad_parent (this_pad);
  GstElementStats *this_elem_stats = get_element_stats (self, this_elem);
  GstElement *that_elem = get_real_pad_parent (that_pad);
  GstElementStats *that_elem_stats = get_element_stats (self, that_elem);

  /* TODO(ensonic): need a quark-table (shared with the tracer-front-ends?) */
  gst_tracer_log_trace (gst_structure_new ("buffer",
          "ts", G_TYPE_UINT64, elapsed,
          "pad-ix", G_TYPE_UINT, this_pad_stats->index,
          "elem-ix", G_TYPE_UINT, this_elem_stats->index,
          "peer-pad-ix", G_TYPE_UINT, that_pad_stats->index,
          "peer-elem-ix", G_TYPE_UINT, that_elem_stats->index,
          "buffer-size", G_TYPE_UINT, gst_buffer_get_size (buf),
          /* TODO(ensonic): do PTS and DTS */
          "buffer-ts", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (buf),
          "buffer-duration", G_TYPE_UINT64, GST_BUFFER_DURATION (buf),
          "buffer-flags", GST_TYPE_BUFFER_FLAGS, GST_BUFFER_FLAGS (buf),
          /*
             scheduling-jitter: for this we need the last_ts on the pad
           */
          NULL));
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
    printf ("%" GST_TIME_FORMAT
        " transmission on unparented target pad %s_%s -> %s_%s\n",
        GST_TIME_ARGS (elapsed), GST_DEBUG_PAD_NAME (pad),
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

  /* announce trace formats */
  /* *INDENT-OFF* */
  gst_tracer_log_trace (gst_structure_new ("buffer.class",
      "pad-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "pad",  /* TODO: use genum */
          NULL),
      "element-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "element",  /* TODO: use genum */
          NULL),
      "peer-pad-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "pad",  /* TODO: use genum */
          NULL),
      "peer-element-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "element",  /* TODO: use genum */
          NULL),
      "buffer-size", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT,
          "description", G_TYPE_STRING, "size of buffer in bytes",
          "flags", G_TYPE_STRING, "",  /* TODO: use gflags */ 
          "min", G_TYPE_UINT, 0, 
          "max", G_TYPE_UINT, G_MAXUINT,
          NULL),
      "buffer-ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "timestamp of the buffer in ns",
          "flags", G_TYPE_STRING, "",  /* TODO: use gflags */ 
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "buffer-duration", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "duration of the buffer in ns",
          "flags", G_TYPE_STRING, "",  /* TODO: use gflags */ 
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      /* TODO(ensonic): "buffer-flags" */
      NULL));
  gst_tracer_log_trace (gst_structure_new ("event.class",
      "pad-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "pad",  /* TODO: use genum */
          NULL),
      "element-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "element",  /* TODO: use genum */
          NULL),
      "name", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "name of the event",
          "flags", G_TYPE_STRING, "",  /* TODO: use gflags */ 
          NULL),
      NULL));
  gst_tracer_log_trace (gst_structure_new ("message.class",
      "element-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "element",  /* TODO: use genum */
          NULL),
      "name", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "name of the message",
          "flags", G_TYPE_STRING, "",  /* TODO: use gflags */ 
          NULL),
      NULL));
  gst_tracer_log_trace (gst_structure_new ("query.class",
      "element-ix", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "related-to", G_TYPE_STRING, "element",  /* TODO: use genum */
          NULL),
      "name", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "description", G_TYPE_STRING, "name of the query",
          "flags", G_TYPE_STRING, "",  /* TODO: use gflags */ 
          NULL),
      NULL));
  /* *INDENT-ON* */
}

/* hooks */

static void
do_push_buffer_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *this_pad = va_arg (var_args, GstPad *);
  GstBuffer *buffer = va_arg (var_args, GstBuffer *);
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);

  do_buffer_stats (self, this_pad, this_pad_stats, that_pad, that_pad_stats,
      buffer, ts);
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
  GstPad *this_pad;
  GstPadStats *this_pad_stats;
  GstPad *that_pad;
  GstPadStats *that_pad_stats;
  guint64 ts;
} DoPushBufferListArgs;

static gboolean
do_push_buffer_list_item (GstBuffer ** buffer, guint idx, gpointer user_data)
{
  DoPushBufferListArgs *args = (DoPushBufferListArgs *) user_data;

  do_buffer_stats (args->self, args->this_pad, args->this_pad_stats,
      args->that_pad, args->that_pad_stats, *buffer, args->ts);
  return TRUE;
}

static void
do_push_buffer_list_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *this_pad = va_arg (var_args, GstPad *);
  GstBufferList *list = va_arg (var_args, GstBufferList *);
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);
  DoPushBufferListArgs args = { self, this_pad, this_pad_stats, that_pad,
    that_pad_stats, ts
  };

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
do_pull_range_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstPadStats *stats = get_pad_stats (self, pad);
  stats->last_ts = ts;
}

static void
do_pull_range_post (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *this_pad = va_arg (var_args, GstPad *);
  GstBuffer *buffer = va_arg (var_args, GstBuffer *);
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  guint64 last_ts = this_pad_stats->last_ts;
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);

  if (buffer != NULL) {
    do_buffer_stats (self, this_pad, this_pad_stats, that_pad, that_pad_stats,
        buffer, ts);
  }
  do_element_stats (self, this_pad, last_ts, ts);
}

static void
do_push_event_pre (GstStatsTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstEvent *ev = va_arg (var_args, GstEvent *);
  GstElement *elem = get_real_pad_parent (pad);
  GstElementStats *elem_stats = get_element_stats (self, elem);
  GstPadStats *pad_stats = get_pad_stats (self, pad);

  elem_stats->last_ts = ts;
  gst_tracer_log_trace (gst_structure_new ("event",
          "ts", G_TYPE_UINT64, ts,
          "pad-ix", G_TYPE_UINT, pad_stats->index,
          "elem-ix", G_TYPE_UINT, elem_stats->index,
          "name", G_TYPE_STRING, GST_EVENT_TYPE_NAME (ev), NULL));
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
  GstMessage *msg = va_arg (var_args, GstMessage *);
  GstElementStats *stats = get_element_stats (self, elem);

  stats->last_ts = ts;
  gst_tracer_log_trace (gst_structure_new ("message",
          "ts", G_TYPE_UINT64, ts,
          "elem-ix", G_TYPE_UINT, stats->index,
          "name", G_TYPE_STRING, GST_MESSAGE_TYPE_NAME (msg), NULL));
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
  GstQuery *qry = va_arg (var_args, GstQuery *);
  GstElementStats *stats = get_element_stats (self, elem);

  stats->last_ts = ts;
  gst_tracer_log_trace (gst_structure_new ("query",
          "ts", G_TYPE_UINT64, ts,
          "elem-ix", G_TYPE_UINT, stats->index,
          "name", G_TYPE_STRING, GST_QUERY_TYPE_NAME (qry), NULL));
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
      do_pull_range_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_POST:
      do_pull_range_post (self, var_args);
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
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}
