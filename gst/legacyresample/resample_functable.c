/* Resampling library
 * Copyright (C) <2001> David A. Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or any later version.
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
#include <config.h>
#endif


#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <gst/math-compat.h>

#include "_stdint.h"

#include "resample.h"
#include "buffer.h"
#include "debug.h"

static void
func_sinc (double *fx, double *dfx, double x, void *closure)
{
  //double scale = *(double *)closure;
  double scale = M_PI;

  if (x == 0) {
    *fx = 1;
    *dfx = 0;
    return;
  }

  x *= scale;
  *fx = sin (x) / x;
  *dfx = scale * (cos (x) - sin (x) / x) / x;
}

static void
func_hanning (double *fx, double *dfx, double x, void *closure)
{
  double width = *(double *) closure;

  if (x < width && x > -width) {
    x /= width;
    *fx = (1 - x * x) * (1 - x * x);
    *dfx = -2 * 2 * x / width * (1 - x * x);
  } else {
    *fx = 0;
    *dfx = 0;
  }
}

#if 0
static double
resample_sinc_window (double x, double halfwidth, double scale)
{
  double y;

  if (x == 0)
    return 1.0;
  if (x < -halfwidth || x > halfwidth)
    return 0.0;

  y = sin (x * M_PI * scale) / (x * M_PI * scale) * scale;

  x /= halfwidth;
  y *= (1 - x * x) * (1 - x * x);

  return y;
}
#endif

#if 0
static void
functable_test (Functable * ft, double halfwidth)
{
  int i;
  double x;

  for (i = 0; i < 100; i++) {
    x = i * 0.1;
    printf ("%d %g %g\n", i, resample_sinc_window (x, halfwidth, 1.0),
        functable_evaluate (ft, x));
  }
  exit (0);

}
#endif


void
resample_scale_functable (ResampleState * r)
{
  if (r->need_reinit) {
    double hanning_width;

    RESAMPLE_DEBUG ("sample size %d", r->sample_size);

    if (r->buffer)
      free (r->buffer);
    r->buffer_len = r->sample_size * r->filter_length;
    r->buffer = malloc (r->buffer_len);
    memset (r->buffer, 0, r->buffer_len);

    r->i_inc = r->o_rate / r->i_rate;
    r->o_inc = r->i_rate / r->o_rate;
    RESAMPLE_DEBUG ("i_inc %g o_inc %g", r->i_inc, r->o_inc);

    r->i_start = -r->i_inc * r->filter_length;

    if (r->ft) {
      functable_free (r->ft);
    }
    r->ft = functable_new ();
    functable_set_length (r->ft, r->filter_length * 16);
    functable_set_offset (r->ft, -r->filter_length / 2);
    functable_set_multiplier (r->ft, 1 / 16.0);

    hanning_width = r->filter_length / 2;
    functable_calculate (r->ft, func_sinc, NULL);
    functable_calculate_multiply (r->ft, func_hanning, &hanning_width);

    //functable_test(r->ft, 0.5 * r->filter_length);
#if 0
    if (r->i_inc < 1.0) {
      r->sinc_scale = r->i_inc;
      if (r->sinc_scale == 0.5) {
        /* strange things happen at integer multiples */
        r->sinc_scale = 1.0;
      }
    } else {
      r->sinc_scale = 1.0;
    }
#else
    r->sinc_scale = 1.0;
#endif

    r->need_reinit = 0;
  }

  while (r->o_size > 0) {
    double midpoint;
    int i;
    int j;

    RESAMPLE_DEBUG ("i_start %g", r->i_start);
    midpoint = r->i_start + (r->filter_length - 1) * 0.5 * r->i_inc;
    if (midpoint > 0.5 * r->i_inc) {
      RESAMPLE_ERROR ("inconsistent state");
    }
    while (midpoint < -0.5 * r->i_inc) {
      AudioresampleBuffer *buffer;

      buffer = audioresample_buffer_queue_pull (r->queue, r->sample_size);
      if (buffer == NULL) {
        RESAMPLE_ERROR ("buffer_queue_pull returned NULL");
        return;
      }

      r->i_start += r->i_inc;
      RESAMPLE_DEBUG ("pulling (i_start = %g)", r->i_start);

      midpoint += r->i_inc;
      memmove (r->buffer, r->buffer + r->sample_size,
          r->buffer_len - r->sample_size);

      memcpy (r->buffer + r->buffer_len - r->sample_size, buffer->data,
          r->sample_size);
      audioresample_buffer_unref (buffer);
    }

    switch (r->format) {
      case RESAMPLE_FORMAT_S16:
        for (i = 0; i < r->n_channels; i++) {
          double acc = 0;
          double offset;
          double x;

          for (j = 0; j < r->filter_length; j++) {
            offset = (r->i_start + j * r->i_inc) * r->o_inc;
            x = *(int16_t *) (r->buffer + i * sizeof (int16_t) +
                j * r->sample_size);
            acc += functable_evaluate (r->ft, offset) * x;
            //acc += resample_sinc_window (offset, r->filter_length * 0.5, r->sinc_scale) * x;
          }
          if (acc < -32768.0)
            acc = -32768.0;
          if (acc > 32767.0)
            acc = 32767.0;

          *(int16_t *) (r->o_buf + i * sizeof (int16_t)) = rint (acc);
        }
        break;
      case RESAMPLE_FORMAT_S32:
        for (i = 0; i < r->n_channels; i++) {
          double acc = 0;
          double offset;
          double x;

          for (j = 0; j < r->filter_length; j++) {
            offset = (r->i_start + j * r->i_inc) * r->o_inc;
            x = *(int32_t *) (r->buffer + i * sizeof (int32_t) +
                j * r->sample_size);
            acc += functable_evaluate (r->ft, offset) * x;
            //acc += resample_sinc_window (offset, r->filter_length * 0.5, r->sinc_scale) * x;
          }
          if (acc < -2147483648.0)
            acc = -2147483648.0;
          if (acc > 2147483647.0)
            acc = 2147483647.0;

          *(int32_t *) (r->o_buf + i * sizeof (int32_t)) = rint (acc);
        }
        break;
      case RESAMPLE_FORMAT_F32:
        for (i = 0; i < r->n_channels; i++) {
          double acc = 0;
          double offset;
          double x;

          for (j = 0; j < r->filter_length; j++) {
            offset = (r->i_start + j * r->i_inc) * r->o_inc;
            x = *(float *) (r->buffer + i * sizeof (float) +
                j * r->sample_size);
            acc += functable_evaluate (r->ft, offset) * x;
            //acc += resample_sinc_window (offset, r->filter_length * 0.5, r->sinc_scale) * x;
          }

          *(float *) (r->o_buf + i * sizeof (float)) = acc;
        }
        break;
      case RESAMPLE_FORMAT_F64:
        for (i = 0; i < r->n_channels; i++) {
          double acc = 0;
          double offset;
          double x;

          for (j = 0; j < r->filter_length; j++) {
            offset = (r->i_start + j * r->i_inc) * r->o_inc;
            x = *(double *) (r->buffer + i * sizeof (double) +
                j * r->sample_size);
            acc += functable_evaluate (r->ft, offset) * x;
            //acc += resample_sinc_window (offset, r->filter_length * 0.5, r->sinc_scale) * x;
          }

          *(double *) (r->o_buf + i * sizeof (double)) = acc;
        }
        break;
    }

    r->i_start -= 1.0;
    r->o_buf += r->sample_size;
    r->o_size -= r->sample_size;
  }

}
