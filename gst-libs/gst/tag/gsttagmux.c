/* GStreamer tag muxer base class
 * Copyright (C) 2006 Christophe Fergeau  <teuf@gnome.org>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 * Copyright (C) 2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
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

/**
 * SECTION:gsttagmux
 * @see_also: GstApeMux, GstId3Mux
 * @short_description: Base class for adding tags that are in one single chunk
 *                     directly at the beginning or at the end of a file
 *
 * <refsect2>
 * <para>
 * Provides a base class for adding tags at the beginning or end of a
 * stream.
 * </para>
 * <title>Deriving from GstTagMux</title>
 * <para>
 * Subclasses have to do the following things:
 * <itemizedlist>
 *  <listitem><para>
 *  In their base init function, they must add pad templates for the sink
 *  pad and the source pad to the element class, describing the media type
 *  they accept and output in the caps of the pad template.
 *  </para></listitem>
 *  <listitem><para>
 *  In their class init function, they must override the
 *  GST_TAG_MUX_CLASS(mux_klass)->render_start_tag and/or
 *  GST_TAG_MUX_CLASS(mux_klass)->render_end_tag vfuncs and set up a render
 *  function.
 *  </para></listitem>
 * </itemizedlist>
 * </para>
 * </refsect2>
 *
 * Since 0.10.36
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>

#include "gsttagmux.h"

struct _GstTagMuxPrivate
{
  GstPad *srcpad;
  GstPad *sinkpad;
  GstTagList *event_tags;       /* tags received from upstream elements */
  GstTagList *final_tags;       /* Final set of tags used for muxing */
  gsize start_tag_size;
  gsize end_tag_size;
  gboolean render_start_tag;
  gboolean render_end_tag;

  gint64 current_offset;
  gint64 max_offset;

  GstEvent *newsegment_ev;      /* cached newsegment event from upstream */
};

GST_DEBUG_CATEGORY_STATIC (gst_tag_mux_debug);
#define GST_CAT_DEFAULT gst_tag_mux_debug

static void
gst_tag_mux_iface_init (GType tag_type)
{
  static const GInterfaceInfo tag_setter_info = {
    NULL,
    NULL,
    NULL
  };

  g_type_add_interface_static (tag_type, GST_TYPE_TAG_SETTER, &tag_setter_info);
}

GST_BOILERPLATE_FULL (GstTagMux, gst_tag_mux,
    GstElement, GST_TYPE_ELEMENT, gst_tag_mux_iface_init);


static GstStateChangeReturn
gst_tag_mux_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_tag_mux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_tag_mux_sink_event (GstPad * pad, GstEvent * event);

static void
gst_tag_mux_finalize (GObject * obj)
{
  GstTagMux *mux = GST_TAG_MUX (obj);

  if (mux->priv->newsegment_ev) {
    gst_event_unref (mux->priv->newsegment_ev);
    mux->priv->newsegment_ev = NULL;
  }

  if (mux->priv->event_tags) {
    gst_tag_list_free (mux->priv->event_tags);
    mux->priv->event_tags = NULL;
  }

  if (mux->priv->final_tags) {
    gst_tag_list_free (mux->priv->final_tags);
    mux->priv->final_tags = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_tag_mux_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_tag_mux_debug, "tagmux", 0,
      "tag muxer base class");
}

static void
gst_tag_mux_class_init (GstTagMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_tag_mux_finalize);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_tag_mux_change_state);

  g_type_class_add_private (klass, sizeof (GstTagMuxPrivate));
}

