/*
 *  gstvaapiutils_h26x.c - H.26x related utilities
 *
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne
 *  Copyright (C) 2017 Intel Corporation
 *    Author: Hyunjun Ko <zzoon@igalia.com>
 *    Author: Mark Thompson <sw@jkqxz.net>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapiutils_h26x_priv.h"

/* Write an unsigned integer Exp-Golomb-coded syntax element. i.e. ue(v) */
gboolean
bs_write_ue (GstBitWriter * bs, guint32 value)
{
  guint32 size_in_bits = 0;
  guint32 tmp_value = ++value;

  while (tmp_value) {
    ++size_in_bits;
    tmp_value >>= 1;
  }
  if (size_in_bits > 1
      && !gst_bit_writer_put_bits_uint32 (bs, 0, size_in_bits - 1))
    return FALSE;
  if (!gst_bit_writer_put_bits_uint32 (bs, value, size_in_bits))
    return FALSE;
  return TRUE;
}

/* Write a signed integer Exp-Golomb-coded syntax element. i.e. se(v) */
gboolean
bs_write_se (GstBitWriter * bs, gint32 value)
{
  guint32 new_val;

  if (value <= 0)
    new_val = -(value << 1);
  else
    new_val = (value << 1) - 1;

  if (!bs_write_ue (bs, new_val))
    return FALSE;
  return TRUE;
}

/* Copy from src to dst, applying emulation prevention bytes.
 *
 * This is copied from libavcodec written by Mark Thompson
 * <sw@jkqxz.net> from
 * http://git.videolan.org/?p=ffmpeg.git;a=commit;h=2c62fcdf5d617791a653d7957d449f75569eede0
 */
static gboolean
gst_vaapi_utils_h26x_nal_unit_to_byte_stream (guint8 * dst, guint * dst_len,
    guint8 * src, guint src_len)
{
  guint dp = 0, sp;
  guint zero_run = 0;

  for (sp = 0; sp < src_len; sp++) {
    if (dp >= *dst_len)
      goto fail;
    if (zero_run < 2) {
      if (src[sp] == 0)
        ++zero_run;
      else
        zero_run = 0;
    } else {
      if ((src[sp] & ~3) == 0) {
        /* emulation_prevention_byte: 0x03 */
        dst[dp++] = 3;
        if (dp >= *dst_len)
          goto fail;
      }
      zero_run = src[sp] == 0;
    }
    dst[dp++] = src[sp];
  }

  *dst_len = dp;
  return TRUE;

fail:
  *dst_len = 0;
  return FALSE;
}

/**
 * gst_vaapi_utils_h26x_write_nal_unit:
 * @bs: a #GstBitWriter instance
 * @nal: the NAL (Network Abstraction Layer) unit to write
 * @nal_size: the size, in bytes, of @nal
 *
 * Writes in the @bs the @nal rewritten with the "emulation prevention
 * bytes" if required.
 *
 * Returns: TRUE if the NAL unit could be coded applying the
 * "emulation prevention bytes"; otherwise FALSE.
 **/
gboolean
gst_vaapi_utils_h26x_write_nal_unit (GstBitWriter * bs, guint8 * nal,
    guint nal_size)
{
  guint8 *byte_stream = NULL;
  guint byte_stream_len;

  byte_stream_len = nal_size + 10;
  byte_stream = g_malloc (byte_stream_len);

  if (!byte_stream)
    return FALSE;

  if (!gst_vaapi_utils_h26x_nal_unit_to_byte_stream (byte_stream,
          &byte_stream_len, nal, nal_size)) {
    g_free (byte_stream);
    return FALSE;
  }

  WRITE_UINT32 (bs, byte_stream_len, 16);
  gst_bit_writer_put_bytes (bs, byte_stream, byte_stream_len);
  g_free (byte_stream);

  return TRUE;

bs_error:
  {
    GST_ERROR ("failed to write codec-data");
    g_free (byte_stream);
    return FALSE;
  }
}
