/*
 * Farsight2 - Farsight Funnel element
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * rtsp-funnel.c: Simple Funnel element
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
 * SECTION:element-rtspfunnel
 * @short_description: N-to-1 simple funnel
 *
 * Takes packets from various input sinks into one output source
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "rtsp-funnel.h"

GST_DEBUG_CATEGORY_STATIC (rtsp_funnel_debug);
#define GST_CAT_DEFAULT rtsp_funnel_debug

static const GstElementDetails rtsp_funnel_details =
GST_ELEMENT_DETAILS ("Farsight Funnel pipe fitting",
    "Generic",
    "N-to-1 pipe fitting",
    "Olivier Crete <olivier.crete@collabora.co.uk>");

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
  GST_DEBUG_CATEGORY_INIT (rtsp_funnel_debug, "rtspfunnel", 0,
      "rtsp funnel element");
}

GST_BOILERPLATE_FULL (RTSPFunnel, rtsp_funnel, GstElement, GST_TYPE_ELEMENT,
    _do_init);



static GstStateChangeReturn rtsp_funnel_change_state (GstElement * element,
    GstStateChange transition);

static GstPad *rtsp_funnel_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void rtsp_funnel_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn rtsp_funnel_chain (GstPad * pad, GstBuffer * buffer);
static gboolean rtsp_funnel_event (GstPad * pad, GstEvent * event);
static gboolean rtsp_funnel_src_event (GstPad * pad, GstEvent * event);
static GstCaps *rtsp_funnel_getcaps (GstPad * pad);


typedef struct
{
  GstSegment segment;
} RTSPFunnelPadPrivate;

static void
rtsp_funnel_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &rtsp_funnel_details);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_src_template));
}


static void
rtsp_funnel_dispose (GObject * object)
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
rtsp_funnel_class_init (RTSPFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (rtsp_funnel_dispose);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (rtsp_funnel_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (rtsp_funnel_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (rtsp_funnel_change_state);
}

static void
rtsp_funnel_init (RTSPFunnel * funnel, RTSPFunnelClass * g_class)
{
  funnel->srcpad = gst_pad_new_from_static_template (&funnel_src_template,
      "src");
  gst_pad_set_event_function (funnel->srcpad, rtsp_funnel_src_event);
  gst_pad_use_fixed_caps (funnel->srcpad);
  gst_element_add_pad (GST_ELEMENT (funnel), funnel->srcpad);
}


static GstPad *
rtsp_funnel_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *sinkpad;
  RTSPFunnelPadPrivate *priv = g_slice_alloc0 (sizeof (RTSPFunnelPadPrivate));

  GST_DEBUG_OBJECT (element, "requesting pad");

  sinkpad = gst_pad_new_from_template (templ, name);

  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR (rtsp_funnel_chain));
  gst_pad_set_event_function (sinkpad, GST_DEBUG_FUNCPTR (rtsp_funnel_event));
  gst_pad_set_getcaps_function (sinkpad,
      GST_DEBUG_FUNCPTR (rtsp_funnel_getcaps));

  gst_segment_init (&priv->segment, GST_FORMAT_UNDEFINED);
  gst_pad_set_element_private (sinkpad, priv);

  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (element, sinkpad);

  return sinkpad;
}

static void
rtsp_funnel_release_pad (GstElement * element, GstPad * pad)
{
  RTSPFunnel *funnel = RTSP_FUNNEL (element);
  RTSPFunnelPadPrivate *priv = gst_pad_get_element_private (pad);

  GST_DEBUG_OBJECT (funnel, "releasing pad");

  gst_pad_set_active (pad, FALSE);

  if (priv)
    g_slice_free1 (sizeof (RTSPFunnelPadPrivate), priv);

  gst_element_remove_pad (GST_ELEMENT_CAST (funnel), pad);
}

static GstCaps *
rtsp_funnel_getcaps (GstPad * pad)
{
  RTSPFunnel *funnel = RTSP_FUNNEL (gst_pad_get_parent (pad));
  GstCaps *caps;

  caps = gst_pad_peer_get_caps (funnel->srcpad);
  if (caps == NULL)
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (funnel);

  return caps;
}

static GstFlowReturn
rtsp_funnel_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res;
  RTSPFunnel *funnel = RTSP_FUNNEL (gst_pad_get_parent (pad));
  RTSPFunnelPadPrivate *priv = gst_pad_get_element_private (pad);
  GstEvent *event = NULL;
  GstClockTime newts;
  GstCaps *padcaps;

  GST_DEBUG_OBJECT (funnel, "received buffer %p", buffer);

  GST_OBJECT_LOCK (funnel);
  if (priv->segment.format == GST_FORMAT_UNDEFINED) {
    GST_WARNING_OBJECT (funnel, "Got buffer without segment,"
        " setting segment [0,inf[");
    gst_segment_set_newsegment_full (&priv->segment, FALSE, 1.0, 1.0,
        GST_FORMAT_TIME, 0, -1, 0);
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
    gst_segment_set_last_stop (&priv->segment, priv->segment.format,
        GST_BUFFER_TIMESTAMP (buffer));

  newts = gst_segment_to_running_time (&priv->segment,
      priv->segment.format, GST_BUFFER_TIMESTAMP (buffer));
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
    if (!gst_pad_push_event (funnel->srcpad, event)) {
      GST_WARNING_OBJECT (funnel, "Could not push out newsegment event");
      res = GST_FLOW_ERROR;
      goto out;
    }
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
rtsp_funnel_event (GstPad * pad, GstEvent * event)
{
  RTSPFunnel *funnel = RTSP_FUNNEL (gst_pad_get_parent (pad));
  RTSPFunnelPadPrivate *priv = gst_pad_get_element_private (pad);
  gboolean forward = TRUE;
  gboolean res = TRUE;

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
      gst_segment_set_newsegment_full (&priv->segment, update, rate, arate,
          format, start, stop, time);
      GST_OBJECT_UNLOCK (funnel);

      forward = FALSE;
      gst_event_unref (event);
    }
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      GST_OBJECT_LOCK (funnel);
      gst_segment_init (&priv->segment, GST_FORMAT_UNDEFINED);
      GST_OBJECT_UNLOCK (funnel);
    }
      break;
    default:
      break;
  }


  if (forward)
    res = gst_pad_push_event (funnel->srcpad, event);

  gst_object_unref (funnel);

  return res;
}

static gboolean
rtsp_funnel_src_event (GstPad * pad, GstEvent * event)
{
  GstElement *funnel;
  GstIterator *iter;
  GstPad *sinkpad;
  gboolean result = FALSE;
  gboolean done = FALSE;

  funnel = gst_pad_get_parent_element (pad);
  g_return_val_if_fail (funnel != NULL, FALSE);

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
  RTSPFunnelPadPrivate *priv = gst_pad_get_element_private (pad);

  GST_OBJECT_LOCK (pad);
  gst_segment_init (&priv->segment, GST_FORMAT_UNDEFINED);
  GST_OBJECT_UNLOCK (pad);
  gst_object_unref (pad);
}

static GstStateChangeReturn
rtsp_funnel_change_state (GstElement * element, GstStateChange transition)
{
  RTSPFunnel *funnel = RTSP_FUNNEL (element);
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
