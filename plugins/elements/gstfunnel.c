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

static GstStaticPadTemplate funnel_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate funnel_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_funnel_debug, "funnel", 0, "funnel element");
#define gst_funnel_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstFunnel, gst_funnel, GST_TYPE_ELEMENT, _do_init);

static GstPad *gst_funnel_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_funnel_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn gst_funnel_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_funnel_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void
gst_funnel_dispose (GObject * object)
{
  GstFunnel *funnel = GST_FUNNEL (object);
  GList *item;

  gst_object_replace ((GstObject **) & funnel->last_sinkpad, NULL);

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

  gst_element_class_set_static_metadata (gstelement_class,
      "Funnel pipe fitting", "Generic", "N-to-1 pipe fitting",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_src_template));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_funnel_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_funnel_release_pad);
}

static void
gst_funnel_init (GstFunnel * funnel)
{
  funnel->srcpad = gst_pad_new_from_static_template (&funnel_src_template,
      "src");
  gst_pad_use_fixed_caps (funnel->srcpad);

  gst_element_add_pad (GST_ELEMENT (funnel), funnel->srcpad);
}

static GstPad *
gst_funnel_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *sinkpad;

  GST_DEBUG_OBJECT (element, "requesting pad");

  sinkpad = gst_pad_new_from_static_template (&funnel_sink_template, name);

  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_event));

  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_CAPS);
  GST_OBJECT_FLAG_SET (sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (element, sinkpad);

  return sinkpad;
}

static gboolean
gst_funnel_all_sinkpads_eos_unlocked (GstFunnel * funnel)
{
  GstElement *element = GST_ELEMENT_CAST (funnel);
  GList *item;

  if (element->numsinkpads == 0)
    return FALSE;

  for (item = element->sinkpads; item != NULL; item = g_list_next (item)) {
    GstPad *sinkpad = item->data;
    GstEvent *eos;

    eos = gst_pad_get_sticky_event (sinkpad, GST_EVENT_EOS, 0);
    if (eos)
      gst_event_unref (eos);

    if (eos == NULL)
      return FALSE;
  }

  return TRUE;
}

static void
gst_funnel_release_pad (GstElement * element, GstPad * pad)
{
  GstFunnel *funnel = GST_FUNNEL (element);
  GstEvent *eos;
  gboolean send_eos = FALSE;

  GST_DEBUG_OBJECT (funnel, "releasing pad");

  gst_pad_set_active (pad, FALSE);

  eos = gst_pad_get_sticky_event (pad, GST_EVENT_EOS, 0);
  if (eos)
    gst_event_unref (eos);

  gst_element_remove_pad (GST_ELEMENT_CAST (funnel), pad);

  GST_OBJECT_LOCK (funnel);
  if (eos == NULL && gst_funnel_all_sinkpads_eos_unlocked (funnel)) {
    GST_DEBUG_OBJECT (funnel, "Pad removed. All others are EOS. Sending EOS");
    send_eos = TRUE;
  }
  GST_OBJECT_UNLOCK (funnel);

  if (send_eos)
    if (!gst_pad_push_event (funnel->srcpad, gst_event_new_eos ()))
      GST_WARNING_OBJECT (funnel, "Failure pushing EOS");
}

static gboolean
forward_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *srcpad = user_data;

  if (GST_EVENT_TYPE (*event) != GST_EVENT_EOS)
    gst_pad_push_event (srcpad, gst_event_ref (*event));

  return TRUE;
}

static GstFlowReturn
gst_funnel_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstFunnel *funnel = GST_FUNNEL (parent);

  GST_DEBUG_OBJECT (funnel, "received buffer %p", buffer);

  GST_PAD_STREAM_LOCK (funnel->srcpad);

  if (funnel->last_sinkpad != pad) {
    gst_object_replace ((GstObject **) & funnel->last_sinkpad,
        GST_OBJECT (pad));

    gst_pad_sticky_events_foreach (pad, forward_events, funnel->srcpad);
  }

  res = gst_pad_push (funnel->srcpad, buffer);

  GST_PAD_STREAM_UNLOCK (funnel->srcpad);

  GST_LOG_OBJECT (funnel, "handled buffer %s", gst_flow_get_name (res));

  return res;
}

static gboolean
gst_funnel_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFunnel *funnel = GST_FUNNEL (parent);
  gboolean forward = TRUE;
  gboolean res = TRUE;
  gboolean unlock = FALSE;

  if (GST_EVENT_IS_STICKY (event)) {
    unlock = TRUE;
    GST_PAD_STREAM_LOCK (funnel->srcpad);

    if (pad != funnel->last_sinkpad)
      forward = FALSE;
  }

  if (forward)
    res = gst_pad_push_event (funnel->srcpad, event);
  else
    gst_event_unref (event);

  if (unlock)
    GST_PAD_STREAM_UNLOCK (funnel->srcpad);

  return res;
}
