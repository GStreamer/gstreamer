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
 * events at sources and process them at sinks.
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
static GQuark latency_probe_pad;
static GQuark latency_probe_ts;

static GstTracerRecord *tr_latency;

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
  GstPad *src_pad;
  guint64 src_ts;
  gchar *src, *sink;

  gst_structure_id_get (data,
      latency_probe_pad, GST_TYPE_PAD, &src_pad,
      latency_probe_ts, G_TYPE_UINT64, &src_ts, NULL);

  src = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (src_pad));
  sink = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (sink_pad));

  gst_tracer_record_log (tr_latency, src, sink,
      GST_CLOCK_DIFF (src_ts, sink_ts), sink_ts);
  g_free (src);
  g_free (sink);
}

static void
send_latency_probe (GstElement * parent, GstPad * pad, guint64 ts)
{
  /* allow for non-parented pads to send latency probes as used in e.g.
   * rtspsrc for TCP connections */
  if (!parent || (!GST_IS_BIN (parent) &&
          GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE))) {
    GstEvent *latency_probe = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
        gst_structure_new_id (latency_probe_id,
            latency_probe_pad, GST_TYPE_PAD, pad,
            latency_probe_ts, G_TYPE_UINT64, ts,
            NULL));
    gst_pad_push_event (pad, latency_probe);
  }
}

static void
calculate_latency (GstElement * parent, GstPad * pad, guint64 ts)
{
  if (parent && (!GST_IS_BIN (parent)) &&
      GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SINK)) {
    GstEvent *ev = g_object_get_qdata ((GObject *) pad, latency_probe_id);

    if (ev) {
      g_object_set_qdata ((GObject *) pad, latency_probe_id, NULL);
      log_latency (gst_event_get_structure (ev), pad, ts);
      gst_event_unref (ev);
    }
  }
}

static void
do_push_buffer_pre (GstTracer * self, guint64 ts, GstPad * pad)
{
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (pad);
  GstElement *peer_parent = get_real_pad_parent (peer_pad);

  send_latency_probe (parent, pad, ts);
  calculate_latency (peer_parent, peer_pad, ts);
}

static void
do_pull_range_pre (GstTracer * self, guint64 ts, GstPad * pad)
{
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (peer_pad);

  send_latency_probe (parent, peer_pad, ts);
}

static void
do_pull_range_post (GstTracer * self, guint64 ts, GstPad * pad)
{
  GstElement *parent = get_real_pad_parent (pad);

  calculate_latency (parent, pad, ts);
}

static void
do_push_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * ev)
{
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (peer_pad);

  if (parent && (!GST_IS_BIN (parent)) &&
      GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SINK)) {
    if (GST_EVENT_TYPE (ev) == GST_EVENT_CUSTOM_DOWNSTREAM) {
      const GstStructure *data = gst_event_get_structure (ev);

      if (gst_structure_get_name_id (data) == latency_probe_id) {
        /* store event and calculate latency when the buffer that follows
         * has been processed */
        g_object_set_qdata ((GObject *) peer_pad, latency_probe_id,
            gst_event_ref (ev));
      }
    }
  }
}

/* tracer class */

static void
gst_latency_tracer_class_init (GstLatencyTracerClass * klass)
{
  latency_probe_id = g_quark_from_static_string ("latency_probe.id");
  latency_probe_pad = g_quark_from_static_string ("latency_probe.pad");
  latency_probe_ts = g_quark_from_static_string ("latency_probe.ts");

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
  /* *INDENT-ON* */
}

static void
gst_latency_tracer_init (GstLatencyTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  /* in push mode, pre/post will be called before/after the peer chain
   * function has been called. For this reaosn, we only use -pre to avoid
   * accounting for the processing time of the peer element (the sink) */
  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));
  gst_tracing_register_hook (tracer, "pad-push-list-pre",
      G_CALLBACK (do_push_buffer_pre));

  /* while in pull mode, pre/post will happend before and after the upstream
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
