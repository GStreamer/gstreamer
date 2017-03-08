/* GStreamer
 *
 * jpegparse: a parser for JPEG streams
 *
 * Copyright (C) <2009> Arnout Vandecappelle (Essensium/Mind) <arnout@mind.be>
 *                      Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-jpegparse
 * @title: jpegparse
 * @short_description: JPEG parser
 *
 * Parses a JPEG stream into JPEG images.  It looks for EOI boundaries to
 * split a continuous stream into single-frame buffers. Also reads the
 * image header searching for image properties such as width and height
 * among others. Jpegparse can also extract metadata (e.g. xmp).
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v souphttpsrc location=... ! jpegparse ! matroskamux ! filesink location=...
 * ]|
 * The above pipeline fetches a motion JPEG stream from an IP camera over
 * HTTP and stores it in a matroska file.
 *
 */
/* FIXME: output plain JFIF APP marker only. This provides best code reuse.
 * JPEG decoders would not need to handle this part anymore. Also when remuxing
 * (... ! jpegparse ! ... ! jifmux ! ...) metadata consolidation would be
 * easier.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/base/gstbytereader.h>
#include <gst/tag/tag.h>

#include "gstjpegparse.h"

static GstStaticPadTemplate gst_jpeg_parse_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "format = (string) { I420, Y41B, UYVY, YV12 }, "
        "width = (int) [ 0, MAX ],"
        "height = (int) [ 0, MAX ], "
        "framerate = (fraction) [ 0/1, MAX ], " "parsed = (boolean) true")
    );

static GstStaticPadTemplate gst_jpeg_parse_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg")
    );

GST_DEBUG_CATEGORY_STATIC (jpeg_parse_debug);
#define GST_CAT_DEFAULT jpeg_parse_debug

struct _GstJpegParsePrivate
{
  guint last_offset;
  guint last_entropy_len;
  gboolean last_resync;

  /* negotiated state */
  gint caps_width, caps_height;
  gint caps_framerate_numerator;
  gint caps_framerate_denominator;

  /* the parsed frame size */
  guint16 width, height;

  /* format color space */
  const gchar *format;

  /* TRUE if the src caps sets a specific framerate */
  gboolean has_fps;

  /* the (expected) timestamp of the next frame */
  guint64 next_ts;

  /* duration of the current frame */
  guint64 duration;

  /* video state */
  gint framerate_numerator;
  gint framerate_denominator;

  /* tags */
  GstTagList *tags;
};

static GstFlowReturn
gst_jpeg_parse_handle_frame (GstBaseParse * bparse, GstBaseParseFrame * frame,
    gint * skipsize);
static gboolean gst_jpeg_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);
static gboolean gst_jpeg_parse_sink_event (GstBaseParse * parse,
    GstEvent * event);
static gboolean gst_jpeg_parse_start (GstBaseParse * parse);
static gboolean gst_jpeg_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_jpeg_parse_pre_push_frame (GstBaseParse * bparse,
    GstBaseParseFrame * frame);

#define gst_jpeg_parse_parent_class parent_class
G_DEFINE_TYPE (GstJpegParse, gst_jpeg_parse, GST_TYPE_BASE_PARSE);

static void
gst_jpeg_parse_class_init (GstJpegParseClass * klass)
{
  GstBaseParseClass *gstbaseparse_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstbaseparse_class = (GstBaseParseClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (gobject_class, sizeof (GstJpegParsePrivate));

  gstbaseparse_class->start = gst_jpeg_parse_start;
  gstbaseparse_class->stop = gst_jpeg_parse_stop;
  gstbaseparse_class->set_sink_caps = gst_jpeg_parse_set_sink_caps;
  gstbaseparse_class->sink_event = gst_jpeg_parse_sink_event;
  gstbaseparse_class->handle_frame = gst_jpeg_parse_handle_frame;
  gstbaseparse_class->pre_push_frame = gst_jpeg_parse_pre_push_frame;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_jpeg_parse_src_pad_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_jpeg_parse_sink_pad_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "JPEG stream parser",
      "Video/Parser",
      "Parse JPEG images into single-frame buffers",
      "Arnout Vandecappelle (Essensium/Mind) <arnout@mind.be>");

  GST_DEBUG_CATEGORY_INIT (jpeg_parse_debug, "jpegparse", 0, "JPEG parser");
}

