/* Resampling library
 * Copyright (C) <2001> David Schleef <ds@schleef.org>
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


#ifndef __GST_RESAMPLE_H__
#define __GST_RESAMPLE_H__

typedef enum
{
  GST_RESAMPLE_NEAREST = 0,
  GST_RESAMPLE_BILINEAR,
  GST_RESAMPLE_SINC_SLOW,
  GST_RESAMPLE_SINC,
} gst_resample_method;

typedef enum
{
  GST_RESAMPLE_S16 = 0,
  GST_RESAMPLE_FLOAT
} gst_resample_format;

typedef struct gst_resample_s gst_resample_t;

struct gst_resample_s
{
  /* parameters */

  gst_resample_method method;
  int channels;
  int verbose;
  gst_resample_format format;

  int filter_length;

  double i_rate;
  double o_rate;

  void *priv;

  void *(*get_buffer) (void *priv, unsigned int size);

  /* internal parameters */

  double halftaps;

  /* filter state */

  void *buffer;
  int buffer_len;

  double i_start;
  double o_start;

  double i_start_buf;
  double i_end_buf;

  double i_inc;
  double o_inc;

  double i_end;
  double o_end;

  int i_samples;
  int o_samples;

  void *i_buf, *o_buf;

  double acc[10];

  /* methods */
  void (*scale) (gst_resample_t * r);

  double ack;
};

void gst_resample_init (gst_resample_t * r);

void gst_resample_reinit (gst_resample_t * r);

void gst_resample_scale (gst_resample_t * r, void *i_buf, unsigned int size);

#endif /* __GST_RESAMPLE_H__ */
