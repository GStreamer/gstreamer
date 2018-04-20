/* GStreamer
 * Copyright (C) 2018 Edward Hervey <edward@centricular.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/base/gstbytereader.h>
#include "video-anc.h"

/**
 * SECTION:gstvideoanc
 * @title: GstVideo Ancillary
 * @short_description: Utilities for Ancillary data, VBI and Closed Caption
 *
 * A collection of objects and methods to assist with handling Ancillary Data
 * present in Vertical Blanking Interval as well as Closed Caption.
 */


#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("video-anc", 0,
        "Ancillary data, VBI and CC utilities");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

struct _GstVideoVBIParser
{
  GstVideoInfo info;            /* format of the lines provided */
  guint8 *work_data;            /* Converted line in planar 16bit format */
  guint32 work_data_size;       /* Size in bytes of work_data */
  guint offset;                 /* Current offset (in bytes) in work_data */
  gboolean bit16;               /* Data is stored as 16bit if TRUE. Else 8bit(without parity) */
};

G_DEFINE_BOXED_TYPE (GstVideoVBIParser, gst_video_vbi_parser,
    (GBoxedCopyFunc) gst_video_vbi_parser_copy,
    (GBoxedFreeFunc) gst_video_vbi_parser_free);

GstVideoVBIParser *
gst_video_vbi_parser_copy (const GstVideoVBIParser * parser)
{
  GstVideoVBIParser *res;

  res = gst_video_vbi_parser_new (GST_VIDEO_INFO_FORMAT (&parser->info),
      parser->info.width);
  if (res) {
    memcpy (res->work_data, parser->work_data, parser->work_data_size);
  }
  return res;
}

/* Smallest ANC size (which would have a size Data Count of 0 though) */
#define SMALLEST_ANC_SIZE 7

static GstVideoVBIParserResult
get_ancillary_16 (GstVideoVBIParser * parser, GstVideoAncillary * anc)
{
  gboolean found = FALSE;
  guint16 *data = (guint16 *) parser->work_data;

  g_return_val_if_fail (parser != NULL, GST_VIDEO_VBI_PARSER_RESULT_ERROR);
  g_return_val_if_fail (anc != NULL, GST_VIDEO_VBI_PARSER_RESULT_ERROR);

  while (parser->offset < parser->work_data_size + SMALLEST_ANC_SIZE) {
    guint8 DID, SDID, DC;
    guint i;

    /* Look for ADF
     * FIXME : This assumes 10bit data with parity ! */
    if (data[parser->offset] != 0x000 ||
        data[parser->offset + 1] != 0x3ff ||
        data[parser->offset + 2] != 0x3ff) {
      parser->offset += 1;
      continue;
    }

    /* FIXME : Add parity and checksum checks at some point if using
     * 10bit data */

    /* We have a valid ADF */
    DID = data[parser->offset + 3] & 0xff;
    SDID = data[parser->offset + 4] & 0xff;
    DC = data[parser->offset + 5] & 0xff;
    /* Check if we have enough room to get the User Data */
    if (parser->offset >= parser->work_data_size + SMALLEST_ANC_SIZE + DC)
      goto not_enough_data;

    /* We found a valid ANC \o/ */
    anc->DID = DID;
    anc->SDID_block_number = SDID;
    anc->data_count = DC;
    memset (anc->data, 0, 256);
    for (i = 0; i < anc->data_count; i++)
      anc->data[i] = data[parser->offset + 6 + i] & 0xff;
    found = TRUE;
    parser->offset += SMALLEST_ANC_SIZE + DC;
    break;
  }

  if (found)
    return GST_VIDEO_VBI_PARSER_RESULT_OK;

  return GST_VIDEO_VBI_PARSER_RESULT_DONE;

  /* ERRORS */
not_enough_data:
  {
    GST_WARNING ("ANC requires more User Data that available line size");
    /* Avoid further calls to go in the same error */
    parser->offset = parser->work_data_size;
    return GST_VIDEO_VBI_PARSER_RESULT_ERROR;
  }
}