static void
gst_jpeg_parse_init (GstJpegParse * parse)
{
  parse->priv = G_TYPE_INSTANCE_GET_PRIVATE (parse, GST_TYPE_JPEG_PARSE,
      GstJpegParsePrivate);

  parse->priv->next_ts = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_jpeg_parse_set_sink_caps (GstBaseParse * bparse, GstCaps * caps)
{
  GstJpegParse *parse = GST_JPEG_PARSE_CAST (bparse);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const GValue *framerate;

  if ((framerate = gst_structure_get_value (s, "framerate")) != NULL) {
    if (GST_VALUE_HOLDS_FRACTION (framerate)) {
      parse->priv->framerate_numerator =
          gst_value_get_fraction_numerator (framerate);
      parse->priv->framerate_denominator =
          gst_value_get_fraction_denominator (framerate);
      parse->priv->has_fps = TRUE;
      GST_DEBUG_OBJECT (parse, "got framerate of %d/%d",
          parse->priv->framerate_numerator, parse->priv->framerate_denominator);
    }
  }

  return TRUE;
}


/*
 * gst_jpeg_parse_skip_to_jpeg_header:
 * @parse: the parser
 *
 * Flush everything until the next JPEG header.  The header is considered
 * to be the a start marker SOI (0xff 0xd8) followed by any other marker
 * (0xff ...).
 *
 * Returns: TRUE if the header was found, FALSE if more data is needed.
 */
static gboolean
gst_jpeg_parse_skip_to_jpeg_header (GstJpegParse * parse, GstMapInfo * mapinfo,
    gint * skipsize)
{
  gboolean ret = TRUE;
  GstByteReader reader;

  if (mapinfo->size < 4)
    return FALSE;

  gst_byte_reader_init (&reader, mapinfo->data, mapinfo->size);

  *skipsize = gst_byte_reader_masked_scan_uint32 (&reader, 0xffffff00,
      0xffd8ff00, 0, mapinfo->size);
  if (*skipsize == -1) {
    *skipsize = mapinfo->size - 3;      /* Last 3 bytes + 1 more may match header. */
    ret = FALSE;
  }
  return ret;
}

static inline gboolean
gst_jpeg_parse_parse_tag_has_entropy_segment (guint8 tag)
{
  if (tag == SOS || (tag >= RST0 && tag <= RST7))
    return TRUE;
  return FALSE;
}

/* returns image length in bytes if parsed successfully,
 * otherwise 0 if more data needed,
 * if < 0 the absolute value needs to be flushed */
static gint
gst_jpeg_parse_get_image_length (GstJpegParse * parse, GstMapInfo * mapinfo)
{
  guint size;
  gboolean resync;
  gint offset, noffset;
  GstByteReader reader;

  size = mapinfo->size;
  gst_byte_reader_init (&reader, mapinfo->data, mapinfo->size);

  /* TODO could be removed as previous functions already guarantee this to be
   * true */
  /* we expect at least 4 bytes, first of which start marker */
  if (gst_byte_reader_masked_scan_uint32 (&reader, 0xffff0000, 0xffd80000, 0,
          4))
    return 0;

  GST_DEBUG ("Parsing jpeg image data (%u bytes)", size);

  GST_DEBUG ("Parse state: offset=%d, resync=%d, entropy len=%d",
      parse->priv->last_offset, parse->priv->last_resync,
      parse->priv->last_entropy_len);

  /* offset is 2 less than actual offset;
   * - adapter needs at least 4 bytes for scanning,
   * - start and end marker ensure at least that much
   */
  /* resume from state offset */
  offset = parse->priv->last_offset;

  while (1) {
    guint frame_len;
    guint32 value;

    noffset =
        gst_byte_reader_masked_scan_uint32_peek (&reader, 0x0000ff00,
        0x0000ff00, offset, size - offset, &value);
    /* lost sync if 0xff marker not where expected */
    if ((resync = (noffset != offset))) {
      GST_DEBUG ("Lost sync at 0x%08x, resyncing", offset + 2);
    }
    /* may have marker, but could have been resyncng */
    resync = resync || parse->priv->last_resync;
    /* Skip over extra 0xff */
    while ((noffset >= 0) && ((value & 0xff) == 0xff)) {
      noffset++;
      noffset =
          gst_byte_reader_masked_scan_uint32_peek (&reader, 0x0000ff00,
          0x0000ff00, noffset, size - noffset, &value);
    }
    /* enough bytes left for marker? (we need 0xNN after the 0xff) */
    if (noffset < 0) {
      GST_DEBUG ("at end of input and no EOI marker found, need more data");
      goto need_more_data;
    }

    /* now lock on the marker we found */
    offset = noffset;
    value = value & 0xff;
    if (value == 0xd9) {
      GST_DEBUG ("0x%08x: EOI marker", offset + 2);
      /* clear parse state */
      parse->priv->last_resync = FALSE;
      parse->priv->last_offset = 0;
      return (offset + 4);
    } else if (value == 0xd8) {
      /* Skip this frame if we found another SOI marker */
      GST_DEBUG ("0x%08x: SOI marker before EOI, skipping", offset + 2);
      /* clear parse state */
      parse->priv->last_resync = FALSE;
      parse->priv->last_offset = 0;
      return -(offset + 2);
    }

    if (value >= 0xd0 && value <= 0xd7)
      frame_len = 0;
    else {
      /* peek tag and subsequent length */
      if (offset + 2 + 4 > size)
        goto need_more_data;
      else
        gst_byte_reader_masked_scan_uint32_peek (&reader, 0x0, 0x0, offset + 2,
            4, &frame_len);
      frame_len = frame_len & 0xffff;
    }
    GST_DEBUG ("0x%08x: tag %02x, frame_len=%u", offset + 2, value, frame_len);
    /* the frame length includes the 2 bytes for the length; here we want at
     * least 2 more bytes at the end for an end marker */
    if (offset + 2 + 2 + frame_len + 2 > size) {
      goto need_more_data;
    }

    if (gst_jpeg_parse_parse_tag_has_entropy_segment (value)) {
      guint eseglen = parse->priv->last_entropy_len;

      GST_DEBUG ("0x%08x: finding entropy segment length", offset + 2);
      noffset = offset + 2 + frame_len + eseglen;
      while (1) {
        noffset = gst_byte_reader_masked_scan_uint32_peek (&reader, 0x0000ff00,
            0x0000ff00, noffset, size - noffset, &value);
        if (noffset < 0) {
          /* need more data */
          parse->priv->last_entropy_len = size - offset - 4 - frame_len - 2;
          goto need_more_data;
        }
        if ((value & 0xff) != 0x00) {
          eseglen = noffset - offset - frame_len - 2;
          break;
        }
        noffset++;
      }
      parse->priv->last_entropy_len = 0;
      frame_len += eseglen;
      GST_DEBUG ("entropy segment length=%u => frame_len=%u", eseglen,
          frame_len);
    }
    if (resync) {
      /* check if we will still be in sync if we interpret
       * this as a sync point and skip this frame */
      noffset = offset + frame_len + 2;
      noffset =
          gst_byte_reader_masked_scan_uint32 (&reader, 0x0000ff00, 0x0000ff00,
          noffset, 4);
      if (noffset < 0) {
        /* ignore and continue resyncing until we hit the end
         * of our data or find a sync point that looks okay */
        offset++;
        continue;
      }
      GST_DEBUG ("found sync at 0x%x", offset + 2);
    }

    offset += frame_len + 2;
  }

  /* EXITS */
need_more_data:
  {
    parse->priv->last_offset = offset;
    parse->priv->last_resync = resync;
    return 0;
  }
}

static inline gboolean
gst_jpeg_parse_sof (GstJpegParse * parse, GstByteReader * reader)
{
  guint8 numcomps = 0;          /* Number of components in image
                                   (1 for gray, 3 for YUV, etc.) */
  guint8 precision;             /* precision (in bits) for the samples */
  guint8 compId[3] G_GNUC_UNUSED;       /* unique value identifying each component */
  guint8 qtId[3] G_GNUC_UNUSED; /* quantization table ID to use for this comp */
  guint8 blockWidth[3];         /* Array[numComponents] giving the number of
                                   blocks (horiz) in this component */
  guint8 blockHeight[3];        /* Same for the vertical part of this component */
  guint8 i, value = 0;
  gint temp;

  /* flush length field */
  if (!gst_byte_reader_skip (reader, 2))
    return FALSE;

  /* Get sample precision */
  if (!gst_byte_reader_get_uint8 (reader, &precision))
    return FALSE;

  /* Get w and h */
  if (!gst_byte_reader_get_uint16_be (reader, &parse->priv->height))
    return FALSE;
  if (!gst_byte_reader_get_uint16_be (reader, &parse->priv->width))
    return FALSE;

  /* Get number of components */
  if (!gst_byte_reader_get_uint8 (reader, &numcomps))
    return FALSE;

  if (numcomps > 3)             /* FIXME */
    return FALSE;

  /* Get decimation and quantization table id for each component */
  for (i = 0; i < numcomps; i++) {
    /* Get component ID number */
    if (!gst_byte_reader_get_uint8 (reader, &value))
      return FALSE;
    compId[i] = value;

    /* Get decimation */
    if (!gst_byte_reader_get_uint8 (reader, &value))
      return FALSE;
    blockWidth[i] = (value & 0xf0) >> 4;
    blockHeight[i] = (value & 0x0f);

    /* Get quantization table id */
    if (!gst_byte_reader_get_uint8 (reader, &value))
      return FALSE;
    qtId[i] = value;
  }

  if (numcomps == 1) {
    /* gray image - no format */
    parse->priv->format = "";
  } else if (numcomps == 3) {
    temp = (blockWidth[0] * blockHeight[0]) / (blockWidth[1] * blockHeight[1]);

    if (temp == 4 && blockHeight[0] == 2)
      parse->priv->format = "I420";
    else if (temp == 4 && blockHeight[0] == 4)
      parse->priv->format = "Y41B";
    else if (temp == 2)
      parse->priv->format = "UYVY";
    else if (temp == 1)
      parse->priv->format = "YV12";
    else
      parse->priv->format = "";
  } else {
    return FALSE;
  }

  GST_DEBUG_OBJECT (parse, "Header parsed");

  return TRUE;
}

static inline gboolean
gst_jpeg_parse_skip_marker (GstJpegParse * parse,
    GstByteReader * reader, guint8 marker)
{
  guint16 size = 0;

  if (!gst_byte_reader_get_uint16_be (reader, &size))
    return FALSE;

#ifndef GST_DISABLE_GST_DEBUG
  /* We'd pry the id of the skipped application segment */
  if (marker >= APP0 && marker <= APP15) {
    const gchar *id_str = NULL;

    if (gst_byte_reader_peek_string_utf8 (reader, &id_str)) {
      GST_DEBUG_OBJECT (parse, "unhandled marker %x: '%s' skiping %u bytes",
          marker, id_str ? id_str : "(NULL)", size);
    } else {
      GST_DEBUG_OBJECT (parse, "unhandled marker %x skiping %u bytes", marker,
          size);
    }
  }
#else
  GST_DEBUG_OBJECT (parse, "unhandled marker %x skiping %u bytes", marker,
      size);
#endif // GST_DISABLE_GST_DEBUG

  if (!gst_byte_reader_skip (reader, size - 2))
    return FALSE;

  return TRUE;
}

static inline GstTagList *
get_tag_list (GstJpegParse * parse)
{
  if (!parse->priv->tags)
    parse->priv->tags = gst_tag_list_new_empty ();
  return parse->priv->tags;
}

static inline void
extract_and_queue_tags (GstJpegParse * parse, guint size, guint8 * data,
    GstTagList * (*tag_func) (GstBuffer * buff))
{
  GstTagList *tags;
  GstBuffer *buf;

  buf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, data, size, 0,
      size, NULL, NULL);

  tags = tag_func (buf);
  gst_buffer_unref (buf);

  if (tags) {
    GstTagList *taglist = parse->priv->tags;
    if (taglist) {
      gst_tag_list_insert (taglist, tags, GST_TAG_MERGE_REPLACE);
      gst_tag_list_unref (tags);
    } else {
      parse->priv->tags = tags;
    }
    GST_DEBUG_OBJECT (parse, "collected tags: %" GST_PTR_FORMAT,
        parse->priv->tags);
  }
}

