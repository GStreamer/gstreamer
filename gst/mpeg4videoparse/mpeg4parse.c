/* GStreamer MPEG4-2 video Parser
 * Copyright (C) <2008> Mindfruit B.V.
 *   @author Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
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

#include "mpeg4parse.h"

#include <gst/base/gstbitreader.h>

GST_DEBUG_CATEGORY_EXTERN (mpeg4v_parse_debug);
#define GST_CAT_DEFAULT mpeg4v_parse_debug


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
next_start_code (GstBitReader * b)
{
  guint32 bits = 0;

  GET_BITS (b, 1, &bits);
  if (bits != 0)
    goto failed;

  while (b->bit != 0) {
    GET_BITS (b, 1, &bits);
    if (bits != 0x1)
      goto failed;
  }

  return TRUE;

failed:
  return FALSE;
}

static inline gboolean
skip_user_data (GstBitReader * bs, guint32 * bits)
{
  while (*bits == MPEG4_USER_DATA_STARTCODE_MARKER) {
    guint32 b = 0;

    do {
      GET_BITS (bs, 8, &b);
      *bits = (*bits << 8) | b;
    } while ((*bits >> 8) != MPEG4_START_MARKER);
  }

  return TRUE;

failed:
  return FALSE;
}


static gint aspect_ratio_table[6][2] = {
  {-1, -1}, {1, 1}, {12, 11}, {10, 11}, {16, 11}, {40, 33}
};

static gboolean
gst_mpeg4_params_parse_vo (MPEG4Params * params, GstBitReader * br)
{
  guint32 bits;
  guint16 time_increment_resolution = 0;
  guint16 fixed_time_increment = 0;
  gint aspect_ratio_width = -1, aspect_ratio_height = -1;
  gint height = -1, width = -1;

  /* expecting a video object startcode */
  GET_BITS (br, 32, &bits);
  if (bits > 0x11F)
    goto failed;

  /* expecting a video object layer startcode */
  GET_BITS (br, 32, &bits);
  if (bits < 0x120 || bits > 0x12F)
    goto failed;

  /* ignore random accessible vol  and video object type indication */
  GET_BITS (br, 9, &bits);

  GET_BITS (br, 1, &bits);
  if (bits) {
    /* skip video object layer verid and priority */
    GET_BITS (br, 7, &bits);
  }

  /* aspect ratio info */
  GET_BITS (br, 4, &bits);
  if (bits == 0)
    goto failed;

  /* check if aspect ratio info  is extended par */
  if (bits == 0xf) {
    GET_BITS (br, 8, &bits);
    aspect_ratio_width = bits;
    GET_BITS (br, 8, &bits);
    aspect_ratio_height = bits;
  } else if (bits < 0x6) {
    aspect_ratio_width = aspect_ratio_table[bits][0];
    aspect_ratio_height = aspect_ratio_table[bits][1];
  }
  GST_DEBUG ("aspect ratio %d/%d", aspect_ratio_width, aspect_ratio_height);

  GET_BITS (br, 1, &bits);
  if (bits) {
    /* vol control parameters, skip chroma and low delay */
    GET_BITS (br, 3, &bits);
    GET_BITS (br, 1, &bits);
    if (bits) {
      /* skip vbv_parameters */
      if (!gst_bit_reader_skip (br, 79))
        goto failed;
    }
  }

  /* layer shape */
  GET_BITS (br, 2, &bits);
  /* only support rectangular */
  if (bits != 0)
    goto failed;

  MARKER_BIT (br);
  GET_BITS (br, 16, &bits);
  time_increment_resolution = bits;
  MARKER_BIT (br);

  GST_DEBUG ("time increment resolution %d", time_increment_resolution);

  GET_BITS (br, 1, &bits);
  if (bits) {
    /* fixed time increment */
    int n;

    /* Length of the time increment is the minimal number of bits needed to
     * represent time_increment_resolution */
    for (n = 0; (time_increment_resolution >> n) != 0; n++);
    GET_BITS (br, n, &bits);

    fixed_time_increment = bits;
  } else {
    /* When fixed_vop_rate is not set we can't guess any framerate */
    fixed_time_increment = 0;
  }
  GST_DEBUG ("fixed time increment %d", fixed_time_increment);

  /* assuming rectangular shape */
  MARKER_BIT (br);
  GET_BITS (br, 13, &bits);
  width = bits;
  MARKER_BIT (br);
  GET_BITS (br, 13, &bits);
  height = bits;
  MARKER_BIT (br);
  GST_DEBUG ("width x height: %d x %d", width, height);

  /* so we got it all, report back */
  params->width = width;
  params->height = height;
  params->time_increment_resolution = time_increment_resolution;
  params->fixed_time_increment = fixed_time_increment;
  params->aspect_ratio_width = aspect_ratio_width;
  params->aspect_ratio_height = aspect_ratio_height;

  return TRUE;

  /* ERRORS */
failed:
  {
    GST_WARNING ("Failed to parse config data");
    return FALSE;
  }
}

static gboolean
gst_mpeg4_params_parse_vos (MPEG4Params * params, GstBitReader * br)
{
  guint32 bits = 0;

  GET_BITS (br, 32, &bits);
  if (bits != MPEG4_VOS_STARTCODE_MARKER)
    goto failed;

  GET_BITS (br, 8, &bits);
  params->profile = bits;

  /* invalid profile, warn but carry on */
  if (params->profile == 0) {
    GST_WARNING ("Invalid profile in VOS");
  }

  /* Expect Visual Object startcode */
  GET_BITS (br, 32, &bits);

  /* but skip optional user data */
  if (!skip_user_data (br, &bits))
    goto failed;

  if (bits != MPEG4_VISUAL_OBJECT_STARTCODE_MARKER)
    goto failed;

  GET_BITS (br, 1, &bits);
  if (bits == 0x1) {
    /* Skip visual_object_verid and priority */
    GET_BITS (br, 7, &bits);
  }

  GET_BITS (br, 4, &bits);
  /* Only support video ID */
  if (bits != 0x1)
    goto failed;

  /* video signal type */
  GET_BITS (br, 1, &bits);

  if (bits == 0x1) {
    /* video signal type, ignore format and range */
    GET_BITS (br, 4, &bits);

    GET_BITS (br, 1, &bits);
    if (bits == 0x1) {
      /* ignore color description */
      GET_BITS (br, 24, &bits);
    }
  }

  if (!next_start_code (br))
    goto failed;

  /* skip optional user data */
  GET_BITS (br, 32, &bits);
  if (!skip_user_data (br, &bits))
    goto failed;

  /* rewind to start code */
  gst_bit_reader_set_pos (br, gst_bit_reader_get_pos (br) - 32);

  return gst_mpeg4_params_parse_vo (params, br);

  /* ERRORS */
failed:
  {
    GST_WARNING ("Failed to parse config data");
    return FALSE;
  }
}

gboolean
gst_mpeg4_params_parse_config (MPEG4Params * params, const guint8 * data,
    guint size)
{
  GstBitReader br;

  if (size < 4)
    return FALSE;

  gst_bit_reader_init (&br, data, size);

  if (data[3] == MPEG4_VOS_STARTCODE)
    return gst_mpeg4_params_parse_vos (params, &br);
  else
    return gst_mpeg4_params_parse_vo (params, &br);
}
