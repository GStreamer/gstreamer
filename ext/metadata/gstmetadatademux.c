/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:metadatademux-metadata
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatademux ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstmetadatademux.h"

#include "metadataexif.h"

#include "metadataiptc.h"

#include "metadataxmp.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_metadata_demux_debug);
#define GST_CAT_DEFAULT gst_metadata_demux_debug

#define GOTO_DONE_IF_NULL(ptr) do { if ( NULL == (ptr) ) goto done; } while(FALSE)
#define GOTO_DONE_IF_NULL_AND_FAIL(ptr, ret) do { if ( NULL == (ptr) ) { (ret) = FALSE; goto done; } } while(FALSE)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_EXIF,
  ARG_IPTC,
  ARG_XMP
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "tags-extracted = (bool) false;"
        "image/png, " "tags-extracted = (bool) false")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "tags-extracted = (bool) true;"
        "image/png, " "tags-extracted = (bool) true")
    );

GST_BOILERPLATE (GstMetadataDemux, gst_metadata_demux, GstElement,
    GST_TYPE_ELEMENT);

static GstMetadataDemuxClass *metadata_parent_class = NULL;

static void gst_metadata_demux_dispose (GObject * object);

static void gst_metadata_demux_finalize (GObject * object);

static void gst_metadata_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_metadata_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_metadata_demux_change_state (GstElement * element,
    GstStateChange transition);

static GstCaps *gst_metadata_demux_get_caps (GstPad * pad);
static gboolean gst_metadata_demux_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_metadata_demux_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_metadata_demux_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_metadata_demux_chain (GstPad * pad, GstBuffer * buf);

static gboolean gst_metadata_demux_checkgetrange (GstPad * srcpad);

static GstFlowReturn
gst_metadata_demux_get_range (GstPad * pad, guint64 offset_orig, guint size,
    GstBuffer ** buf);

static gboolean gst_metadata_demux_sink_activate (GstPad * pad);

static gboolean
gst_metadata_demux_src_activate_pull (GstPad * pad, gboolean active);

static gboolean gst_metadata_demux_pull_range_demux (GstMetadataDemux * filter);

static void gst_metadata_demux_init_members (GstMetadataDemux * filter);
static void gst_metadata_demux_dispose_members (GstMetadataDemux * filter);

static gboolean
gst_metadata_demux_configure_srccaps (GstMetadataDemux * filter);

static gboolean gst_metadata_demux_configure_caps (GstMetadataDemux * filter);

static int
gst_metadata_demux_parse (GstMetadataDemux * filter, const guint8 * buf,
    guint32 size);

static void gst_metadata_demux_send_tags (GstMetadataDemux * filter);

static const GstQueryType *gst_metadata_demux_get_query_types (GstPad * pad);

static gboolean gst_metadata_demux_src_query (GstPad * pad, GstQuery * query);

static void
gst_metadata_demux_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "Metadata demuxr",
    "Demuxr/Extracter/Metadata",
    "Send metadata tags (EXIF, IPTC and XMP) and remove metadata chunks from stream",
    "Edgard Lima <edgard.lima@indt.org.br>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the plugin's class */
static void
gst_metadata_demux_class_init (GstMetadataDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  metadata_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_metadata_demux_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_metadata_demux_finalize);

  gobject_class->set_property = gst_metadata_demux_set_property;
  gobject_class->get_property = gst_metadata_demux_get_property;

  g_object_class_install_property (gobject_class, ARG_EXIF,
      g_param_spec_boolean ("exif", "EXIF", "Send EXIF metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_IPTC,
      g_param_spec_boolean ("iptc", "IPTC", "Send IPTC metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_XMP,
      g_param_spec_boolean ("xmp", "XMP", "Send XMP metadata ?",
          TRUE, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_metadata_demux_change_state;

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_metadata_demux_init (GstMetadataDemux * filter,
    GstMetadataDemuxClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  /* sink pad */

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_get_caps));
  gst_pad_set_event_function (filter->sinkpad, gst_metadata_demux_sink_event);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_chain));
  gst_pad_set_activate_function (filter->sinkpad,
      gst_metadata_demux_sink_activate);

  /* source pad */

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_get_caps));
  gst_pad_set_event_function (filter->srcpad, gst_metadata_demux_src_event);
  gst_pad_set_query_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_src_query));
  gst_pad_set_query_type_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_get_query_types));
  gst_pad_use_fixed_caps (filter->srcpad);

  gst_pad_set_checkgetrange_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_checkgetrange));
  gst_pad_set_getrange_function (filter->srcpad, gst_metadata_demux_get_range);

  gst_pad_set_activatepull_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_demux_src_activate_pull));
  /* addind pads */

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  metadataparse_xmp_init ();
  /* init members */

  filter->options = META_OPT_EXIF | META_OPT_IPTC | META_OPT_XMP;

  gst_metadata_demux_init_members (filter);

}

