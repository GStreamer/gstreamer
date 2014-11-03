/* GStreamer
 * Copyright (C) <2014> Wim Taymans <wim.taymans@gmail.com>
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
#include <stdio.h>
#include <math.h>

#ifndef DISABLE_ORC
#include <orc/orcfunctions.h>
#else
#define orc_memcpy memcpy
#endif

#include "video-orc.h"
#include "video-scaler.h"

#define S16_SCALE       12
#define S16_SCALE_ROUND (1 << (S16_SCALE -1))

#define LQ

typedef void (*GstVideoScalerHFunc) (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width);
typedef void (*GstVideoScalerVFunc) (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width);

struct _GstVideoScaler
{
  GstVideoResamplerMethod method;
  GstVideoScalerFlags flags;

  GstVideoResampler resampler;

  /* cached integer coefficients */
  gint16 *taps_s16;
  gint16 *taps_s16_4;
  guint32 *offset_n;
  /* for ORC */
  gint inc;

  guint32 *tmpline1;
  guint32 *tmpline2;
};

static void
resampler_zip (GstVideoResampler * resampler, const GstVideoResampler * r1,
    const GstVideoResampler * r2)
{
  guint i, out_size, max_taps;
  gdouble *taps;
  guint32 *offset, *phase;

  g_return_if_fail (r1->max_taps == r2->max_taps);

  out_size = r1->out_size + r2->out_size;
  max_taps = r1->max_taps;
  offset = g_malloc (sizeof (guint32) * out_size);
  phase = g_malloc (sizeof (guint32) * out_size);
  taps = g_malloc (sizeof (gdouble) * max_taps * out_size);

  resampler->in_size = r1->in_size + r2->in_size;
  resampler->out_size = out_size;
  resampler->max_taps = max_taps;
  resampler->offset = offset;
  resampler->phase = phase;
  resampler->n_taps = g_malloc (sizeof (guint32) * out_size);
  resampler->taps = taps;

  for (i = 0; i < out_size; i++) {
    guint idx = i / 2;
    const GstVideoResampler *r;

    r = (i & 1) ? r2 : r1;

    offset[i] = r->offset[idx] * 2 + (i & 1);
    phase[i] = i;

    memcpy (taps + i * max_taps, r->taps + idx * max_taps,
        max_taps * sizeof (gdouble));
  }
}

/**
 * gst_video_scaler_new:
 * @method: a #GstVideoResamplerMethod
 * @flags: #GstVideoScalerFlags
 * @n_taps: number of taps to use
 * @in_size: number of source elements
 * @out_size: number of destination elements
 * @options: (allow-none): extra options
 *
 * Make a new @method video scaler. @in_size source lines/pixels will
 * be scaled to @out_size destination lines/pixels.
 *
 * @n_taps specifies the amount of pixels to use from the source for one output
 * pixel. If n_taps is 0, this function chooses a good value automatically based
 * on the @method and @in_size/@out_size.
 *
 * Returns: a #GstVideoResample
 */
GstVideoScaler *
gst_video_scaler_new (GstVideoResamplerMethod method, GstVideoScalerFlags flags,
    guint n_taps, guint in_size, guint out_size, GstStructure * options)
{
  GstVideoScaler *scale;

  g_return_val_if_fail (in_size != 0, NULL);
  g_return_val_if_fail (out_size != 0, NULL);

  scale = g_slice_new0 (GstVideoScaler);

  GST_DEBUG ("%d %u  %u->%u", method, n_taps, in_size, out_size);

  scale->method = method;
  scale->flags = flags;

  if (flags & GST_VIDEO_SCALER_FLAG_INTERLACED) {
    GstVideoResampler tresamp, bresamp;

    gst_video_resampler_init (&tresamp, method, 0, (out_size + 1) / 2, n_taps,
        0.0, (in_size + 1) / 2, (out_size + 1) / 2, options);

    gst_video_resampler_init (&bresamp, method, 0, out_size - tresamp.out_size,
        n_taps, -1.0, in_size - tresamp.in_size,
        out_size - tresamp.out_size, options);

    resampler_zip (&scale->resampler, &tresamp, &bresamp);
    gst_video_resampler_clear (&tresamp);
    gst_video_resampler_clear (&bresamp);
  } else {
    gst_video_resampler_init (&scale->resampler, method,
        GST_VIDEO_RESAMPLER_FLAG_NONE, out_size, n_taps, 0.0, in_size, out_size,
        options);
  }

  if (out_size == 1)
    scale->inc = 0;
  else
    scale->inc = ((in_size - 1) << 16) / (out_size - 1) - 1;

  scale->tmpline1 =
      g_malloc (sizeof (gint32) * out_size * 4 * scale->resampler.max_taps);
  scale->tmpline2 = g_malloc (sizeof (gint32) * out_size * 4);

  return scale;
}

