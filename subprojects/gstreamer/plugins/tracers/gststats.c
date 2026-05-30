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
 * SECTION:tracer-stats
 * @short_description: log event stats
 *
 * A tracing module that builds usage statistic for elements and pads.
 *
 * The statistics are emitted as structured tracer events. Enable the `log`
 * tracer alongside it so they are written to the `GST_TRACER` debug category,
 * then post-process the log with `gst-stats-1.0`:
 *
 * ```
 * GST_TRACERS="stats;log" GST_DEBUG=GST_TRACER:7 GST_DEBUG_FILE=trace.log ./...
 * gst-stats-1.0 trace.log
 * ```
 */

/**
 * GstStatsTracer:
 *
 * Since: 1.8
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gststats.h"

#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (gst_stats_debug);
#define GST_CAT_DEFAULT gst_stats_debug

static GQuark data_quark;
G_LOCK_DEFINE (_elem_stats);
G_LOCK_DEFINE (_pad_stats);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_stats_debug, "stats", 0, "stats tracer"); \
    data_quark = g_quark_from_static_string ("gststats:data");
#define gst_stats_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstStatsTracer, gst_stats_tracer, GST_TYPE_TRACER,
    _do_init);

static GstTraceFormat *tr_new_element;
static GstTraceFormat *tr_new_pad;
static GstTraceFormat *tr_buffer;
static GstTraceFormat *tr_element_query;
static GstTraceFormat *tr_event;
static GstTraceFormat *tr_message;
static GstTraceFormat *tr_query;

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
  GstElementStats *stats = g_new0 (GstElementStats, 1);

  stats->index = self->num_elements++;
  stats->parent_ix = G_MAXUINT;
  return stats;
}

static void
log_new_element_stats (GstElementStats * stats, GstElement * element,
    GstClockTime elapsed)
{
  gst_trace_event (tr_new_element,
      GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT64 (elapsed),
          UINT (stats->index),
          UINT (stats->parent_ix),
          STRING (GST_OBJECT_NAME (element)),
          STRING (G_OBJECT_TYPE_NAME (element)),
          BOOLEAN (GST_IS_BIN (element))));
}

static void
free_element_stats (gpointer data)
{
  g_free (data);
}

static GstElementStats *
create_element_stats (GstStatsTracer * self, GstElement * element)
{
  GstElementStats *stats;

  stats = fill_element_stats (self, element);
  g_object_set_qdata_full ((GObject *) element, data_quark, stats,
      free_element_stats);

  return stats;
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

  G_LOCK (_elem_stats);
  if (!(stats = g_object_get_qdata ((GObject *) element, data_quark))) {
    stats = create_element_stats (self, element);
    is_new = TRUE;
  }
  G_UNLOCK (_elem_stats);
  if (G_UNLIKELY (stats->parent_ix == G_MAXUINT)) {
    GstElement *parent = GST_ELEMENT_PARENT (element);
    if (parent) {
      GstElementStats *parent_stats = get_element_stats (self, parent);
      stats->parent_ix = parent_stats->index;
    }
  }
  if (G_UNLIKELY (is_new)) {
    log_new_element_stats (stats, element, GST_CLOCK_TIME_NONE);
  }
  return stats;
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
  GstPadStats *stats = g_new0 (GstPadStats, 1);

  stats->index = self->num_pads++;
  stats->parent_ix = G_MAXUINT;

  return stats;
}

static void
log_new_pad_stats (GstPadStats * stats, GstPad * pad)
{
  gst_trace_event (tr_new_pad, GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT (stats->index),
          UINT (stats->parent_ix),
          STRING (GST_OBJECT_NAME (pad)),
          STRING (G_OBJECT_TYPE_NAME (pad)),
          BOOLEAN (GST_IS_GHOST_PAD (pad)), UINT (GST_PAD_DIRECTION (pad))));
}

static void
free_pad_stats (gpointer data)
{
  g_free (data);
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

  G_LOCK (_pad_stats);
  if (!(stats = g_object_get_qdata ((GObject *) pad, data_quark))) {
    stats = fill_pad_stats (self, pad);
    g_object_set_qdata_full ((GObject *) pad, data_quark, stats,
        free_pad_stats);
    is_new = TRUE;
  }
  G_UNLOCK (_pad_stats);
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
do_buffer_stats (GstStatsTracer * self, GstPad * this_pad,
    GstPadStats * this_pad_stats, GstPad * that_pad,
    GstPadStats * that_pad_stats, GstBuffer * buf, GstClockTime elapsed)
{
  GstElement *this_elem = get_real_pad_parent (this_pad);
  GstElementStats *this_elem_stats = get_element_stats (self, this_elem);
  GstElement *that_elem = get_real_pad_parent (that_pad);
  GstElementStats *that_elem_stats = get_element_stats (self, that_elem);
  GstClockTime pts = GST_BUFFER_PTS (buf);
  GstClockTime dts = GST_BUFFER_DTS (buf);
  GstClockTime dur = GST_BUFFER_DURATION (buf);

  gst_trace_event (tr_buffer, GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT64 (elapsed),
          UINT (this_pad_stats->index),
          UINT (this_elem_stats->index),
          UINT (that_pad_stats->index),
          UINT (that_elem_stats->index),
          UINT (gst_buffer_get_size (buf)),
          BOOLEAN (GST_CLOCK_TIME_IS_VALID (pts)),
          UINT64 (pts),
          BOOLEAN (GST_CLOCK_TIME_IS_VALID (dts)),
          UINT64 (dts),
          BOOLEAN (GST_CLOCK_TIME_IS_VALID (dur)),
          UINT64 (dur), UINT (GST_BUFFER_FLAGS (buf))));
}

static void
do_query_stats (GstStatsTracer * self, GstPad * this_pad,
    GstPadStats * this_pad_stats, GstPad * that_pad,
    GstPadStats * that_pad_stats, GstQuery * qry, GstClockTime elapsed,
    gboolean have_res, gboolean res)
{
  GstElement *this_elem = get_real_pad_parent (this_pad);
  GstElementStats *this_elem_stats = get_element_stats (self, this_elem);
  GstElement *that_elem = get_real_pad_parent (that_pad);
  GstElementStats *that_elem_stats = get_element_stats (self, that_elem);

  gst_trace_event (tr_query, GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT64 (elapsed),
          UINT (this_pad_stats->index),
          UINT (this_elem_stats->index),
          UINT (that_pad_stats->index),
          UINT (that_elem_stats->index),
          STRING (GST_QUERY_TYPE_NAME (qry)),
          STRUCTURE ((gpointer) gst_query_get_structure (qry)),
          BOOLEAN (have_res), BOOLEAN (res)));
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

/* hooks */