static void
gst_metadata_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMetadataDemux *filter = GST_METADATA_DEMUX (object);

  switch (prop_id) {
    case ARG_EXIF:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_EXIF;
      else
        filter->options &= ~META_OPT_EXIF;
      break;
    case ARG_IPTC:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_IPTC;
      else
        filter->options &= ~META_OPT_IPTC;
      break;
    case ARG_XMP:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_XMP;
      else
        filter->options &= ~META_OPT_XMP;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_metadata_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMetadataDemux *filter = GST_METADATA_DEMUX (object);

  switch (prop_id) {
    case ARG_EXIF:
      g_value_set_boolean (value, filter->options & META_OPT_EXIF);
      break;
    case ARG_IPTC:
      g_value_set_boolean (value, filter->options & META_OPT_IPTC);
      break;
    case ARG_XMP:
      g_value_set_boolean (value, filter->options & META_OPT_XMP);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

static GstCaps *
gst_metadata_demux_get_caps (GstPad * pad)
{
  GstMetadataDemux *filter = NULL;
  GstPad *otherpad;
  GstCaps *caps_new = NULL;
  GstCaps *caps_otherpad_peer = NULL;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  (filter->srcpad == pad) ? (otherpad = filter->sinkpad) : (otherpad =
      filter->srcpad);

  caps_new = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  caps_otherpad_peer = gst_pad_get_allowed_caps (otherpad);
  GOTO_DONE_IF_NULL (caps_otherpad_peer);

  if (gst_caps_is_empty (caps_otherpad_peer)
      || gst_caps_is_any (caps_otherpad_peer)) {
    goto done;
  } else {

    guint i;
    guint caps_size = 0;

    caps_size = gst_caps_get_size (caps_otherpad_peer);

    gst_caps_unref (caps_new);

    caps_new = gst_caps_new_empty ();

    for (i = 0; i < caps_size; ++i) {
      GstStructure *structure = NULL;
      GstStructure *structure_new = NULL;
      const gchar *mime = NULL;

      structure = gst_caps_get_structure (caps_otherpad_peer, i);

      mime = gst_structure_get_name (structure);

      if (pad == filter->sinkpad) {
        structure_new =
            gst_structure_new (mime, "tags-extracted", G_TYPE_BOOLEAN, FALSE,
            NULL);
      } else {
        structure_new =
            gst_structure_new (mime, "tags-extracted", G_TYPE_BOOLEAN, TRUE,
            NULL);
      }

      gst_caps_append_structure (caps_new, structure_new);

    }

  }

done:

  if (caps_otherpad_peer) {
    gst_caps_unref (caps_otherpad_peer);
    caps_otherpad_peer = NULL;
  }

  gst_object_unref (filter);

  return caps_new;

}

static gboolean
gst_metadata_demux_src_event (GstPad * pad, GstEvent * event)
{
  GstMetadataDemux *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type;
      gint64 start;
      GstSeekType stop_type;
      gint64 stop;

      /* we don't know where are the chunks to be stripped before demux */
      if (filter->common.state != MT_STATE_PARSED)
        goto done;

      gst_event_parse_seek (event, &rate, &format, &flags,
          &start_type, &start, &stop_type, &stop);

      switch (format) {
        case GST_FORMAT_BYTES:
          break;
        case GST_FORMAT_PERCENT:
          if (filter->common.duration < 0)
            goto done;
          start = start * filter->common.duration / 100;
          stop = stop * filter->common.duration / 100;
          break;
        default:
          goto done;
      }
      format = GST_FORMAT_BYTES;

      if (start_type == GST_SEEK_TYPE_CUR)
        start = filter->offset + start;
      else if (start_type == GST_SEEK_TYPE_END) {
        if (filter->common.duration < 0)
          goto done;
        start = filter->common.duration + start;
      }
      start_type == GST_SEEK_TYPE_SET;

      if (filter->prepend_buffer) {
        gst_buffer_unref (filter->prepend_buffer);
        filter->prepend_buffer = NULL;
      }

      /* FIXME: related to append */
      filter->offset = start;
      gst_metadata_common_translate_pos_to_orig (&filter->common, start, &start,
          &filter->prepend_buffer);
      filter->offset_orig = start;

      if (stop_type == GST_SEEK_TYPE_CUR)
        stop = filter->offset + stop;
      else if (stop_type == GST_SEEK_TYPE_END) {
        if (filter->common.duration < 0)
          goto done;
        stop = filter->common.duration + stop;
      }
      stop_type == GST_SEEK_TYPE_SET;

      gst_metadata_common_translate_pos_to_orig (&filter->common, stop, &stop,
          NULL);

      gst_event_unref (event);
      event = gst_event_new_seek (rate, format, flags,
          start_type, start, stop_type, stop);

    }
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);
  event = NULL;                 /* event has another owner */

done:

  if (event) {
    gst_event_unref (event);
  }

  gst_object_unref (filter);

  return ret;

}