/**
 * gst_video_scaler_free:
 * @scale: a #GstVideoScaler
 *
 * Free a previously allocated #GstVideoScaler @scale.
 */
void
gst_video_scaler_free (GstVideoScaler * scale)
{
  g_return_if_fail (scale != NULL);

  gst_video_resampler_clear (&scale->resampler);
  g_free (scale->taps_s16);
  g_free (scale->taps_s16_4);
  g_free (scale->offset_n);
  g_free (scale->tmpline1);
  g_free (scale->tmpline2);
  g_slice_free (GstVideoScaler, scale);
}

/**
 * gst_video_scaler_get_coeff:
 * @scale: a #GstVideoScaler
 * @out_offset: an output offset
 * @in_offset: result input offset
 * @n_taps: result n_taps
 *
 * For a given pixel at @out_offset, get the first required input pixel at
 * @in_offset and the @n_taps filter coefficients.
 *
 * Note that for interlaced content, @in_offset needs to be incremented with
 * 2 to get the next input line.
 *
 * Returns: an array of @n_tap gdouble values with filter coefficients.
 */
const gdouble *
gst_video_scaler_get_coeff (GstVideoScaler * scale,
    guint out_offset, guint * in_offset, guint * n_taps)
{
  guint offset, phase;

  g_return_val_if_fail (scale != NULL, NULL);
  g_return_val_if_fail (out_offset < scale->resampler.out_size, NULL);

  offset = scale->resampler.offset[out_offset];
  phase = scale->resampler.phase[out_offset];

  if (in_offset)
    *in_offset = offset;
  if (n_taps) {
    *n_taps = scale->resampler.max_taps;
    if (scale->flags & GST_VIDEO_SCALER_FLAG_INTERLACED)
      *n_taps *= 2;
  }
  return scale->resampler.taps + phase * scale->resampler.max_taps;
}

static gboolean
resampler_convert_coeff (const gdouble * src,
    gpointer dest, guint n, guint bits, guint precision)
{
  gdouble multiplier;
  gint i, j;
  gdouble offset, l_offset, h_offset;
  gboolean exact = FALSE;

  multiplier = (1 << precision);

  /* Round to integer, but with an adjustable bias that we use to
   * eliminate the DC error. */
  l_offset = 0.0;
  h_offset = 1.0;
  offset = 0.5;

  for (i = 0; i < 64; i++) {
    gint sum = 0;

    for (j = 0; j < n; j++) {
      gint16 tap = floor (offset + src[j] * multiplier);

      ((gint16 *) dest)[j] = tap;

      sum += tap;
    }
    if (sum == (1 << precision)) {
      exact = TRUE;
      break;
    }

    if (l_offset == h_offset)
      break;

    if (sum < (1 << precision)) {
      if (offset > l_offset)
        l_offset = offset;
      offset += (h_offset - l_offset) / 2;
    } else {
      if (offset < h_offset)
        h_offset = offset;
      offset -= (h_offset - l_offset) / 2;
    }
  }

  if (!exact)
    GST_WARNING ("can't find exact taps");

  return exact;
}

static void
make_s16_taps (GstVideoScaler * scale, gint precision)
{
  gint i, j, max_taps, n_phases, out_size;
  gint16 *taps_s16, *taps_s16_4;
  gdouble *taps;
  guint32 *phase, *offset, *offset_n;

  n_phases = scale->resampler.n_phases;
  max_taps = scale->resampler.max_taps;

  taps = scale->resampler.taps;
  taps_s16 = scale->taps_s16 = g_malloc (sizeof (gint16) * n_phases * max_taps);

  for (i = 0; i < n_phases; i++) {
    resampler_convert_coeff (taps, taps_s16, max_taps, 16, precision);

    taps += max_taps;
    taps_s16 += max_taps;
  }

  out_size = scale->resampler.out_size;

  taps_s16 = scale->taps_s16;
  phase = scale->resampler.phase;
  offset = scale->resampler.offset;

  taps_s16_4 = scale->taps_s16_4 =
      g_malloc (sizeof (gint16) * out_size * max_taps * 4);
  offset_n = scale->offset_n =
      g_malloc (sizeof (guint32) * out_size * max_taps);

  for (j = 0; j < max_taps; j++) {
    for (i = 0; i < out_size; i++) {
      gint16 tap;

      offset_n[j * out_size + i] = offset[i] + j;
      tap = taps_s16[phase[i] * max_taps + j];
      taps_s16_4[(j * out_size + i) * 4 + 0] = tap;
      taps_s16_4[(j * out_size + i) * 4 + 1] = tap;
      taps_s16_4[(j * out_size + i) * 4 + 2] = tap;
      taps_s16_4[(j * out_size + i) * 4 + 3] = tap;
    }
  }
}

