/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstlatency.c: tracing module that logs processing latency stats
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
 * SECTION:tracer-latency
 * @short_description: log processing latency stats
 *
 * A tracing module that determines src-to-sink latencies by injecting custom
 * events at sources and process them at sinks. This elements supports tracing
 * the entire pipeline latency and per element latency. By default, only
 * pipeline latency is traced. The 'flags' parameter can be used to enabled
 * element tracing and/or the latency reported by each element.
 *
 * ```
 * GST_TRACERS="latency(flags=pipeline+element+reported)" GST_DEBUG=GST_TRACER:7 ./...
 * ```
 */
/* TODO(ensonic): if there are two sources feeding into a mixer/muxer and later
 * we fan-out with tee and have two sinks, each sink would get all two events,
 * the later event would overwrite the former. Unfortunately when the buffer
 * arrives on the sink we don't know to which event it correlates. Better would
 * be to use the buffer meta in 1.0 instead of the event. Or we track a min/max
 * latency.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstlatency.h"

GST_DEBUG_CATEGORY_STATIC (gst_latency_debug);
#define GST_CAT_DEFAULT gst_latency_debug

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_latency_debug, "latency", 0, "latency tracer");
#define gst_latency_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstLatencyTracer, gst_latency_tracer, GST_TYPE_TRACER,
    _do_init);

static void latency_query_stack_destroy (gpointer data);

static GQuark latency_probe_id;
static GQuark sub_latency_probe_id;
static GQuark latency_probe_pad;
static GQuark latency_probe_element;
static GQuark latency_probe_element_id;
static GQuark latency_probe_ts;
static GQuark drop_sub_latency_quark;

static GstTracerRecord *tr_latency;
static GstTracerRecord *tr_element_latency;
static GstTracerRecord *tr_element_reported_latency;

/* The private stack for each thread */
static GPrivate latency_query_stack =
G_PRIVATE_INIT (latency_query_stack_destroy);

struct LatencyQueryTableValue
{
  GstElement *peer_element;
  guint64 min;
  guint64 max;
};

/* data helpers */

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

  parent = gst_object_get_parent (GST_OBJECT_CAST (pad));

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD (parent)) {
    GstObject *tmp;
    pad = GST_PAD_CAST (parent);
    tmp = gst_object_get_parent (GST_OBJECT_CAST (pad));
    gst_object_unref (parent);
    parent = tmp;
  }
  return GST_ELEMENT_CAST (parent);
}

static void
latency_query_table_value_destroy (gpointer data)
{
  struct LatencyQueryTableValue *v = data;

  /* Unref the element */
  if (v->peer_element) {
    gst_object_unref (v->peer_element);
    v->peer_element = NULL;
  }

  /* Destroy the structure */
  g_free (v);
}

static void
latency_query_stack_destroy (gpointer data)
{
  GQueue *queue = data;
  g_queue_free_full (queue, latency_query_table_value_destroy);
}

static GQueue *
local_latency_query_stack_get (void)
{
  /* Make sure the stack exists */
  GQueue *stack = g_private_get (&latency_query_stack);
  if (!stack) {
    g_private_set (&latency_query_stack, g_queue_new ());
    stack = g_private_get (&latency_query_stack);
  }

  g_assert (stack);
  return stack;
}

static struct LatencyQueryTableValue *
local_latency_query_stack_pop (void)
{
  GQueue *stack = local_latency_query_stack_get ();
  return g_queue_pop_tail (stack);
}

static void
local_latency_query_stack_push (struct LatencyQueryTableValue *value)
{
  GQueue *stack = local_latency_query_stack_get ();
  g_queue_push_tail (stack, value);
}

/* hooks */

static void
log_latency (const GstStructure * data, GstElement * sink_parent,
    GstPad * sink_pad, guint64 sink_ts)
{
  guint64 src_ts;
  const char *src, *element_src, *id_element_src;
  const GValue *value;
  gchar *sink, *element_sink, *id_element_sink;

  g_return_if_fail (sink_parent);
  g_return_if_fail (sink_pad);

  value = gst_structure_id_get_value (data, latency_probe_ts);
  src_ts = g_value_get_uint64 (value);

  value = gst_structure_id_get_value (data, latency_probe_pad);
  src = g_value_get_string (value);

  value = gst_structure_id_get_value (data, latency_probe_element);
  element_src = g_value_get_string (value);

  value = gst_structure_id_get_value (data, latency_probe_element_id);
  id_element_src = g_value_get_string (value);

  id_element_sink = g_strdup_printf ("%p", sink_parent);
  element_sink = gst_element_get_name (sink_parent);
  sink = gst_pad_get_name (sink_pad);
  gst_tracer_record_log (tr_latency, id_element_src, element_src, src,
      id_element_sink, element_sink, sink, GST_CLOCK_DIFF (src_ts, sink_ts),
      sink_ts);
  g_free (sink);
  g_free (element_sink);
  g_free (id_element_sink);
}

