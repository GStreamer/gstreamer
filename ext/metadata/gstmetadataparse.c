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
 * SECTION:metadataparse-metadata
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadataparse ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstmetadataparse.h"


#include "metadataparseexif.h"

#include "metadataparseiptc.h"

#include "metadataparsexmp.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_metadata_parse_debug);
#define GST_CAT_DEFAULT gst_metadata_parse_debug

GST_DEBUG_CATEGORY_EXTERN (gst_metadata_parse_exif_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_metadata_parse_iptc_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_metadata_parse_xmp_debug);

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

GST_BOILERPLATE (GstMetadataParse, gst_metadata_parse, GstElement,
    GST_TYPE_ELEMENT);

static GstMetadataParseClass *metadata_parent_class = NULL;

static void gst_metadata_parse_dispose (GObject * object);

static void gst_metadata_parse_finalize (GObject * object);

static void gst_metadata_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_metadata_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_metadata_parse_change_state (GstElement * element,
    GstStateChange transition);

static GstCaps *gst_metadata_parse_get_caps (GstPad * pad);
static gboolean gst_metadata_parse_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_metadata_parse_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_metadata_parse_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_metadata_parse_chain (GstPad * pad, GstBuffer * buf);

static gboolean
gst_metadata_parse_get_range (GstPad * pad, guint64 offset, guint size,
    GstBuffer ** buf);

static gboolean gst_metadata_parse_activate (GstPad * pad);

static gboolean
gst_metadata_parse_element_activate_src_pull (GstPad * pad, gboolean active);

static gboolean gst_metadata_parse_pull_range_parse (GstMetadataParse * filter);

static void gst_metadata_parse_init_members (GstMetadataParse * filter);
static void gst_metadata_parse_dispose_members (GstMetadataParse * filter);

static gboolean
gst_metadata_parse_configure_srccaps (GstMetadataParse * filter);
static gboolean gst_metadata_parse_configure_caps (GstMetadataParse * filter);

static int
gst_metadata_parse_parse (GstMetadataParse * filter, const guint8 * buf,
    guint32 size);

static void gst_metadata_parse_send_tags (GstMetadataParse * filter);

static gboolean gst_metadata_parse_activate (GstPad * pad);

static gboolean
gst_metadata_parse_get_strip_range (gint64 * boffset, guint32 * bsize,
    const gint64 seg_offset, const guint32 seg_size,
    gint64 * toffset, guint32 * tsize, gint64 * ioffset);

static void
gst_metadata_parse_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "Metadata parser",
    "Parser/Extracter/Metadata",
    "Send metadata tags (EXIF, IPTC and XMP) while passing throught the contents",
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
gst_metadata_parse_class_init (GstMetadataParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  metadata_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_metadata_parse_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_metadata_parse_finalize);

  gobject_class->set_property = gst_metadata_parse_set_property;
  gobject_class->get_property = gst_metadata_parse_get_property;

  g_object_class_install_property (gobject_class, ARG_EXIF,
      g_param_spec_boolean ("exif", "EXIF", "Send EXIF metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_IPTC,
      g_param_spec_boolean ("iptc", "IPTC", "Send IPTC metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_XMP,
      g_param_spec_boolean ("xmp", "XMP", "Send XMP metadata ?",
          TRUE, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_metadata_parse_change_state;

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_metadata_parse_init (GstMetadataParse * filter,
    GstMetadataParseClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  /* sink pad */

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_parse_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_parse_get_caps));
  gst_pad_set_event_function (filter->sinkpad, gst_metadata_parse_sink_event);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_parse_chain));
  gst_pad_set_activate_function (filter->sinkpad, gst_metadata_parse_activate);

  /* source pad */

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_parse_get_caps));
  gst_pad_set_event_function (filter->srcpad, gst_metadata_parse_src_event);
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_pad_set_getrange_function (filter->srcpad, gst_metadata_parse_get_range);
  gst_pad_set_activatepull_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_metadata_parse_element_activate_src_pull));
  /* addind pads */

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  /* init members */

  gst_metadata_parse_init_members (filter);

}