static void
gst_tag_mux_init (GstTagMux * mux, GstTagMuxClass * mux_class)
{
  GstElementClass *element_klass = GST_ELEMENT_CLASS (mux_class);
  GstPadTemplate *tmpl;

  mux->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (mux, GST_TYPE_TAG_MUX, GstTagMuxPrivate);

  /* pad through which data comes in to the element */
  tmpl = gst_element_class_get_pad_template (element_klass, "sink");
  if (tmpl) {
    mux->priv->sinkpad = gst_pad_new_from_template (tmpl, "sink");
  } else {
    g_warning ("GstTagMux subclass '%s' did not install a %s pad template!\n",
        G_OBJECT_CLASS_NAME (mux_class), "sink");
    mux->priv->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  }
  gst_pad_set_chain_function (mux->priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_mux_chain));
  gst_pad_set_event_function (mux->priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tag_mux_sink_event));
  gst_element_add_pad (GST_ELEMENT (mux), mux->priv->sinkpad);

  /* pad through which data goes out of the element */
  tmpl = gst_element_class_get_pad_template (element_klass, "src");
  if (tmpl) {
    GstCaps *tmpl_caps = gst_pad_template_get_caps (tmpl);

    mux->priv->srcpad = gst_pad_new_from_template (tmpl, "src");
    gst_pad_use_fixed_caps (mux->priv->srcpad);
    if (tmpl_caps != NULL && gst_caps_is_fixed (tmpl_caps)) {
      gst_pad_set_caps (mux->priv->srcpad, tmpl_caps);
    }
  } else {
    g_warning ("GstTagMux subclass '%s' did not install a %s pad template!\n",
        G_OBJECT_CLASS_NAME (mux_class), "source");
    mux->priv->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  }
  gst_element_add_pad (GST_ELEMENT (mux), mux->priv->srcpad);

  mux->priv->render_start_tag = TRUE;
  mux->priv->render_end_tag = TRUE;
}

static GstTagList *
gst_tag_mux_get_tags (GstTagMux * mux)
{
  GstTagSetter *tagsetter = GST_TAG_SETTER (mux);
  const GstTagList *tagsetter_tags;
  GstTagMergeMode merge_mode;

  if (mux->priv->final_tags)
    return mux->priv->final_tags;

  tagsetter_tags = gst_tag_setter_get_tag_list (tagsetter);
  merge_mode = gst_tag_setter_get_tag_merge_mode (tagsetter);

  GST_LOG_OBJECT (mux, "merging tags, merge mode = %d", merge_mode);
  GST_LOG_OBJECT (mux, "event tags: %" GST_PTR_FORMAT, mux->priv->event_tags);
  GST_LOG_OBJECT (mux, "set   tags: %" GST_PTR_FORMAT, tagsetter_tags);

  mux->priv->final_tags =
      gst_tag_list_merge (tagsetter_tags, mux->priv->event_tags, merge_mode);

  GST_LOG_OBJECT (mux, "final tags: %" GST_PTR_FORMAT, mux->priv->final_tags);

  return mux->priv->final_tags;
}

static GstFlowReturn
gst_tag_mux_render_start_tag (GstTagMux * mux)
{
  GstTagMuxClass *klass;
  GstBuffer *buffer;
  GstTagList *taglist;
  GstEvent *event;
  GstFlowReturn ret;

  taglist = gst_tag_mux_get_tags (mux);

  klass = GST_TAG_MUX_CLASS (G_OBJECT_GET_CLASS (mux));

  if (klass->render_start_tag == NULL)
    goto no_vfunc;

  buffer = klass->render_start_tag (mux, taglist);

  /* Null buffer is ok, just means we're not outputting anything */
  if (buffer == NULL) {
    GST_INFO_OBJECT (mux, "No start tag generated");
    mux->priv->start_tag_size = 0;
    return GST_FLOW_OK;
  }

  if (GST_BUFFER_CAPS (buffer) == NULL) {
    buffer = gst_buffer_make_metadata_writable (buffer);
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (mux->priv->srcpad));
  }

  mux->priv->start_tag_size = GST_BUFFER_SIZE (buffer);
  GST_LOG_OBJECT (mux, "tag size = %" G_GSIZE_FORMAT " bytes",
      mux->priv->start_tag_size);

  /* Send newsegment event from byte position 0, so the tag really gets
   * written to the start of the file, independent of the upstream segment */
  gst_pad_push_event (mux->priv->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));

  /* Send an event about the new tags to downstream elements */
  /* gst_event_new_tag takes ownership of the list, so use a copy */
  event = gst_event_new_tag (gst_tag_list_copy (taglist));
  gst_pad_push_event (mux->priv->srcpad, event);

  GST_BUFFER_OFFSET (buffer) = 0;
  ret = gst_pad_push (mux->priv->srcpad, buffer);

  mux->priv->current_offset = mux->priv->start_tag_size;
  mux->priv->max_offset =
      MAX (mux->priv->max_offset, mux->priv->current_offset);

  return ret;

