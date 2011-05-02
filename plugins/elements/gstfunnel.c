/*
 * GStreamer Funnel element
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * gstfunnel.c: Simple Funnel element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:element-funnel
 *
 * Takes packets from various input sinks into one output source.
 *
 * funnel always outputs a single, open ended segment from
 * 0 with in %GST_FORMAT_TIME and outputs the buffers of the
 * different sinkpads with timestamps that are set to the
 * running time for that stream. funnel does not synchronize
 * the different input streams but simply forwards all buffers
 * immediately when they arrive.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfunnel.h"

GST_DEBUG_CATEGORY_STATIC (gst_funnel_debug);
#define GST_CAT_DEFAULT gst_funnel_debug

GType gst_funnel_pad_get_type (void);
#define GST_TYPE_FUNNEL_PAD \
  (gst_funnel_pad_get_type())
#define GST_FUNNEL_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FUNNEL_PAD, GstFunnelPad))
#define GST_FUNNEL_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FUNNEL_PAD, GstFunnelPadClass))
#define GST_IS_FUNNEL_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FUNNEL_PAD))
#define GST_IS_FUNNEL_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FUNNEL_PAD))
#define GST_FUNNEL_PAD_CAST(obj) \
  ((GstFunnelPad *)(obj))

typedef struct _GstFunnelPad GstFunnelPad;
typedef struct _GstFunnelPadClass GstFunnelPadClass;

struct _GstFunnelPad
{
  GstPad parent;

  GstSegment segment;
};

struct _GstFunnelPadClass
{
  GstPadClass parent;
};

G_DEFINE_TYPE (GstFunnelPad, gst_funnel_pad, GST_TYPE_PAD);

static void
gst_funnel_pad_class_init (GstFunnelPadClass * klass)
{
}

static void
gst_funnel_pad_reset (GstFunnelPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_funnel_pad_init (GstFunnelPad * pad)
{
  gst_funnel_pad_reset (pad);
}

static GstStaticPadTemplate funnel_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate funnel_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (gst_funnel_debug, "funnel", 0, "funnel element");
}

GST_BOILERPLATE_FULL (GstFunnel, gst_funnel, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static GstStateChangeReturn gst_funnel_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_funnel_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_funnel_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn gst_funnel_sink_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_funnel_sink_buffer_alloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static gboolean gst_funnel_sink_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_funnel_sink_getcaps (GstPad * pad);

static gboolean gst_funnel_src_event (GstPad * pad, GstEvent * event);

static void
gst_funnel_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Funnel pipe fitting", "Generic", "N-to-1 pipe fitting",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_src_template));
}

static void
gst_funnel_dispose (GObject * object)
{
  GList *item;

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);

    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_funnel_class_init (GstFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_funnel_dispose);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_funnel_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_funnel_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_funnel_change_state);
}

static void
gst_funnel_init (GstFunnel * funnel, GstFunnelClass * g_class)
{
  funnel->srcpad = gst_pad_new_from_static_template (&funnel_src_template,
      "src");
  gst_pad_set_event_function (funnel->srcpad, gst_funnel_src_event);
  gst_pad_use_fixed_caps (funnel->srcpad);
  gst_element_add_pad (GST_ELEMENT (funnel), funnel->srcpad);
}

static GstFlowReturn
gst_funnel_sink_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstFunnel *funnel = GST_FUNNEL (gst_pad_get_parent_element (pad));
  GstFlowReturn ret;

  if (G_UNLIKELY (funnel == NULL))
    return GST_FLOW_WRONG_STATE;

  ret = gst_pad_alloc_buffer (funnel->srcpad, offset, size, caps, buf);

  gst_object_unref (funnel);

  return ret;
}

static GstPad *
gst_funnel_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *sinkpad;

  GST_DEBUG_OBJECT (element, "requesting pad");

  sinkpad = GST_PAD_CAST (g_object_new (GST_TYPE_FUNNEL_PAD,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));

  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_event));
  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_getcaps));
  gst_pad_set_bufferalloc_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_buffer_alloc));

  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (element, sinkpad);

  return sinkpad;
}

static void
gst_funnel_release_pad (GstElement * element, GstPad * pad)
{
  GstFunnel *funnel = GST_FUNNEL (element);

  GST_DEBUG_OBJECT (funnel, "releasing pad");

  gst_pad_set_active (pad, FALSE);

  gst_element_remove_pad (GST_ELEMENT_CAST (funnel), pad);
}

static GstCaps *
gst_funnel_sink_getcaps (GstPad * pad)
{
  GstFunnel *funnel = GST_FUNNEL (gst_pad_get_parent (pad));
  GstCaps *caps;

  if (G_UNLIKELY (funnel == NULL))
    return gst_caps_new_any ();

  caps = gst_pad_peer_get_caps_reffed (funnel->srcpad);
  if (caps == NULL)
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (funnel);

  return caps;
}

static GstFlowReturn
gst_funnel_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstFunnel *funnel = GST_FUNNEL (gst_pad_get_parent (pad));
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);
  GstEvent *event = NULL;
  GstClockTime newts;
  GstCaps *padcaps;

  GST_DEBUG_OBJECT (funnel, "received buffer %p", buffer);

  GST_OBJECT_LOCK (funnel);
  if (fpad->segment.format == GST_FORMAT_UNDEFINED) {
    GST_WARNING_OBJECT (funnel, "Got buffer without segment,"
        " setting segment [0,inf[");
    gst_segment_set_newsegment_full (&fpad->segment, FALSE, 1.0, 1.0,
        GST_FORMAT_TIME, 0, -1, 0);
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
    gst_segment_set_last_stop (&fpad->segment, fpad->segment.format,
        GST_BUFFER_TIMESTAMP (buffer));

  newts = gst_segment_to_running_time (&fpad->segment,
      fpad->segment.format, GST_BUFFER_TIMESTAMP (buffer));
  if (newts != GST_BUFFER_TIMESTAMP (buffer)) {
    buffer = gst_buffer_make_metadata_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = newts;
  }

  if (!funnel->has_segment) {
    event = gst_event_new_new_segment_full (FALSE, 1.0, 1.0, GST_FORMAT_TIME,
        0, -1, 0);
    funnel->has_segment = TRUE;
  }
  GST_OBJECT_UNLOCK (funnel);

  if (event) {
    if (!gst_pad_push_event (funnel->srcpad, event))
      GST_WARNING_OBJECT (funnel, "Could not push out newsegment event");
  }

  GST_OBJECT_LOCK (pad);
  padcaps = GST_PAD_CAPS (funnel->srcpad);
  GST_OBJECT_UNLOCK (pad);

  if (GST_BUFFER_CAPS (buffer) && GST_BUFFER_CAPS (buffer) != padcaps) {
    if (!gst_pad_set_caps (funnel->srcpad, GST_BUFFER_CAPS (buffer))) {
      res = GST_FLOW_NOT_NEGOTIATED;
      goto out;
    }
  }

  res = gst_pad_push (funnel->srcpad, buffer);

  GST_LOG_OBJECT (funnel, "handled buffer %s", gst_flow_get_name (res));

out:
  gst_object_unref (funnel);

  return res;
}

static gboolean
gst_funnel_sink_event (GstPad * pad, GstEvent * event)
{
  GstFunnel *funnel = GST_FUNNEL (gst_pad_get_parent (pad));
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);
  gboolean forward = TRUE;
  gboolean res = TRUE;

  if (G_UNLIKELY (funnel == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, arate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate,
          &format, &start, &stop, &time);

      GST_OBJECT_LOCK (funnel);
      gst_segment_set_newsegment_full (&fpad->segment, update, rate, arate,
          format, start, stop, time);
      GST_OBJECT_UNLOCK (funnel);

      forward = FALSE;
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      GST_OBJECT_LOCK (funnel);
      gst_segment_init (&fpad->segment, GST_FORMAT_UNDEFINED);
      funnel->has_segment = FALSE;
      GST_OBJECT_UNLOCK (funnel);
    }
      break;
    default:
      break;
  }

  if (forward)
    res = gst_pad_push_event (funnel->srcpad, event);
  else
    gst_event_unref (event);

  gst_object_unref (funnel);

  return res;
}

static gboolean
gst_funnel_src_event (GstPad * pad, GstEvent * event)
{
  GstElement *funnel;
  GstIterator *iter;
  GstPad *sinkpad;
  gboolean result = FALSE;
  gboolean done = FALSE;

  funnel = gst_pad_get_parent_element (pad);
  if (G_UNLIKELY (funnel == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  iter = gst_element_iterate_sink_pads (funnel);

  while (!done) {
    switch (gst_iterator_next (iter, (gpointer) & sinkpad)) {
      case GST_ITERATOR_OK:
        gst_event_ref (event);
        result |= gst_pad_push_event (sinkpad, event);
        gst_object_unref (sinkpad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        result = FALSE;
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (funnel, "Error iterating sinkpads");
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_object_unref (funnel);
  gst_event_unref (event);

  return result;
}

static void
reset_pad (gpointer data, gpointer user_data)
{
  GstPad *pad = data;
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);

  GST_OBJECT_LOCK (pad);
  gst_funnel_pad_reset (fpad);
  GST_OBJECT_UNLOCK (pad);
  gst_object_unref (pad);
}

static GstStateChangeReturn
gst_funnel_change_state (GstElement * element, GstStateChange transition)
{
  GstFunnel *funnel = GST_FUNNEL (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GstIterator *iter = gst_element_iterate_sink_pads (element);
      GstIteratorResult res;

      do {
        res = gst_iterator_foreach (iter, reset_pad, NULL);
      } while (res == GST_ITERATOR_RESYNC);

      gst_iterator_free (iter);

      if (res == GST_ITERATOR_ERROR)
        return GST_STATE_CHANGE_FAILURE;

      GST_OBJECT_LOCK (funnel);
      funnel->has_segment = FALSE;
      GST_OBJECT_UNLOCK (funnel);
    }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}
