/*
 * GStreamer
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
 * Copyright (C) 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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
 * SECTION:element-katetag
 * @see_also: #oggdemux, #oggmux, #kateparse, #GstTagSetter
 * @short_description: retags kate streams
 *
 * <refsect2>
 * <para>
 * The katetag element can change the tag contained within a raw
 * kate stream. Specifically, it modifies the comments header packet
 * of the kate stream, as well as the language and category of the
 * kate stream.
 * </para>
 * <para>
 * The element will also process the stream as the #kateparse element does
 * so it can be used when remuxing an Ogg Kate stream, without additional
 * elements.
 * </para>
 * <para>
 * Applications can set the tags to write using the #GstTagSetter interface.
 * Tags contained within the kate stream will be picked up
 * automatically (and merged according to the merge mode set via the tag
 * setter interface).
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * This element is only useful with gst-launch for modifying the language
 * and/or category (which are properties of the stream located in the kate
 * beginning of stream header), because it does not support setting the tags
 * on a #GstTagSetter interface. Conceptually, the element will usually be
 * used like:
 * <programlisting>
 * gst-launch -v filesrc location=foo.ogg ! oggdemux ! katetag ! oggmux ! filesink location=bar.ogg
 * </programlisting>
 * </para>
 * <para>
 * This pipeline will set the language and category of the stream to the
 * given values:
 * <programlisting>
 * gst-launch -v filesrc location=foo.ogg ! oggdemux ! katetag language=pt_BR category=subtitles ! oggmux ! filesink location=bar.ogg
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <glib.h>
#include <gst/tag/tag.h>
#include <gst/gsttagsetter.h>

#include <kate/kate.h>

#include "gstkatetag.h"


GST_DEBUG_CATEGORY_EXTERN (gst_katetag_debug);
#define GST_CAT_DEFAULT gst_katetag_debug

enum
{
  ARG_0,
  ARG_LANGUAGE,
  ARG_CATEGORY,
  ARG_ORIGINAL_CANVAS_WIDTH,
  ARG_ORIGINAL_CANVAS_HEIGHT,
};

static GstFlowReturn gst_kate_tag_parse_packet (GstKateParse * parse,
    GstBuffer * buffer);
static void gst_kate_tag_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_kate_tag_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_kate_tag_dispose (GObject * object);

#define gst_kate_tag_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstKateTag, gst_kate_tag, GST_TYPE_KATE_PARSE,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL));

static void
gst_kate_tag_class_init (GstKateTagClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstKateParseClass *gstkateparse_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstkateparse_class = GST_KATE_PARSE_CLASS (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_kate_tag_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_kate_tag_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_kate_tag_dispose);

  g_object_class_install_property (gobject_class, ARG_LANGUAGE,
      g_param_spec_string ("language", "Language",
          "Set the language of the stream", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_CATEGORY,
      g_param_spec_string ("category", "Category",
          "Set the category of the stream", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_ORIGINAL_CANVAS_WIDTH,
      g_param_spec_int ("original-canvas-width", "Original canvas width",
          "Set the width of the canvas this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_ORIGINAL_CANVAS_HEIGHT,
      g_param_spec_int ("original-canvas-height", "Original canvas height",
          "Set the height of the canvas this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Kate stream tagger",
      "Formatter/Metadata",
      "Retags kate streams", "Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>");

  gstkateparse_class->parse_packet =
      GST_DEBUG_FUNCPTR (gst_kate_tag_parse_packet);
}

static void
gst_kate_tag_init (GstKateTag * kt)
{
  kt->language = NULL;
  kt->category = NULL;
  kt->original_canvas_width = -1;
  kt->original_canvas_height = -1;
}

static void
gst_kate_tag_dispose (GObject * object)
{
  GstKateTag *kt = GST_KATE_TAG (object);

  GST_LOG_OBJECT (kt, "disposing");

  if (kt->language) {
    g_free (kt->language);
    kt->language = NULL;
  }
  if (kt->category) {
    g_free (kt->category);
    kt->category = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_kate_tag_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKateTag *kt = GST_KATE_TAG (object);
  const char *str;

  switch (prop_id) {
    case ARG_LANGUAGE:
      if (kt->language) {
        g_free (kt->language);
        kt->language = NULL;
      }
      str = g_value_get_string (value);
      if (str)
        kt->language = g_strdup (str);
      break;
    case ARG_CATEGORY:
      if (kt->category) {
        g_free (kt->category);
        kt->category = NULL;
      }
      str = g_value_get_string (value);
      if (str)
        kt->category = g_strdup (str);
      break;
    case ARG_ORIGINAL_CANVAS_WIDTH:
      kt->original_canvas_width = g_value_get_int (value);
      break;
    case ARG_ORIGINAL_CANVAS_HEIGHT:
      kt->original_canvas_height = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kate_tag_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKateTag *kt = GST_KATE_TAG (object);

  switch (prop_id) {
    case ARG_LANGUAGE:
      g_value_set_string (value, kt->language ? kt->language : "");
      break;
    case ARG_CATEGORY:
      g_value_set_string (value, kt->category ? kt->category : "");
      break;
    case ARG_ORIGINAL_CANVAS_WIDTH:
      g_value_set_int (value, kt->original_canvas_width);
      break;
    case ARG_ORIGINAL_CANVAS_HEIGHT:
      g_value_set_int (value, kt->original_canvas_height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint16
encode_canvas_size (size_t size)
{
  size_t base = size;
  size_t shift = 0;
  int value;

  while (base & ~((1 << 12) - 1)) {
    /* we have a high bit we can't fit, increase shift if we wouldn't lose low bits */
    if ((size >> shift) & 1)
      return 0;
    ++shift;
    base >>= 1;
  }
  if (G_UNLIKELY (shift >= 16))
    return 0;

  /* the size can be represented in our encoding */
  value = (base << 4) | shift;

  return (guint16) value;
}