no_vfunc:
  {
    GST_ERROR_OBJECT (mux, "Subclass does not implement "
        "render_start_tag vfunc!");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_tag_mux_render_end_tag (GstTagMux * mux)
{
  GstTagMuxClass *klass;
  GstBuffer *buffer;
  GstTagList *taglist;
  GstFlowReturn ret;

  taglist = gst_tag_mux_get_tags (mux);

  klass = GST_TAG_MUX_CLASS (G_OBJECT_GET_CLASS (mux));

  if (klass->render_end_tag == NULL)
    goto no_vfunc;

  buffer = klass->render_end_tag (mux, taglist);

  if (buffer == NULL) {
    GST_INFO_OBJECT (mux, "No end tag generated");
    mux->priv->end_tag_size = 0;
    return GST_FLOW_OK;
  }

  if (GST_BUFFER_CAPS (buffer) == NULL) {
    buffer = gst_buffer_make_metadata_writable (buffer);
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (mux->priv->srcpad));
  }

  mux->priv->end_tag_size = GST_BUFFER_SIZE (buffer);
  GST_LOG_OBJECT (mux, "tag size = %" G_GSIZE_FORMAT " bytes",
      mux->priv->end_tag_size);

  /* Send newsegment event from the end of the file, so it gets written there,
     independent of whatever new segment events upstream has sent us */
  gst_pad_push_event (mux->priv->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
          mux->priv->max_offset, -1, 0));

  GST_BUFFER_OFFSET (buffer) = mux->priv->max_offset;
  ret = gst_pad_push (mux->priv->srcpad, buffer);

  return ret;

no_vfunc:
  {
    GST_ERROR_OBJECT (mux, "Subclass does not implement "
        "render_end_tag vfunc!");
    return GST_FLOW_ERROR;
  }
}

static GstEvent *
gst_tag_mux_adjust_event_offsets (GstTagMux * mux,
    const GstEvent * newsegment_event)
{
  GstFormat format;
  gint64 start, stop, cur;

  gst_event_parse_new_segment ((GstEvent *) newsegment_event, NULL, NULL,
      &format, &start, &stop, &cur);

  g_assert (format == GST_FORMAT_BYTES);

  if (start != -1)
    start += mux->priv->start_tag_size;
  if (stop != -1)
    stop += mux->priv->start_tag_size;
  if (cur != -1)
    cur += mux->priv->start_tag_size;

  GST_DEBUG_OBJECT (mux, "adjusting newsegment event offsets to start=%"
      G_GINT64_FORMAT ", stop=%" G_GINT64_FORMAT ", cur=%" G_GINT64_FORMAT
      " (delta = +%" G_GSIZE_FORMAT ")", start, stop, cur,
      mux->priv->start_tag_size);

  return gst_event_new_new_segment (TRUE, 1.0, format, start, stop, cur);
}

static GstFlowReturn
gst_tag_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstTagMux *mux = GST_TAG_MUX (GST_OBJECT_PARENT (pad));
  GstFlowReturn ret;
  int length;

  if (mux->priv->render_start_tag) {

    GST_INFO_OBJECT (mux, "Adding tags to stream");
    ret = gst_tag_mux_render_start_tag (mux);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (mux, "flow: %s", gst_flow_get_name (ret));
      gst_buffer_unref (buffer);
      return ret;
    }

    /* Now send the cached newsegment event that we got from upstream */
    if (mux->priv->newsegment_ev) {
      gint64 start;
      GstEvent *newseg;

      GST_DEBUG_OBJECT (mux, "sending cached newsegment event");
      newseg = gst_tag_mux_adjust_event_offsets (mux, mux->priv->newsegment_ev);
      gst_event_unref (mux->priv->newsegment_ev);
      mux->priv->newsegment_ev = NULL;

      gst_event_parse_new_segment (newseg, NULL, NULL, NULL, &start, NULL,
          NULL);

      gst_pad_push_event (mux->priv->srcpad, newseg);
      mux->priv->current_offset = start;
      mux->priv->max_offset =
          MAX (mux->priv->max_offset, mux->priv->current_offset);
    } else {
      /* upstream sent no newsegment event or only one in a non-BYTE format */
    }

    mux->priv->render_start_tag = FALSE;
  }

  buffer = gst_buffer_make_metadata_writable (buffer);

  if (GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE) {
    GST_LOG_OBJECT (mux, "Adjusting buffer offset from %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, GST_BUFFER_OFFSET (buffer),
        GST_BUFFER_OFFSET (buffer) + mux->priv->start_tag_size);
    GST_BUFFER_OFFSET (buffer) += mux->priv->start_tag_size;
  }

  length = GST_BUFFER_SIZE (buffer);

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (mux->priv->srcpad));
  ret = gst_pad_push (mux->priv->srcpad, buffer);

  mux->priv->current_offset += length;
  mux->priv->max_offset =
      MAX (mux->priv->max_offset, mux->priv->current_offset);

  return ret;
}

