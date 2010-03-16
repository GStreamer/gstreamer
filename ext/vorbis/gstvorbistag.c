/*
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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
 * SECTION:element-vorbistag
 * @see_also: #oggdemux, #oggmux, #vorbisparse, #GstTagSetter
 *
 * The vorbistags element can change the tag contained within a raw
 * vorbis stream. Specifically, it modifies the comments header packet
 * of the vorbis stream.
 *
 * The element will also process the stream as the #vorbisparse element does
 * so it can be used when remuxing an Ogg Vorbis stream, without additional
 * elements.
 *
 * Applications can set the tags to write using the #GstTagSetter interface.
 * Tags contained withing the vorbis bitstream will be picked up
 * automatically (and merged according to the merge mode set via the tag
 * setter interface).
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=foo.ogg ! oggdemux ! vorbistag ! oggmux ! filesink location=bar.ogg
 * ]| This element is not useful with gst-launch, because it does not support
 * setting the tags on a #GstTagSetter interface. Conceptually, the element
 * will usually be used in this order though.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#include <gst/tag/tag.h>
#include <gst/gsttagsetter.h>

#include <vorbis/codec.h>

#include "gstvorbistag.h"


GST_DEBUG_CATEGORY_EXTERN (vorbisparse_debug);
#define GST_CAT_DEFAULT vorbisparse_debug

static GstFlowReturn gst_vorbis_tag_parse_packet (GstVorbisParse * parse,
    GstBuffer * buffer);

#define _do_init(type)                                                          \
  G_STMT_START{                                                                 \
    static const GInterfaceInfo tag_setter_info = {                             \
      NULL,                                                                     \
      NULL,                                                                     \
      NULL                                                                      \
    };                                                                          \
    g_type_add_interface_static (type, GST_TYPE_TAG_SETTER,                     \
                                 &tag_setter_info);                             \
  }G_STMT_END

GST_BOILERPLATE_FULL (GstVorbisTag, gst_vorbis_tag, GstVorbisParse,
    GST_TYPE_VORBIS_PARSE, _do_init);


static void
gst_vorbis_tag_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "VorbisTag", "Formatter/Metadata",
      "Retags vorbis streams", "James Livingston <doclivingston@gmail.com>");
}

static void
gst_vorbis_tag_class_init (GstVorbisTagClass * klass)
{
  GstVorbisParseClass *vorbisparse_class = GST_VORBIS_PARSE_CLASS (klass);

  vorbisparse_class->parse_packet = gst_vorbis_tag_parse_packet;
}

static void
gst_vorbis_tag_init (GstVorbisTag * tagger, GstVorbisTagClass * g_class)
{
  /* nothing to do */
}


static GstFlowReturn
gst_vorbis_tag_parse_packet (GstVorbisParse * parse, GstBuffer * buffer)
{
  GstTagList *old_tags, *new_tags;
  const GstTagList *user_tags;
  GstVorbisTag *tagger;
  gchar *encoder = NULL;
  GstBuffer *new_buf;

  /* just pass everything except the comments packet */
  if (GST_BUFFER_SIZE (buffer) >= 1 && GST_BUFFER_DATA (buffer)[0] != 0x03) {
    return GST_VORBIS_PARSE_CLASS (parent_class)->parse_packet (parse, buffer);
  }

  tagger = GST_VORBIS_TAG (parse);

  old_tags =
      gst_tag_list_from_vorbiscomment_buffer (buffer, (guint8 *) "\003vorbis",
      7, &encoder);
  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (tagger));

  /* build new tag list */
  new_tags = gst_tag_list_merge (user_tags, old_tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (tagger)));
  gst_tag_list_free (old_tags);

  new_buf =
      gst_tag_list_to_vorbiscomment_buffer (new_tags, (guint8 *) "\003vorbis",
      7, encoder);
  gst_buffer_copy_metadata (new_buf, buffer, GST_BUFFER_COPY_TIMESTAMPS);

  gst_tag_list_free (new_tags);
  g_free (encoder);
  gst_buffer_unref (buffer);

  return GST_VORBIS_PARSE_CLASS (parent_class)->parse_packet (parse, new_buf);
}