static gboolean
gst_metadata_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstMetadataDemux *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (filter->need_more_data) {
        GST_ELEMENT_WARNING (filter, STREAM, DEMUX, (NULL),
            ("Need more data. Unexpected EOS"));
      }
      break;
    case GST_EVENT_TAG:
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);

  gst_object_unref (filter);

  return ret;

}

static void
gst_metadata_demux_dispose (GObject * object)
{
  GstMetadataDemux *filter = NULL;

  filter = GST_METADATA_DEMUX (object);

  gst_metadata_demux_dispose_members (filter);

  metadataparse_xmp_dispose ();

  G_OBJECT_CLASS (metadata_parent_class)->dispose (object);
}

static void
gst_metadata_demux_finalize (GObject * object)
{
  G_OBJECT_CLASS (metadata_parent_class)->finalize (object);
}

static void
gst_metadata_demux_dispose_members (GstMetadataDemux * filter)
{
  gst_metadata_common_dispose (&filter->common);

  if (filter->adapter_parsing) {
    gst_object_unref (filter->adapter_parsing);
    filter->adapter_parsing = NULL;
  }

  if (filter->adapter_holding) {
    gst_object_unref (filter->adapter_holding);
    filter->adapter_holding = NULL;
  }

  if (filter->prepend_buffer) {
    gst_buffer_unref (filter->prepend_buffer);
    filter->prepend_buffer = NULL;
  }
}

static void
gst_metadata_demux_init_members (GstMetadataDemux * filter)
{
  filter->need_send_tag = FALSE;

  filter->adapter_parsing = NULL;
  filter->adapter_holding = NULL;
  filter->next_offset = 0;
  filter->next_size = 0;
  filter->img_type = IMG_NONE;
  filter->offset_orig = 0;
  filter->offset = 0;
  filter->need_more_data = FALSE;

  filter->prepend_buffer = NULL;

  memset (&filter->common, 0x00, sizeof (filter->common));

}

static gboolean
gst_metadata_demux_configure_srccaps (GstMetadataDemux * filter)
{
  GstCaps *caps = NULL;
  gboolean ret = FALSE;
  gchar *mime = NULL;

  switch (filter->img_type) {
    case IMG_JPEG:
      mime = "image/jpeg";
      break;
    case IMG_PNG:
      mime = "image/png";
      break;
    default:
      ret = FALSE;
      goto done;
      break;
  }

  caps =
      gst_caps_new_simple (mime, "tags-extracted", G_TYPE_BOOLEAN, TRUE, NULL);

  ret = gst_pad_set_caps (filter->srcpad, caps);

done:

  if (caps) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  return ret;

}

static gboolean
gst_metadata_demux_configure_caps (GstMetadataDemux * filter)
{
  GstCaps *caps = NULL;
  gboolean ret = FALSE;
  gchar *mime = NULL;
  GstPad *peer = NULL;

  peer = gst_pad_get_peer (filter->sinkpad);

  switch (filter->img_type) {
    case IMG_JPEG:
      mime = "image/jpeg";
      break;
    case IMG_PNG:
      mime = "image/png";
      break;
    default:
      goto done;
      break;
  }

  caps = gst_caps_new_simple (mime, NULL);

  if (!gst_pad_set_caps (peer, caps)) {
    goto done;
  }

  ret = gst_pad_set_caps (filter->sinkpad, caps);

done:

  if (caps) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  if (peer) {
    gst_object_unref (peer);
    peer = NULL;
  }

  return ret;

}

