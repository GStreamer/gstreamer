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
#include <liboil/liboil.h>

#include "resample.h"
#include "buffer.h"
#include "debug.h"

void resample_scale_ref (ResampleState * r);
void resample_scale_functable (ResampleState * r);



void
resample_init (void)
{
  static int inited = 0;
  const char *debug;

  if (!inited) {
    oil_init ();
    inited = 1;
  }

  if ((debug = g_getenv ("RESAMPLE_DEBUG"))) {
    resample_debug_set_level (atoi (debug));
  }
}

ResampleState *
resample_new (void)
{
  ResampleState *r;

  r = malloc (sizeof (ResampleState));
  memset (r, 0, sizeof (ResampleState));

  r->filter_length = 16;

  r->i_start = 0;
  if (r->filter_length & 1) {
    r->o_start = 0;
  } else {
    r->o_start = r->o_inc * 0.5;
  }

  r->queue = audioresample_buffer_queue_new ();
  r->out_tmp = malloc (10000 * sizeof (double));

  r->need_reinit = 1;

  return r;
}

void
resample_free (ResampleState * r)
{
  if (r->buffer) {
    free (r->buffer);
  }
  if (r->ft) {
    functable_free (r->ft);
  }
  if (r->queue) {
    audioresample_buffer_queue_free (r->queue);
  }
  if (r->out_tmp) {
    free (r->out_tmp);
  }

  free (r);
}

static void
resample_buffer_free (AudioresampleBuffer * buffer, void *priv)
{
  if (buffer->priv2) {
    ((void (*)(void *)) buffer->priv2) (buffer->priv);
  }
}

/**
 * free_func: a function that frees the given closure.  If NULL, caller is
 *            responsible for freeing.
 */
void
resample_add_input_data (ResampleState * r, void *data, int size,
    void (*free_func) (void *), void *closure)
{
  AudioresampleBuffer *buffer;

  RESAMPLE_DEBUG ("data %p size %d", data, size);

  buffer = audioresample_buffer_new_with_data (data, size);
  buffer->free = resample_buffer_free;
  buffer->priv2 = free_func;
  buffer->priv = closure;

  audioresample_buffer_queue_push (r->queue, buffer);
}

void
resample_input_eos (ResampleState * r)
{
  AudioresampleBuffer *buffer;
  int sample_size;

  sample_size = r->n_channels * resample_format_size (r->format);

  buffer = audioresample_buffer_new_and_alloc (sample_size *
      (r->filter_length / 2));
  memset (buffer->data, 0, buffer->length);

  audioresample_buffer_queue_push (r->queue, buffer);

  r->eos = 1;
}

int
resample_get_output_size_for_input (ResampleState * r, int size)
{
  int outsize;
  double outd;

  g_return_val_if_fail (r->sample_size != 0, 0);

  RESAMPLE_DEBUG ("size %d, o_rate %f, i_rate %f", size, r->o_rate, r->i_rate);
  outd = (double) size / r->i_rate * r->o_rate;
  outsize = (int) floor (outd);

  /* round off for sample size */
  return outsize - (outsize % r->sample_size);
}

int
resample_get_output_size (ResampleState * r)
{
  return resample_get_output_size_for_input (r,
      audioresample_buffer_queue_get_depth (r->queue));
}

int
resample_get_output_data (ResampleState * r, void *data, int size)
{
  r->o_buf = data;
  r->o_size = size;

  switch (r->method) {
    case 0:
      resample_scale_ref (r);
      break;
    case 1:
      resample_scale_functable (r);
      break;
    default:
      break;
  }

  return size - r->o_size;
}

void
resample_set_filter_length (ResampleState * r, int length)
{
  r->filter_length = length;
  r->need_reinit = 1;
}

void
resample_set_input_rate (ResampleState * r, double rate)
{
  r->i_rate = rate;
  r->need_reinit = 1;
}

void
resample_set_output_rate (ResampleState * r, double rate)
{
  r->o_rate = rate;
  r->need_reinit = 1;
}

void
resample_set_n_channels (ResampleState * r, int n_channels)
{
  r->n_channels = n_channels;
  r->sample_size = r->n_channels * resample_format_size (r->format);
  r->need_reinit = 1;
}

void
resample_set_format (ResampleState * r, ResampleFormat format)
{
  r->format = format;
  r->sample_size = r->n_channels * resample_format_size (r->format);
  r->need_reinit = 1;
}

void
resample_set_method (ResampleState * r, int method)
{
  r->method = method;
  r->need_reinit = 1;
}

int
resample_format_size (ResampleFormat format)
{
  switch (format) {
    case RESAMPLE_FORMAT_S16:
      return 2;
    case RESAMPLE_FORMAT_S32:
    case RESAMPLE_FORMAT_F32:
      return 4;
    case RESAMPLE_FORMAT_F64:
      return 8;
  }
  return 0;
}
