/*
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Sebastian Dröge <sebastian@centricular.com>
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
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
 * SECTION:element-proxysink
 * @title: proxysink
 *
 * Proxysink is a sink element that proxies events, queries, and buffers to
 * another pipeline that contains a matching proxysrc element. The purpose is
 * to allow two decoupled pipelines to function as though they are one without
 * having to manually shuttle buffers, events, queries, etc between the two.
 *
 * This element also copies sticky events onto the matching proxysrc element.
 *
 * For example usage, see proxysrc.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstproxysink.h"
#include "gstproxysrc.h"
#include "gstproxy-priv.h"

#define GST_CAT_DEFAULT gst_proxy_sink_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* We're not subclassing from basesink because we don't want any of the special
 * handling it has for events/queries/etc. We just pass-through everything. */

/* Unlink proxysrc, we don't contain any elements so our parent is GstElement */
#define parent_class gst_proxy_sink_parent_class
G_DEFINE_TYPE (GstProxySink, gst_proxy_sink, GST_TYPE_ELEMENT);

static gboolean gst_proxy_sink_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_proxy_sink_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_proxy_sink_sink_chain_list (GstPad * pad,
    GstObject * parent, GstBufferList * list);
static gboolean gst_proxy_sink_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn gst_proxy_sink_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_proxy_sink_class_init (GstProxySinkClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_proxy_sink_debug, "proxysink", 0, "proxy sink");

  gstelement_class->change_state = gst_proxy_sink_change_state;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "Proxy Sink",
      "Sink", "Proxy source for internal process communication",
      "Sebastian Dröge <sebastian@centricular.com>");
}

static void
gst_proxy_sink_init (GstProxySink * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_proxy_sink_sink_chain));
  gst_pad_set_chain_list_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_proxy_sink_sink_chain_list));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_proxy_sink_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_proxy_sink_sink_query));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);
}

static GstStateChangeReturn
gst_proxy_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstElementClass *gstelement_class =
      GST_ELEMENT_CLASS (gst_proxy_sink_parent_class);
  GstProxySink *self = GST_PROXY_SINK (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->pending_sticky_events = FALSE;
      break;
    default:
      break;
  }

  ret = gstelement_class->change_state (element, transition);

  return ret;
}

static gboolean
gst_proxy_sink_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstProxySink *self = GST_PROXY_SINK (parent);
  GstProxySrc *src;
  gboolean ret = FALSE;

  GST_LOG_OBJECT (pad, "Handling query of type '%s'",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  src = g_weak_ref_get (&self->proxysrc);
  if (src) {
    GstPad *srcpad;
    srcpad = gst_proxy_src_get_internal_srcpad (src);

    ret = gst_pad_peer_query (srcpad, query);
    gst_object_unref (srcpad);
    gst_object_unref (src);
  }

  return ret;
}

typedef struct
{
  GstPad *otherpad;
  GstFlowReturn ret;
} CopyStickyEventsData;

static gboolean
copy_sticky_events (G_GNUC_UNUSED GstPad * pad, GstEvent ** event,
    gpointer user_data)
{
  CopyStickyEventsData *data = user_data;

  data->ret = gst_pad_store_sticky_event (data->otherpad, *event);

  return data->ret == GST_FLOW_OK;
}

static gboolean
gst_proxy_sink_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstProxySink *self = GST_PROXY_SINK (parent);
  GstProxySrc *src;
  gboolean ret = FALSE;
  gboolean sticky = GST_EVENT_IS_STICKY (event);

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP)
    self->pending_sticky_events = FALSE;

  src = g_weak_ref_get (&self->proxysrc);
  if (src) {
    GstPad *srcpad;
    srcpad = gst_proxy_src_get_internal_srcpad (src);

    if (sticky && self->pending_sticky_events) {
      CopyStickyEventsData data = { srcpad, GST_FLOW_OK };

      gst_pad_sticky_events_foreach (pad, copy_sticky_events, &data);
      self->pending_sticky_events = data.ret != GST_FLOW_OK;
    }

    ret = gst_pad_push_event (srcpad, event);
    gst_object_unref (srcpad);
    gst_object_unref (src);

    if (!ret && sticky) {
      self->pending_sticky_events = TRUE;
      ret = TRUE;
    }
  } else
    gst_event_unref (event);

  return ret;
}

static GstFlowReturn
gst_proxy_sink_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstProxySink *self = GST_PROXY_SINK (parent);
  GstProxySrc *src;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (pad, "Chaining buffer %p", buffer);

  src = g_weak_ref_get (&self->proxysrc);
  if (src) {
    GstPad *srcpad;
    srcpad = gst_proxy_src_get_internal_srcpad (src);

    if (self->pending_sticky_events) {
      CopyStickyEventsData data = { srcpad, GST_FLOW_OK };

      gst_pad_sticky_events_foreach (pad, copy_sticky_events, &data);
      self->pending_sticky_events = data.ret != GST_FLOW_OK;
    }

    ret = gst_pad_push (srcpad, buffer);
    gst_object_unref (srcpad);
    gst_object_unref (src);

    GST_LOG_OBJECT (pad, "Chained buffer %p: %s", buffer,
        gst_flow_get_name (ret));
  } else {
    gst_buffer_unref (buffer);
    GST_LOG_OBJECT (pad, "Dropped buffer %p: no otherpad", buffer);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_proxy_sink_sink_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * list)
{
  GstProxySink *self = GST_PROXY_SINK (parent);
  GstProxySrc *src;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (pad, "Chaining buffer list %p", list);

  src = g_weak_ref_get (&self->proxysrc);
  if (src) {
    GstPad *srcpad;
    srcpad = gst_proxy_src_get_internal_srcpad (src);

    if (self->pending_sticky_events) {
      CopyStickyEventsData data = { srcpad, GST_FLOW_OK };

      gst_pad_sticky_events_foreach (pad, copy_sticky_events, &data);
      self->pending_sticky_events = data.ret != GST_FLOW_OK;
    }

    ret = gst_pad_push_list (srcpad, list);
    gst_object_unref (srcpad);
    gst_object_unref (src);
    GST_LOG_OBJECT (pad, "Chained buffer list %p: %s", list,
        gst_flow_get_name (ret));
  } else {
    gst_buffer_list_unref (list);
    GST_LOG_OBJECT (pad, "Dropped buffer list %p: no otherpad", list);
  }

  return GST_FLOW_OK;
}

/* Wrapper function for accessing private member
 * This can also be retrieved with gst_element_get_static_pad, but that depends
 * on the implementation of GstProxySink */
GstPad *
gst_proxy_sink_get_internal_sinkpad (GstProxySink * self)
{
  g_return_val_if_fail (self, NULL);
  return gst_object_ref (self->sinkpad);
}

void
gst_proxy_sink_set_proxysrc (GstProxySink * self, GstProxySrc * src)
{
  g_return_if_fail (self);
  g_weak_ref_set (&self->proxysrc, src);
}