static GstVideoVBIParserResult
get_ancillary_8 (GstVideoVBIParser * parser, GstVideoAncillary * anc)
{
  gboolean found = FALSE;
  guint8 *data = parser->work_data;

  g_return_val_if_fail (parser != NULL, GST_VIDEO_VBI_PARSER_RESULT_ERROR);
  g_return_val_if_fail (anc != NULL, GST_VIDEO_VBI_PARSER_RESULT_ERROR);

  while (parser->offset < parser->work_data_size + SMALLEST_ANC_SIZE) {
    guint8 DID, SDID, DC;
    guint i;

    /* Look for 8bit ADF (0x00 0xff 0xff) */
    if (data[parser->offset] != 0x00 ||
        data[parser->offset + 1] != 0xff || data[parser->offset + 2] != 0xff) {
      parser->offset += 1;
      continue;
    }

    /* We have a valid ADF */
    DID = data[parser->offset + 3];
    SDID = data[parser->offset + 4];
    DC = data[parser->offset + 5];
    /* Check if we have enough room to get the User Data */
    if (parser->offset >= parser->work_data_size + SMALLEST_ANC_SIZE + DC)
      goto not_enough_data;

    /* We found a valid ANC \o/ */
    anc->DID = DID;
    anc->SDID_block_number = SDID;
    anc->data_count = DC;
    memset (anc->data, 0, 256);
    for (i = 0; i < anc->data_count; i++)
      anc->data[i] = data[parser->offset + 6 + i];
    found = TRUE;
    parser->offset += SMALLEST_ANC_SIZE + DC;
    break;
  }

  if (found)
    return GST_VIDEO_VBI_PARSER_RESULT_OK;

  return GST_VIDEO_VBI_PARSER_RESULT_DONE;

  /* ERRORS */
not_enough_data:
  {
    GST_WARNING ("ANC requires more User Data that available line size");
    /* Avoid further calls to go in the same error */
    parser->offset = parser->work_data_size;
    return GST_VIDEO_VBI_PARSER_RESULT_ERROR;
  }
}

/**
 * gst_video_vbi_parser_get_ancillary:
 * @parser: a #GstVideoVBIParser
 * @anc: (out caller-allocates): a #GstVideoAncillary to start the eventual ancillary data
 *
 * Parse the line provided previously by gst_video_vbi_parser_add_line().
 *
 * Since: 1.16
 *
 * Returns: %GST_VIDEO_VBI_PARSER_RESULT_OK if ancillary data was found and
 * @anc was filled. %GST_VIDEO_VBI_PARSER_RESULT_DONE if there wasn't any
 * data.
 */

GstVideoVBIParserResult
gst_video_vbi_parser_get_ancillary (GstVideoVBIParser * parser,
    GstVideoAncillary * anc)
{
  if (parser->bit16)
    return get_ancillary_16 (parser, anc);
  return get_ancillary_8 (parser, anc);
}

/**
 * gst_video_vbi_parser_new:
 * @format: a #GstVideoFormat
 * @pixel_width: The width in pixel to use
 *
 * Create a new #GstVideoVBIParser for the specified @format and @pixel_width.
 *
 * Since: 1.16
 *
 * Returns: The new #GstVideoVBIParser or %NULL if the @format and/or @pixel_width
 * is not supported.
 */
GstVideoVBIParser *
gst_video_vbi_parser_new (GstVideoFormat format, guint32 pixel_width)
{
  GstVideoVBIParser *parser;

  switch (format) {
    case GST_VIDEO_FORMAT_v210:
      parser = g_new0 (GstVideoVBIParser, 1);
      parser->bit16 = TRUE;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      parser = g_new0 (GstVideoVBIParser, 1);
      parser->bit16 = FALSE;
      break;
    default:
      GST_WARNING ("Format not supported by GstVideoVBIParser");
      return NULL;
  }

  gst_video_info_init (&parser->info);
  if (!gst_video_info_set_format (&parser->info, format, pixel_width, 1)) {
    GST_ERROR ("Could not create GstVideoInfo");
    g_free (parser);
    return NULL;
  }

  /* Allocate the workspace which is going to be 2 * pixel_width big
   *  2 : number of pixels per "component" (we only deal with 4:2:2)
   * We use 1 or 2 bytes per pixel depending on whether we are internally
   * working in 8 or 16bit */
  parser->work_data_size = 2 * pixel_width;
  if (parser->bit16)
    parser->work_data = g_malloc0 (parser->work_data_size * 2);
  else
    parser->work_data = g_malloc0 (parser->work_data_size);
  parser->offset = 0;

  return parser;
}