static inline gboolean
gst_jpeg_parse_app1 (GstJpegParse * parse, GstByteReader * reader)
{
  guint16 size = 0;
  const gchar *id_str;
  const guint8 *data = NULL;

  if (!gst_byte_reader_get_uint16_be (reader, &size))
    return FALSE;

  size -= 2;                    /* 2 bytes for the mark */
  if (!gst_byte_reader_peek_string_utf8 (reader, &id_str))
    return FALSE;

  if (!strncmp (id_str, "Exif", 4)) {

    /* skip id + NUL + padding */
    if (!gst_byte_reader_skip (reader, 6))
      return FALSE;
    size -= 6;

    /* handle exif metadata */
    if (!gst_byte_reader_get_data (reader, size, &data))
      return FALSE;

    extract_and_queue_tags (parse, size, (guint8 *) data,
        gst_tag_list_from_exif_buffer_with_tiff_header);

    GST_LOG_OBJECT (parse, "parsed marker %x: '%s' %u bytes",
        APP1, id_str, size);

  } else if (!strncmp (id_str, "http://ns.adobe.com/xap/1.0/", 28)) {

    /* skip the id + NUL */
    if (!gst_byte_reader_skip (reader, 29))
      return FALSE;
    size -= 29;

    /* handle xmp metadata */
    if (!gst_byte_reader_get_data (reader, size, &data))
      return FALSE;

    extract_and_queue_tags (parse, size, (guint8 *) data,
        gst_tag_list_from_xmp_buffer);

    GST_LOG_OBJECT (parse, "parsed marker %x: '%s' %u bytes",
        APP1, id_str, size);

  } else {
    if (!gst_jpeg_parse_skip_marker (parse, reader, APP1))
      return FALSE;
  }

  return TRUE;
}

