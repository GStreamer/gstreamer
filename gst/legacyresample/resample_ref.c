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

void
resample_scale_ref (ResampleState * r)
{
  if (r->need_reinit) {
    RESAMPLE_DEBUG ("sample size %d", r->sample_size);

    if (r->buffer)
      free (r->buffer);
    r->buffer_len = r->sample_size * r->filter_length;
    r->buffer = malloc (r->buffer_len);
    memset (r->buffer, 0, r->buffer_len);
    r->buffer_filled = 0;

    r->i_inc = r->o_rate / r->i_rate;
    r->o_inc = r->i_rate / r->o_rate;
    RESAMPLE_DEBUG ("i_inc %g o_inc %g", r->i_inc, r->o_inc);

    r->i_start = -r->i_inc * r->filter_length;

    r->need_reinit = 0;

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
  }

  RESAMPLE_DEBUG ("asked to resample %d bytes", r->o_size);
  RESAMPLE_DEBUG ("%d bytes in queue",
      audioresample_buffer_queue_get_depth (r->queue));

  while (r->o_size >= r->sample_size) {
    double midpoint;
    int i;
    int j;

    midpoint = r->i_start + (r->filter_length - 1) * 0.5 * r->i_inc;
    RESAMPLE_DEBUG
        ("still need to output %d bytes, %d input left, i_start %g, midpoint %f",
        r->o_size, audioresample_buffer_queue_get_depth (r->queue), r->i_start,
        midpoint);
    if (midpoint > 0.5 * r->i_inc) {
      RESAMPLE_ERROR ("inconsistent state");
    }
    while (midpoint < -0.5 * r->i_inc) {
      AudioresampleBuffer *buffer;

      RESAMPLE_DEBUG ("midpoint %f < %f, r->i_inc %f", midpoint,
          -0.5 * r->i_inc, r->i_inc);
      buffer = audioresample_buffer_queue_pull (r->queue, r->sample_size);
      if (buffer == NULL) {
        /* FIXME: for the first buffer, this isn't necessarily an error,
         * since because of the filter length we'll output less buffers.
         * deal with that so we don't print to console */
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
      r->buffer_filled = MIN (r->buffer_filled + r->sample_size, r->buffer_len);

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
            acc +=
                resample_sinc_window (offset, r->filter_length * 0.5,
                r->sinc_scale) * x;
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
            acc +=
                resample_sinc_window (offset, r->filter_length * 0.5,
                r->sinc_scale) * x;
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
            acc +=
                resample_sinc_window (offset, r->filter_length * 0.5,
                r->sinc_scale) * x;
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
            acc +=
                resample_sinc_window (offset, r->filter_length * 0.5,
                r->sinc_scale) * x;
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