static gboolean
gst_tag_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstTagMux *mux;
  gboolean result;

  mux = GST_TAG_MUX (gst_pad_get_parent (pad));
  result = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *tags;

      gst_event_parse_tag (event, &tags);

      GST_INFO_OBJECT (mux, "Got tag event: %" GST_PTR_FORMAT, tags);

      if (mux->priv->event_tags != NULL) {
        gst_tag_list_insert (mux->priv->event_tags, tags,
            GST_TAG_MERGE_REPLACE);
      } else {
        mux->priv->event_tags = gst_tag_list_copy (tags);
      }

      GST_INFO_OBJECT (mux, "Event tags are now: %" GST_PTR_FORMAT,
          mux->priv->event_tags);

      /* just drop the event, we'll push a new tag event in render_start_tag */
      gst_event_unref (event);
      result = TRUE;
      break;
    }
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gint64 start;

      gst_event_parse_new_segment (event, NULL, NULL, &fmt, &start, NULL, NULL);

      if (fmt != GST_FORMAT_BYTES) {
        GST_WARNING_OBJECT (mux, "dropping newsegment event in %s format",
            gst_format_get_name (fmt));
        gst_event_unref (event);
        break;
      }

      if (mux->priv->render_start_tag) {
        /* we have not rendered the tag yet, which means that we don't know
         * how large it is going to be yet, so we can't adjust the offsets
         * here at this point and need to cache the newsegment event for now
         * (also, there could be tag events coming after this newsegment event
         *  and before the first buffer). */
        if (mux->priv->newsegment_ev) {
          GST_WARNING_OBJECT (mux, "discarding old cached newsegment event");
          gst_event_unref (mux->priv->newsegment_ev);
        }

        GST_LOG_OBJECT (mux, "caching newsegment event for later");
        mux->priv->newsegment_ev = event;
      } else {
        GST_DEBUG_OBJECT (mux, "got newsegment event, adjusting offsets");
        gst_pad_push_event (mux->priv->srcpad,
            gst_tag_mux_adjust_event_offsets (mux, event));
        gst_event_unref (event);

        mux->priv->current_offset = start;
        mux->priv->max_offset =
            MAX (mux->priv->max_offset, mux->priv->current_offset);
      }
      event = NULL;
      result = TRUE;
      break;
    }
    case GST_EVENT_EOS:{
      if (mux->priv->render_end_tag) {
        GstFlowReturn ret;

        GST_INFO_OBJECT (mux, "Adding tags to stream");
        ret = gst_tag_mux_render_end_tag (mux);
        if (ret != GST_FLOW_OK) {
          GST_DEBUG_OBJECT (mux, "flow: %s", gst_flow_get_name (ret));
          return ret;
        }

        mux->priv->render_end_tag = FALSE;
      }

      /* Now forward EOS */
      result = gst_pad_event_default (pad, event);
      break;
    }
    default:
      result = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (mux);

  return result;
}


static GstStateChangeReturn
gst_tag_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstTagMux *mux;
  GstStateChangeReturn result;

  mux = GST_TAG_MUX (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (result != GST_STATE_CHANGE_SUCCESS) {
    return result;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      if (mux->priv->newsegment_ev) {
        gst_event_unref (mux->priv->newsegment_ev);
        mux->priv->newsegment_ev = NULL;
      }
      if (mux->priv->event_tags) {
        gst_tag_list_free (mux->priv->event_tags);
        mux->priv->event_tags = NULL;
      }
      mux->priv->start_tag_size = 0;
      mux->priv->end_tag_size = 0;
      mux->priv->render_start_tag = TRUE;
      mux->priv->render_end_tag = TRUE;
      mux->priv->current_offset = 0;
      mux->priv->max_offset = 0;
      break;
    }
    default:
      break;
  }

  return result;
}
