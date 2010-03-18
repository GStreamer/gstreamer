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
 * SECTION: element-metadatademux
 * @see_also: #metadatamux
 *
 * <refsect2>
 * <para>
 * This element parses image files JPEG and PNG, to find metadata chunks (EXIF,
 * IPTC, XMP) in it, and then send individual tags as a 'tag message' do the
 * application and as 'tag event' to the next element in pipeline. It also
 * strips out the metadata chunks from original stream (unless the 'parse-only'
 * property is set to 'true'). In addition the whole metadata chunk (striped
 * or not) it also sent as a message to the application bus, so the application
 * can have more controls about the metadata.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatademux ! fakesink
 * silent=TRUE
 * </programlisting>
 * <programlisting>
 * GST_DEBUG:*metadata:5 gst-launch filesrc location=./test.jpeg ! 
 * metadatademux ! fakesink
 * </programlisting>
 * </para>
 * <title>Application sample code using 'libexif' to have more control</title>
 * <para>
 * <programlisting>
 * val = gst_tag_list_get_value_index (taglist, GST_TAG_EXIF, 0);
 * if (val) {
 *  exif_chunk = gst_value_get_buffer (val);
 *  if (exif_chunk) {
 *    ed = exif_data_new_from_data (GST_BUFFER_DATA (exif_chunk),
 *        GST_BUFFER_SIZE (exif_chunk));
 *  }
 * }
 * </programlisting>
 * This same idea can be used to handle IPTC and XMP directly by using
 * libdata and exempi (or any other libraries). Notice: the whole metadata
 * chunk sent as a message to the application contains only metadata data, i.e.
 * the wrapper specific to the file format (JPEG, PNG, ...) is already
 * striped out.
 * </para>
 * </refsect2>
 */


/*
 * includes
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


/*
 * enum and types
 */

enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PARSE_ONLY
};

/*
 * defines and static global vars
 */


GST_DEBUG_CATEGORY (gst_metadata_demux_debug);
#define GST_CAT_DEFAULT gst_metadata_demux_debug

#define GOTO_DONE_IF_NULL(ptr) \
    do { if ( NULL == (ptr) ) goto done; } while(FALSE)
#define GOTO_DONE_IF_NULL_AND_FAIL(ptr, ret) \
    do { if ( NULL == (ptr) ) { (ret) = FALSE; goto done; } } while(FALSE)

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

static GstMetadataDemuxClass *metadata_parent_class = NULL;

/*
 * static helper functions declaration
 */

static gboolean
gst_metadata_demux_configure_srccaps (GstMetadataDemux * filter);

/*
 * GObject callback functions declaration
 */

static void gst_metadata_demux_base_init (gpointer gclass);

static void gst_metadata_demux_class_init (GstMetadataDemuxClass * klass);

static void
gst_metadata_demux_init (GstMetadataDemux * filter,
    GstMetadataDemuxClass * gclass);

static void gst_metadata_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_metadata_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/*
 * GstBaseMetadata virtual functions declaration
 */

static void gst_metadata_demux_send_tags (GstBaseMetadata * base);

static gboolean gst_metadata_demux_set_caps (GstPad * pad, GstCaps * caps);

static GstCaps *gst_metadata_demux_get_caps (GstPad * pad);

static gboolean gst_metadata_demux_sink_event (GstPad * pad, GstEvent * event);


/*
 * GST BOILERPLATE
 */

GST_BOILERPLATE (GstMetadataDemux, gst_metadata_demux, GstBaseMetadata,
    GST_TYPE_BASE_METADATA);

/*
 * static helper functions implementation
 */

static gboolean
gst_metadata_demux_configure_srccaps (GstMetadataDemux * filter)
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

  caps =
      gst_caps_new_simple (mime, "tags-extracted", G_TYPE_BOOLEAN, TRUE, NULL);

  ret = gst_pad_set_caps (GST_BASE_METADATA_SRC_PAD (filter), caps);

done:

  if (caps) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  return ret;

}

/*
 * GObject callback functions implementation
 */

static void
gst_metadata_demux_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class, "Metadata demuxer",
      "Demuxer/Extracter/Metadata",
      "Send metadata tags (EXIF, IPTC and XMP) and "
      "remove metadata chunks from stream",
      "Edgard Lima <edgard.lima@indt.org.br>");
}

static void
gst_metadata_demux_class_init (GstMetadataDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseMetadataClass *gstbasemetadata_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasemetadata_class = (GstBaseMetadataClass *) klass;

  metadata_parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_metadata_demux_set_property;
  gobject_class->get_property = gst_metadata_demux_get_property;

  g_object_class_install_property (gobject_class, ARG_PARSE_ONLY,
      g_param_spec_boolean ("parse-only", "parse-only",
          "If TRUE, don't strip out any chunk", FALSE, G_PARAM_READWRITE));

  gstbasemetadata_class->processing =
      GST_DEBUG_FUNCPTR (gst_metadata_demux_send_tags);
  gstbasemetadata_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_metadata_demux_set_caps);
  gstbasemetadata_class->get_sink_caps =
      GST_DEBUG_FUNCPTR (gst_metadata_demux_get_caps);
  gstbasemetadata_class->get_src_caps =
      GST_DEBUG_FUNCPTR (gst_metadata_demux_get_caps);
  gstbasemetadata_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_metadata_demux_sink_event);

}

