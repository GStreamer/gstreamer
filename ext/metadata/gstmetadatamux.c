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
 * SECTION:metadatamux-metadata
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatamux ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstmetadatamux.h"

#include "metadataexif.h"

#include "metadataiptc.h"

#include "metadataxmp.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_metadata_mux_debug);
#define GST_CAT_DEFAULT gst_metadata_mux_debug

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
  ARG_PARSE_ONLY
};


static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, tags-extracted = (bool) true;"
        "image/png,  tags-extracted = (bool) true")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg; " "image/png")
    );

static void
gst_metadata_mux_add_interfaces (GType type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };

  g_type_add_interface_static (type, GST_TYPE_TAG_SETTER, &tag_setter_info);
}


GST_BOILERPLATE_FULL (GstMetadataMux, gst_metadata_mux, GstBaseMetadata,
    GST_TYPE_BASE_METADATA, gst_metadata_mux_add_interfaces);

static GstMetadataMuxClass *metadata_parent_class = NULL;

static void gst_metadata_mux_dispose (GObject * object);

static void gst_metadata_mux_finalize (GObject * object);

static void gst_metadata_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_metadata_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_metadata_mux_get_caps (GstPad * pad);
static gboolean gst_metadata_mux_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_metadata_mux_sink_event (GstPad * pad, GstEvent * event);

static void gst_metadata_mux_create_chunks_from_tags (GstBaseMetadata * base);

static void
gst_metadata_mux_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "Metadata muxr",
    "Muxr/Extracter/Metadata",
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
gst_metadata_mux_class_init (GstMetadataMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseMetadataClass *gstbasemetadata_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasemetadata_class = (GstBaseMetadataClass *) klass;

  metadata_parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_metadata_mux_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_metadata_mux_finalize);

  gobject_class->set_property = gst_metadata_mux_set_property;
  gobject_class->get_property = gst_metadata_mux_get_property;

  g_object_class_install_property (gobject_class, ARG_PARSE_ONLY,
      g_param_spec_boolean ("parse-only", "parse-only",
          "If TRUE, don't strip out any chunk", FALSE, G_PARAM_READWRITE));

  gstbasemetadata_class->processing =
      GST_DEBUG_FUNCPTR (gst_metadata_mux_create_chunks_from_tags);
  gstbasemetadata_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_metadata_mux_set_caps);
  gstbasemetadata_class->get_sink_caps =
      GST_DEBUG_FUNCPTR (gst_metadata_mux_get_caps);
  gstbasemetadata_class->get_src_caps =
      GST_DEBUG_FUNCPTR (gst_metadata_mux_get_caps);
  gstbasemetadata_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_metadata_mux_sink_event);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_metadata_mux_init (GstMetadataMux * filter, GstMetadataMuxClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  gst_base_metadata_set_option_flag (GST_BASE_METADATA (filter),
      META_OPT_EXIF | META_OPT_IPTC | META_OPT_XMP | META_OPT_MUX);

}