/* this function handles the link with other elements */
static gboolean
gst_metadata_demux_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMetadataDemux *filter = NULL;
  GstStructure *structure = NULL;
  const gchar *mime = NULL;
  gboolean ret = FALSE;
  gboolean demuxd = TRUE;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  mime = gst_structure_get_name (structure);

  if (strcmp (mime, "image/jpeg") == 0) {
    filter->img_type = IMG_JPEG;
  } else if (strcmp (mime, "image/png") == 0) {
    filter->img_type = IMG_PNG;
  } else {
    ret = FALSE;
    goto done;
  }

  if (gst_structure_get_boolean (structure, "tags-extracted", &demuxd)) {
    if (demuxd == TRUE) {
      ret = FALSE;
      goto done;
    }
  }

  ret = gst_metadata_demux_configure_srccaps (filter);

done:

  gst_object_unref (filter);

  return ret;
}

static void
gst_metadata_demux_send_tags (GstMetadataDemux * filter)
{

  GstMessage *msg;
  GstTagList *taglist = gst_tag_list_new ();
  GstEvent *event;

  if (filter->options & META_OPT_EXIF)
    metadataparse_exif_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        filter->common.metadata.exif_adapter, METADATA_TAG_MAP_WHOLECHUNK);
  if (filter->options & META_OPT_IPTC)
    metadataparse_iptc_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        filter->common.metadata.iptc_adapter, METADATA_TAG_MAP_WHOLECHUNK);
  if (filter->options & META_OPT_XMP)
    metadataparse_xmp_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        filter->common.metadata.xmp_adapter, METADATA_TAG_MAP_WHOLECHUNK);

  if (taglist && !gst_tag_list_is_empty (taglist)) {

    msg =
        gst_message_new_tag (GST_OBJECT (filter), gst_tag_list_copy (taglist));
    gst_element_post_message (GST_ELEMENT (filter), msg);

    event = gst_event_new_tag (taglist);
    gst_pad_push_event (filter->srcpad, event);
    taglist = NULL;
  }

  if (!taglist)
    taglist = gst_tag_list_new ();

  if (filter->options & META_OPT_EXIF)
    metadataparse_exif_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        filter->common.metadata.exif_adapter, METADATA_TAG_MAP_INDIVIDUALS);
  if (filter->options & META_OPT_IPTC)
    metadataparse_iptc_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        filter->common.metadata.iptc_adapter, METADATA_TAG_MAP_INDIVIDUALS);
  if (filter->options & META_OPT_XMP)
    metadataparse_xmp_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        filter->common.metadata.xmp_adapter, METADATA_TAG_MAP_INDIVIDUALS);

  if (taglist && !gst_tag_list_is_empty (taglist)) {

    msg = gst_message_new_tag (GST_OBJECT (filter), taglist);
    gst_element_post_message (GST_ELEMENT (filter), msg);
    taglist = NULL;
  }

  if (taglist)
    gst_tag_list_free (taglist);

  filter->need_send_tag = FALSE;
}

static const GstQueryType *
gst_metadata_demux_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_metadata_demux_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_FORMATS,
    0
  };

  return gst_metadata_demux_src_query_types;
}

static gboolean
gst_metadata_demux_src_query (GstPad * pad, GstQuery * query)
{
  gboolean ret = FALSE;
  GstFormat format;
  GstMetadataDemux *filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_BYTES) {
        gst_query_set_position (query, GST_FORMAT_BYTES, filter->offset);
        ret = TRUE;
      }
      break;
    case GST_QUERY_DURATION:
      if (filter->common.state != MT_STATE_PARSED)
        goto done;

      gst_query_parse_duration (query, &format, NULL);

      if (format == GST_FORMAT_BYTES) {
        if (filter->common.duration >= 0) {
          gst_query_set_duration (query, GST_FORMAT_BYTES,
              filter->common.duration);
          ret = TRUE;
        }
      }
      break;
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 1, GST_FORMAT_BYTES);
      ret = TRUE;
      break;
    default:
      break;
  }

