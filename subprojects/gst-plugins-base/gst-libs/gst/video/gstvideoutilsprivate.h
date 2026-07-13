/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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

#ifndef __GST_VIDEO_H__
#include <gst/video/video.h>
#endif

#ifndef _GST_VIDEO_UTILS_PRIVATE_H_
#define _GST_VIDEO_UTILS_PRIVATE_H_

#include <gst/gst.h>

G_BEGIN_DECLS

/* IEEE half-float (binary16) <-> float conversion, shared by the RGBA_F16
 * (un)pack in video-format.c and the RGBA_F16 <-> RGBA_F32 converter fast
 * path in video-converter.c. */
static inline gfloat
gst_half_to_float (guint16 h)
{
  union
  {
    guint32 i;
    gfloat f;
  } u;
  guint32 sign = ((guint32) h & 0x8000) << 16;
  guint32 exponent = (h >> 10) & 0x1f;
  guint32 mantissa = h & 0x3ff;

  if (exponent == 0) {
    if (mantissa == 0) {
      u.i = sign;
      return u.f;
    }
    /* subnormal half: renormalize */
    exponent = 127 - 15 + 1;
    while ((mantissa & 0x400) == 0) {
      mantissa <<= 1;
      exponent--;
    }
    mantissa &= 0x3ff;
    u.i = sign | (exponent << 23) | (mantissa << 13);
  } else if (exponent == 31) {
    u.i = sign | 0x7f800000 | (mantissa << 13);
  } else {
    u.i = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
  }
  return u.f;
}

static inline guint16
gst_float_to_half (gfloat f)
{
  union
  {
    gfloat f;
    guint32 i;
  } u;
  guint32 sign, mantissa, shift, rest, halfway;
  gint32 exponent;
  guint16 h;

  u.f = f;
  sign = (u.i >> 16) & 0x8000;
  exponent = (gint32) ((u.i >> 23) & 0xff) - 127 + 15;
  mantissa = u.i & 0x7fffff;

  /* infinity / NaN: NaN keeps the top payload bits, with the (mantissa == 0)
   * term ensuring a NaN whose payload is truncated away stays a NaN */
  if (((u.i >> 23) & 0xff) == 0xff) {
    if (mantissa == 0)
      return sign | 0x7c00;
    mantissa >>= 13;
    return sign | 0x7c00 | mantissa | (mantissa == 0);
  }

  /* overflow: round to infinity */
  if (exponent >= 31)
    return sign | 0x7c00;

  /* subnormal half or underflow to zero, round to nearest even */
  if (exponent <= 0) {
    if (exponent < -10)
      return sign;
    mantissa |= 0x800000;
    shift = 14 - exponent;
    h = mantissa >> shift;
    rest = mantissa & ((1u << shift) - 1);
    halfway = 1u << (shift - 1);
    if (rest > halfway || (rest == halfway && (h & 1)))
      h++;
    return sign | h;
  }

  /* round to nearest even, a carry into the exponent rounds up to infinity */
  h = (exponent << 10) | (mantissa >> 13);
  rest = mantissa & 0x1fff;
  if (rest > 0x1000 || (rest == 0x1000 && (h & 1)))
    h++;
  return sign | h;
}

/* Element utility functions */
G_GNUC_INTERNAL
GstCaps *__gst_video_element_proxy_getcaps (GstElement * element, GstPad * sinkpad,
                                            GstPad * srcpad, GstCaps * initial_caps,
                                            GstCaps * filter);

G_GNUC_INTERNAL
gboolean __gst_video_encoded_video_convert (gint64 bytes, gint64 time,
                                            GstFormat src_format, gint64 src_value,
                                            GstFormat * dest_format, gint64 * dest_value);

G_GNUC_INTERNAL
gboolean __gst_video_rawvideo_convert (GstVideoCodecState * state, GstFormat src_format,
                                       gint64 src_value, GstFormat * dest_format,
                                       gint64 * dest_value);

G_END_DECLS

#endif
