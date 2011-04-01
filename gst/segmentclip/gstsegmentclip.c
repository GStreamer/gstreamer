/* GStreamer
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstsegmentclip.h"

static void gst_segment_clip_reset (GstSegmentClip * self);

static GstStateChangeReturn gst_segment_clip_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_segment_clip_sink_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_segment_clip_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_segment_clip_sink_bufferalloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

static gboolean gst_segment_clip_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_segment_clip_getcaps (GstPad * pad);
static gboolean gst_segment_clip_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_segment_clip_query_type (GstPad * pad);

GST_DEBUG_CATEGORY_STATIC (gst_segment_clip_debug);
#define GST_CAT_DEFAULT gst_segment_clip_debug

GST_BOILERPLATE (GstSegmentClip, gst_segment_clip, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_segment_clip_base_init (gpointer g_class)
{
}

static void
gst_segment_clip_class_init (GstSegmentClipClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_segment_clip_debug, "segmentclip", 0,
      "segmentclip base class");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_segment_clip_change_state);
}

static void
gst_segment_clip_init (GstSegmentClip * self, GstSegmentClipClass * g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *templ;

  templ = gst_element_class_get_pad_template (element_class, "sink");
  g_assert (templ);

  self->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_sink_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_sink_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_getcaps));
  gst_pad_set_bufferalloc_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_sink_bufferalloc));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_query));
  gst_pad_set_query_type_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_query_type));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  gst_object_unref (templ);

  templ = gst_element_class_get_pad_template (element_class, "src");
  g_assert (templ);

  self->srcpad = gst_pad_new_from_template (templ, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_event));
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_getcaps));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_query));
  gst_pad_set_query_type_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_segment_clip_query_type));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  gst_segment_clip_reset (self);
}

static void
gst_segment_clip_reset (GstSegmentClip * self)
{
  GstSegmentClipClass *klass = GST_SEGMENT_CLIP_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Resetting internal state");

  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
  if (klass->reset)
    klass->reset (self);
}

static GstFlowReturn
gst_segment_clip_sink_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (pad, "Allocating buffer with offset 0x%" G_GINT64_MODIFIER
      "x and size %u with caps: %" GST_PTR_FORMAT, offset, size, caps);

  *buf = NULL;

  ret = gst_pad_alloc_buffer (self->srcpad, offset, size, caps, buf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    GST_ERROR_OBJECT (pad, "Allocating buffer failed: %s",
        gst_flow_get_name (ret));

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_segment_clip_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (gst_pad_get_parent (pad));
  GstSegmentClipClass *klass = GST_SEGMENT_CLIP_GET_CLASS (self);
  gboolean ret;

  GST_DEBUG_OBJECT (pad, "Setting caps: %" GST_PTR_FORMAT, caps);
  ret = klass->set_caps (self, caps);

  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_segment_clip_getcaps (GstPad * pad)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *tmp, *ret;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  tmp = gst_pad_peer_get_caps (otherpad);
  if (tmp) {
    ret = gst_caps_intersect (tmp, gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (tmp);
  } else {
    ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  gst_object_unref (self);

  GST_LOG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_segment_clip_query (GstPad * pad, GstQuery * query)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (gst_pad_get_parent (pad));
  gboolean ret;
  GstPad *otherpad;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  GST_LOG_OBJECT (pad, "Handling query of type '%s'",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));

  ret = gst_pad_peer_query (otherpad, query);

  gst_object_unref (self);
  return ret;
}

static const GstQueryType *
gst_segment_clip_query_type (GstPad * pad)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (gst_pad_get_parent (pad));
  GstPad *otherpad, *peer;
  const GstQueryType *types = NULL;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;
  peer = gst_pad_get_peer (otherpad);
  if (peer) {
    types = gst_pad_get_query_types (peer);
    gst_object_unref (peer);
  }

  gst_object_unref (self);

  return types;
}


static GstFlowReturn
gst_segment_clip_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (GST_PAD_PARENT (pad));
  GstFlowReturn ret;
  GstSegmentClipClass *klass = GST_SEGMENT_CLIP_GET_CLASS (self);
  GstBuffer *outbuf = NULL;

  GST_LOG_OBJECT (pad,
      "Handling buffer with timestamp %" GST_TIME_FORMAT " and duration %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  ret = klass->clip_buffer (self, buffer, &outbuf);
  if (ret == GST_FLOW_OK && outbuf)
    ret = gst_pad_push (self->srcpad, outbuf);

  return ret;
}

static GstStateChangeReturn
gst_segment_clip_change_state (GstElement * element, GstStateChange transition)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_segment_clip_reset (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_segment_clip_event (GstPad * pad, GstEvent * event)
{
  GstSegmentClip *self = GST_SEGMENT_CLIP (gst_pad_get_parent (pad));
  GstPad *otherpad;
  gboolean ret;

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;
  ret = gst_pad_push_event (otherpad, gst_event_ref (event));

  if (ret) {
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_NEWSEGMENT:{
        GstFormat fmt;
        gboolean is_update;
        gint64 start, end, base;
        gdouble rate;

        gst_event_parse_new_segment (event, &is_update, &rate, &fmt, &start,
            &end, &base);
        GST_DEBUG_OBJECT (pad,
            "Got NEWSEGMENT event in %s format, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")",
            gst_format_get_name (fmt), GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
        gst_segment_set_newsegment (&self->segment, is_update, rate, fmt, start,
            end, base);
        break;
      }
      case GST_EVENT_FLUSH_STOP:
        gst_segment_clip_reset (self);
        break;
      default:
        break;
    }
  }

  gst_event_unref (event);

  gst_object_unref (self);
  return ret;
}