static void
log_element_latency (const GstStructure * data, GstElement * parent,
    GstPad * pad, guint64 sink_ts)
{
  guint64 src_ts;
  gchar *pad_name, *element_name, *element_id;
  const GValue *value;

  g_return_if_fail (parent);
  g_return_if_fail (pad);

  element_id = g_strdup_printf ("%p", parent);
  element_name = gst_element_get_name (parent);
  pad_name = gst_pad_get_name (pad);

  /* TODO filtering */

  value = gst_structure_id_get_value (data, latency_probe_ts);
  src_ts = g_value_get_uint64 (value);

  gst_tracer_record_log (tr_element_latency, element_id, element_name, pad_name,
      GST_CLOCK_DIFF (src_ts, sink_ts), sink_ts);

  g_free (pad_name);
  g_free (element_name);
  g_free (element_id);
}

static void
send_latency_probe (GstLatencyTracer * self, GstElement * parent, GstPad * pad,
    guint64 ts)
{
  GstPad *peer_pad = gst_pad_get_peer (pad);
  GstElement *peer_parent = get_real_pad_parent (peer_pad);

  /* allow for non-parented pads to send latency probes as used in e.g.
   * rtspsrc for TCP connections */
  if (peer_pad && (!parent || (!GST_IS_BIN (parent)))) {
    gchar *pad_name, *element_name, *element_id;
    GstEvent *latency_probe;

    if (parent && self->flags & GST_LATENCY_TRACER_FLAG_PIPELINE &&
        GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE)) {
      element_id = g_strdup_printf ("%p", parent);
      element_name = gst_element_get_name (parent);
      pad_name = gst_pad_get_name (pad);

      latency_probe = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
          gst_structure_new_id (latency_probe_id,
              latency_probe_element_id, G_TYPE_STRING, element_id,
              latency_probe_element, G_TYPE_STRING, element_name,
              latency_probe_pad, G_TYPE_STRING, pad_name,
              latency_probe_ts, G_TYPE_UINT64, ts, NULL));

      GST_DEBUG ("%s_%s: Sending latency event %p", GST_DEBUG_PAD_NAME (pad),
          latency_probe);

      g_free (pad_name);
      g_free (element_name);
      g_free (element_id);
      gst_pad_push_event (pad, latency_probe);
    }

    if (peer_parent && peer_pad &&
        self->flags & GST_LATENCY_TRACER_FLAG_ELEMENT) {
      element_id = g_strdup_printf ("%p", peer_parent);
      element_name = gst_element_get_name (peer_parent);
      pad_name = gst_pad_get_name (peer_pad);

      latency_probe = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
          gst_structure_new_id (sub_latency_probe_id,
              latency_probe_element_id, G_TYPE_STRING, element_id,
              latency_probe_element, G_TYPE_STRING, element_name,
              latency_probe_pad, G_TYPE_STRING, pad_name,
              latency_probe_ts, G_TYPE_UINT64, ts, NULL));

      GST_DEBUG ("%s_%s: Sending sub-latency event %p",
          GST_DEBUG_PAD_NAME (pad), latency_probe);

      gst_pad_push_event (pad, latency_probe);
      g_free (pad_name);
      g_free (element_name);
      g_free (element_id);
    }
  }
  if (peer_pad)
    gst_object_unref (peer_pad);
  if (peer_parent)
    gst_object_unref (peer_parent);
}

