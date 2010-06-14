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

#include "resample.h"
#include "buffer.h"
#include "debug.h"

GST_DEBUG_CATEGORY (libaudioresample_debug);

void
resample_init (void)
{
  static int inited = 0;

  if (!inited) {
    inited = 1;
    GST_DEBUG_CATEGORY_INIT (libaudioresample_debug, "libaudioresample", 0,
        "audio resampling library");

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

/*
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
  buffer->priv2 = (void *) free_func;
  buffer->priv = closure;

  audioresample_buffer_queue_push (r->queue, buffer);
}

void
resample_input_flush (ResampleState * r)
{
  RESAMPLE_DEBUG ("flush");

  audioresample_buffer_queue_flush (r->queue);
  r->buffer_filled = 0;
  r->need_reinit = 1;
}

void
resample_input_pushthrough (ResampleState * r)
{
  AudioresampleBuffer *buffer;
  int filter_bytes;
  int buffer_filled;

  if (r->sample_size == 0)
    return;

  filter_bytes = r->filter_length * r->sample_size;
  buffer_filled = r->buffer_filled;

  RESAMPLE_DEBUG ("pushthrough filter_bytes %d, filled %d",
      filter_bytes, buffer_filled);

  /* if we have no pending samples, we don't need to do anything. */
  if (buffer_filled <= 0)
    return;

  /* send filter_length/2 number of samples so we can get to the
   * last queued samples */
  buffer = audioresample_buffer_new_and_alloc (filter_bytes / 2);
  memset (buffer->data, 0, buffer->length);

  RESAMPLE_DEBUG ("pushthrough %u", buffer->length);

  audioresample_buffer_queue_push (r->queue, buffer);
}

void
resample_input_eos (ResampleState * r)
{
  RESAMPLE_DEBUG ("EOS");
  resample_input_pushthrough (r);
  r->eos = 1;
}

int
resample_get_output_size_for_input (ResampleState * r, int size)
{
  int outsize;
  double outd;
  int avail;
  int filter_bytes;
  int buffer_filled;

  if (r->sample_size == 0)
    return 0;

  filter_bytes = r->filter_length * r->sample_size;
  buffer_filled = filter_bytes / 2 - r->buffer_filled / 2;

  avail =
      audioresample_buffer_queue_get_depth (r->queue) + size - buffer_filled;

  RESAMPLE_DEBUG ("avail %d, o_rate %f, i_rate %f, filter_bytes %d, filled %d",
      avail, r->o_rate, r->i_rate, filter_bytes, buffer_filled);
  if (avail <= 0)
    return 0;

  outd = (double) avail *r->o_rate / r->i_rate;

  outsize = (int) floor (outd);

  /* round off for sample size */
  outsize -= outsize % r->sample_size;

  return outsize;
}

int
resample_get_input_size_for_output (ResampleState * r, int size)
{
  int outsize;
  double outd;
  int avail;

  if (r->sample_size == 0)
    return 0;

  avail = size;

  RESAMPLE_DEBUG ("size %d, o_rate %f, i_rate %f", avail, r->o_rate, r->i_rate);
  outd = (double) avail *r->i_rate / r->o_rate;

  outsize = (int) ceil (outd);

  /* round off for sample size */
  outsize -= outsize % r->sample_size;

  return outsize;
}

int
resample_get_output_size (ResampleState * r)
{
  return resample_get_output_size_for_input (r, 0);
}

int
resample_get_output_data (ResampleState * r, void *data, int size)
{
  r->o_buf = data;
  r->o_size = size;

  if (size == 0)
    return 0;

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
