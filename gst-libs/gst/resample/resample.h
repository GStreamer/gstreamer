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


#ifndef __RESAMPLE_H__
#define __RESAMPLE_H__

typedef enum {
	RESAMPLE_NEAREST = 0,
	RESAMPLE_BILINEAR,
	RESAMPLE_SINC_SLOW,
	RESAMPLE_SINC,
} resample_method;

typedef enum {
	RESAMPLE_S16 = 0,
	RESAMPLE_FLOAT
} resample_format;

typedef struct resample_s resample_t;

struct resample_s {
	/* parameters */

	resample_method method;
	int channels;
	int verbose;
	resample_format format;

	int filter_length;

	double i_rate;
	double o_rate;

	void *priv;

	void *(*get_buffer)(void *priv, unsigned int size);

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
	void (*scale)(resample_t *r);

	double ack;
};

void resample_init(resample_t *r);

void resample_reinit(resample_t *r);

void resample_scale(resample_t *r, void *i_buf, unsigned int size);

#endif /* __RESAMPLE_H__ */

