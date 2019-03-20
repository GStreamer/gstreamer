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
 * SECTION:element-latencytracer
 * @short_description: log processing latency stats
 *
 * A tracing module that determines src-to-sink latencies by injecting custom
 * events at sources and process them at sinks. This elements supports tracing
 * the entire pipeline latency and per element latency. By default, only
 * pipeline latency is traced. The 'flags' parameter can be used to enabled
 * element tracing.
 *
 * ```
 * GST_TRACERS="latency(flags=pipeline+element)" GST_DEBUG=GST_TRACER:7 ./...
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

static GQuark latency_probe_id;
static GQuark sub_latency_probe_id;
static GQuark latency_probe_pad;
static GQuark latency_probe_ts;
static GQuark drop_sub_latency_quark;

static GstTracerRecord *tr_latency;
static GstTracerRecord *tr_element_latency;

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

  parent = GST_OBJECT_PARENT (pad);

  /* if parent of pad is a ghost-pad, then pad is a proxy_pad */
  if (parent && GST_IS_GHOST_PAD (parent)) {
    pad = GST_PAD_CAST (parent);
    parent = GST_OBJECT_PARENT (pad);
  }
  return GST_ELEMENT_CAST (parent);
}

/* hooks */

static void
log_latency (const GstStructure * data, GstPad * sink_pad, guint64 sink_ts)
{
  guint64 src_ts;
  const char *src;
  const GValue *value;
  gchar *sink;

  value = gst_structure_id_get_value (data, latency_probe_ts);
  src_ts = g_value_get_uint64 (value);

  value = gst_structure_id_get_value (data, latency_probe_pad);
  src = g_value_get_string (value);

  sink = g_strdup_printf ("%s_%s",
      GST_DEBUG_PAD_NAME (GST_PAD_PEER (sink_pad)));
  gst_tracer_record_log (tr_latency, src, sink,
      GST_CLOCK_DIFF (src_ts, sink_ts), sink_ts);
  g_free (sink);
}

static void
log_element_latency (const GstStructure * data, GstPad * pad, guint64 sink_ts)
{
  guint64 src_ts;
  gchar *pad_name;
  const GValue *value;

  pad_name = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (pad));

  /* TODO filtering */

  value = gst_structure_id_get_value (data, latency_probe_ts);
  src_ts = g_value_get_uint64 (value);

  gst_tracer_record_log (tr_element_latency, pad_name,
      GST_CLOCK_DIFF (src_ts, sink_ts), sink_ts);

  g_free (pad_name);
}

static void
send_latency_probe (GstLatencyTracer * self, GstElement * parent, GstPad * pad,
    guint64 ts)
{
  GstPad *peer_pad = GST_PAD_PEER (pad);

  /* allow for non-parented pads to send latency probes as used in e.g.
   * rtspsrc for TCP connections */
  if (peer_pad && (!parent || (!GST_IS_BIN (parent)))) {
    gchar *pad_name;
    GstEvent *latency_probe;

    if (self->flags & GST_LATENCY_TRACER_FLAG_PIPELINE &&
        GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE)) {
      pad_name = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (pad));

      GST_DEBUG ("%s: Sending latency event", pad_name);

      latency_probe = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
          gst_structure_new_id (latency_probe_id,
              latency_probe_pad, G_TYPE_STRING, pad_name,
              latency_probe_ts, G_TYPE_UINT64, ts, NULL));
      g_free (pad_name);
      gst_pad_push_event (pad, latency_probe);
    }

    if (self->flags & GST_LATENCY_TRACER_FLAG_ELEMENT) {
      GST_DEBUG ("%s_%s: Sending sub-latency event", GST_DEBUG_PAD_NAME (pad));

      pad_name = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (peer_pad));
      latency_probe = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
          gst_structure_new_id (sub_latency_probe_id,
              latency_probe_pad, G_TYPE_STRING, pad_name,
              latency_probe_ts, G_TYPE_UINT64, ts, NULL));
      gst_pad_push_event (pad, latency_probe);
      g_free (pad_name);
    }
  }
}

static void
calculate_latency (GstElement * parent, GstPad * pad, guint64 ts)
{
  GstElement *peer_parent = get_real_pad_parent (GST_PAD_PEER (pad));

  if (parent && (!GST_IS_BIN (parent)) &&
      (!GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE))) {
    GstEvent *ev;

    /* FIXME unsafe use of peer */
    if (GST_OBJECT_FLAG_IS_SET (peer_parent, GST_ELEMENT_FLAG_SINK)) {
      GST_DEBUG ("%s_%s: Should log full lantency now",
          GST_DEBUG_PAD_NAME (pad));
      ev = g_object_get_qdata ((GObject *) pad, latency_probe_id);
      if (ev) {
        g_object_set_qdata ((GObject *) pad, latency_probe_id, NULL);
        log_latency (gst_event_get_structure (ev), pad, ts);
        gst_event_unref (ev);
      }
    }

    GST_DEBUG ("%s_%s: Should log sub lantency now", GST_DEBUG_PAD_NAME (pad));
    ev = g_object_get_qdata ((GObject *) pad, sub_latency_probe_id);
    if (ev) {
      g_object_set_qdata ((GObject *) pad, sub_latency_probe_id, NULL);
      log_element_latency (gst_event_get_structure (ev), pad, ts);
      gst_event_unref (ev);
    }
  }
}

static void
do_push_buffer_pre (GstTracer * tracer, guint64 ts, GstPad * pad)
{
  GstLatencyTracer *self = (GstLatencyTracer *) tracer;
  GstElement *parent = get_real_pad_parent (pad);

  send_latency_probe (self, parent, pad, ts);
  calculate_latency (parent, pad, ts);
}

static void
do_pull_range_pre (GstTracer * tracer, guint64 ts, GstPad * pad)
{
  GstLatencyTracer *self = (GstLatencyTracer *) tracer;
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (peer_pad);

  send_latency_probe (self, parent, peer_pad, ts);
}

static void
do_pull_range_post (GstTracer * self, guint64 ts, GstPad * pad)
{
  GstElement *parent = get_real_pad_parent (pad);

  calculate_latency (parent, pad, ts);
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
      const GValue *value;
      /* FIXME unsafe peer pad usage */
      gchar *pad_name = g_strdup_printf ("%s_%s",
          GST_DEBUG_PAD_NAME (GST_PAD_PEER (pad)));

      value = gst_structure_id_get_value (data, latency_probe_pad);
      if (!g_str_equal (g_value_get_string (value), pad_name)) {
        GST_DEBUG ("%s: Dropping sub-latency event", pad_name);
        ret = GST_PAD_PROBE_DROP;
      }

      g_free (pad_name);
    }
  }

  return ret;
}

static void
do_push_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * ev)
{
  GstElement *parent = get_real_pad_parent (pad);
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *peer_parent = get_real_pad_parent (peer_pad);

  if (parent && (!GST_IS_BIN (parent)) &&
      (!GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE)) &&
      GST_EVENT_TYPE (ev) == GST_EVENT_CUSTOM_DOWNSTREAM) {
    const GstStructure *data = gst_event_get_structure (ev);

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

      /* FIXME unsafe peer access */
      if (GST_OBJECT_FLAG_IS_SET (peer_parent, GST_ELEMENT_FLAG_SINK)) {
        GST_DEBUG ("%s_%s: Storing latency event", GST_DEBUG_PAD_NAME (pad));

        /* store event so that we can calculate latency when the buffer that
         * follows has been processed */
        if (!g_object_get_qdata ((GObject *) pad, latency_probe_id))
          g_object_set_qdata ((GObject *) pad, latency_probe_id,
              gst_event_ref (ev));
      }
    }

    if (gst_structure_get_name_id (data) == sub_latency_probe_id) {
      const GValue *value;
      gchar *pad_name = g_strdup_printf ("%s_%s",
          GST_DEBUG_PAD_NAME (peer_pad));

      value = gst_structure_id_get_value (data, latency_probe_pad);

      if (!g_str_equal (g_value_get_string (value), pad_name)) {
        GST_DEBUG ("%s: Storing sub-latency event", pad_name);
        if (!g_object_get_qdata ((GObject *) pad, sub_latency_probe_id))
          g_object_set_qdata ((GObject *) pad, sub_latency_probe_id,
              gst_event_ref (ev));
      }

      g_free (pad_name);
    }
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

  /* Read the flags if available */
  if (params_struct) {
    const gchar *flags = gst_structure_get_string (params_struct, "flags");

    self->flags = 0;

    if (flags) {
      GStrv split = g_strsplit (flags, "+", -1);
      gint i;

      for (i = 0; split[i]; i++) {
        if (g_str_equal (split[i], "pipeline"))
          self->flags |= GST_LATENCY_TRACER_FLAG_PIPELINE;
        else if (g_str_equal (split[i], "element"))
          self->flags |= GST_LATENCY_TRACER_FLAG_ELEMENT;
        else
          GST_WARNING ("Invalid latency tracer flags %s", split[i]);
      }

      g_strfreev (split);
    }
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
  latency_probe_ts = g_quark_from_static_string ("latency_probe.ts");
  drop_sub_latency_quark =
      g_quark_from_static_string ("drop_sub_latency.quark");

  /* announce trace formats */
  /* *INDENT-OFF* */
  tr_latency = gst_tracer_record_new ("latency.class",
      "src", GST_TYPE_STRUCTURE, gst_structure_new ("scope",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          "related-to", GST_TYPE_TRACER_VALUE_SCOPE, GST_TRACER_VALUE_SCOPE_PAD,
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
  /* *INDENT-ON* */
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
}