done:

  gst_object_unref (filter);

  return ret;

}

/*
 * Do parsing step-by-step and reconfigure caps if need
 * return:
 *   META_PARSING_ERROR
 *   META_PARSING_DONE
 *   META_PARSING_NEED_MORE_DATA
 */

static int
gst_metadata_demux_parse (GstMetadataDemux * filter, const guint8 * buf,
    guint32 size)
{

  int ret = META_PARSING_ERROR;

  filter->next_offset = 0;
  filter->next_size = 0;

  ret = metadata_parse (&filter->common.metadata, buf, size,
      &filter->next_offset, &filter->next_size);

  if (ret == META_PARSING_ERROR) {
    if (META_DATA_IMG_TYPE (filter->common.metadata) == IMG_NONE) {
      /* image type not recognized */
      GST_ELEMENT_ERROR (filter, STREAM, TYPE_NOT_FOUND, (NULL),
          ("Only jpeg and png are supported"));
      goto done;
    }
  } else if (ret == META_PARSING_NEED_MORE_DATA) {
    filter->need_more_data = TRUE;
  } else {
    filter->common.state = MT_STATE_PARSED;
    gst_metadata_common_calculate_offsets (&filter->common);
    filter->need_more_data = FALSE;
    filter->need_send_tag = TRUE;
  }

  /* reconfigure caps if it is different from type detected by 'metadata_demux' function */
  if (filter->img_type != META_DATA_IMG_TYPE (filter->common.metadata)) {
    filter->img_type = META_DATA_IMG_TYPE (filter->common.metadata);
    if (!gst_metadata_demux_configure_caps (filter)) {
      GST_ELEMENT_ERROR (filter, STREAM, FORMAT, (NULL),
          ("Couldn't reconfigure caps for %s",
              gst_metadata_common_get_type_name (filter->img_type)));
      ret = META_PARSING_ERROR;
      goto done;
    }
  }

done:

  return ret;

}

/* chain function
 * this function does the actual processing
 */

/* FIXME */
/* Current demux is just done before is pull mode could be activated */
/* may be it is possible to demux in chain mode by doing some trick with gst-adapter */
/* the pipeline below would be a test for that case */
/* gst-launch-0.10 filesrc location=Exif.jpg ! queue !  metadatademux ! filesink location=gen3.jpg */

