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

#include "resampler.h"
#include "video-scaler.h"

#define S16_SCALE       12
#define S16_SCALE_ROUND (1 << (S16_SCALE -1))

typedef void (*GstVideoScalerHFunc) (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width);
typedef void (*GstVideoScalerVFunc) (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width);

struct _GstVideoScaler
{
  GstResamplerMethod method;
  GstVideoScalerFlags flags;

  GstResampler resampler;

  /* cached integer coefficients */
  gint16 *taps_s16;
};

static void
resampler_zip (GstResampler * resampler, const GstResampler * r1,
    const GstResampler * r2)
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
    const GstResampler *r;

    r = (i & 1) ? r2 : r1;

    offset[i] = r->offset[idx] * 2 + (i & 1);
    phase[i] = i;

    memcpy (taps + i * max_taps, r->taps + idx * max_taps,
        max_taps * sizeof (gdouble));
  }
}

/**
 * gst_video_scaler_new:
 * @method: a #GstResamplerMethod
 * @flags: #GstVideoScalerFlags
 * @n_taps: number of taps to use
 * @in_size: number of source elements
 * @out_size: number of destination elements
 * @options: (allow none): extra options
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
gst_video_scaler_new (GstResamplerMethod method, GstVideoScalerFlags flags,
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
    GstResampler tresamp, bresamp;

    gst_resampler_init (&tresamp, method, 0, (out_size + 1) / 2, n_taps,
        0.0, (in_size + 1) / 2, (out_size + 1) / 2, options);

    gst_resampler_init (&bresamp, method, 0, out_size - tresamp.out_size,
        n_taps, -1.0, in_size - tresamp.in_size,
        out_size - tresamp.out_size, options);

    resampler_zip (&scale->resampler, &tresamp, &bresamp);
    gst_resampler_clear (&tresamp);
    gst_resampler_clear (&bresamp);
  } else {
    gst_resampler_init (&scale->resampler, method, flags, out_size, n_taps,
        0.0, in_size, out_size, options);
  }
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

  gst_resampler_clear (&scale->resampler);
  g_free (scale->taps_s16);
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
make_s16_taps (GstVideoScaler * scale)
{
  gint i, max_taps, n_phases;
  gint16 *taps_s16;
  gdouble *taps;

  n_phases = scale->resampler.n_phases;
  max_taps = scale->resampler.max_taps;

  taps = scale->resampler.taps;
  taps_s16 = scale->taps_s16 = g_malloc (sizeof (gint16) * n_phases * max_taps);

  for (i = 0; i < n_phases; i++) {
    resampler_convert_coeff (taps, taps_s16, max_taps, 16, S16_SCALE);

    taps += max_taps;
    taps_s16 += max_taps;
  }
}

static void
video_scale_h_near_8888 (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  gint i;
  guint32 *s, *d;
  guint32 *offset;

  offset = scale->resampler.offset + dest_offset;

  d = (guint32 *) dest + dest_offset;
  s = (guint32 *) src;

  for (i = 0; i < width; i++)
    d[i] = s[offset[i]];
}

static void
video_scale_v_near_8888 (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width)
{
  memcpy (dest, srcs[0], 4 * width);
}

static void
video_scale_h_ntap_8888 (GstVideoScaler * scale,
    gpointer src, gpointer dest, guint dest_offset, guint width)
{
  gint16 *taps, *t;
  gint i, j, max_taps, sum0, sum1, sum2, sum3;
  guint8 *s, *d;
  guint32 *offset, *phase;

  if (scale->taps_s16 == NULL)
    make_s16_taps (scale);

  max_taps = scale->resampler.max_taps;
  offset = scale->resampler.offset + dest_offset;
  phase = scale->resampler.phase + dest_offset;
  taps = scale->taps_s16;
  d = (guint8 *) dest + 4 * dest_offset;

  for (i = 0; i < width; i++) {
    s = (guint8 *) src + 4 * offset[i];
    t = taps + (phase[i] * max_taps);

    sum0 = sum1 = sum2 = sum3 = 0;
    for (j = 0; j < max_taps; j++) {
      sum0 += t[j] * s[j * 4 + 0];
      sum1 += t[j] * s[j * 4 + 1];
      sum2 += t[j] * s[j * 4 + 2];
      sum3 += t[j] * s[j * 4 + 3];
    }
    sum0 = (sum0 + S16_SCALE_ROUND) >> S16_SCALE;
    sum1 = (sum1 + S16_SCALE_ROUND) >> S16_SCALE;
    sum2 = (sum2 + S16_SCALE_ROUND) >> S16_SCALE;
    sum3 = (sum3 + S16_SCALE_ROUND) >> S16_SCALE;

    d[i * 4 + 0] = CLAMP (sum0, 0, 255);
    d[i * 4 + 1] = CLAMP (sum1, 0, 255);
    d[i * 4 + 2] = CLAMP (sum2, 0, 255);
    d[i * 4 + 3] = CLAMP (sum3, 0, 255);
  }
}

static void
video_scale_v_ntap_8888 (GstVideoScaler * scale,
    gpointer srcs[], gpointer dest, guint dest_offset, guint width)
{
  gint16 *t;
  gint i, j, k, max_taps, sum0, sum1, sum2, sum3, src_inc;
  guint8 *s, *d;

  if (scale->taps_s16 == NULL)
    make_s16_taps (scale);

  max_taps = scale->resampler.max_taps;
  t = scale->taps_s16 + (scale->resampler.phase[dest_offset] * max_taps);
  d = (guint8 *) dest;

  if (scale->flags & GST_VIDEO_SCALER_FLAG_INTERLACED)
    src_inc = 2;
  else
    src_inc = 1;

  for (i = 0; i < width; i++) {
    sum0 = sum1 = sum2 = sum3 = 0;
    for (j = 0, k = 0; j < max_taps; j++, k += src_inc) {
      s = (guint8 *) (srcs[k]);

      sum0 += t[j] * s[4 * i + 0];
      sum1 += t[j] * s[4 * i + 1];
      sum2 += t[j] * s[4 * i + 2];
      sum3 += t[j] * s[4 * i + 3];
    }
    sum0 = (sum0 + S16_SCALE_ROUND) >> S16_SCALE;
    sum1 = (sum1 + S16_SCALE_ROUND) >> S16_SCALE;
    sum2 = (sum2 + S16_SCALE_ROUND) >> S16_SCALE;
    sum3 = (sum3 + S16_SCALE_ROUND) >> S16_SCALE;

    d[i * 4 + 0] = CLAMP (sum0, 0, 255);
    d[i * 4 + 1] = CLAMP (sum1, 0, 255);
    d[i * 4 + 2] = CLAMP (sum2, 0, 255);
    d[i * 4 + 3] = CLAMP (sum3, 0, 255);
  }
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
    GstVideoColorRange range, gpointer src, gpointer dest, guint dest_offset,
    guint width)
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
 * @srcs: source pixels lines
 * @dest: destination pixels
 * @dest_offset: the vertical destination offset
 * @width: the number of pixels to scale
 *
 * Vertically combine @width pixels in the lines in @srcs to @dest.
 * @dest is the location of the target line at @dest_offset and
 * @srcs are the input lines for @dest_offset, as obtained with
 * gst_video_scaler_get_info().
 */
void
gst_video_scaler_vertical (GstVideoScaler * scale, GstVideoFormat format,
    GstVideoColorRange range, gpointer srcs[], gpointer dest, guint dest_offset,
    guint width)
{
  GstVideoScalerVFunc func;

  g_return_if_fail (scale != NULL);
  g_return_if_fail (srcs != NULL);
  g_return_if_fail (dest != NULL);
  g_return_if_fail (dest_offset <= scale->resampler.out_size);

  switch (scale->resampler.max_taps) {
    case 1:
      func = video_scale_v_near_8888;
      break;
    default:
      func = video_scale_v_ntap_8888;
      break;
  }
  func (scale, srcs, dest, dest_offset, width);
}
