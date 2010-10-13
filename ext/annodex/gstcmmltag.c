/*
 * gstcmmltags.c - GStreamer CMML tag support
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstcmmlparser.h"
#include "gstcmmltag.h"
#include "gstannodex.h"

enum
{
  ARG_0,
  GST_CMML_TAG_STREAM_TIMEBASE,
  GST_CMML_TAG_STREAM_UTC,
  GST_CMML_TAG_STREAM_IMPORTS,
  GST_CMML_TAG_HEAD_TITLE,
  GST_CMML_TAG_HEAD_BASE,
  GST_CMML_TAG_HEAD_META,
  GST_CMML_TAG_CLIP_EMPTY,
  GST_CMML_TAG_CLIP_ID,
  GST_CMML_TAG_CLIP_TRACK,
  GST_CMML_TAG_CLIP_START_TIME,
  GST_CMML_TAG_CLIP_END_TIME,
  GST_CMML_TAG_CLIP_ANCHOR_HREF,
  GST_CMML_TAG_CLIP_ANCHOR_TEXT,
  GST_CMML_TAG_CLIP_IMG_SRC,
  GST_CMML_TAG_CLIP_IMG_ALT,
  GST_CMML_TAG_CLIP_DESC_TEXT,
  GST_CMML_TAG_CLIP_META,
};

G_DEFINE_TYPE (GstCmmlTagStream, gst_cmml_tag_stream, G_TYPE_OBJECT);
static void gst_cmml_tag_stream_finalize (GObject * object);
static void gst_cmml_tag_stream_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_cmml_tag_stream_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_cmml_tag_stream_value_from_string_value (const GValue * src,
    GValue * dest);

G_DEFINE_TYPE (GstCmmlTagHead, gst_cmml_tag_head, G_TYPE_OBJECT);
static void gst_cmml_tag_head_finalize (GObject * object);
static void gst_cmml_tag_head_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cmml_tag_head_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);
static void gst_cmml_tag_head_value_from_string_value (const GValue * src,
    GValue * dest);

G_DEFINE_TYPE (GstCmmlTagClip, gst_cmml_tag_clip, G_TYPE_OBJECT);
static void gst_cmml_tag_clip_finalize (GObject * object);
static void gst_cmml_tag_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cmml_tag_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);

static void gst_cmml_tag_clip_value_from_string_value (const GValue * src,
    GValue * dest);

static void set_object_on_value (GObject * object, GValue * dest);

static const gchar default_preamble[] =
    "<?xml version=\"1.0\" standalone=\"yes\"?>";

/* Stream tag */
static void
gst_cmml_tag_stream_class_init (GstCmmlTagStreamClass * stream_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (stream_class);

  klass->set_property = gst_cmml_tag_stream_set_property;
  klass->get_property = gst_cmml_tag_stream_get_property;
  klass->finalize = gst_cmml_tag_stream_finalize;

  g_object_class_install_property (klass, GST_CMML_TAG_STREAM_TIMEBASE,
      g_param_spec_string ("base-time",
          "Base time",
          "Playback time (in seconds) of the first data packet",
          "0", G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_STREAM_UTC,
      g_param_spec_string ("calendar-base-time",
          "Calendar base time",
          "Date and wall-clock time (expressed as UTC time in the format "
          "YYYYMMDDTHHMMSS.sssZ) associated with the base-time",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_STREAM_IMPORTS,
      g_param_spec_value_array ("input-streams",
          "Input streams",
          "List of input streams that compose this bitstream",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_value_register_transform_func (G_TYPE_STRING, GST_TYPE_CMML_TAG_STREAM,
      gst_cmml_tag_stream_value_from_string_value);
}

static void
gst_cmml_tag_stream_init (GstCmmlTagStream * stream)
{
}

static void
gst_cmml_tag_stream_finalize (GObject * object)
{
  GstCmmlTagStream *stream = GST_CMML_TAG_STREAM (object);

  g_free (stream->timebase);
  g_free (stream->utc);
  if (stream->imports)
    g_value_array_free (stream->imports);

  if (G_OBJECT_CLASS (gst_cmml_tag_stream_parent_class)->finalize)
    G_OBJECT_CLASS (gst_cmml_tag_stream_parent_class)->finalize (object);
}

static void
gst_cmml_tag_stream_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCmmlTagStream *stream = GST_CMML_TAG_STREAM (object);

  switch (property_id) {
    case GST_CMML_TAG_STREAM_TIMEBASE:
      g_free (stream->timebase);
      stream->timebase = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_STREAM_UTC:
      g_free (stream->utc);
      stream->utc = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_STREAM_IMPORTS:
    {
      GValueArray *va = g_value_get_boxed (value);

      if (stream->imports)
        g_value_array_free (stream->imports);
      stream->imports = va != NULL ? g_value_array_copy (va) : NULL;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
gst_cmml_tag_stream_value_from_string_value (const GValue * src, GValue * dest)
{
  GstCmmlParser *parser;
  const gchar *str;
  guint size;

  parser = gst_cmml_parser_new (GST_CMML_PARSER_DECODE);
  parser->user_data = dest;
  parser->stream_callback = (GstCmmlParserStreamCallback) set_object_on_value;
  gst_cmml_parser_parse_chunk (parser,
      default_preamble, strlen (default_preamble), NULL);

  str = g_value_get_string (src);
  size = strlen (str);
  gst_cmml_parser_parse_chunk (parser, str, size, NULL);

  gst_cmml_parser_free (parser);
}

static void
gst_cmml_tag_stream_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCmmlTagStream *stream = GST_CMML_TAG_STREAM (object);

  switch (property_id) {
    case GST_CMML_TAG_STREAM_TIMEBASE:
      g_value_set_string (value, (gchar *) stream->timebase);
      break;
    case GST_CMML_TAG_STREAM_UTC:
      g_value_set_string (value, (gchar *) stream->utc);
      break;
    case GST_CMML_TAG_STREAM_IMPORTS:
      g_value_set_boxed (value, stream->imports);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

/* Head tag */
static void
gst_cmml_tag_head_class_init (GstCmmlTagHeadClass * head_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (head_class);

  klass->set_property = gst_cmml_tag_head_set_property;
  klass->get_property = gst_cmml_tag_head_get_property;
  klass->finalize = gst_cmml_tag_head_finalize;

  g_object_class_install_property (klass, GST_CMML_TAG_HEAD_TITLE,
      g_param_spec_string ("title",
          "Title",
          "Title of the bitstream",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_HEAD_BASE,
      g_param_spec_string ("base-uri",
          "Base URI",
          "Base URI of the bitstream. All relative URIs are relative to this",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_HEAD_META,
      g_param_spec_value_array ("meta",
          "Meta annotations",
          "Meta annotations for the complete Annodex bitstream",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_value_register_transform_func (G_TYPE_STRING, GST_TYPE_CMML_TAG_HEAD,
      gst_cmml_tag_head_value_from_string_value);
}

static void
gst_cmml_tag_head_init (GstCmmlTagHead * head)
{
}

static void
gst_cmml_tag_head_finalize (GObject * object)
{
  GstCmmlTagHead *head = GST_CMML_TAG_HEAD (object);

  g_free (head->title);
  g_free (head->base);
  if (head->meta)
    g_value_array_free (head->meta);

  if (G_OBJECT_CLASS (gst_cmml_tag_head_parent_class)->finalize)
    G_OBJECT_CLASS (gst_cmml_tag_head_parent_class)->finalize (object);
}

static void
gst_cmml_tag_head_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCmmlTagHead *head = GST_CMML_TAG_HEAD (object);

  switch (property_id) {
    case GST_CMML_TAG_HEAD_TITLE:
      g_free (head->title);
      head->title = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_HEAD_BASE:
      g_free (head->base);
      head->base = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_HEAD_META:
    {
      GValueArray *va = g_value_get_boxed (value);

      if (head->meta)
        g_value_array_free (head->meta);
      head->meta = va != NULL ? g_value_array_copy (va) : NULL;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_cmml_tag_head_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCmmlTagHead *head = GST_CMML_TAG_HEAD (object);

  switch (property_id) {
    case GST_CMML_TAG_HEAD_TITLE:
      g_value_set_string (value, (gchar *) head->title);
      break;
    case GST_CMML_TAG_HEAD_BASE:
      g_value_set_string (value, (gchar *) head->base);
      break;
    case GST_CMML_TAG_HEAD_META:
      g_value_set_boxed (value, head->meta);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_cmml_tag_head_value_from_string_value (const GValue * src, GValue * dest)
{
  GstCmmlParser *parser;
  const gchar *str;
  guint size;

  parser = gst_cmml_parser_new (GST_CMML_PARSER_DECODE);
  parser->user_data = dest;
  parser->head_callback = (GstCmmlParserHeadCallback) set_object_on_value;
  gst_cmml_parser_parse_chunk (parser,
      default_preamble, strlen (default_preamble), NULL);

  str = g_value_get_string (src);
  size = strlen (str);
  gst_cmml_parser_parse_chunk (parser, str, size, NULL);

  gst_cmml_parser_free (parser);
}

/* Clip tag */
static void
gst_cmml_tag_clip_class_init (GstCmmlTagClipClass * clip_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (clip_class);

  klass->set_property = gst_cmml_tag_clip_set_property;
  klass->get_property = gst_cmml_tag_clip_get_property;
  klass->finalize = gst_cmml_tag_clip_finalize;

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_EMPTY,
      g_param_spec_boolean ("empty",
          "Empty clip flag",
          "An empty clip only marks the end of the previous clip",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_ID,
      g_param_spec_string ("id",
          "Clip id",
          "Id of the clip", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_TRACK,
      g_param_spec_string ("track",
          "Track number",
          "The track this clip belongs to",
          "default",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_START_TIME,
      g_param_spec_uint64 ("start-time",
          "Start time",
          "The start time (in seconds) of the clip",
          0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_END_TIME,
      g_param_spec_uint64 ("end-time",
          "End time",
          "The end time (in seconds) of the clip (only set if extract-mode=true)",
          0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_ANCHOR_HREF,
      g_param_spec_string ("anchor-uri",
          "Anchor URI",
          "The location of a Web resource closely connected to the clip",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_ANCHOR_TEXT,
      g_param_spec_string ("anchor-text",
          "Anchor text",
          "A short description of the resource pointed by anchor-uri",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_IMG_SRC,
      g_param_spec_string ("img-uri",
          "Image URI",
          "The URI of a representative image for the clip",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_IMG_ALT,
      g_param_spec_string ("img-alt",
          "Image alternative text",
          "Alternative text to be displayed instead of the image "
          "specified in img-uri", NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_DESC_TEXT,
      g_param_spec_string ("description",
          "Description",
          "A textual description of the content of the clip",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_TAG_CLIP_META,
      g_param_spec_value_array ("meta",
          "Meta annotations",
          "Meta annotations for the clip",
          NULL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_value_register_transform_func (G_TYPE_STRING, GST_TYPE_CMML_TAG_CLIP,
      gst_cmml_tag_clip_value_from_string_value);
}

static void
gst_cmml_tag_clip_init (GstCmmlTagClip * clip)
{
}

static void
gst_cmml_tag_clip_finalize (GObject * object)
{
  GstCmmlTagClip *clip = GST_CMML_TAG_CLIP (object);

  g_free (clip->id);
  g_free (clip->track);
  g_free (clip->anchor_href);
  g_free (clip->anchor_text);
  g_free (clip->img_src);
  g_free (clip->img_alt);
  g_free (clip->desc_text);
  if (clip->meta)
    g_value_array_free (clip->meta);

  if (G_OBJECT_CLASS (gst_cmml_tag_clip_parent_class)->finalize)
    G_OBJECT_CLASS (gst_cmml_tag_clip_parent_class)->finalize (object);
}

static void
gst_cmml_tag_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCmmlTagClip *clip = GST_CMML_TAG_CLIP (object);

  switch (property_id) {
    case GST_CMML_TAG_CLIP_EMPTY:
      clip->empty = g_value_get_boolean (value);
      break;
    case GST_CMML_TAG_CLIP_ID:
      g_free (clip->id);
      clip->id = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_TRACK:
      g_free (clip->track);
      clip->track = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_START_TIME:
      clip->start_time = g_value_get_uint64 (value);
      break;
    case GST_CMML_TAG_CLIP_END_TIME:
      clip->end_time = g_value_get_uint64 (value);
      break;
    case GST_CMML_TAG_CLIP_ANCHOR_HREF:
      g_free (clip->anchor_href);
      clip->anchor_href = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_ANCHOR_TEXT:
      g_free (clip->anchor_text);
      clip->anchor_text = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_IMG_SRC:
      g_free (clip->img_src);
      clip->img_src = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_IMG_ALT:
      g_free (clip->img_alt);
      clip->img_alt = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_DESC_TEXT:
      g_free (clip->desc_text);
      clip->desc_text = (guchar *) g_value_dup_string (value);
      break;
    case GST_CMML_TAG_CLIP_META:
    {
      GValueArray *va = (GValueArray *) g_value_get_boxed (value);

      if (clip->meta)
        g_value_array_free (clip->meta);

      clip->meta = va != NULL ? g_value_array_copy (va) : NULL;

      break;
    }
  }
}

static void
gst_cmml_tag_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCmmlTagClip *clip = GST_CMML_TAG_CLIP (object);

  switch (property_id) {
    case GST_CMML_TAG_CLIP_EMPTY:
      g_value_set_boolean (value, clip->empty);
      break;
    case GST_CMML_TAG_CLIP_ID:
      g_value_set_string (value, (gchar *) clip->id);
      break;
    case GST_CMML_TAG_CLIP_TRACK:
      g_value_set_string (value, (gchar *) clip->track);
      break;
    case GST_CMML_TAG_CLIP_START_TIME:
      g_value_set_uint64 (value, clip->start_time);
      break;
    case GST_CMML_TAG_CLIP_END_TIME:
      g_value_set_uint64 (value, clip->end_time);
      break;
    case GST_CMML_TAG_CLIP_ANCHOR_HREF:
      g_value_set_string (value, (gchar *) clip->anchor_href);
      break;
    case GST_CMML_TAG_CLIP_ANCHOR_TEXT:
      g_value_set_string (value, (gchar *) clip->anchor_text);
      break;
    case GST_CMML_TAG_CLIP_IMG_SRC:
      g_value_set_string (value, (gchar *) clip->img_src);
      break;
    case GST_CMML_TAG_CLIP_IMG_ALT:
      g_value_set_string (value, (gchar *) clip->img_alt);
      break;
    case GST_CMML_TAG_CLIP_DESC_TEXT:
      g_value_set_string (value, (gchar *) clip->desc_text);
      break;
    case GST_CMML_TAG_CLIP_META:
      g_value_set_boxed (value, clip->meta);
      break;
  }
}

static void
gst_cmml_tag_clip_value_from_string_value (const GValue * src, GValue * dest)
{
  GstCmmlParser *parser;
  const gchar *str;
  guint size;

  parser = gst_cmml_parser_new (GST_CMML_PARSER_DECODE);
  parser->user_data = dest;
  parser->clip_callback = (GstCmmlParserClipCallback) set_object_on_value;

  gst_cmml_parser_parse_chunk (parser, default_preamble,
      strlen (default_preamble), NULL);

  str = g_value_get_string (src);
  size = strlen (str);

  gst_cmml_parser_parse_chunk (parser, str, size, NULL);

  gst_cmml_parser_free (parser);
}

static void
set_object_on_value (GObject * tag, GValue * dest)
{
  g_value_take_object (dest, tag);
}