static void
do_push_buffer_pre (GstStatsTracer * self, guint64 ts, GstPad * this_pad,
    GstBuffer * buffer)
{
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);

  do_buffer_stats (self, this_pad, this_pad_stats, that_pad, that_pad_stats,
      buffer, ts);
}

static void
do_push_buffer_post (GstStatsTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
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
do_push_buffer_list_pre (GstStatsTracer * self, guint64 ts, GstPad * this_pad,
    GstBufferList * list)
{
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);
  DoPushBufferListArgs args = { self, this_pad, this_pad_stats, that_pad,
    that_pad_stats, ts
  };

  gst_buffer_list_foreach (list, do_push_buffer_list_item, &args);
}

static void
do_push_buffer_list_post (GstStatsTracer * self, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  GstPadStats *stats = get_pad_stats (self, pad);

  do_element_stats (self, pad, stats->last_ts, ts);
}

static void
do_pull_range_pre (GstStatsTracer * self, guint64 ts, GstPad * pad)
{
  GstPadStats *stats = get_pad_stats (self, pad);
  stats->last_ts = ts;
}

static void
do_pull_range_post (GstStatsTracer * self, guint64 ts, GstPad * this_pad,
    GstBuffer * buffer)
{
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
do_push_event_pre (GstStatsTracer * self, guint64 ts, GstPad * pad,
    GstEvent * ev)
{
  GstElement *elem = get_real_pad_parent (pad);
  GstElementStats *elem_stats = get_element_stats (self, elem);
  GstPadStats *pad_stats = get_pad_stats (self, pad);

  elem_stats->last_ts = ts;
  gst_trace_event (tr_event, GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT64 (ts),
          UINT (pad_stats->index),
          UINT (elem_stats->index), STRING (GST_EVENT_TYPE_NAME (ev))));
}

static void
do_post_message_pre (GstStatsTracer * self, guint64 ts, GstElement * elem,
    GstMessage * msg)
{
  GstElementStats *stats = get_element_stats (self, elem);
  const GstStructure *msg_s = gst_message_get_structure (msg);
  GstStructure *s =
      msg_s ? (GstStructure *) msg_s : gst_structure_new_empty ("dummy");

  stats->last_ts = ts;
  /* FIXME: work out whether using NULL instead of a dummy struct would work */
  gst_trace_event (tr_message, GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT64 (ts),
          UINT (stats->index),
          STRING (GST_MESSAGE_TYPE_NAME (msg)), STRUCTURE (s)));
  if (s != msg_s)
    gst_structure_free (s);
}