static inline gchar *
get_utf8_from_data (const guint8 * data, guint16 size)
{
  const gchar *env_vars[] = { "GST_JPEG_TAG_ENCODING",
    "GST_TAG_ENCODING", NULL
  };
  const char *str = (gchar *) data;

  return gst_tag_freeform_string_to_utf8 (str, size, env_vars);
}

/* read comment and post as tag */
static inline gboolean
gst_jpeg_parse_com (GstJpegParse * parse, GstByteReader * reader)
{
  const guint8 *data = NULL;
  guint16 size = 0;
  gchar *comment;

  if (!gst_byte_reader_get_uint16_be (reader, &size))
    return FALSE;

  size -= 2;
  if (!gst_byte_reader_get_data (reader, size, &data))
    return FALSE;

  comment = get_utf8_from_data (data, size);

  if (comment) {
    GstTagList *taglist = get_tag_list (parse);
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_COMMENT, comment, NULL);
    GST_DEBUG_OBJECT (parse, "collected tags: %" GST_PTR_FORMAT, taglist);
    g_free (comment);
  }

  return TRUE;
}

static gboolean
gst_jpeg_parse_read_header (GstJpegParse * parse, GstMapInfo * map, gint len)
{
  GstByteReader reader;
  guint8 marker = 0;
  gboolean foundSOF = FALSE;

  gst_byte_reader_init (&reader, map->data, len);

  if (!gst_byte_reader_peek_uint8 (&reader, &marker))
    goto error;

  while (marker == 0xff) {
    if (!gst_byte_reader_skip (&reader, 1))
      goto error;

    if (!gst_byte_reader_get_uint8 (&reader, &marker))
      goto error;

    GST_DEBUG_OBJECT (parse, "marker = %x", marker);

    switch (marker) {
      case SOS:                /* start of scan (begins compressed data) */
        goto done;

      case SOI:
        break;

      case DRI:
        if (!gst_byte_reader_skip (&reader, 4)) /* fixed size */
          goto error;
        break;

      case COM:
        if (!gst_jpeg_parse_com (parse, &reader))
          goto error;
        break;

      case APP1:
        if (!gst_jpeg_parse_app1 (parse, &reader))
          goto error;
        break;

      case DHT:
      case DQT:
        /* Ignore these codes */
        if (!gst_jpeg_parse_skip_marker (parse, &reader, marker))
          goto error;
        break;

      case SOF0:
        /* parse Start Of Frame */
        if (!gst_jpeg_parse_sof (parse, &reader))
          goto error;

        foundSOF = TRUE;
        goto done;

      default:
        if (marker == JPG || (marker >= JPG0 && marker <= JPG13) ||
            (marker >= APP0 && marker <= APP15)) {
          if (!gst_jpeg_parse_skip_marker (parse, &reader, marker))
            goto error;
        } else
          goto unhandled;
    }

    if (!gst_byte_reader_peek_uint8 (&reader, &marker))
      goto error;
  }
done:

  return foundSOF;

  /* ERRORS */
error:
  {
    GST_WARNING_OBJECT (parse,
        "Error parsing image header (need more than %u bytes available)",
        gst_byte_reader_get_remaining (&reader));
    return FALSE;
  }
unhandled:
  {
    GST_WARNING_OBJECT (parse, "unhandled marker %x, leaving", marker);
    /* Not SOF or SOI.  Must not be a JPEG file (or file pointer
     * is placed wrong).  In either case, it's an error. */
    return FALSE;
  }
}