/**
 * gst_video_vbi_parser_free:
 * @parser: a #GstVideoVBIParser
 *
 * Frees the @parser.
 *
 * Since: 1.16
 */
void
gst_video_vbi_parser_free (GstVideoVBIParser * parser)
{
  g_free (parser->work_data);
  g_free (parser);
}


static void
convert_line_uyvy (GstVideoVBIParser * parser, const guint8 * data)
{
  guint i;
  guint8 *y = parser->work_data;
  guint8 *uv = y + parser->info.width;

  for (i = 0; i < parser->info.width - 3; i += 4) {
    *uv++ = data[(i / 4) * 4 + 0];
    *y++ = data[(i / 4) * 4 + 1];
    *uv++ = data[(i / 4) * 4 + 2];
    *y++ = data[(i / 4) * 4 + 3];
  }
  GST_MEMDUMP ("Converted line", parser->work_data, 128);
}

static void
gst_info_dump_mem16_line (gchar * linebuf, gsize linebuf_size,
    const guint16 * mem, gsize mem_offset, gsize mem_size)
{
  gchar hexstr[50], digitstr[6];

  if (mem_size > 8)
    mem_size = 8;

  hexstr[0] = '\0';

  if (mem != NULL) {
    guint i = 0;

    mem += mem_offset;
    while (i < mem_size) {
      g_snprintf (digitstr, sizeof (digitstr), "%04x ", mem[i]);
      g_strlcat (hexstr, digitstr, sizeof (hexstr));
      ++i;
    }
  }

  g_snprintf (linebuf, linebuf_size, "%08x: %-48.48s",
      (guint) mem_offset, hexstr);
}

static void
convert_line_v210 (GstVideoVBIParser * parser, const guint8 * data)
{
  guint i;
  guint16 *y = (guint16 *) parser->work_data;
  guint16 *uv = y + parser->info.width;
  guint32 a, b, c, d;

  /* Convert the line */
  for (i = 0; i < parser->info.width - 5; i += 6) {
    a = GST_READ_UINT32_LE (data + (i / 6) * 16 + 0);
    b = GST_READ_UINT32_LE (data + (i / 6) * 16 + 4);
    c = GST_READ_UINT32_LE (data + (i / 6) * 16 + 8);
    d = GST_READ_UINT32_LE (data + (i / 6) * 16 + 12);

    *uv++ = (a >> 0) & 0x3ff;
    *y++ = (a >> 10) & 0x3ff;
    *uv++ = (a >> 20) & 0x3ff;
    *y++ = (b >> 0) & 0x3ff;

    *uv++ = (b >> 10) & 0x3ff;
    *y++ = (b >> 20) & 0x3ff;
    *uv++ = (c >> 0) & 0x3ff;
    *y++ = (c >> 10) & 0x3ff;

    *uv++ = (c >> 20) & 0x3ff;
    *y++ = (d >> 0) & 0x3ff;
    *uv++ = (d >> 10) & 0x3ff;
    *y++ = (d >> 20) & 0x3ff;
  }

  if (0) {
    guint off = 0;
    gsize length = parser->info.width * 2;

    GST_TRACE ("--------"
        "-------------------------------------------------------------------");

    while (off < length) {
      gchar buf[128];

      /* gst_info_dump_mem_line will process 16 bytes (8 16bit chunks) at most */
      gst_info_dump_mem16_line (buf, sizeof (buf),
          (guint16 *) parser->work_data, off, length - off);
      GST_TRACE ("%s", buf);
      off += 8;
    }
    GST_TRACE ("--------"
        "-------------------------------------------------------------------");
  }
}

