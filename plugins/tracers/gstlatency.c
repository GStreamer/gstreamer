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

/* logging */

static void
log_trace (GstStructure * s)
{
  gchar *data;

  // TODO(ensonic): use a GVariant?
  data = gst_structure_to_string (s);
  GST_TRACE ("%s", data);
  g_free (data);
  gst_structure_free (s);
}

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
  /* if pad is a ghost-pad, then parent is a bin and it is the parent of a
   * proxy_pad */
  while (parent && GST_IS_GHOST_PAD (pad)) {
    pad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
    parent = pad ? GST_OBJECT_PARENT (pad) : NULL;
  }
  return GST_ELEMENT_CAST (parent);
}

/* tracer class */

static void gst_latency_tracer_invoke (GstTracer * obj, GstTracerHookId id,
    GstTracerMessageId mid, va_list var_args);

static void
gst_latency_tracer_class_init (GstLatencyTracerClass * klass)
{
  GstTracerClass *gst_tracer_class = GST_TRACER_CLASS (klass);

  gst_tracer_class->invoke = gst_latency_tracer_invoke;

  latency_probe_id = g_quark_from_static_string ("latency_probe.id");
  latency_probe_pad = g_quark_from_static_string ("latency_probe.pad");
  latency_probe_ts = g_quark_from_static_string ("latency_probe.ts");
}

static void
gst_latency_tracer_init (GstLatencyTracer * self)
{
  g_object_set (self, "mask", GST_TRACER_HOOK_BUFFERS | GST_TRACER_HOOK_EVENTS,
      NULL);
}

/* hooks */

static void
send_latency_probe (GstLatencyTracer * self, GstElement * parent, GstPad * pad,
    guint64 ts)
{
  if (parent && !GST_IS_BIN (parent) &&
      GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SOURCE)) {
    GstEvent *latency_probe = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
        gst_structure_new_id (latency_probe_id,
            latency_probe_pad, GST_TYPE_PAD, pad,
            latency_probe_ts, G_TYPE_UINT64, ts,
            NULL));
    gst_pad_push_event (pad, latency_probe);
  }
}

static void
do_push_buffer_pre (GstLatencyTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstElement *parent = get_real_pad_parent (pad);

  send_latency_probe (self, parent, pad, ts);
}

static void
do_pull_buffer_pre (GstLatencyTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (peer_pad);

  send_latency_probe (self, parent, peer_pad, ts);
}

static void
do_push_event_pre (GstLatencyTracer * self, va_list var_args)
{
  guint64 ts = va_arg (var_args, guint64);
  GstPad *pad = va_arg (var_args, GstPad *);
  GstEvent *ev = va_arg (var_args, GstEvent *);
  GstPad *peer_pad = GST_PAD_PEER (pad);
  GstElement *parent = get_real_pad_parent (peer_pad);

  if (parent && !GST_IS_BIN (parent) &&
      GST_OBJECT_FLAG_IS_SET (parent, GST_ELEMENT_FLAG_SINK)) {
    if (GST_EVENT_TYPE (ev) == GST_EVENT_CUSTOM_DOWNSTREAM) {
      const GstStructure *data = gst_event_get_structure (ev);

      if (gst_structure_get_name_id (data) == latency_probe_id) {
        GstPad *origin_pad;
        guint64 origin_ts;
        gchar *from, *to;

        /* TODO(ensonic): we'd like to do this when actually rendering */
        gst_structure_id_get (data,
            latency_probe_pad, GST_TYPE_PAD, &origin_pad,
            latency_probe_ts, G_TYPE_UINT64, &origin_ts, NULL);

        from = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (origin_pad));
        to = g_strdup_printf ("%s_%s", GST_DEBUG_PAD_NAME (peer_pad));

        /* TODO(ensonic): report format is still unstable */
        log_trace (gst_structure_new ("latency",
                "from", G_TYPE_STRING, from,
                "to", G_TYPE_STRING, to,
                "time", G_TYPE_UINT64, GST_CLOCK_DIFF (origin_ts, ts), NULL));
        g_free (from);
        g_free (to);
      }
    }
  }
}

static void
gst_latency_tracer_invoke (GstTracer * obj, GstTracerHookId hid,
    GstTracerMessageId mid, va_list var_args)
{
  GstLatencyTracer *self = GST_LATENCY_TRACER_CAST (obj);

  switch (mid) {
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_PRE:
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_PRE:
      do_push_buffer_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_PRE:
      do_pull_buffer_pre (self, var_args);
      break;
    case GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_PRE:
      do_push_event_pre (self, var_args);
      break;
    default:
      break;
  }
}