static gboolean
gst_jpeg_parse_set_new_caps (GstJpegParse * parse, gboolean header_ok)
{
  GstCaps *caps;
  gboolean res;

  GST_DEBUG_OBJECT (parse, "setting caps on srcpad (hdr_ok=%d, have_fps=%d)",
      header_ok, parse->priv->has_fps);

  caps = gst_caps_new_simple ("image/jpeg",
      "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (header_ok == TRUE) {
    gst_caps_set_simple (caps,
        "format", G_TYPE_STRING, parse->priv->format,
        "width", G_TYPE_INT, parse->priv->width,
        "height", G_TYPE_INT, parse->priv->height, NULL);
  }

  if (parse->priv->has_fps == TRUE) {
    /* we have a framerate */
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
        parse->priv->framerate_numerator,
        parse->priv->framerate_denominator, NULL);

    if (!GST_CLOCK_TIME_IS_VALID (parse->priv->duration)
        && parse->priv->framerate_numerator != 0) {
      parse->priv->duration = gst_util_uint64_scale_int (GST_SECOND,
          parse->priv->framerate_denominator, parse->priv->framerate_numerator);
    }
  } else {
    /* unknown duration */
    parse->priv->duration = GST_CLOCK_TIME_NONE;
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 1, 1, NULL);
  }

  GST_DEBUG_OBJECT (parse,
      "setting downstream caps on %s:%s to %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (GST_BASE_PARSE_SRC_PAD (parse)), caps);
  res = gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
  gst_caps_unref (caps);

  return res;

}