static void
calculate_latency (GstElement * parent, GstPad * pad, guint64 ts)
{
  if (parent && (!GST_IS_BIN (parent)) &&
      (!GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE))) {
    GstEvent *ev;
    GstPad *peer_pad = gst_pad_get_peer (pad);
    GstElement *peer_parent = get_real_pad_parent (peer_pad);

    /* Protect against element being unlinked */
    if (peer_pad && peer_parent &&
        GST_OBJECT_FLAG_IS_SET (peer_parent, GST_ELEMENT_FLAG_SINK)) {
      ev = g_object_get_qdata ((GObject *) pad, latency_probe_id);
      GST_DEBUG ("%s_%s: Should log full latency now (event %p)",
          GST_DEBUG_PAD_NAME (pad), ev);
      if (ev) {
        log_latency (gst_event_get_structure (ev), peer_parent, peer_pad, ts);
        g_object_set_qdata ((GObject *) pad, latency_probe_id, NULL);
      }
    }

    ev = g_object_get_qdata ((GObject *) pad, sub_latency_probe_id);
    GST_DEBUG ("%s_%s: Should log sub latency now (event %p)",
        GST_DEBUG_PAD_NAME (pad), ev);
    if (ev) {
      log_element_latency (gst_event_get_structure (ev), parent, pad, ts);
      g_object_set_qdata ((GObject *) pad, sub_latency_probe_id, NULL);
    }
    if (peer_pad)
      gst_object_unref (peer_pad);
    if (peer_parent)
      gst_object_unref (peer_parent);
  }
}

static void
do_push_buffer_pre (GstTracer * tracer, guint64 ts, GstPad * pad)
{
  GstLatencyTracer *self = (GstLatencyTracer *) tracer;
  GstElement *parent = get_real_pad_parent (pad);

  send_latency_probe (self, parent, pad, ts);
  calculate_latency (parent, pad, ts);

  if (parent)
    gst_object_unref (parent);
}

static void
do_pull_range_pre (GstTracer * tracer, guint64 ts, GstPad * pad)
{
  GstLatencyTracer *self = (GstLatencyTracer *) tracer;
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (peer_pad);

  send_latency_probe (self, parent, peer_pad, ts);

  if (parent)
    gst_object_unref (parent);
}

static void
do_pull_range_post (GstTracer * self, guint64 ts, GstPad * pad)
{
  GstElement *parent = get_real_pad_parent (pad);

  calculate_latency (parent, pad, ts);

  if (parent)
    gst_object_unref (parent);
}

static GstPadProbeReturn
do_drop_sub_latency_event (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstEvent *ev = info->data;
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (GST_EVENT_TYPE (ev) == GST_EVENT_CUSTOM_DOWNSTREAM) {
    const GstStructure *data = gst_event_get_structure (ev);

    if (gst_structure_get_name_id (data) == sub_latency_probe_id) {
      GstPad *peer_pad = gst_pad_get_peer (pad);
      GstElement *peer_parent = get_real_pad_parent (peer_pad);
      const GValue *value;
      gchar *element_id = g_strdup_printf ("%p", peer_parent);
      gchar *pad_name = peer_pad ? gst_pad_get_name (peer_pad) : NULL;
      const gchar *value_element_id, *value_pad_name;

      /* Get the element id, element name and pad name from data */
      value = gst_structure_id_get_value (data, latency_probe_element_id);
      value_element_id = g_value_get_string (value);
      value = gst_structure_id_get_value (data, latency_probe_pad);
      value_pad_name = g_value_get_string (value);

      if (pad_name == NULL ||
          !g_str_equal (value_element_id, element_id) ||
          !g_str_equal (value_pad_name, pad_name)) {
        GST_DEBUG ("%s_%s: Dropping sub-latency event",
            GST_DEBUG_PAD_NAME (pad));
        ret = GST_PAD_PROBE_DROP;
      }

      g_free (pad_name);
      g_free (element_id);

      if (peer_pad)
        gst_object_unref (peer_pad);
      if (peer_parent)
        gst_object_unref (peer_parent);
    }
  }

  return ret;
}