static void
do_element_new (GstStatsTracer * self, guint64 ts, GstElement * elem)
{
  GstElementStats *stats;

  stats = create_element_stats (self, elem);
  log_new_element_stats (stats, elem, ts);
}

static void
do_element_query_pre (GstStatsTracer * self, guint64 ts, GstElement * elem,
    GstQuery * qry)
{
  GstElementStats *stats = get_element_stats (self, elem);

  stats->last_ts = ts;
  gst_trace_event (tr_element_query,
      GST_TRACE_VALUES (UINT64 ((guint64) (guintptr)
              g_thread_self ()), UINT64 (ts),
          UINT (stats->index), STRING (GST_QUERY_TYPE_NAME (qry))));
}

static void
do_query_pre (GstStatsTracer * self, guint64 ts, GstPad * this_pad,
    GstQuery * qry)
{
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);

  do_query_stats (self, this_pad, this_pad_stats, that_pad, that_pad_stats,
      qry, ts, FALSE, FALSE);
}

static void
do_query_post (GstStatsTracer * self, guint64 ts, GstPad * this_pad,
    GstQuery * qry, gboolean res)
{
  GstPadStats *this_pad_stats = get_pad_stats (self, this_pad);
  GstPad *that_pad = GST_PAD_PEER (this_pad);
  GstPadStats *that_pad_stats = get_pad_stats (self, that_pad);

  do_query_stats (self, this_pad, this_pad_stats, that_pad, that_pad_stats,
      qry, ts, TRUE, res);
}

/* tracer class */

static void
gst_stats_tracer_constructed (GObject * object)
{
  GstStatsTracer *self = GST_STATS_TRACER (object);
  gchar *params, *tmp;
  const gchar *name;
  GstStructure *params_struct = NULL;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  g_object_get (self, "params", &params, NULL);

  if (!params)
    return;

  tmp = g_strdup_printf ("stats,%s", params);
  params_struct = gst_structure_from_string (tmp, NULL);
  g_free (tmp);
  if (!params_struct)
    return;

  /* Set the name if assigned */
  name = gst_structure_get_string (params_struct, "name");
  if (name)
    gst_object_set_name (GST_OBJECT (self), name);
  gst_structure_free (params_struct);
}

static void
add_scoped (GstTraceFormatBuilder * builder, const gchar * name,
    GstTracerFieldType type, GstTracerValueScope scope)
{
  gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_set_scope (gst_trace_field_new (name, type), scope));
}

static void
add_described (GstTraceFormatBuilder * builder, const gchar * name,
    GstTracerFieldType type, const gchar * description)
{
  gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_set_description (gst_trace_field_new (name, type),
          description));
}

static void
add_optional (GstTraceFormatBuilder * builder, const gchar * name,
    GstTracerFieldType type, const gchar * description)
{
  gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_set_flags (gst_trace_field_set_description
          (gst_trace_field_new (name, type), description),
          GST_TRACER_VALUE_FLAGS_OPTIONAL));
}

