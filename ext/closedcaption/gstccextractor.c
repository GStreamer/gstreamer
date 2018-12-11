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
    ("closedcaption/x-cea-608,format={ (string) raw, (string) s334-1a}; "
        "closedcaption/x-cea-708,format={ (string) cc_data, (string) cdp }"));

G_DEFINE_TYPE (GstCCExtractor, gst_cc_extractor, GST_TYPE_ELEMENT);
#define parent_class gst_cc_extractor_parent_class

static void gst_cc_extractor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cc_extractor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_cc_extractor_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_cc_extractor_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
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

  memset (&filter->video_info, 0, sizeof (filter->video_info));
}

static void
gst_cc_extractor_init (GstCCExtractor * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cc_extractor_sink_event));
  gst_pad_set_query_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_cc_extractor_sink_query));
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
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      if (!gst_video_info_from_caps (&filter->video_info, caps)) {
        /* We require any kind of video caps here */
        gst_event_unref (event);
        return FALSE;
      }
      break;
    }
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

static gboolean
gst_cc_extractor_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GST_LOG_OBJECT (pad, "received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:{
      GstCaps *caps;
      const GstStructure *s;

      gst_query_parse_accept_caps (query, &caps);

      /* FIXME: Ideally we would declare this in our caps but there's no way
       * to declare caps of type "video/" and "image/" that would match all
       * such caps
       */
      s = gst_caps_get_structure (caps, 0);
      if (s && (g_str_has_prefix (gst_structure_get_name (s), "video/")
              || g_str_has_prefix (gst_structure_get_name (s), "image/")))
        gst_query_set_accept_caps_result (query, TRUE);
      else
        gst_query_set_accept_caps_result (query, FALSE);

      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static GstCaps *
create_caps_from_caption_type (GstVideoCaptionType caption_type,
    const GstVideoInfo * video_info)
{
  GstCaps *caption_caps = gst_video_caption_type_to_caps (caption_type);

  gst_caps_set_simple (caption_caps, "framerate", GST_TYPE_FRACTION,
      video_info->fps_n, video_info->fps_d, NULL);

  return caption_caps;
}

static GstFlowReturn
gst_cc_extractor_handle_meta (GstCCExtractor * filter, GstBuffer * buf,
    GstVideoCaptionMeta * meta, GstVideoTimeCodeMeta * tc_meta)
{
  GstBuffer *outbuf = NULL;
  GstEvent *event;
  gchar *captionid;
  GstFlowReturn flow;

  GST_DEBUG_OBJECT (filter, "Handling meta");

  /* Check if the meta type matches the configured one */
  if (filter->captionpad == NULL) {
    GstCaps *caption_caps =
        create_caps_from_caption_type (meta->caption_type, &filter->video_info);
    GstEvent *stream_event;

    GST_DEBUG_OBJECT (filter, "Creating new caption pad");
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
  } else if (meta->caption_type != filter->caption_type) {
    GstCaps *caption_caps =
        create_caps_from_caption_type (meta->caption_type, &filter->video_info);

    GST_DEBUG_OBJECT (filter, "Caption type changed from %d to %d",
        filter->caption_type, meta->caption_type);
    if (caption_caps == NULL) {
      GST_ERROR_OBJECT (filter, "Unknown/invalid caption type");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    gst_pad_set_caps (filter->captionpad, caption_caps);
    gst_caps_unref (caption_caps);

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

  if (tc_meta)
    gst_buffer_add_video_time_code_meta (outbuf, &tc_meta->tc);

  /* We don't really care about the flow return */
  flow = gst_pad_push (filter->captionpad, outbuf);

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
  GstVideoTimeCodeMeta *tc_meta;
  gpointer iter = NULL;

  tc_meta = gst_buffer_get_video_time_code_meta (buf);

  while ((cc_meta =
          (GstVideoCaptionMeta *) gst_buffer_iterate_meta_filtered (buf, &iter,
              GST_VIDEO_CAPTION_META_API_TYPE)) && flow == GST_FLOW_OK) {
    flow = gst_cc_extractor_handle_meta (filter, buf, cc_meta, tc_meta);
  }

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
