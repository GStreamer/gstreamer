/* GStreamer
 * Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
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
#  include "config.h"
#endif

#include "mpegvideoparse.h"

#include <string.h>
#include <gst/base/gstbitreader.h>

GST_DEBUG_CATEGORY_EXTERN (mpegv_parse_debug);
#define GST_CAT_DEFAULT mpegv_parse_debug


#define GET_BITS(b, num, bits) G_STMT_START {        \
  if (!gst_bit_reader_get_bits_uint32(b, bits, num)) \
    goto failed;                                     \
  GST_TRACE ("parsed %d bits: %d", num, *(bits));    \
} G_STMT_END

#define MARKER_BIT(b) G_STMT_START {  \
  guint32 i;                          \
  GET_BITS(b, 1, &i);                 \
  if (i != 0x1)                       \
    goto failed;                      \
} G_STMT_END

static inline gboolean
find_start_code (GstBitReader * b)
{
  guint32 bits;

  /* 0 bits until byte aligned */
  while (b->bit != 0) {
    GET_BITS (b, 1, &bits);
  }

  /* 0 bytes until startcode */
  while (gst_bit_reader_peek_bits_uint32 (b, &bits, 32)) {
    if (bits >> 8 == 0x1) {
      return TRUE;
    } else {
      gst_bit_reader_skip (b, 8);
    }
  }

  return FALSE;

failed:
  return FALSE;
}

static gboolean
gst_mpeg_video_params_parse_extension (MPEGVParams * params, GstBitReader * br)
{
  guint32 bits;

  /* double-check */
  GET_BITS (br, 32, &bits);
  if (bits != 0x100 + MPEG_PACKET_EXTENSION)
    goto failed;

  /* extension_start_code identifier */
  GET_BITS (br, 4, &bits);

  /* profile_and_level_indication */
  GET_BITS (br, 4, &bits);
  params->profile = bits;
  GET_BITS (br, 4, &bits);
  params->level = bits;

  /* progressive_sequence */
  GET_BITS (br, 1, &bits);
  params->progressive = bits;

  /* chroma_format */
  GET_BITS (br, 2, &bits);

  /* horizontal_size_extension */
  GET_BITS (br, 2, &bits);
  params->width += (bits << 12);
  /* vertical_size_extension */
  GET_BITS (br, 2, &bits);
  params->height += (bits << 12);

  /* bit_rate_extension */
  GET_BITS (br, 12, &bits);
  if (params->bitrate)
    params->bitrate += (bits << 18) * 400;
  /* marker_bit */
  MARKER_BIT (br);
  /* vbv_buffer_size_extension */
  GET_BITS (br, 8, &bits);
  /* low_delay */
  GET_BITS (br, 1, &bits);

  /* frame_rate_extension_n */
  GET_BITS (br, 2, &bits);
  params->fps_n *= bits + 1;
  /* frame_rate_extension_d */
  GET_BITS (br, 5, &bits);
  params->fps_d *= bits + 1;

  return TRUE;

  /* ERRORS */
failed:
  {
    GST_WARNING ("Failed to parse sequence extension");
    return FALSE;
  }
}

/* Set the Pixel Aspect Ratio in our hdr from a DAR code in the data */
static void
set_par_from_dar (MPEGVParams * params, guint8 asr_code)
{
  /* Pixel_width = DAR_width * display_vertical_size */
  /* Pixel_height = DAR_height * display_horizontal_size */
  switch (asr_code) {
    case 0x02:                 /* 3:4 DAR = 4:3 pixels */
      params->par_w = 4 * params->height;
      params->par_h = 3 * params->width;
      break;
    case 0x03:                 /* 9:16 DAR */
      params->par_w = 16 * params->height;
      params->par_h = 9 * params->width;
      break;
    case 0x04:                 /* 1:2.21 DAR */
      params->par_w = 221 * params->height;
      params->par_h = 100 * params->width;
      break;
    case 0x01:                 /* Square pixels */
      params->par_w = params->par_h = 1;
      break;
    default:
      GST_DEBUG ("unknown/invalid aspect_ratio_information %d", asr_code);
      break;
  }
}

static void
set_fps_from_code (MPEGVParams * params, guint8 fps_code)
{
  const gint framerates[][2] = {
    {30, 1}, {24000, 1001}, {24, 1}, {25, 1},
    {30000, 1001}, {30, 1}, {50, 1}, {60000, 1001},
    {60, 1}, {30, 1}
  };

  if (fps_code && fps_code < 10) {
    params->fps_n = framerates[fps_code][0];
    params->fps_d = framerates[fps_code][1];
  } else {
    GST_DEBUG ("unknown/invalid frame_rate_code %d", fps_code);
    /* Force a valid framerate */
    /* FIXME or should this be kept unknown ?? */
    params->fps_n = 30000;
    params->fps_d = 1001;
  }
}

static gboolean
gst_mpeg_video_params_parse_sequence (MPEGVParams * params, GstBitReader * br)
{
  guint32 bits;

  GET_BITS (br, 32, &bits);
  if (bits != 0x100 + MPEG_PACKET_SEQUENCE)
    goto failed;

  /* assume MPEG-1 till otherwise discovered */
  params->mpeg_version = 1;

  GET_BITS (br, 12, &bits);
  params->width = bits;
  GET_BITS (br, 12, &bits);
  params->height = bits;

  GET_BITS (br, 4, &bits);
  set_par_from_dar (params, bits);
  GET_BITS (br, 4, &bits);
  set_fps_from_code (params, bits);

  GET_BITS (br, 18, &bits);
  if (bits == 0x3ffff) {
    /* VBR stream */
    params->bitrate = 0;
  } else {
    /* Value in header is in units of 400 bps */
    params->bitrate *= 400;
  }

  /* constrained_parameters_flag */
  GET_BITS (br, 1, &bits);

  /* load_intra_quantiser_matrix */
  GET_BITS (br, 1, &bits);
  if (bits) {
    if (!gst_bit_reader_skip (br, 8 * 64))
      goto failed;
  }

  /* load_non_intra_quantiser_matrix */
  GET_BITS (br, 1, &bits);
  if (bits) {
    if (!gst_bit_reader_skip (br, 8 * 64))
      goto failed;
  }

  /* check for MPEG-2 sequence extension */
  while (find_start_code (br)) {
    gst_bit_reader_peek_bits_uint32 (br, &bits, 32);
    if (bits == 0x100 + MPEG_PACKET_EXTENSION) {
      if (!gst_mpeg_video_params_parse_extension (params, br))
        goto failed;
      params->mpeg_version = 2;
    }
  }

  /* dump some info */
  GST_LOG ("width x height: %d x %d", params->width, params->height);
  GST_LOG ("fps: %d/%d", params->fps_n, params->fps_d);
  GST_LOG ("par: %d/%d", params->par_w, params->par_h);
  GST_LOG ("profile/level: %d/%d", params->profile, params->level);
  GST_LOG ("bitrate/progressive: %d/%d", params->bitrate, params->progressive);

  return TRUE;

  /* ERRORS */
failed:
  {
    GST_WARNING ("Failed to parse sequence header");
    /* clear out stuff */
    memset (params, 0, sizeof (*params));
    return FALSE;
  }
}

gboolean
gst_mpeg_video_params_parse_config (MPEGVParams * params, const guint8 * data,
    guint size)
{
  GstBitReader br;

  if (size < 4)
    return FALSE;

  gst_bit_reader_init (&br, data, size);

  return gst_mpeg_video_params_parse_sequence (params, &br);
}