static GstFlowReturn
gst_metadata_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstMetadataDemux *filter = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint32 buf_size = 0;
  guint32 new_buf_size = 0;
  gboolean append = FALSE;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  if (filter->common.state != MT_STATE_PARSED) {
    guint32 adpt_size = gst_adapter_available (filter->adapter_parsing);

    if (filter->next_offset) {
      if (filter->next_offset >= adpt_size) {
        /* clean adapter */
        gst_adapter_clear (filter->adapter_parsing);
        filter->next_offset -= adpt_size;
        if (filter->next_offset >= GST_BUFFER_SIZE (buf)) {
          /* we don't need data in this buffer */
          filter->next_offset -= GST_BUFFER_SIZE (buf);
        } else {
          GstBuffer *new_buf;

          /* add to adapter just need part from buf */
          new_buf =
              gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buf) -
              filter->next_offset);
          memcpy (GST_BUFFER_DATA (new_buf),
              GST_BUFFER_DATA (buf) + filter->next_offset,
              GST_BUFFER_SIZE (buf) - filter->next_offset);
          filter->next_offset = 0;
          gst_adapter_push (filter->adapter_parsing, new_buf);
        }
      } else {
        /* remove first bytes and add buffer */
        gst_adapter_flush (filter->adapter_parsing, filter->next_offset);
        filter->next_offset = 0;
        gst_adapter_push (filter->adapter_parsing, gst_buffer_copy (buf));
      }
    } else {
      /* just push buffer */
      gst_adapter_push (filter->adapter_parsing, gst_buffer_copy (buf));
    }

    adpt_size = gst_adapter_available (filter->adapter_parsing);

    if (adpt_size && filter->next_size <= adpt_size) {
      const guint8 *new_buf =
          gst_adapter_peek (filter->adapter_parsing, adpt_size);

      if (gst_metadata_demux_parse (filter, new_buf,
              adpt_size) == META_PARSING_ERROR) {
        ret = GST_FLOW_ERROR;
        goto done;
      }
    }
  }

  if (filter->common.state == MT_STATE_PARSED) {

    if (filter->adapter_holding) {
      gst_adapter_push (filter->adapter_holding, buf);
      buf = gst_adapter_take_buffer (filter->adapter_holding,
          gst_adapter_available (filter->adapter_holding));
      g_object_unref (filter->adapter_holding);
      filter->adapter_holding = NULL;
    }

    if (filter->need_send_tag) {
      gst_metadata_demux_send_tags (filter);
    }

    if (filter->offset_orig + GST_BUFFER_SIZE (buf) ==
        filter->common.duration_orig)
      append = TRUE;

    buf_size = GST_BUFFER_SIZE (buf);

    gst_metadata_common_strip_push_buffer (&filter->common, filter->offset_orig,
        &filter->prepend_buffer, &buf);

    if (buf) {                  /* may be all buffer has been striped */
      gst_buffer_set_caps (buf, GST_PAD_CAPS (filter->srcpad));
      new_buf_size = GST_BUFFER_SIZE (buf);

      ret = gst_pad_push (filter->srcpad, buf);
      buf = NULL;               /* this function don't owner it anymore */
      if (ret != GST_FLOW_OK)
        goto done;
    } else {
      ret = GST_FLOW_OK;
    }

    if (append && filter->common.append_buffer) {
      gst_buffer_set_caps (filter->common.append_buffer,
          GST_PAD_CAPS (filter->srcpad));
      gst_buffer_ref (filter->common.append_buffer);
      ret = gst_pad_push (filter->srcpad, filter->common.append_buffer);
      if (ret != GST_FLOW_OK)
        goto done;
    }

    filter->offset_orig += buf_size;
    filter->offset += new_buf_size;

  } else {
    /* just store while still not demuxd */
    if (!filter->adapter_holding)
      filter->adapter_holding = gst_adapter_new ();
    gst_adapter_push (filter->adapter_holding, buf);
    buf = NULL;
    ret = GST_FLOW_OK;
  }

done:


  if (buf) {
    /* there was an error and buffer wasn't pushed */
    gst_buffer_unref (buf);
    buf = NULL;
  }

  gst_object_unref (filter);

  return ret;

}

static gboolean
gst_metadata_demux_pull_range_demux (GstMetadataDemux * filter)
{

  int res;
  gboolean ret = TRUE;
  guint32 offset = 0;
  gint64 duration = 0;
  GstFormat format = GST_FORMAT_BYTES;

  if (!(ret =
          gst_pad_query_peer_duration (filter->sinkpad, &format, &duration))) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }
  filter->common.duration_orig = duration;

  if (format != GST_FORMAT_BYTES) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }

  do {
    GstFlowReturn flow;
    GstBuffer *buf = NULL;

    offset += filter->next_offset;

    /* 'filter->next_size' only says the minimum required number of bytes.
       We try provided more bytes (4096) just to avoid a lot of calls to 'metadata_demux'
       returning META_PARSING_NEED_MORE_DATA */
    if (filter->next_size < 4096) {
      if (duration - offset < 4096) {
        /* In case there is no 4096 bytes available upstream.
           It should be done upstream but we do here for safety */
        filter->next_size = duration - offset;
      } else {
        filter->next_size = 4096;
      }
    }

    flow =
        gst_pad_pull_range (filter->sinkpad, offset, filter->next_size, &buf);
    if (GST_FLOW_OK != flow) {
      ret = FALSE;
      goto done;
    }

    res =
        gst_metadata_demux_parse (filter, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    if (res == META_PARSING_ERROR) {
      ret = FALSE;
      goto done;
    }

    gst_buffer_unref (buf);

  } while (res == META_PARSING_NEED_MORE_DATA);

done:

  return ret;

}