static void
gst_metadata_demux_init (GstMetadataDemux * filter,
    GstMetadataDemuxClass * gclass)
{
  gst_base_metadata_set_option_flag (GST_BASE_METADATA (filter),
      META_OPT_EXIF | META_OPT_IPTC | META_OPT_XMP | META_OPT_DEMUX);
}

static void
gst_metadata_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
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
gst_metadata_demux_get_property (GObject * object, guint prop_id,
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

/*
 * GstBaseMetadata virtual functions implementation
 */

/*
 * gst_metadata_demux_send_tags:
 * @base: the base metadata instance
 * 
 * Send individual tags as message to the bus and as event to the next
 * element, and send the whole metadata chunk (with file specific wrapper
 * striped) to the next element as a event.
 *
 * Returns: nothing
 *
 */

static void
gst_metadata_demux_send_tags (GstBaseMetadata * base)
{

  GstMetadataDemux *filter = GST_METADATA_DEMUX (base);
  GstMessage *msg;
  GstTagList *taglist = gst_tag_list_new ();
  GstEvent *event;
  GstPad *srcpad = GST_BASE_METADATA_SRC_PAD (filter);

  /* get whole chunk */

  if (gst_base_metadata_get_option_flag (base) & META_OPT_EXIF)
    metadataparse_exif_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        GST_BASE_METADATA_EXIF_ADAPTER (base), METADATA_TAG_MAP_WHOLECHUNK);
  if (gst_base_metadata_get_option_flag (base) & META_OPT_IPTC)
    metadataparse_iptc_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        GST_BASE_METADATA_IPTC_ADAPTER (base), METADATA_TAG_MAP_WHOLECHUNK);
  if (gst_base_metadata_get_option_flag (base) & META_OPT_XMP)
    metadataparse_xmp_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        GST_BASE_METADATA_XMP_ADAPTER (base), METADATA_TAG_MAP_WHOLECHUNK);

  if (taglist && !gst_tag_list_is_empty (taglist)) {

    msg =
        gst_message_new_tag (GST_OBJECT (filter), gst_tag_list_copy (taglist));
    gst_element_post_message (GST_ELEMENT (filter), msg);

    event = gst_event_new_tag (taglist);
    gst_pad_push_event (srcpad, event);
    taglist = NULL;
  }

  if (!taglist)
    taglist = gst_tag_list_new ();

  /*get individual tags */

  if (gst_base_metadata_get_option_flag (base) & META_OPT_EXIF)
    metadataparse_exif_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        GST_BASE_METADATA_EXIF_ADAPTER (base), METADATA_TAG_MAP_INDIVIDUALS);
  if (gst_base_metadata_get_option_flag (base) & META_OPT_IPTC)
    metadataparse_iptc_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        GST_BASE_METADATA_IPTC_ADAPTER (base), METADATA_TAG_MAP_INDIVIDUALS);
  if (gst_base_metadata_get_option_flag (base) & META_OPT_XMP)
    metadataparse_xmp_tag_list_add (taglist, GST_TAG_MERGE_KEEP,
        GST_BASE_METADATA_XMP_ADAPTER (base), METADATA_TAG_MAP_INDIVIDUALS);

  if (taglist && !gst_tag_list_is_empty (taglist)) {

    msg = gst_message_new_tag (GST_OBJECT (filter), taglist);
    gst_element_post_message (GST_ELEMENT (filter), msg);
    taglist = NULL;
  }

  if (taglist)
    gst_tag_list_free (taglist);

}

static gboolean
gst_metadata_demux_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMetadataDemux *filter = NULL;
  GstStructure *structure = NULL;
  const gchar *mime = NULL;
  gboolean ret = FALSE;
  gboolean based = TRUE;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

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
    if (based == TRUE) {
      ret = FALSE;
      goto done;
    }
  }

  ret = gst_metadata_demux_configure_srccaps (filter);

done:

  gst_object_unref (filter);

  return ret;
}

static GstCaps *
gst_metadata_demux_get_caps (GstPad * pad)
{
  GstMetadataDemux *filter = NULL;
  GstPad *otherpad;
  GstCaps *caps_new = NULL;
  GstCaps *caps_otherpad_peer = NULL;

  filter = GST_METADATA_DEMUX (gst_pad_get_parent (pad));

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
gst_metadata_demux_sink_event (GstPad * pad, GstEvent * event)
{
  return gst_pad_event_default (pad, event);
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
      GST_RANK_NONE, GST_TYPE_METADATA_DEMUX);
}