static void
gst_stats_tracer_class_init (GstStatsTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstTraceFormatBuilder *builder;

  gobject_class->constructed = gst_stats_tracer_constructed;

  /* announce trace formats */
  builder = gst_trace_format_builder_new ("buffer");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_described (builder, "ts", GST_TRACER_FIELD_TYPE_UINT64, "event ts");
  add_scoped (builder, "pad-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_PAD);
  add_scoped (builder, "element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_scoped (builder, "peer-pad-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_PAD);
  add_scoped (builder, "peer-element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "buffer-size", GST_TRACER_FIELD_TYPE_UINT,
      "size of buffer in bytes");
  add_optional (builder, "buffer-pts", GST_TRACER_FIELD_TYPE_UINT64,
      "presentation timestamp of the buffer in ns");
  add_optional (builder, "buffer-dts", GST_TRACER_FIELD_TYPE_UINT64,
      "decoding timestamp of the buffer in ns");
  add_optional (builder, "buffer-duration", GST_TRACER_FIELD_TYPE_UINT64,
      "duration of the buffer in ns");
  add_described (builder, "buffer-flags", GST_TRACER_FIELD_TYPE_UINT,
      "flags of the buffer");
  tr_buffer = gst_trace_format_builder_register (builder);

  builder = gst_trace_format_builder_new ("event");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_described (builder, "ts", GST_TRACER_FIELD_TYPE_UINT64, "event ts");
  add_scoped (builder, "pad-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_PAD);
  add_scoped (builder, "element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "name", GST_TRACER_FIELD_TYPE_STRING,
      "name of the event");
  tr_event = gst_trace_format_builder_register (builder);

  builder = gst_trace_format_builder_new ("message");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_described (builder, "ts", GST_TRACER_FIELD_TYPE_UINT64, "event ts");
  add_scoped (builder, "element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "name", GST_TRACER_FIELD_TYPE_STRING,
      "name of the message");
  add_described (builder, "structure", GST_TRACER_FIELD_TYPE_STRUCTURE,
      "message structure");
  tr_message = gst_trace_format_builder_register (builder);

  builder = gst_trace_format_builder_new ("element-query");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_described (builder, "ts", GST_TRACER_FIELD_TYPE_UINT64, "event ts");
  add_scoped (builder, "element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "name", GST_TRACER_FIELD_TYPE_STRING,
      "name of the query");
  tr_element_query = gst_trace_format_builder_register (builder);

  builder = gst_trace_format_builder_new ("query");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_described (builder, "ts", GST_TRACER_FIELD_TYPE_UINT64, "event ts");
  add_scoped (builder, "pad-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_PAD);
  add_scoped (builder, "element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_scoped (builder, "peer-pad-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_PAD);
  add_scoped (builder, "peer-element-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "name", GST_TRACER_FIELD_TYPE_STRING,
      "name of the query");
  add_described (builder, "structure", GST_TRACER_FIELD_TYPE_STRUCTURE,
      "query structure");
  add_optional (builder, "res", GST_TRACER_FIELD_TYPE_BOOLEAN, "query result");
  tr_query = gst_trace_format_builder_register (builder);

  builder = gst_trace_format_builder_new ("new-element");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_described (builder, "ts", GST_TRACER_FIELD_TYPE_UINT64, "event ts");
  add_scoped (builder, "ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_scoped (builder, "parent-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "name", GST_TRACER_FIELD_TYPE_STRING,
      "name of the element");
  add_described (builder, "type", GST_TRACER_FIELD_TYPE_STRING,
      "type name of the element");
  add_described (builder, "is-bin", GST_TRACER_FIELD_TYPE_BOOLEAN,
      "is element a bin");
  tr_new_element = gst_trace_format_builder_register (builder);

  builder = gst_trace_format_builder_new ("new-pad");
  add_scoped (builder, "thread-id", GST_TRACER_FIELD_TYPE_UINT64,
      GST_TRACER_VALUE_SCOPE_THREAD);
  add_scoped (builder, "ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_PAD);
  add_scoped (builder, "parent-ix", GST_TRACER_FIELD_TYPE_UINT,
      GST_TRACER_VALUE_SCOPE_ELEMENT);
  add_described (builder, "name", GST_TRACER_FIELD_TYPE_STRING,
      "name of the pad");
  add_described (builder, "type", GST_TRACER_FIELD_TYPE_STRING,
      "type name of the pad");
  add_described (builder, "is-ghostpad", GST_TRACER_FIELD_TYPE_BOOLEAN,
      "is pad a ghostpad");
  add_described (builder, "pad-direction", GST_TRACER_FIELD_TYPE_UINT,
      "ipad direction");
  tr_new_pad = gst_trace_format_builder_register (builder);
}

static void
gst_stats_tracer_init (GstStatsTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-push-post",
      G_CALLBACK (do_push_buffer_post));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (do_push_buffer_list_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-post",
      G_CALLBACK (do_push_buffer_list_post));
  gst_tracing_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (do_pull_range_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (do_pull_range_post));
  gst_tracing_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));
  gst_tracing_register_hook (tracer, "element-new",
      G_CALLBACK (do_element_new));
  gst_tracing_register_hook (tracer, "element-post-message-pre",
      G_CALLBACK (do_post_message_pre));
  gst_tracing_register_hook (tracer, "element-query-pre",
      G_CALLBACK (do_element_query_pre));
  gst_tracing_register_hook (tracer, "pad-query-pre",
      G_CALLBACK (do_query_pre));
  gst_tracing_register_hook (tracer, "pad-query-post",
      G_CALLBACK (do_query_post));
}