static void
do_push_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * ev)
{
  GstElement *parent = get_real_pad_parent (pad);

  if (parent && (!GST_IS_BIN (parent)) &&
      (!GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE)) &&
      GST_EVENT_TYPE (ev) == GST_EVENT_CUSTOM_DOWNSTREAM) {
    const GstStructure *data = gst_event_get_structure (ev);
    GstPad *peer_pad = gst_pad_get_peer (pad);
    GstElement *peer_parent = get_real_pad_parent (peer_pad);

    /* if not set yet, add a pad probe that prevents sub-latency event from
     * flowing further */
    if (gst_structure_get_name_id (data) == latency_probe_id) {

      if (!g_object_get_qdata ((GObject *) pad, drop_sub_latency_quark)) {
        GST_DEBUG ("%s_%s: Adding pad probe to drop sub-latency event",
            GST_DEBUG_PAD_NAME (pad));
        gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
            do_drop_sub_latency_event, NULL, NULL);
        g_object_set_qdata ((GObject *) pad, drop_sub_latency_quark,
            (gpointer) 1);
      }

      if (peer_parent == NULL
          || GST_OBJECT_FLAG_IS_SET (peer_parent, GST_ELEMENT_FLAG_SINK)) {
        /* store event so that we can calculate latency when the buffer that
         * follows has been processed */
        g_object_set_qdata_full ((GObject *) pad, latency_probe_id,
            gst_event_ref (ev), (GDestroyNotify) gst_event_unref);
      }
    }

    if (gst_structure_get_name_id (data) == sub_latency_probe_id) {
      const GValue *value;
      gchar *element_id = g_strdup_printf ("%p", peer_parent);
      gchar *pad_name = peer_pad ? gst_pad_get_name (peer_pad) : NULL;
      const gchar *value_element_id, *value_pad_name;

      /* Get the element id, element name and pad name from data */
      value = gst_structure_id_get_value (data, latency_probe_element_id);
      value_element_id = g_value_get_string (value);
      value = gst_structure_id_get_value (data, latency_probe_pad);
      value_pad_name = g_value_get_string (value);

      if (!g_str_equal (value_element_id, element_id) ||
          g_strcmp0 (value_pad_name, pad_name) != 0) {
        GST_DEBUG ("%s_%s: Storing sub-latency event",
            GST_DEBUG_PAD_NAME (pad));
        g_object_set_qdata_full ((GObject *) pad, sub_latency_probe_id,
            gst_event_ref (ev), (GDestroyNotify) gst_event_unref);
      }

      g_free (pad_name);
      g_free (element_id);
    }
    if (peer_pad)
      gst_object_unref (peer_pad);
    if (peer_parent)
      gst_object_unref (peer_parent);
  }
  if (parent)
    gst_object_unref (parent);
}

static void
do_query_post (GstLatencyTracer * tracer, GstClockTime ts, GstPad * pad,
    GstQuery * query, gboolean res)
{
  /* Only check for latency queries if flag is enabled */
  if ((tracer->flags & GST_LATENCY_TRACER_FLAG_REPORTED_ELEMENT) &&
      (GST_QUERY_TYPE (query) == GST_QUERY_LATENCY)) {
    gboolean live;
    guint64 min = 0, max = 0, min_prev = 0, max_prev = 0;
    gchar *element_name, *element_id;
    struct LatencyQueryTableValue *value;
    GstElement *element = get_real_pad_parent (pad);
    GstPad *peer_pad = gst_pad_get_peer (pad);
    GstElement *peer_element = get_real_pad_parent (peer_pad);

    /* If something is being removed/unlinked, cleanup the stack so we can
     * ignore this query in the trace. */
    if (!element || !peer_element || !peer_pad) {
      while ((value = local_latency_query_stack_pop ()))
        latency_query_table_value_destroy (value);
      return;
    }

    /* Parse the query */
    gst_query_parse_latency (query, &live, &min, &max);

    /* Pop from stack */
    value = local_latency_query_stack_pop ();
    while (value && value->peer_element == element) {
      min_prev = MAX (value->min, min_prev);
      max_prev = MAX (value->max, max_prev);
      latency_query_table_value_destroy (value);
      value = local_latency_query_stack_pop ();
    }
    if (value)
      latency_query_table_value_destroy (value);

    /* Push to stack */
    value = g_new0 (struct LatencyQueryTableValue, 1);
    value->peer_element = gst_object_ref (peer_element);
    value->min = min;
    value->max = max;
    local_latency_query_stack_push (value);

    /* Get the pad name */
    element_id = g_strdup_printf ("%p", element);
    element_name = gst_element_get_name (element);

    /* Log reported latency */
    gst_tracer_record_log (tr_element_reported_latency, element_id,
        element_name, live, GST_CLOCK_DIFF (min_prev, min),
        GST_CLOCK_DIFF (max_prev, max), ts);

    /* Clean up */
    g_free (element_name);
    g_free (element_id);

    gst_object_unref (peer_pad);
    gst_object_unref (peer_element);
    gst_object_unref (element);
  }
}

/* tracer class */