static void
gst_metadata_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMetadataMux *filter = GST_METADATA_MUX (object);

  switch (prop_id) {
    case ARG_PARSE_ONLY:
      if (g_value_get_boolean (value))
        gst_base_metadata_set_option_flag (GST_BASE_METADATA (object),
            META_OPT_PARSE_ONLY);
      else
        gst_base_metadata_unset_option_flag (GST_BASE_METADATA (object),
            META_OPT_PARSE_ONLY);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_metadata_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  guint8 option =
      gst_base_metadata_get_option_flag (GST_BASE_METADATA (object));

  switch (prop_id) {
    case ARG_PARSE_ONLY:
      g_value_set_boolean (value, option & META_OPT_PARSE_ONLY);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}



static void
gst_metadata_mux_dispose (GObject * object)
{
  GstMetadataMux *filter = NULL;

  G_OBJECT_CLASS (metadata_parent_class)->dispose (object);
}

static void
gst_metadata_mux_finalize (GObject * object)
{
  G_OBJECT_CLASS (metadata_parent_class)->finalize (object);
}

static void
gst_metadata_mux_create_chunks_from_tags (GstBaseMetadata * base)
{

  GstMetadataMux *filter = GST_METADATA_MUX (base);
  GstMessage *msg;
  GstTagSetter *setter = GST_TAG_SETTER (filter);
  const GstTagList *taglist = gst_tag_setter_get_tag_list (setter);
  GstEvent *event;
  guint8 *buf = NULL;
  guint32 size = 0;

  if (taglist) {

    if (gst_base_metadata_get_option_flag (base) & META_OPT_EXIF) {
      metadatamux_exif_create_chunk_from_tag_list (&buf, &size, taglist);
      gst_base_metadata_update_segment_with_new_buffer (base, &buf, &size,
          MD_CHUNK_EXIF);
    }

    if (gst_base_metadata_get_option_flag (base) & META_OPT_IPTC) {
      metadatamux_iptc_create_chunk_from_tag_list (&buf, &size, taglist);
      gst_base_metadata_update_segment_with_new_buffer (base, &buf, &size,
          MD_CHUNK_IPTC);
    }

    if (gst_base_metadata_get_option_flag (base) & META_OPT_XMP) {
      metadatamux_xmp_create_chunk_from_tag_list (&buf, &size, taglist);
      gst_base_metadata_update_segment_with_new_buffer (base, &buf, &size,
          MD_CHUNK_XMP);
    }

  }

  if (buf) {
    g_free (buf);
  }

  gst_base_metadata_chunk_array_remove_zero_size (base);

}

static gboolean
gst_metadata_mux_configure_srccaps (GstMetadataMux * filter)
{
  GstCaps *caps = NULL;
  gboolean ret = FALSE;
  gchar *mime = NULL;

  switch (GST_BASE_METADATA_IMG_TYPE (filter)) {
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

  caps = gst_caps_new_simple (mime, NULL);

  ret = gst_pad_set_caps (GST_BASE_METADATA_SRC_PAD (filter), caps);

done:

  if (caps) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  return ret;

}


/* this function handles the link with other elements */
static gboolean
gst_metadata_mux_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMetadataMux *filter = NULL;
  GstStructure *structure = NULL;
  const gchar *mime = NULL;
  gboolean ret = FALSE;
  gboolean based = TRUE;

  filter = GST_METADATA_MUX (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  mime = gst_structure_get_name (structure);

  if (strcmp (mime, "image/jpeg") == 0) {
    GST_BASE_METADATA_IMG_TYPE (filter) = IMG_JPEG;
  } else if (strcmp (mime, "image/png") == 0) {
    GST_BASE_METADATA_IMG_TYPE (filter) = IMG_PNG;
  } else {
    ret = FALSE;
    goto done;
  }

  if (gst_structure_get_boolean (structure, "tags-extracted", &based)) {
    if (based == FALSE) {
      ret = FALSE;
      goto done;
    }
  }

  ret = gst_metadata_mux_configure_srccaps (filter);

done:

  gst_object_unref (filter);

  return ret;
}

static GstCaps *
gst_metadata_mux_get_caps (GstPad * pad)
{
  GstMetadataMux *filter = NULL;
  GstPad *otherpad;
  GstCaps *caps_new = NULL;
  GstCaps *caps_otherpad_peer = NULL;

  filter = GST_METADATA_MUX (gst_pad_get_parent (pad));

  (GST_BASE_METADATA_SRC_PAD (filter) == pad) ?
      (otherpad = GST_BASE_METADATA_SINK_PAD (filter)) :
      (otherpad = GST_BASE_METADATA_SRC_PAD (filter));

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

      if (pad == GST_BASE_METADATA_SINK_PAD (filter)) {
        structure_new =
            gst_structure_new (mime, "tags-extracted", G_TYPE_BOOLEAN, TRUE,
            NULL);
      } else {
        structure_new = gst_structure_new (mime, NULL);
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
gst_metadata_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstMetadataMux *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_METADATA_MUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = NULL;
      GstTagSetter *setter = GST_TAG_SETTER (filter);

      gst_event_parse_tag (event, &taglist);
      gst_tag_setter_merge_tags (setter, taglist, GST_TAG_MERGE_REPLACE);

    }
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);

  gst_object_unref (filter);

  return ret;

}

/*
 * element plugin init function
 */

gboolean
gst_metadata_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_metadata_mux_debug, "metadatamux", 0,
      "Metadata muxer");

  return gst_element_register (plugin, "metadatamux",
      GST_RANK_NONE, GST_TYPE_METADATA_MUX);
}