static GstFlowReturn
gst_jpeg_parse_pre_push_frame (GstBaseParse * bparse, GstBaseParseFrame * frame)
{
  GstJpegParse *parse = GST_JPEG_PARSE_CAST (bparse);
  GstBuffer *outbuf = frame->buffer;

  GST_BUFFER_TIMESTAMP (outbuf) = parse->priv->next_ts;

  if (parse->priv->has_fps && GST_CLOCK_TIME_IS_VALID (parse->priv->next_ts)
      && GST_CLOCK_TIME_IS_VALID (parse->priv->duration)) {
    parse->priv->next_ts += parse->priv->duration;
  } else {
    parse->priv->duration = GST_CLOCK_TIME_NONE;
    parse->priv->next_ts = GST_CLOCK_TIME_NONE;
  }

  GST_BUFFER_DURATION (outbuf) = parse->priv->duration;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_jpeg_parse_handle_frame (GstBaseParse * bparse, GstBaseParseFrame * frame,
    gint * skipsize)
{
  GstJpegParse *parse = GST_JPEG_PARSE_CAST (bparse);
  gint len;
  GstClockTime timestamp, duration;
  GstMapInfo mapinfo;
  gboolean header_ok;

  timestamp = GST_BUFFER_PTS (frame->buffer);
  duration = GST_BUFFER_DURATION (frame->buffer);

  if (!gst_buffer_map (frame->buffer, &mapinfo, GST_MAP_READ)) {
    return GST_FLOW_ERROR;
  }

  if (!gst_jpeg_parse_skip_to_jpeg_header (parse, &mapinfo, skipsize)) {
    gst_buffer_unmap (frame->buffer, &mapinfo);
    return GST_FLOW_OK;
  }

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (parse->priv->next_ts)))
    parse->priv->next_ts = timestamp;

  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (duration)))
    parse->priv->duration = duration;

  len = gst_jpeg_parse_get_image_length (parse, &mapinfo);
  if (len == 0) {
    gst_buffer_unmap (frame->buffer, &mapinfo);
    return GST_FLOW_OK;
  } else if (len < 0) {
    *skipsize = -len;
    gst_buffer_unmap (frame->buffer, &mapinfo);
    return GST_FLOW_OK;
  }

  /* check if we already have a EOI */
  GST_LOG_OBJECT (parse, "parsed image of size %d", len);

  /* reset the offset (only when we flushed) */
  parse->priv->last_offset = 0;
  parse->priv->last_entropy_len = 0;

  header_ok = gst_jpeg_parse_read_header (parse, &mapinfo, len);

  gst_buffer_unmap (frame->buffer, &mapinfo);

  if (parse->priv->width != parse->priv->caps_width
      || parse->priv->height != parse->priv->caps_height
      || parse->priv->framerate_numerator !=
      parse->priv->caps_framerate_numerator
      || parse->priv->framerate_denominator !=
      parse->priv->caps_framerate_denominator) {
    if (!gst_jpeg_parse_set_new_caps (parse, header_ok)) {
      GST_ELEMENT_ERROR (parse, CORE, NEGOTIATION,
          ("Can't set caps to the src pad"), ("Can't set caps to the src pad"));
      return GST_FLOW_ERROR;
    }

    if (parse->priv->tags) {
      GST_DEBUG_OBJECT (parse, "Pushing tags: %" GST_PTR_FORMAT,
          parse->priv->tags);
      gst_pad_push_event (GST_BASE_PARSE_SRC_PAD (parse),
          gst_event_new_tag (parse->priv->tags));
      parse->priv->tags = NULL;
    }

    parse->priv->caps_width = parse->priv->width;
    parse->priv->caps_height = parse->priv->height;
    parse->priv->caps_framerate_numerator = parse->priv->framerate_numerator;
    parse->priv->caps_framerate_denominator =
        parse->priv->framerate_denominator;
  }


  return gst_base_parse_finish_frame (bparse, frame, len);
}