static gboolean
gst_metadata_demux_sink_activate (GstPad * pad)
{
  GstMetadataDemux *filter = NULL;
  gboolean ret = TRUE;


  filter = GST_METADATA_DEMUX (GST_PAD_PARENT (pad));

  if (!gst_pad_check_pull_range (pad) ||
      !gst_pad_activate_pull (filter->sinkpad, TRUE)) {
    /* FIXME: currently it is not possible to demux in chain. Fail here ? */
    /* nothing to be done by now, activate push mode */
    return gst_pad_activate_push (pad, TRUE);
  }

  /* try to demux */
  if (filter->common.state == MT_STATE_NULL) {
    ret = gst_metadata_demux_pull_range_demux (filter);
  }

done:

  if (ret) {
    gst_pad_activate_pull (pad, FALSE);
    gst_pad_activate_push (filter->srcpad, FALSE);
    if (!gst_pad_is_active (pad)) {
      ret = gst_pad_activate_push (filter->srcpad, TRUE);
      ret = ret && gst_pad_activate_push (pad, TRUE);
    }
  }

  return ret;

}

static gboolean
gst_metadata_demux_checkgetrange (GstPad * srcpad)
{
  GstMetadataDemux *filter = NULL;

  filter = GST_METADATA_DEMUX (GST_PAD_PARENT (srcpad));

  return gst_pad_check_pull_range (filter->sinkpad);
}

static GstFlowReturn
gst_metadata_demux_get_range (GstPad * pad,
    guint64 offset, guint size, GstBuffer ** buf)
{
  GstMetadataDemux *filter = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 offset_orig = 0;
  guint size_orig;
  GstBuffer *prepend = NULL;
  gboolean need_append = FALSE;

  filter = GST_METADATA_DEMUX (GST_PAD_PARENT (pad));

  if (filter->common.state != MT_STATE_PARSED) {
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (offset + size > filter->common.duration) {
    size = filter->common.duration - offset;
  }

  size_orig = size;

  if (filter->need_send_tag) {
    gst_metadata_demux_send_tags (filter);
  }

  gst_metadata_common_translate_pos_to_orig (&filter->common, offset,
      &offset_orig, &prepend);

  if (size > 1) {
    gint64 pos;

    pos = offset + size - 1;
    gst_metadata_common_translate_pos_to_orig (&filter->common, pos, &pos,
        NULL);
    size_orig = pos + 1 - offset_orig;
  }

  if (size_orig) {

    ret = gst_pad_pull_range (filter->sinkpad, offset_orig, size_orig, buf);

    if (ret == GST_FLOW_OK && *buf) {
      gst_metadata_common_strip_push_buffer (&filter->common, offset_orig,
          &prepend, buf);

      if (GST_BUFFER_SIZE (*buf) < size) {
        /* need append */
        need_append = TRUE;
      }

    }
  } else {
    *buf = prepend;
  }

done:

  if (need_append) {
    /* FIXME: together with SEEK and
     * gst_metadata_common_translate_pos_to_orig
     * this way if chunk is added in the end we are in trolble
     * ...still not implemented 'cause it will not be the
     * case for the time being
     */
  }

  return ret;

}

static gboolean
gst_metadata_demux_src_activate_pull (GstPad * pad, gboolean active)
{
  GstMetadataDemux *filter = NULL;
  gboolean ret;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

  ret = gst_pad_activate_pull (filter->sinkpad, active);

  if (ret && filter->common.state == MT_STATE_NULL) {
    ret = gst_metadata_demux_pull_range_demux (filter);
  }

  gst_object_unref (filter);

  return ret;
}


static GstStateChangeReturn
gst_metadata_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstMetadataDemux *filter = GST_METADATA_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_metadata_demux_init_members (filter);
      filter->adapter_parsing = gst_adapter_new ();
      gst_metadata_common_init (&filter->common, TRUE, filter->options);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      filter->offset = 0;
      filter->offset_orig = 0;
      if (filter->adapter_parsing) {
        gst_adapter_clear (filter->adapter_parsing);
      }
      if (filter->adapter_holding) {
        gst_adapter_clear (filter->adapter_holding);
      }
      if (filter->common.state != MT_STATE_PARSED) {
        /* cleanup demuxr */
        gst_metadata_common_dispose (&filter->common);
        gst_metadata_common_init (&filter->common, TRUE, filter->options);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_metadata_demux_dispose_members (filter);
      break;
    default:
      break;
  }

done:

  return ret;
}

/*
 * element plugin init function
 */

gboolean
gst_metadata_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_metadata_demux_debug, "metadatademux", 0,
      "Metadata demuxer");

  return gst_element_register (plugin, "metadatademux",
      GST_RANK_PRIMARY + 1, GST_TYPE_METADATA_DEMUX);
}
