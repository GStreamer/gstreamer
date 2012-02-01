/* GStreamer taglib-based muxer base class
 * Copyright (C) 2006 Christophe Fergeau  <teuf@gnome.org>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2006 Sebastian Dröge <slomo@circular-chaos.org>

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
#include <config.h>
#endif

#include <string.h>
#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>

#include "gsttaglibmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_tag_lib_mux_debug);
#define GST_CAT_DEFAULT gst_tag_lib_mux_debug

static GstStaticPadTemplate gst_tag_lib_mux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));


#define gst_tag_lib_mux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstTagLibMux, gst_tag_lib_mux, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL));

static GstStateChangeReturn
gst_tag_lib_mux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_tag_lib_mux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_tag_lib_mux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void
gst_tag_lib_mux_finalize (GObject * obj)
{
  GstTagLibMux *mux = GST_TAG_LIB_MUX (obj);

  if (mux->newsegment_ev) {
    gst_event_unref (mux->newsegment_ev);
    mux->newsegment_ev = NULL;
  }

  if (mux->event_tags) {
    gst_tag_list_free (mux->event_tags);
    mux->event_tags = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_tag_lib_mux_class_init (GstTagLibMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_tag_lib_mux_finalize;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_tag_lib_mux_sink_template));

  GST_DEBUG_CATEGORY_INIT (gst_tag_lib_mux_debug, "taglibmux", 0,
      "taglib-based muxer");
}

static void
gst_tag_lib_mux_init (GstTagLibMux * mux)
{
  GstTagLibMuxClass *mux_class = GST_TAG_LIB_MUX_GET_CLASS (mux);
  GstElementClass *element_klass = GST_ELEMENT_CLASS (mux_class);
  GstPadTemplate *tmpl;

  /* pad through which data comes in to the element */
  mux->sinkpad =
      gst_pad_new_from_static_template (&gst_tag_lib_mux_sink_template, "sink");
  gst_pad_set_chain_function (mux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_chain));
  gst_pad_set_event_function (mux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_lib_mux_sink_event));
  gst_element_add_pad (GST_ELEMENT (mux), mux->sinkpad);

  /* pad through which data goes out of the element */
  tmpl = gst_element_class_get_pad_template (element_klass, "src");
  if (tmpl) {
    mux->srcpad = gst_pad_new_from_template (tmpl, "src");
    gst_pad_use_fixed_caps (mux->srcpad);
    gst_pad_set_caps (mux->srcpad, gst_pad_template_get_caps (tmpl));
    gst_element_add_pad (GST_ELEMENT (mux), mux->srcpad);
  }

  mux->render_tag = TRUE;
}

static GstBuffer *
gst_tag_lib_mux_render_tag (GstTagLibMux * mux)
{
  GstTagLibMuxClass *klass;
  GstTagMergeMode merge_mode;
  GstTagSetter *tagsetter;
  GstBuffer *buffer;
  const GstTagList *tagsetter_tags;
  GstTagList *taglist;
  GstEvent *event;
  GstSegment segment;

  tagsetter = GST_TAG_SETTER (mux);

  tagsetter_tags = gst_tag_setter_get_tag_list (tagsetter);
  merge_mode = gst_tag_setter_get_tag_merge_mode (tagsetter);

  GST_LOG_OBJECT (mux, "merging tags, merge mode = %d", merge_mode);
  GST_LOG_OBJECT (mux, "event tags: %" GST_PTR_FORMAT, mux->event_tags);
  GST_LOG_OBJECT (mux, "set   tags: %" GST_PTR_FORMAT, tagsetter_tags);

  taglist = gst_tag_list_merge (tagsetter_tags, mux->event_tags, merge_mode);

  GST_LOG_OBJECT (mux, "final tags: %" GST_PTR_FORMAT, taglist);

  klass = GST_TAG_LIB_MUX_CLASS (G_OBJECT_GET_CLASS (mux));

  if (klass->render_tag == NULL)
    goto no_vfunc;

  buffer = klass->render_tag (mux, taglist);

  if (buffer == NULL)
    goto render_error;

  mux->tag_size = gst_buffer_get_size (buffer);
  GST_LOG_OBJECT (mux, "tag size = %" G_GSIZE_FORMAT " bytes", mux->tag_size);

  /* Send newsegment event from byte position 0, so the tag really gets
   * written to the start of the file, independent of the upstream segment */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mux->srcpad, gst_event_new_segment (&segment));

  /* Send an event about the new tags to downstream elements */
  /* gst_event_new_tag takes ownership of the list, so no need to unref it */
  event = gst_event_new_tag (taglist);
  gst_pad_push_event (mux->srcpad, event);

  GST_BUFFER_OFFSET (buffer) = 0;

  return buffer;

no_vfunc:
  {
    GST_ERROR_OBJECT (mux, "Subclass does not implement render_tag vfunc!");
    gst_tag_list_free (taglist);
    return NULL;
  }

render_error:
  {
    GST_ERROR_OBJECT (mux, "Failed to render tag");
    gst_tag_list_free (taglist);
    return NULL;
  }
}