static void
gst_latency_tracer_constructed (GObject * object)
{
  GstLatencyTracer *self = GST_LATENCY_TRACER (object);
  gchar *params, *tmp;
  GstStructure *params_struct = NULL;

  g_object_get (self, "params", &params, NULL);

  if (!params)
    return;

  tmp = g_strdup_printf ("latency,%s", params);
  params_struct = gst_structure_from_string (tmp, NULL);
  g_free (tmp);

  if (params_struct) {
    const gchar *name, *flags;
    /* Set the name if assigned */
    name = gst_structure_get_string (params_struct, "name");
    if (name)
      gst_object_set_name (GST_OBJECT (self), name);

    /* Read the flags if available */
    flags = gst_structure_get_string (params_struct, "flags");

    self->flags = 0;

    if (flags) {
      GStrv split = g_strsplit (flags, "+", -1);
      gint i;

      for (i = 0; split[i]; i++) {
        if (g_str_equal (split[i], "pipeline"))
          self->flags |= GST_LATENCY_TRACER_FLAG_PIPELINE;
        else if (g_str_equal (split[i], "element"))
          self->flags |= GST_LATENCY_TRACER_FLAG_ELEMENT;
        else if (g_str_equal (split[i], "reported"))
          self->flags |= GST_LATENCY_TRACER_FLAG_REPORTED_ELEMENT;
        else
          GST_WARNING ("Invalid latency tracer flags %s", split[i]);
      }

      g_strfreev (split);
    }
    gst_structure_free (params_struct);
  }

  g_free (params);
}

static void
gst_latency_tracer_class_init (GstLatencyTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_latency_tracer_constructed;

  latency_probe_id = g_quark_from_static_string ("latency_probe.id");
  sub_latency_probe_id = g_quark_from_static_string ("sub_latency_probe.id");
  latency_probe_pad = g_quark_from_static_string ("latency_probe.pad");
  latency_probe_element = g_quark_from_static_string ("latency_probe.element");
  latency_probe_element_id =
      g_quark_from_static_string ("latency_probe.element_id");
  latency_probe_ts = g_quark_from_static_string ("latency_probe.ts");
  drop_sub_latency_quark =
      g_quark_from_static_string ("drop_sub_latency.quark");

  /* announce trace formats */
  /* *INDENT-OFF* */
  tr_latency = gst_tracer_record_new ("latency.class",
      "src-element-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "src-element", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "src", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PAD,
          NULL),
      "sink-element-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "sink-element", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "sink", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PAD,
          NULL),
      "time", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING,
              "time it took for the buffer to go from src to sink ns",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "ts when the latency has been logged",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL);

  tr_element_latency = gst_tracer_record_new ("element-latency.class",
      "element-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "element", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "src", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PAD,
          NULL),
      "time", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING,
              "time it took for the buffer to go from src to sink ns",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "ts when the latency has been logged",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL);


  tr_element_reported_latency = gst_tracer_record_new (
      "element-reported-latency.class",
      "element-id", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "element", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE,
          GST_TRACER_VALUE_SCOPE_ELEMENT,
          NULL),
      "live", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_BOOLEAN,
          "description", G_TYPE_STRING,
              "wether the it is a live stream or not",
          NULL),
      "min", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING,
              "the minimum reported latency",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "max", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "the maximum reported latency",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      "ts", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_UINT64,
          "description", G_TYPE_STRING, "ts when the latency has been reported",
          "min", G_TYPE_UINT64, G_GUINT64_CONSTANT (0),
          "max", G_TYPE_UINT64, G_MAXUINT64,
          NULL),
      NULL);
  /* *INDENT-ON* */

  GST_OBJECT_FLAG_SET (tr_latency, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_OBJECT_FLAG_SET (tr_element_latency, GST_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_OBJECT_FLAG_SET (tr_element_reported_latency,
      GST_OBJECT_FLAG_MAY_BE_LEAKED);
}

static void
gst_latency_tracer_init (GstLatencyTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  /* only trace pipeline latency by default */
  self->flags = GST_LATENCY_TRACER_FLAG_PIPELINE;

  /* in push mode, pre/post will be called before/after the peer chain
   * function has been called. For this reaosn, we only use -pre to avoid
   * accounting for the processing time of the peer element (the sink) */
  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (do_push_buffer_pre));

  /* while in pull mode, pre/post will happen before and after the upstream
   * pull_range call is made, so it already only account for the upstream
   * processing time. As a side effect, in pull mode, we can measure the
   * source processing latency, while in push mode, we can't */
  gst_tracing_register_hook (tracer, "pad-pull-range-pre",
      G_CALLBACK (do_pull_range_pre));
  gst_tracing_register_hook (tracer, "pad-pull-range-post",
      G_CALLBACK (do_pull_range_post));

  gst_tracing_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));

  /* Add pad query post hook to get the reported per-element latency */
  gst_tracing_register_hook (tracer, "pad-query-post",
      G_CALLBACK (do_query_post));
}
