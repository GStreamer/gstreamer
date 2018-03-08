/*
 * GStreamer
 * Copyright (C) 2018 Edward Hervey <edward@centricular.com>
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
 * SECTION:element-ccextractor
 * @title: ccextractor
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstccextractor.h"

GST_DEBUG_CATEGORY_STATIC (gst_cc_extractor_debug);
#define GST_CAT_DEFAULT gst_cc_extractor_debug

enum
{
  PROP_0,
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate captiontemplate =
    GST_STATIC_PAD_TEMPLATE ("caption",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS
    ("closedcaption/x-cea-608,format={ (string) raw, (string) cc_data}; "
        "closedcaption/x-cea-708,format={ (string) cc_data, (string) cdp }"));

G_DEFINE_TYPE (GstCCExtractor, gst_cc_extractor, GST_TYPE_ELEMENT);
#define parent_class gst_cc_extractor_parent_class

static void gst_cc_extractor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cc_extractor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_cc_extractor_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_cc_extractor_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstStateChangeReturn gst_cc_extractor_change_state (GstElement *
    element, GstStateChange transition);
static void gst_cc_extractor_finalize (GObject * self);


static void
gst_cc_extractor_class_init (GstCCExtractorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_cc_extractor_set_property;
  gobject_class->get_property = gst_cc_extractor_get_property;
  gobject_class->finalize = gst_cc_extractor_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_cc_extractor_change_state);

  gst_element_class_set_static_metadata (gstelement_class,
      "Closed Caption Extractor",
      "Filter",
      "Extract GstVideoCaptionMeta from input stream",
      "Edward Hervey <edward@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class,
      &captiontemplate);

  GST_DEBUG_CATEGORY_INIT (gst_cc_extractor_debug, "ccextractor",
      0, "Closed Caption extractor");
}

static GstIterator *
gst_cc_extractor_iterate_internal_links (GstPad * pad, GstObject * parent)
{
  GstCCExtractor *filter = (GstCCExtractor *) parent;
  GstIterator *it = NULL;
  GstPad *opad = NULL;

  if (pad == filter->sinkpad)
    opad = filter->srcpad;
  else if (pad == filter->srcpad || pad == filter->captionpad)
    opad = filter->sinkpad;

  if (opad) {
    GValue value = { 0, };

    g_value_init (&value, GST_TYPE_PAD);
    g_value_set_object (&value, opad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &value);
    g_value_unset (&value);
  }

  return it;
}

static void
gst_cc_extractor_reset (GstCCExtractor * filter)
{
  filter->caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
  gst_flow_combiner_reset (filter->combiner);
  gst_flow_combiner_add_pad (filter->combiner, filter->srcpad);

  if (filter->captionpad) {
    gst_flow_combiner_remove_pad (filter->combiner, filter->captionpad);
    gst_pad_set_active (filter->captionpad, FALSE);
    gst_element_remove_pad ((GstElement *) filter, filter->captionpad);
    filter->captionpad = NULL;
  }
}

static void
gst_cc_extractor_init (GstCCExtractor * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cc_extractor_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cc_extractor_chain));
  gst_pad_set_iterate_internal_links_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cc_extractor_iterate_internal_links));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (filter->sinkpad);
  GST_PAD_SET_PROXY_SCHEDULING (filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_pad_set_iterate_internal_links_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_cc_extractor_iterate_internal_links));
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (filter->srcpad);
  GST_PAD_SET_PROXY_SCHEDULING (filter->srcpad);

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->combiner = gst_flow_combiner_new ();

  gst_cc_extractor_reset (filter);
}

static void
gst_cc_extractor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstCCExtractor *filter = GST_CCEXTRACTOR (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cc_extractor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstCCExtractor *filter = GST_CCEXTRACTOR (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_cc_extractor_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstCCExtractor *filter = GST_CCEXTRACTOR (parent);

  GST_LOG_OBJECT (pad, "received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
      /* Also forward to the caption pad if present */
      if (filter->captionpad)
        gst_pad_push_event (filter->captionpad, gst_event_ref (event));
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_cc_extractor_handle_meta (GstCCExtractor * filter, GstBuffer * buf,
    GstVideoCaptionMeta * meta)
{
  GstBuffer *outbuf = NULL;
  GstEvent *event;
  gchar *captionid;
  GstFlowReturn flow;

  GST_DEBUG_OBJECT (filter, "Handling meta");

  /* Check if the meta type matches the configured one */
  if (filter->captionpad != NULL && meta->caption_type != filter->caption_type) {
    GST_ERROR_OBJECT (filter,
        "GstVideoCaptionMeta type changed, Not handled currently");
    flow = GST_FLOW_NOT_NEGOTIATED;
    goto out;
  }

  if (filter->captionpad == NULL) {
    GstCaps *caption_caps = NULL;
    GstEvent *stream_event;

    GST_DEBUG_OBJECT (filter, "Creating new caption pad");
    switch (meta->caption_type) {
      case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
        caption_caps =
            gst_caps_from_string ("closedcaption/x-cea-608,format=(string)raw");
        break;
      case GST_VIDEO_CAPTION_TYPE_CEA608_IN_CEA708_RAW:
        caption_caps =
            gst_caps_from_string
            ("closedcaption/x-cea-608,format=(string)cc_data");
        break;
      case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
        caption_caps =
            gst_caps_from_string
            ("closedcaption/x-cea-708,format=(string)cc_data");
        break;
      case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
        caption_caps =
            gst_caps_from_string ("closedcaption/x-cea-708,format=(string)cdp");
        break;
      default:
        break;
    }
    if (caption_caps == NULL) {
      GST_ERROR_OBJECT (filter, "Unknown/invalid caption type");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    /* Create the caption pad and set the caps */
    filter->captionpad =
        gst_pad_new_from_static_template (&captiontemplate, "caption");
    gst_pad_set_iterate_internal_links_function (filter->sinkpad,
        GST_DEBUG_FUNCPTR (gst_cc_extractor_iterate_internal_links));
    gst_pad_set_active (filter->captionpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (filter), filter->captionpad);
    gst_flow_combiner_add_pad (filter->combiner, filter->captionpad);

    captionid =
        gst_pad_create_stream_id (filter->captionpad, (GstElement *) filter,
        "caption");
    stream_event = gst_event_new_stream_start (captionid);
    g_free (captionid);

    /* FIXME : Create a proper stream-id */
    if ((event =
            gst_pad_get_sticky_event (filter->srcpad, GST_EVENT_STREAM_START,
                0))) {
      guint group_id;
      if (gst_event_parse_group_id (event, &group_id))
        gst_event_set_group_id (stream_event, group_id);
      gst_event_unref (event);
    }
    gst_pad_push_event (filter->captionpad, stream_event);
    gst_pad_set_caps (filter->captionpad, caption_caps);
    gst_caps_unref (caption_caps);

    /* Carry over sticky events */
    if ((event =
            gst_pad_get_sticky_event (filter->srcpad, GST_EVENT_SEGMENT, 0)))
      gst_pad_push_event (filter->captionpad, event);
    if ((event = gst_pad_get_sticky_event (filter->srcpad, GST_EVENT_TAG, 0)))
      gst_pad_push_event (filter->captionpad, event);


    filter->caption_type = meta->caption_type;
  }

  GST_DEBUG_OBJECT (filter,
      "Creating new buffer of size %" G_GSIZE_FORMAT " bytes", meta->size);
  /* Extract caption data into new buffer with identical buffer timestamps */
  outbuf = gst_buffer_new_allocate (NULL, meta->size, NULL);
  gst_buffer_fill (outbuf, 0, meta->data, meta->size);
  GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (buf);
  GST_BUFFER_DTS (outbuf) = GST_BUFFER_DTS (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  /* We don't really care about the flow return */
  flow = gst_pad_push (filter->captionpad, outbuf);

out:
  /* Set flow return on pad and return combined value */
  return gst_flow_combiner_update_pad_flow (filter->combiner,
      filter->captionpad, flow);
}

static GstFlowReturn
gst_cc_extractor_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstCCExtractor *filter = (GstCCExtractor *) parent;
  GstFlowReturn flow = GST_FLOW_OK;
  GstVideoCaptionMeta *cc_meta;

  cc_meta = gst_buffer_get_video_caption_meta (buf);
  if (cc_meta)
    flow = gst_cc_extractor_handle_meta (filter, buf, cc_meta);
  else
    GST_DEBUG_OBJECT (filter, "No CC meta on buffer");

  /* If there's an issue handling the CC, return immediately */
  if (flow != GST_FLOW_OK) {
    gst_buffer_unref (buf);
    return flow;
  }

  /* Push the buffer downstream and return the combined flow return */
  return gst_flow_combiner_update_pad_flow (filter->combiner, filter->srcpad,
      gst_pad_push (filter->srcpad, buf));
}

static GstStateChangeReturn
gst_cc_extractor_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstCCExtractor *filter = GST_CCEXTRACTOR (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_cc_extractor_reset (filter);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}

static void
gst_cc_extractor_finalize (GObject * object)
{
  GstCCExtractor *filter = GST_CCEXTRACTOR (object);

  gst_flow_combiner_free (filter->combiner);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}