static GstFlowReturn
gst_kate_tag_parse_packet (GstKateParse * parse, GstBuffer * buffer)
{
  GstTagList *old_tags, *new_tags;
  const GstTagList *user_tags;
  GstKateTag *kt;
  gchar *encoder = NULL;
  GstBuffer *new_buf;
  GstMapInfo info;

  kt = GST_KATE_TAG (parse);

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (parse, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  /* rewrite the language and category */
  if (info.size >= 64 && info.data[0] == 0x80) {
    GstBuffer *new_buffer;

    gst_buffer_unmap (buffer, &info);
    new_buffer = gst_buffer_copy (buffer);
    gst_buffer_unref (buffer);
    buffer = new_buffer;

    if (!gst_buffer_map (buffer, &info, GST_MAP_READWRITE)) {
      GST_ERROR_OBJECT (parse, "Failed to map copied buffer READWRITE");
      return GST_FLOW_ERROR;
    }
    /* language is at offset 32, 16 bytes, zero terminated */
    if (kt->language) {
      strncpy ((char *) info.data + 32, kt->language, 15);
      info.data[47] = 0;
    }
    /* category is at offset 48, 16 bytes, zero terminated */
    if (kt->category) {
      strncpy ((char *) info.data + 48, kt->category, 15);
      info.data[63] = 0;
    }
    if (kt->original_canvas_width >= 0) {
      guint16 v = encode_canvas_size (kt->original_canvas_width);
      info.data[16] = v & 0xff;
      info.data[17] = (v >> 8) & 0xff;
    }
    if (kt->original_canvas_height >= 0) {
      guint16 v = encode_canvas_size (kt->original_canvas_height);
      info.data[18] = v & 0xff;
      info.data[19] = (v >> 8) & 0xff;
    }
  }

  /*  rewrite the comments packet */
  if (info.size >= 9 && info.data[0] == 0x81) {
    old_tags =
        gst_tag_list_from_vorbiscomment (info.data, info.size,
        (const guint8 *) "\201kate\0\0\0\0", 9, &encoder);
    user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (kt));
    gst_buffer_unmap (buffer, &info);

    /* build new tag list */
    new_tags = gst_tag_list_merge (user_tags, old_tags,
        gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (kt)));
    gst_tag_list_unref (old_tags);

    new_buf =
        gst_tag_list_to_vorbiscomment_buffer (new_tags,
        (const guint8 *) "\201kate\0\0\0\0", 9, encoder);
    gst_buffer_copy_into (new_buf, buffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    gst_tag_list_unref (new_tags);
    g_free (encoder);
    gst_buffer_unref (buffer);

    /* the buffer will have the framing bit used by Vorbis, but we don't use it */
    gst_buffer_resize (new_buf, 0, gst_buffer_get_size (new_buf) - 1);

    buffer = new_buf;
  } else {
    gst_buffer_unmap (buffer, &info);
  }

  return GST_KATE_PARSE_CLASS (parent_class)->parse_packet (parse, buffer);
}