static gboolean
gst_jpeg_parse_sink_event (GstBaseParse * bparse, GstEvent * event)
{
  GstJpegParse *parse = GST_JPEG_PARSE_CAST (bparse);
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (parse, "event : %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      parse->priv->next_ts = GST_CLOCK_TIME_NONE;
      parse->priv->duration = GST_CLOCK_TIME_NONE;
      parse->priv->last_offset = 0;
      parse->priv->last_entropy_len = 0;
      parse->priv->last_resync = FALSE;
      res = GST_BASE_PARSE_CLASS (parent_class)->sink_event (bparse, event);
      break;
    case GST_EVENT_TAG:{
      if (gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (parse)))
        res = GST_BASE_PARSE_CLASS (parent_class)->sink_event (bparse, event);
      else {
        GstTagList *taglist = NULL;

        gst_event_parse_tag (event, &taglist);
        /* Hold on to the tags till the srcpad caps are definitely set */
        gst_tag_list_insert (get_tag_list (parse), taglist,
            GST_TAG_MERGE_REPLACE);
        GST_DEBUG ("collected tags: %" GST_PTR_FORMAT, parse->priv->tags);
        gst_event_unref (event);
      }
      break;
    }
    default:
      res = GST_BASE_PARSE_CLASS (parent_class)->sink_event (bparse, event);
      break;
  }

  return res;
}

static gboolean
gst_jpeg_parse_start (GstBaseParse * bparse)
{
  GstJpegParse *parse;

  parse = GST_JPEG_PARSE_CAST (bparse);

  parse->priv->has_fps = FALSE;

  parse->priv->width = parse->priv->height = 0;
  parse->priv->framerate_numerator = 0;
  parse->priv->framerate_denominator = 1;

  parse->priv->caps_framerate_numerator =
      parse->priv->caps_framerate_denominator = 0;
  parse->priv->caps_width = parse->priv->caps_height = -1;

  parse->priv->next_ts = GST_CLOCK_TIME_NONE;
  parse->priv->duration = GST_CLOCK_TIME_NONE;

  parse->priv->last_offset = 0;
  parse->priv->last_entropy_len = 0;
  parse->priv->last_resync = FALSE;

  parse->priv->tags = NULL;

  return TRUE;
}

static gboolean
gst_jpeg_parse_stop (GstBaseParse * bparse)
{
  GstJpegParse *parse;

  parse = GST_JPEG_PARSE_CAST (bparse);

  if (parse->priv->tags) {
    gst_tag_list_unref (parse->priv->tags);
    parse->priv->tags = NULL;
  }

  return TRUE;
}