/**
 * gst_video_vbi_parser_add_line:
 * @parser: a #GstVideoVBIParser
 * @data: (array) (transfer none): The line of data to parse
 *
 * Provide a new line of data to the @parser. Call gst_video_vbi_parser_get_ancillary()
 * to get the Ancillary data that might be present on that line.
 *
 * Since: 1.16
 */
void
gst_video_vbi_parser_add_line (GstVideoVBIParser * parser, const guint8 * data)
{
  /* Reset offset */
  parser->offset = 0;

  switch (GST_VIDEO_INFO_FORMAT (&parser->info)) {
    case GST_VIDEO_FORMAT_v210:
      convert_line_v210 (parser, data);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      convert_line_uyvy (parser, data);
      break;
    default:
      GST_ERROR ("UNSUPPORTED FORMAT !");
      g_assert_not_reached ();
      break;
  }
}

/* Closed Caption Meta implementation *******************************************/

GType
gst_video_caption_meta_api_get_type (void)
{
  static volatile GType type;

  if (g_once_init_enter (&type)) {
    static const gchar *tags[] = { NULL };
    GType _type = gst_meta_api_type_register ("GstVideoCaptionMetaAPI", tags);
    GST_INFO ("registering");
    g_once_init_leave (&type, _type);
  }
  return type;
}


static gboolean
gst_video_caption_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoCaptionMeta *dmeta, *smeta;

  /* We always copy over the caption meta */
  smeta = (GstVideoCaptionMeta *) meta;

  GST_DEBUG ("copy caption metadata");
  dmeta =
      gst_buffer_add_video_caption_meta (dest, smeta->caption_type,
      smeta->data, smeta->size);
  if (!dmeta)
    return FALSE;

  return TRUE;
}

static gboolean
gst_video_caption_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoCaptionMeta *emeta = (GstVideoCaptionMeta *) meta;

  emeta->caption_type = GST_VIDEO_CAPTION_TYPE_UNKNOWN;
  emeta->data = NULL;
  emeta->size = 0;

  return TRUE;
}

static void
gst_video_caption_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoCaptionMeta *emeta = (GstVideoCaptionMeta *) meta;

  g_free (emeta->data);
}

const GstMetaInfo *
gst_video_caption_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_VIDEO_CAPTION_META_API_TYPE,
        "GstVideoCaptionMeta",
        sizeof (GstVideoCaptionMeta),
        gst_video_caption_meta_init,
        gst_video_caption_meta_free,
        gst_video_caption_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & meta_info, (GstMetaInfo *) mi);
  }
  return meta_info;
}

/**
 * gst_buffer_add_video_caption_meta:
 * @buffer: a #GstBuffer
 * @caption_type: The type of Closed Caption to add
 * @data: (array length=size) (transfer none): The Closed Caption data
 * @size: The size of @data in bytes
 *
 * Attaches #GstVideoCaptionMeta metadata to @buffer with the given
 * parameters.
 *
 * Returns: (transfer none): the #GstVideoCaptionMeta on @buffer.
 *
 * Since: 1.16
 */
GstVideoCaptionMeta *
gst_buffer_add_video_caption_meta (GstBuffer * buffer,
    GstVideoCaptionType caption_type, const guint8 * data, gsize size)
{
  GstVideoCaptionMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);

  switch (caption_type) {
    case GST_VIDEO_CAPTION_TYPE_CEA608_RAW:
    case GST_VIDEO_CAPTION_TYPE_CEA608_IN_CEA708_RAW:
    case GST_VIDEO_CAPTION_TYPE_CEA708_RAW:
    case GST_VIDEO_CAPTION_TYPE_CEA708_CDP:
      break;
    default:
      GST_ERROR ("Unknown caption type !");
      return NULL;
  }
  /* FIXME : Add checks for content ? */

  meta = (GstVideoCaptionMeta *) gst_buffer_add_meta (buffer,
      GST_VIDEO_CAPTION_META_INFO, NULL);
  g_return_val_if_fail (meta != NULL, NULL);

  meta->caption_type = caption_type;
  meta->data = g_memdup (data, size);
  meta->size = size;

  return meta;
}