static void
gst_metadata_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMetadataParse *filter = GST_METADATA_PARSE (object);

  switch (prop_id) {
    case ARG_EXIF:
      if (g_value_get_boolean (value))
        set_parse_option (filter->parse_data, PARSE_OPT_EXIF);
      else
        unset_parse_option (filter->parse_data, PARSE_OPT_EXIF);
      break;
    case ARG_IPTC:
      if (g_value_get_boolean (value))
        set_parse_option (filter->parse_data, PARSE_OPT_IPTC);
      else
        unset_parse_option (filter->parse_data, PARSE_OPT_IPTC);
      break;
    case ARG_XMP:
      if (g_value_get_boolean (value))
        set_parse_option (filter->parse_data, PARSE_OPT_XMP);
      else
        unset_parse_option (filter->parse_data, PARSE_OPT_XMP);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_metadata_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMetadataParse *filter = GST_METADATA_PARSE (object);

  switch (prop_id) {
    case ARG_EXIF:
      g_value_set_boolean (value,
          PARSE_DATA_OPTION (filter->parse_data) & PARSE_OPT_EXIF);
      break;
    case ARG_IPTC:
      g_value_set_boolean (value,
          PARSE_DATA_OPTION (filter->parse_data) & PARSE_OPT_IPTC);
      break;
    case ARG_XMP:
      g_value_set_boolean (value,
          PARSE_DATA_OPTION (filter->parse_data) & PARSE_OPT_XMP);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

static GstCaps *
gst_metadata_parse_get_caps (GstPad * pad)
{
  GstMetadataParse *filter = NULL;
  GstPad *otherpad;
  GstCaps *caps_new = NULL;
  GstCaps *caps_otherpad_peer = NULL;

  filter = GST_METADATA_PARSE (gst_pad_get_parent (pad));

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
gst_metadata_parse_src_event (GstPad * pad, GstEvent * event)
{
  GstMetadataParse *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_METADATA_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (filter->state != MT_STATE_PARSED) {
        /* FIXME: What to do here */
        ret = TRUE;
        goto done;
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
gst_metadata_parse_sink_event (GstPad * pad, GstEvent * event)
{
  GstMetadataParse *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_METADATA_PARSE (gst_pad_get_parent (pad));

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
gst_metadata_parse_dispose (GObject * object)
{
  GstMetadataParse *filter = NULL;

  filter = GST_METADATA_PARSE (object);

  gst_metadata_parse_dispose_members (filter);

  G_OBJECT_CLASS (metadata_parent_class)->dispose (object);
}

static void
gst_metadata_parse_finalize (GObject * object)
{
  G_OBJECT_CLASS (metadata_parent_class)->finalize (object);
}

static void
gst_metadata_parse_dispose_members (GstMetadataParse * filter)
{
  metadataparse_dispose (&filter->parse_data);
  if (filter->adapter) {
    gst_object_unref (filter->adapter);
    filter->adapter = NULL;
  }
  if (filter->taglist) {
    gst_tag_list_free (filter->taglist);
    filter->taglist = NULL;
  }
}

static void
gst_metadata_parse_init_members (GstMetadataParse * filter)
{
  filter->need_send_tag = FALSE;
  filter->exif = TRUE;
  filter->iptc = TRUE;
  filter->xmp = TRUE;

  memset (filter->seg_offset, 0x00, sizeof (filter->seg_offset[0]) * 3);
  memset (filter->seg_size, 0x00, sizeof (filter->seg_size[0]) * 3);

  memset (filter->seg_inject_data, 0x00,
      sizeof (filter->seg_inject_data[0]) * 3);
  memset (filter->seg_inject_size, 0x00,
      sizeof (filter->seg_inject_size[0]) * 3);

  filter->num_segs = 0;

  filter->taglist = NULL;
  filter->adapter = NULL;
  filter->next_offset = 0;
  filter->next_size = 0;
  filter->img_type = IMG_NONE;
  filter->duration = -1;
  filter->offset = 0;
  filter->state = MT_STATE_NULL;
  filter->need_more_data = FALSE;
  metadataparse_init (&filter->parse_data);
}

static gboolean
gst_metadata_parse_configure_srccaps (GstMetadataParse * filter)
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
gst_metadata_parse_configure_caps (GstMetadataParse * filter)
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
gst_metadata_parse_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMetadataParse *filter = NULL;
  GstStructure *structure = NULL;
  const gchar *mime = NULL;
  gboolean ret = FALSE;
  gboolean parsed = TRUE;

  filter = GST_METADATA_PARSE (gst_pad_get_parent (pad));

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

  if (gst_structure_get_boolean (structure, "tags-extracted", &parsed)) {
    if (parsed == TRUE) {
      ret = FALSE;
      goto done;
    }
  }

  ret = gst_metadata_parse_configure_srccaps (filter);

done:

  gst_object_unref (filter);

  return ret;
}

static const gchar *
gst_metadata_parse_get_type_name (int img_type)
{
  gchar *type_name = NULL;

  switch (img_type) {
    case IMG_JPEG:
      type_name = "jpeg";
      break;
    case IMG_PNG:
      type_name = "png";
      break;
    default:
      type_name = "invalid type";
      break;
  }
  return type_name;
}

static void
gst_metadata_parse_send_tags (GstMetadataParse * filter)
{

  GstMessage *msg;
  GstTagList *taglist;
  GstEvent *event;

  if (PARSE_DATA_OPTION (filter->parse_data) & PARSE_OPT_EXIF)
    metadataparse_exif_tag_list_add (filter->taglist, GST_TAG_MERGE_KEEP,
        filter->parse_data.exif.adapter);
  if (PARSE_DATA_OPTION (filter->parse_data) & PARSE_OPT_IPTC)
    metadataparse_iptc_tag_list_add (filter->taglist, GST_TAG_MERGE_KEEP,
        filter->parse_data.iptc.adapter);
  if (PARSE_DATA_OPTION (filter->parse_data) & PARSE_OPT_XMP)
    metadataparse_xmp_tag_list_add (filter->taglist, GST_TAG_MERGE_KEEP,
        filter->parse_data.xmp.adapter);

  if (!gst_tag_list_is_empty (filter->taglist)) {

    taglist = gst_tag_list_copy (filter->taglist);
    msg = gst_message_new_tag (GST_OBJECT (filter), taglist);
    gst_element_post_message (GST_ELEMENT (filter), msg);

    taglist = gst_tag_list_copy (filter->taglist);
    event = gst_event_new_tag (taglist);
    gst_pad_push_event (filter->srcpad, event);
  }



  filter->need_send_tag = FALSE;
}

/*
 * return:
 *   -1 -> error
 *    0 -> succeded
 *    1 -> need more data
 */

static int
gst_metadata_parse_parse (GstMetadataParse * filter, const guint8 * buf,
    guint32 size)
{

  int ret = -1;

  filter->next_offset = 0;
  filter->next_size = 0;

  ret = metadataparse_parse (&filter->parse_data, buf, size,
      &filter->next_offset, &filter->next_size);

  if (ret < 0) {
    if (PARSE_DATA_IMG_TYPE (filter->parse_data) == IMG_NONE) {
      /* image type not recognized */
      GST_ELEMENT_ERROR (filter, STREAM, TYPE_NOT_FOUND, (NULL),
          ("Only jpeg and png are supported"));
      goto done;
    }
  } else if (ret > 0) {
    filter->need_more_data = TRUE;
  } else {
    int i, j;

    filter->num_segs = 0;

    /* FIXME: better design for segments */

    filter->seg_size[0] = filter->parse_data.exif.size;
    filter->seg_offset[0] = filter->parse_data.exif.offset;
    filter->seg_inject_size[0] = filter->parse_data.exif.size_inject;
    filter->seg_inject_data[0] = filter->parse_data.exif.buffer;

    filter->seg_size[1] = filter->parse_data.iptc.size;
    filter->seg_offset[1] = filter->parse_data.iptc.offset;
    filter->seg_inject_size[1] = filter->parse_data.iptc.size_inject;
    filter->seg_inject_data[1] = filter->parse_data.iptc.buffer;

    filter->seg_size[2] = filter->parse_data.xmp.size;
    filter->seg_offset[2] = filter->parse_data.xmp.offset;
    filter->seg_inject_size[2] = filter->parse_data.xmp.size_inject;
    filter->seg_inject_data[2] = filter->parse_data.xmp.buffer;

    for (i = 0; i < 3; ++i) {
      if (filter->seg_size[i]) {
        filter->num_segs++;
      }
    }

    if (filter->num_segs) {

      for (i = 0; i < 3; ++i) {
        int j, min = i;

        for (j = i + 1; j < 3; ++j) {
          if (filter->seg_size[j])
            if (filter->seg_size[min] == 0
                || filter->seg_offset[j] < filter->seg_offset[min])
              min = j;
        }
        if (min != i) {
          gint64 aux_offset;
          guint32 aux_size;
          guint8 *aux_data;

          aux_offset = filter->seg_offset[i];
          aux_size = filter->seg_size[i];

          filter->seg_offset[i] = filter->seg_offset[min];
          filter->seg_size[i] = filter->seg_size[min];

          filter->seg_offset[min] = aux_offset;
          filter->seg_size[min] = aux_size;

          aux_size = filter->seg_inject_size[i];
          filter->seg_inject_size[i] = filter->seg_inject_size[min];
          filter->seg_inject_size[min] = aux_size;

          aux_data = filter->seg_inject_data[i];
          filter->seg_inject_data[i] = filter->seg_inject_data[min];
          filter->seg_inject_data[min] = aux_data;
        }
      }

    }

    filter->state = MT_STATE_PARSED;
    filter->need_more_data = FALSE;
    filter->need_send_tag = TRUE;
  }

  if (filter->img_type != PARSE_DATA_IMG_TYPE (filter->parse_data)) {
    filter->img_type = PARSE_DATA_IMG_TYPE (filter->parse_data);
    if (!gst_metadata_parse_configure_caps (filter)) {
      GST_ELEMENT_ERROR (filter, STREAM, FORMAT, (NULL),
          ("Couldn't reconfigure caps for %s",
              gst_metadata_parse_get_type_name (filter->img_type)));
      ret = -1;
      goto done;
    }
  }

done:

  return ret;

}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_metadata_parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstMetadataParse *filter = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint32 buf_size;

  filter = GST_METADATA_PARSE (gst_pad_get_parent (pad));

  /* commented until I figure out how to strip if it wasn't parsed yet */
#if 0
  if (filter->state != MT_STATE_PARSED) {
    guint32 adpt_size = gst_adapter_available (filter->adapter);

    if (filter->next_offset) {
      if (filter->next_offset >= adpt_size) {
        /* clean adapter */
        gst_adapter_clear (filter->adapter);
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
          memcpy (GST_BUFFER_DATA (new_buf), GST_BUFFER_DATA (buf),
              GST_BUFFER_SIZE (buf) - filter->next_offset);
          filter->next_offset = 0;
          gst_adapter_push (filter->adapter, new_buf);
        }
      } else {
        /* remove first bytes and add buffer */
        gst_adapter_flush (filter->adapter, filter->next_offset);
        filter->next_offset = 0;
        gst_adapter_push (filter->adapter, gst_buffer_copy (buf));
      }
    } else {
      /* just push buffer */
      gst_adapter_push (filter->adapter, gst_buffer_copy (buf));
    }

    adpt_size = gst_adapter_available (filter->adapter);

    if (adpt_size && filter->next_size <= adpt_size) {
      const guint8 *new_buf = gst_adapter_peek (filter->adapter, adpt_size);

      if (gst_metadata_parse_parse (filter, new_buf, adpt_size) < 0)
        goto done;
    }
  }
#endif

  if (filter->need_send_tag) {
    gst_metadata_parse_send_tags (filter);
  }

  buf_size = GST_BUFFER_SIZE (buf);
  gst_metadata_parse_strip_buffer (filter, &buf);
  filter->offset += buf_size;

  if (buf) {                    /* may be all buffer has been striped */
    gst_buffer_set_caps (buf, GST_PAD_CAPS (filter->srcpad));

    ret = gst_pad_push (filter->srcpad, buf);
    buf = NULL;                 /* this function don't owner it anymore */
  } else {
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
gst_metadata_parse_pull_range_parse (GstMetadataParse * filter)
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
  if (format != GST_FORMAT_BYTES) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }

  do {
    GstFlowReturn flow;
    GstBuffer *buf = NULL;

    offset += filter->next_offset;

    if (filter->next_size < 4096) {
      if (duration - offset < 4096) {
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
        gst_metadata_parse_parse (filter, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    if (res < 0) {
      ret = FALSE;
      goto done;
    }

    gst_buffer_unref (buf);

  } while (res > 0);

done:

  return ret;

}

static gboolean
gst_metadata_parse_activate (GstPad * pad)
{
  GstMetadataParse *filter = NULL;
  gboolean ret = TRUE;


  filter = GST_METADATA_PARSE (GST_PAD_PARENT (pad));

  if (!gst_pad_check_pull_range (pad) ||
      !gst_pad_activate_pull (filter->sinkpad, TRUE)) {
    /* nothing to be done by now, activate push mode */
    return gst_pad_activate_push (pad, TRUE);
  }

  /* try to parse */
  if (filter->state == MT_STATE_NULL) {
    ret = gst_metadata_parse_pull_range_parse (filter);
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

/*
 * Returns FALSE if nothing has stripped, TRUE if otherwise
 */

gboolean
gst_metadata_parse_get_strip_range (gint64 * boffset, guint32 * bsize,
    const gint64 seg_offset, const guint32 seg_size,
    gint64 * toffset, guint32 * tsize, gint64 * ioffset)
{

  gboolean ret = FALSE;

  if (seg_size == 0 || *bsize == 0)
    goto done;

  /* segment after buffer */
  if (seg_offset > *boffset + *bsize) {
    *bsize = 0;
    goto done;
  }

  if (seg_offset < *boffset) {
    /* segment start somewhere before buffer */
    guint32 cut_size;

    /* all segment before buffer */
    if (seg_offset + seg_size <= *boffset) {
      *tsize = *bsize;
      *bsize = 0;
      *toffset = *boffset;
      goto done;
    }

    cut_size = seg_size - (*boffset - seg_offset);

    if (cut_size >= *bsize) {
      /* all buffer striped out */
      *bsize = 0;
      *tsize = 0;
    } else {
      /* just beginning buffers striped */
      *toffset = *boffset + cut_size;
      *tsize = *bsize - cut_size;
      *bsize = 0;
    }

  } else {
    /* segment start somewhere into buffer */
    *ioffset = seg_offset;

    if (seg_offset + seg_size >= *boffset + *bsize) {
      /* strip just tail */
      *bsize = seg_offset - *boffset;
    } else {
      /* strip the middle */
      *toffset = seg_offset + seg_size;
      *tsize = *boffset + *bsize - seg_offset - seg_size;
      *bsize = seg_offset - *boffset;
    }

  }

  ret = TRUE;

done:

  return ret;

}

/*
 *  TRUE -> buffer striped (or injeted)
 *  FALSE -> buffer unmodified
 */

gboolean
gst_metadata_parse_strip_buffer (GstMetadataParse * filter, GstBuffer ** buf)
{

  gint64 boffset[3];
  guint32 bsize[3];

  gint64 toffset[3];
  guint32 tsize[3];

  gint64 ioffset[3];

  const gint64 *seg_offset = filter->seg_offset;

  const guint32 *seg_size = filter->seg_size;

  int i;
  gboolean ret = FALSE;
  guint32 new_size = 0;
  const gint64 offset = filter->offset;

  if (filter->num_segs == 0)
    goto done;

  memset (bsize, 0x00, sizeof (bsize));
  memset (tsize, 0x00, sizeof (tsize));

  for (i = 0; i < filter->num_segs; ++i) {
    ioffset[i] = -1;
  }

  boffset[0] = offset;
  new_size = bsize[0] = GST_BUFFER_SIZE (*buf);
  i = 0;

  for (i = 0; i < filter->num_segs; ++i) {
    guint32 striped_size;

    striped_size = bsize[i];
    if (gst_metadata_parse_get_strip_range (&boffset[i], &bsize[i],
            seg_offset[i], seg_size[i], &toffset[i], &tsize[i], &ioffset[i])) {
      ret = TRUE;
      new_size -= (striped_size - (bsize[i] + tsize[i]));
    }
    if (i + 1 < filter->num_segs) {
      if (tsize[i]) {
        boffset[i + 1] = toffset[i];
        bsize[i + 1] = GST_BUFFER_SIZE (*buf) - (toffset[i] - offset);
        tsize[i] = 0;
      } else {
        /* buffer already consumed */
        break;
      }
    }
    if (new_size == 0) {
      /* all buffer striped already */
      break;
    }

  }

  /* segments must be sorted by offset to make this copy */
  if (ret) {
    if (new_size == 0) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    } else {
      GstBuffer *new_buf;
      guint8 *data;

      for (i = 0; i < filter->num_segs; ++i) {
        if (filter->seg_inject_size[i] == 0)
          ioffset[i] = -1;
        else
          new_size += filter->seg_inject_size[i];
      }

      if (GST_BUFFER_SIZE (new_buf) < new_size) {
        /* FIXME: alloc more memory here */
      }

      /* FIXME: try to use buffer data in place */
      new_buf = gst_buffer_copy (*buf);
      data = GST_BUFFER_DATA (new_buf);

      GST_BUFFER_SIZE (new_buf) = new_size;

      for (i = 0; i < filter->num_segs; ++i) {
        if (bsize[i]) {
          memcpy (data,
              GST_BUFFER_DATA (*buf) + (boffset[i] - offset), bsize[i]);
          data += bsize[i];
        }
        if (ioffset[i] >= 0) {
          memcpy (data, filter->seg_inject_data[i], filter->seg_inject_size[i]);
          data += filter->seg_inject_size[i];
        }
        if (tsize[i]) {
          memcpy (data,
              GST_BUFFER_DATA (*buf) + (toffset[i] - offset), tsize[i]);
          data += tsize[i];
        }
      }
      gst_buffer_unref (*buf);
      *buf = new_buf;
      GST_BUFFER_FLAG_UNSET (*buf, GST_BUFFER_FLAG_READONLY);
    }
  }


done:

  return ret;

}

static gboolean
gst_metadata_parse_get_range (GstPad * pad,
    guint64 offset, guint size, GstBuffer ** buf)
{
  GstMetadataParse *filter = NULL;

  filter = GST_METADATA_PARSE (GST_PAD_PARENT (pad));

  if (filter->need_send_tag) {
    gst_metadata_parse_send_tags (filter);
  }

  return gst_pad_pull_range (filter->sinkpad, offset, size, buf);

}

static gboolean
gst_metadata_parse_element_activate_src_pull (GstPad * pad, gboolean active)
{
  GstMetadataParse *filter = NULL;
  gboolean ret;

  filter = GST_METADATA_PARSE (gst_pad_get_parent (pad));

  ret = gst_pad_activate_pull (filter->sinkpad, active);

  if (ret && filter->state == MT_STATE_NULL) {
    ret = gst_metadata_parse_pull_range_parse (filter);
  }

  gst_object_unref (filter);

  return ret;
}


static GstStateChangeReturn
gst_metadata_parse_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstMetadataParse *filter = GST_METADATA_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_metadata_parse_init_members (filter);
      filter->adapter = gst_adapter_new ();
      filter->taglist = gst_tag_list_new ();
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_metadata_parse_dispose_members (filter);
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
gst_metadata_parse_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_metadata_parse_debug, "metadataparse", 0,
      "Metadata demuxer");

  GST_DEBUG_CATEGORY_INIT (gst_metadata_parse_exif_debug, "metadataparse_exif",
      0, "Metadata exif demuxer");
  GST_DEBUG_CATEGORY_INIT (gst_metadata_parse_iptc_debug, "metadataparse_iptc",
      0, "Metadata iptc demuxer");
  GST_DEBUG_CATEGORY_INIT (gst_metadata_parse_xmp_debug, "metadataparse_xmp", 0,
      "Metadata xmp demuxer");

  /* FIXME: register tag should be done by plugin 'cause muxer element also uses it */
  metadataparse_exif_tags_register ();

  metadataparse_iptc_tags_register ();

  metadataparse_xmp_tags_register ();

  return gst_element_register (plugin, "metadataparse",
      GST_RANK_PRIMARY + 1, GST_TYPE_METADATA_PARSE);
}