static GstEvent *
gst_tag_lib_mux_adjust_event_offsets (GstTagLibMux * mux,
    const GstEvent * newsegment_event)
{
  gint64 start, stop, cur;
  GstSegment segment;

  gst_event_copy_segment ((GstEvent *) newsegment_event, &segment);

  g_assert (segment.format == GST_FORMAT_BYTES);

  if (segment.start != -1)
    start += mux->tag_size;
  if (segment.stop != -1)
    stop += mux->tag_size;
  if (segment.time != -1)
    cur += mux->tag_size;

  GST_DEBUG_OBJECT (mux, "adjusting newsegment event offsets to start=%"
      G_GUINT64_FORMAT ", stop=%" G_GUINT64_FORMAT ", cur=%" G_GUINT64_FORMAT
      " (delta = +%" G_GSIZE_FORMAT ")",
      segment.start, segment.stop, segment.time, mux->tag_size);

  return gst_event_new_segment (&segment);
}

static GstFlowReturn
gst_tag_lib_mux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstTagLibMux *mux = GST_TAG_LIB_MUX (parent);

  if (mux->render_tag) {
    GstFlowReturn ret;
    GstBuffer *tag_buffer;
    GstCaps *tcaps;

    GST_INFO_OBJECT (mux, "Adding tags to stream");
    tag_buffer = gst_tag_lib_mux_render_tag (mux);
    if (tag_buffer == NULL)
      goto no_tag_buffer;
    ret = gst_pad_push (mux->srcpad, tag_buffer);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (mux, "flow: %s", gst_flow_get_name (ret));
      gst_buffer_unref (buffer);
      return ret;
    }

    /* Now send the cached newsegment event that we got from upstream */
    if (mux->newsegment_ev) {
      GST_DEBUG_OBJECT (mux, "sending cached newsegment event");
      gst_pad_push_event (mux->srcpad,
          gst_tag_lib_mux_adjust_event_offsets (mux, mux->newsegment_ev));
      gst_event_unref (mux->newsegment_ev);
      mux->newsegment_ev = NULL;
    } else {
      /* upstream sent no newsegment event or only one in a non-BYTE format */
    }

    mux->render_tag = FALSE;

    /* we have data flow, so pad is active and caps can be set */
    tcaps = gst_pad_get_pad_template_caps (mux->srcpad);
    gst_pad_set_caps (mux->srcpad, tcaps);
    gst_caps_unref (tcaps);
  }

  buffer = gst_buffer_make_writable (buffer);

  if (GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (mux, "Adjusting buffer offset from %" G_GUINT64_FORMAT
        " to %" G_GUINT64_FORMAT, GST_BUFFER_OFFSET (buffer),
        GST_BUFFER_OFFSET (buffer) + mux->tag_size);
    GST_BUFFER_OFFSET (buffer) += mux->tag_size;
  }

  return gst_pad_push (mux->srcpad, buffer);

/* ERRORS */
no_tag_buffer:
  {
    GST_ELEMENT_ERROR (mux, LIBRARY, ENCODE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_tag_lib_mux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTagLibMux *mux;
  gboolean result;

  mux = GST_TAG_LIB_MUX (parent);
  result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *tags;

      gst_event_parse_tag (event, &tags);

      GST_INFO_OBJECT (mux, "Got tag event: %" GST_PTR_FORMAT, tags);

      if (mux->event_tags != NULL) {
        gst_tag_list_insert (mux->event_tags, tags, GST_TAG_MERGE_REPLACE);
      } else {
        mux->event_tags = gst_tag_list_copy (tags);
      }

      GST_INFO_OBJECT (mux, "Event tags are now: %" GST_PTR_FORMAT,
          mux->event_tags);

      /* just drop the event, we'll push a new tag event in render_tag */
      gst_event_unref (event);
      result = TRUE;
      break;
    }
    case GST_EVENT_SEGMENT:{
      const GstSegment *segment;

      gst_event_parse_segment (event, &segment);

      if (segment->format != GST_FORMAT_BYTES) {
        GST_WARNING_OBJECT (mux, "dropping newsegment event in %s format",
            gst_format_get_name (segment->format));
        gst_event_unref (event);
        break;
      }

      if (mux->render_tag) {
        /* we have not rendered the tag yet, which means that we don't know
         * how large it is going to be yet, so we can't adjust the offsets
         * here at this point and need to cache the newsegment event for now
         * (also, there could be tag events coming after this newsegment event
         *  and before the first buffer). */
        if (mux->newsegment_ev) {
          GST_WARNING_OBJECT (mux, "discarding old cached newsegment event");
          gst_event_unref (mux->newsegment_ev);
        }

        GST_LOG_OBJECT (mux, "caching newsegment event for later");
        mux->newsegment_ev = event;
      } else {
        GST_DEBUG_OBJECT (mux, "got newsegment event, adjusting offsets");
        gst_pad_push_event (mux->srcpad,
            gst_tag_lib_mux_adjust_event_offsets (mux, event));
        gst_event_unref (event);
      }
      event = NULL;
      result = TRUE;
      break;
    }
    default:
      result = gst_pad_event_default (pad, parent, event);
      break;
  }

  return result;
}


static GstStateChangeReturn
gst_tag_lib_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstTagLibMux *mux;
  GstStateChangeReturn result;

  mux = GST_TAG_LIB_MUX (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result != GST_STATE_CHANGE_SUCCESS) {
    return result;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      if (mux->newsegment_ev) {
        gst_event_unref (mux->newsegment_ev);
        mux->newsegment_ev = NULL;
      }
      if (mux->event_tags) {
        gst_tag_list_free (mux->event_tags);
        mux->event_tags = NULL;
      }
      mux->tag_size = 0;
      mux->render_tag = TRUE;
      break;
    }
    default:
      break;
  }

  return result;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return (gst_id3v2_mux_plugin_init (plugin)
      && gst_apev2_mux_plugin_init (plugin));
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "taglib",
    "Tag writing plug-in based on taglib",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