static void
video_scale_h_near_8888 (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  guint32 *s, *d;

  d = (guint32 *) dest + dest_offset;
  s = (guint32 *) src;

#if 0
  /* ORC is slower on this */
  video_orc_resample_h_near_8888_lq (d, s, 0, scale->inc, width);
#else
  {
    gint i;
    guint32 *offset;

    offset = scale->resampler.offset + dest_offset;
    for (i = 0; i < width; i++)
      d[i] = s[offset[i]];
  }
#endif
}

static void
video_scale_h_2tap_8888 (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  guint32 *s, *d;

  d = (guint32 *) dest + dest_offset;
  s = (guint32 *) src;

  video_orc_resample_h_2tap_8888_lq (d, s, 0, scale->inc, width);
}

static void
video_scale_h_ntap_8888 (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  gint16 *taps;
  gint i, max_taps, count;
  guint8 *d;
  guint32 *offset_n;
  guint32 *pixels;
  gint32 *temp;

  if (scale->taps_s16 == NULL)
#ifdef LQ
    make_s16_taps (scale, 6);
#else
    make_s16_taps (scale, S16_SCALE);
#endif

  max_taps = scale->resampler.max_taps;
  offset_n = scale->offset_n;

  d = (guint8 *) dest + 4 * dest_offset;

  /* prepare the arrays FIXME, we can add this into ORC */
  count = width * max_taps;
  pixels = (guint32 *) scale->tmpline1;
  for (i = 0; i < count; i++)
    pixels[i] = ((guint32 *) src)[offset_n[i]];

  temp = (gint32 *) scale->tmpline2;
  taps = scale->taps_s16_4;
  count = width * 4;

#ifdef LQ
  /* first pixels with first tap to t4 */
  video_orc_resample_h_multaps_8_lq (temp, pixels, taps, count);
  /* add other pixels with other taps to t4 */
  video_orc_resample_h_muladdtaps_8_lq (temp, 0, pixels + width, count,
      taps + count, count * 2, count, max_taps - 1);
  /* scale and write final result */
  video_orc_resample_scaletaps_8_lq (d, temp, count);
#else
  /* first pixels with first tap to t4 */
  video_orc_resample_h_multaps_8 (temp, pixels, taps, count);
  /* add other pixels with other taps to t4 */
  video_orc_resample_h_muladdtaps_8 (temp, 0, pixels + width, count,
      taps + count, count * 2, count, max_taps - 1);
  /* scale and write final result */
  video_orc_resample_scaletaps_8 (d, temp, count);
#endif
}

static void
video_scale_v_near_8888 (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width)
{
  orc_memcpy (dest, srcs[0], 4 * width);
}

static void
video_scale_v_2tap_8888 (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width)
{
  gint max_taps;
  guint32 *s1, *s2, *d;
  guint64 p1;

  if (scale->taps_s16 == NULL)
#ifdef LQ
    make_s16_taps (scale, 8);
#else
    make_s16_taps (scale, S16_SCALE);
#endif

  max_taps = scale->resampler.max_taps;

  d = (guint32 *) dest;
  s1 = (guint32 *) srcs[0];
  s2 = (guint32 *) srcs[1];
  p1 = scale->taps_s16[dest_offset * max_taps + 1];

#ifdef LQ
  video_orc_resample_v_2tap_8_lq (d, s1, s2, p1, width * 4);
#else
  video_orc_resample_v_2tap_8 (d, s1, s2, p1, width * 4);
#endif
}

#if 0
static void
video_scale_h_4tap_8888 (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  gint16 *taps;
  gint i, max_taps, count;
  guint8 *d;
  guint32 *offset_n;
  guint32 *pixels;

  if (scale->taps_s16 == NULL)
    make_s16_taps (scale, S16_SCALE);

  max_taps = scale->resampler.max_taps;
  offset_n = scale->offset_n;

  d = (guint8 *) dest + 4 * dest_offset;

  /* prepare the arrays FIXME, we can add this into ORC */
  count = width * max_taps;
  pixels = (guint32 *) scale->tmpline1;
  for (i = 0; i < count; i++)
    pixels[i] = ((guint32 *) src)[offset_n[i]];

  taps = scale->taps_s16_4;
  count = width * 4;

  video_orc_resample_h_4tap_8 (d, pixels, pixels + width, pixels + 2 * width,
      pixels + 3 * width, taps, taps + count, taps + 2 * count,
      taps + 3 * count, count);
}
#endif

static void
video_scale_v_4tap_8888 (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width)
{
  gint max_taps;
  guint32 *s1, *s2, *s3, *s4, *d;
  gint p1, p2, p3, p4, src_inc;
  gint16 *taps;

  if (scale->taps_s16 == NULL)
#ifdef LQ
    make_s16_taps (scale, 6);
#else
    make_s16_taps (scale, S16_SCALE);
#endif

  max_taps = scale->resampler.max_taps;
  taps = scale->taps_s16 + dest_offset * max_taps;

  if (scale->flags & GST_VIDEO_SCALER_FLAG_INTERLACED)
    src_inc = 2;
  else
    src_inc = 1;

  d = (guint32 *) dest;
  s1 = (guint32 *) srcs[0 * src_inc];
  s2 = (guint32 *) srcs[1 * src_inc];
  s3 = (guint32 *) srcs[2 * src_inc];
  s4 = (guint32 *) srcs[3 * src_inc];
  p1 = taps[0];
  p2 = taps[1];
  p3 = taps[2];
  p4 = taps[3];

#ifdef LQ
  video_orc_resample_v_4tap_8_lq (d, s1, s2, s3, s4, p1, p2, p3, p4, width * 4);
#else
  video_orc_resample_v_4tap_8 (d, s1, s2, s3, s4, p1, p2, p3, p4, width * 4);
#endif
}

static void
video_scale_v_ntap_8888 (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width)
{
  gint16 *taps;
  gint i, max_taps, count, src_inc;
  guint8 *d;
  gint32 *temp;

  if (scale->taps_s16 == NULL)
#ifdef LQ
    make_s16_taps (scale, 6);
#else
    make_s16_taps (scale, S16_SCALE);
#endif

  max_taps = scale->resampler.max_taps;
  taps = scale->taps_s16 + (scale->resampler.phase[dest_offset] * max_taps);

  d = (guint8 *) dest;

  if (scale->flags & GST_VIDEO_SCALER_FLAG_INTERLACED)
    src_inc = 2;
  else
    src_inc = 1;

  temp = (gint32 *) scale->tmpline2;
  count = width * 4;

#ifdef LQ
  video_orc_resample_v_multaps_8_lq (temp, srcs[0], taps[0], count);
  for (i = 1; i < max_taps - 1; i++) {
    video_orc_resample_v_muladdtaps_8_lq (temp, srcs[i * src_inc], taps[i],
        count);
  }
  video_orc_resample_scaletaps_8_lq (d, temp, count);
#else
  video_orc_resample_v_multaps_8 (temp, srcs[0], taps[0], count);
  for (i = 1; i < max_taps - 1; i++) {
    video_orc_resample_v_muladdtaps_8 (temp, srcs[i * src_inc], taps[i], count);
  }
  video_orc_resample_scaletaps_8 (d, temp, count);
#endif
}

/**
 * gst_video_scaler_horizontal:
 * @scale: a #GstVideoScaler
 * @format: a #GstVideoFormat for @src and @dest
 * @src: source pixels
 * @dest: destination pixels
 * @dest_offset: the horizontal destination offset
 * @width: the number of pixels to scale
 *
 * Horizontally scale the pixels in @src to @dest, starting from @dest_offset
 * for @width samples.
 */
void
gst_video_scaler_horizontal (GstVideoScaler * scale, GstVideoFormat format,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  GstVideoScalerHFunc func;

  g_return_if_fail (scale != NULL);
  g_return_if_fail (src != NULL);
  g_return_if_fail (dest != NULL);
  g_return_if_fail (dest_offset + width <= scale->resampler.out_size);

  switch (scale->resampler.max_taps) {
    case 1:
      func = video_scale_h_near_8888;
      break;
    case 2:
      func = video_scale_h_2tap_8888;
      break;
    default:
      func = video_scale_h_ntap_8888;
      break;
  }
  func (scale, src, dest, dest_offset, width);
}

/**
 * gst_video_scaler_vertical:
 * @scale: a #GstVideoScaler
 * @format: a #GstVideoFormat for @srcs and @dest
 * @src_lines: source pixels lines
 * @dest: destination pixels
 * @dest_offset: the vertical destination offset
 * @width: the number of pixels to scale
 *
 * Vertically combine @width pixels in the lines in @src_lines to @dest.
 * @dest is the location of the target line at @dest_offset and
 * @srcs are the input lines for @dest_offset, as obtained with
 * gst_video_scaler_get_info().
 */
void
gst_video_scaler_vertical (GstVideoScaler * scale, GstVideoFormat format,
    gpointer src_lines[], gpointer dest, guint dest_offset, guint width)
{
  GstVideoScalerVFunc func;

  g_return_if_fail (scale != NULL);
  g_return_if_fail (src_lines != NULL);
  g_return_if_fail (dest != NULL);
  g_return_if_fail (dest_offset < scale->resampler.out_size);

  switch (scale->resampler.max_taps) {
    case 1:
      func = video_scale_v_near_8888;
      break;
    case 2:
      func = video_scale_v_2tap_8888;
      break;
    case 4:
      func = video_scale_v_4tap_8888;
      break;
    default:
      func = video_scale_v_ntap_8888;
      break;
  }
  func (scale, src_lines, dest, dest_offset, width);
}
