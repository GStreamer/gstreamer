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
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "private.h"
#include <gst/gstplugin.h>
#include <gst/gstversion.h>

inline double sinc(double x)
{
	if(x==0)return 1;
	return sin(x) / x;
}

inline double window_func(double x)
{
	x = 1 - x*x;
	return x*x;
}

signed short double_to_s16(double x)
{
	if(x<-32768){
		printf("clipped\n");
		return -32768;
	}
	if(x>32767){
		printf("clipped\n");
		return -32767;
	}
	return rint(x);
}

signed short double_to_s16_ppcasm(double x)
{
	if(x<-32768){
		return -32768;
	}
	if(x>32767){
		return -32767;
	}
	return rint(x);
}

void gst_resample_init(gst_resample_t * r)
{
	r->i_start = 0;
	if(r->filter_length&1){
		r->o_start = 0;
	}else{
		r->o_start = r->o_inc * 0.5;
	}

	memset(r->acc, 0, sizeof(r->acc));

	gst_resample_reinit(r);
}

void gst_resample_reinit(gst_resample_t * r)
{
	/* i_inc is the number of samples that the output increments for
	 * each input sample.  o_inc is the opposite. */
	r->i_inc = (double) r->o_rate / r->i_rate;
	r->o_inc = (double) r->i_rate / r->o_rate;

	r->halftaps = (r->filter_length - 1.0) * 0.5;

	if (r->format == GST_RESAMPLE_S16) {
		switch (r->method) {
		default:
		case GST_RESAMPLE_NEAREST:
			r->scale = gst_resample_nearest_s16;
			break;
		case GST_RESAMPLE_BILINEAR:
			r->scale = gst_resample_bilinear_s16;
			break;
		case GST_RESAMPLE_SINC_SLOW:
			r->scale = gst_resample_sinc_s16;
			break;
		case GST_RESAMPLE_SINC:
			r->scale = gst_resample_sinc_ft_s16;
			break;
		}
	} else if (r->format == GST_RESAMPLE_FLOAT) {
		switch (r->method) {
		default:
		case GST_RESAMPLE_NEAREST:
			r->scale = gst_resample_nearest_float;
			break;
		case GST_RESAMPLE_BILINEAR:
			r->scale = gst_resample_bilinear_float;
			break;
		case GST_RESAMPLE_SINC_SLOW:
			r->scale = gst_resample_sinc_float;
			break;
		case GST_RESAMPLE_SINC:
			r->scale = gst_resample_sinc_ft_float;
			break;
		}
	} else {
		fprintf (stderr, "gst_resample: Unexpected format \"%d\"\n", r->format);
	}
}

/*
 * Prepare to be confused.
 *
 * We keep a "timebase" that is based on output samples.  The zero
 * of the timebase cooresponds to the next output sample that will
 * be written.
 *
 * i_start is the "time" that corresponds to the first input sample
 * in an incoming buffer.  Since the output depends on input samples
 * ahead in time, i_start will tend to be around halftaps.
 *
 * i_start_buf is the time of the first sample in the temporary
 * buffer.
 */
void gst_resample_scale(gst_resample_t * r, void *i_buf, unsigned int i_size)
{
	int o_size;

	r->i_buf = i_buf;

	r->i_samples = i_size / 2 / r->channels;

	r->i_start_buf = r->i_start - r->filter_length * r->i_inc;

	/* i_start is the offset (in a given output sample) that is the
	 * beginning of the current input buffer */
	r->i_end = r->i_start + r->i_inc * r->i_samples;

	r->o_samples = floor(r->i_end - r->halftaps * r->i_inc);

	o_size = r->o_samples * r->channels * 2;
	r->o_buf = r->get_buffer(r->priv, o_size);

	if(r->verbose){
		printf("gst_resample_scale: i_buf=%p i_size=%d\n",
			i_buf,i_size);
		printf("gst_resample_scale: i_samples=%d o_samples=%d i_inc=%g o_buf=%p\n",
			r->i_samples, r->o_samples, r->i_inc, r->o_buf);
		printf("gst_resample_scale: i_start=%g i_end=%g o_start=%g\n",
			r->i_start, r->i_end, r->o_start);
	}

	if ((r->filter_length + r->i_samples)*sizeof(double)*2 > r->buffer_len) {
		int size = (r->filter_length + r->i_samples) * sizeof(double) * 2;

		if(r->verbose){
			printf("gst_resample temp buffer size=%d\n",size);
		}
		if(r->buffer)free(r->buffer);
		r->buffer_len = size;
		r->buffer = malloc(size);
		memset(r->buffer, 0, size);
	}

        if (r->format==GST_RESAMPLE_S16) {
		if(r->channels==2){
			conv_double_short(
					r->buffer + r->filter_length * sizeof(double) * 2,
					r->i_buf, r->i_samples * 2);
		} else {
			conv_double_short_dstr(
					r->buffer + r->filter_length * sizeof(double) * 2,
					r->i_buf, r->i_samples, sizeof(double) * 2);
		}
	} else if (r->format==GST_RESAMPLE_FLOAT) {
		if(r->channels==2){
			conv_double_float(
					r->buffer + r->filter_length * sizeof(double) * 2,
					r->i_buf, r->i_samples * 2);
		} else {
			conv_double_float_dstr(
					r->buffer + r->filter_length * sizeof(double) * 2,
					r->i_buf, r->i_samples, sizeof(double) * 2);
		}
	}

	r->scale(r);

	memcpy(r->buffer,
		r->buffer + r->i_samples * sizeof(double) * 2,
		r->filter_length * sizeof(double) * 2);

	/* updating times */
	r->i_start += r->i_samples * r->i_inc;
	r->o_start += r->o_samples * r->o_inc - r->i_samples;
	
	/* adjusting timebase zero */
	r->i_start -= r->o_samples;
}

void gst_resample_nearest_s16(gst_resample_t * r)
{
	signed short *i_ptr, *o_ptr;
	int i_count = 0;
	double a;
	int i;

	i_ptr = (signed short *) r->i_buf;
	o_ptr = (signed short *) r->o_buf;

	a = r->o_start;
	i_count = 0;
#define SCALE_LOOP(COPY,INC) \
	for (i = 0; i < r->o_samples; i++) {	\
		COPY;							\
		a += r->o_inc;						\
		while (a >= 1) {				\
			a -= 1;						\
			i_ptr+=INC;					\
			i_count++;					\
		}								\
		o_ptr+=INC;						\
   	}

	switch (r->channels) {
	case 1:
		SCALE_LOOP(o_ptr[0] = i_ptr[0], 1);
		break;
	case 2:
		SCALE_LOOP(o_ptr[0] = i_ptr[0];
			   o_ptr[1] = i_ptr[1], 2);
		break;
	default:
	{
		int n, n_chan = r->channels;

		SCALE_LOOP(for (n = 0; n < n_chan; n++) o_ptr[n] =
			   i_ptr[n], n_chan);
	}
	}
	if (i_count != r->i_samples) {
		printf("handled %d in samples (expected %d)\n", i_count,
		       r->i_samples);
	}
}

void gst_resample_bilinear_s16(gst_resample_t * r)
{
	signed short *i_ptr, *o_ptr;
	int o_count = 0;
	double b;
	int i;
	double acc0, acc1;

	i_ptr = (signed short *) r->i_buf;
	o_ptr = (signed short *) r->o_buf;

	acc0 = r->acc[0];
	acc1 = r->acc[1];
	b = r->i_start;
	for (i = 0; i < r->i_samples; i++) {
		b += r->i_inc;
		/*printf("in %d\n",i_ptr[0]); */
		if(b>=2){
			printf("not expecting b>=2\n");
		}
		if (b >= 1) {
			acc0 += (1.0 - (b-r->i_inc)) * i_ptr[0];
			acc1 += (1.0 - (b-r->i_inc)) * i_ptr[1];

			o_ptr[0] = rint(acc0);
			/*printf("out %d\n",o_ptr[0]); */
			o_ptr[1] = rint(acc1);
			o_ptr += 2;
			o_count++;

			b -= 1.0;

			acc0 = b * i_ptr[0];
			acc1 = b * i_ptr[1];
		} else {
			acc0 += i_ptr[0] * r->i_inc;
			acc1 += i_ptr[1] * r->i_inc;
		}
		i_ptr += 2;
	}
	r->acc[0] = acc0;
	r->acc[1] = acc1;

	if (o_count != r->o_samples) {
		printf("handled %d out samples (expected %d)\n", o_count,
		       r->o_samples);
	}
}

void gst_resample_sinc_slow_s16(gst_resample_t * r)
{
	signed short *i_ptr, *o_ptr;
	int i, j;
	double c0, c1;
	double a;
	int start;
	double center;
	double weight;

	if (!r->buffer) {
		int size = r->filter_length * 2 * r->channels;

		printf("gst_resample temp buffer\n");
		r->buffer = malloc(size);
		memset(r->buffer, 0, size);
	}

	i_ptr = (signed short *) r->i_buf;
	o_ptr = (signed short *) r->o_buf;

	a = r->i_start;
#define GETBUF(index,chan) (((index)<0) \
			? ((short *)(r->buffer))[((index)+r->filter_length)*2+(chan)] \
			: i_ptr[(index)*2+(chan)])
	{
		double sinx, cosx, sind, cosd;
		double x, d;
		double t;

		for (i = 0; i < r->o_samples; i++) {
			start = floor(a) - r->filter_length;
			center = a - r->halftaps;
			x = M_PI * (start - center) * r->o_inc;
			sinx = sin(M_PI * (start - center) * r->o_inc);
			cosx = cos(M_PI * (start - center) * r->o_inc);
			d = M_PI * r->o_inc;
			sind = sin(M_PI * r->o_inc);
			cosd = cos(M_PI * r->o_inc);
			c0 = 0;
			c1 = 0;
			for (j = 0; j < r->filter_length; j++) {
				weight = (x==0)?1:(sinx/x);
/*printf("j %d sin %g cos %g\n",j,sinx,cosx); */
/*printf("j %d sin %g x %g sinc %g\n",j,sinx,x,weight); */
				c0 += weight * GETBUF((start + j), 0);
				c1 += weight * GETBUF((start + j), 1);
				t = cosx * cosd - sinx * sind;
				sinx = cosx * sind + sinx * cosd;
				cosx = t;
				x += d;
			}
			o_ptr[0] = rint(c0);
			o_ptr[1] = rint(c1);
			o_ptr += 2;
			a += r->o_inc;
		}
	}
#undef GETBUF

	memcpy(r->buffer,
	       i_ptr + (r->i_samples - r->filter_length) * r->channels,
	       r->filter_length * 2 * r->channels);
}

/* only works for channels == 2 ???? */
void gst_resample_sinc_s16(gst_resample_t * r)
{
	double *ptr;
	signed short *o_ptr;
	int i, j;
	double c0, c1;
	double a;
	int start;
	double center;
	double weight;
	double x0, x, d;
	double scale;

	ptr = (double *) r->buffer;
	o_ptr = (signed short *) r->o_buf;

	/* scale provides a cutoff frequency for the low
	 * pass filter aspects of sinc().  scale=M_PI
	 * will cut off at the input frequency, which is
	 * good for up-sampling, but will cause aliasing
	 * for downsampling.  Downsampling needs to be
	 * cut off at o_rate, thus scale=M_PI*r->i_inc. */
	/* actually, it needs to be M_PI*r->i_inc*r->i_inc.
	 * Need to research why. */
	scale = M_PI*r->i_inc;
	for (i = 0; i < r->o_samples; i++) {
		a = r->o_start + i * r->o_inc;
		start = floor(a - r->halftaps);
/*printf("%d: a=%g start=%d end=%d\n",i,a,start,start+r->filter_length-1); */
		center = a;
		/*x = M_PI * (start - center) * r->o_inc; */
		/*d = M_PI * r->o_inc; */
		/*x = (start - center) * r->o_inc; */
		x0 = (start - center) * r->o_inc;
		d = r->o_inc;
		c0 = 0;
		c1 = 0;
		for (j = 0; j < r->filter_length; j++) {
			x = x0 + d * j;
			weight = sinc(x*scale*r->i_inc)*scale/M_PI;
			weight *= window_func(x/r->halftaps*r->i_inc);
			c0 += weight * ptr[(start + j + r->filter_length)*2 + 0];
			c1 += weight * ptr[(start + j + r->filter_length)*2 + 1];
		}
		o_ptr[0] = double_to_s16(c0);
		o_ptr[1] = double_to_s16(c1);
		o_ptr += 2;
	}
}

/*
 * Resampling audio is best done using a sinc() filter.
 *
 *
 *  out[t] = Sum( in[t'] * sinc((t-t')/delta_t), all t')
 *
 * The immediate problem with this algorithm is that it involves a
 * sum over an infinite number of input samples, both in the past
 * and future.  Note that even though sinc(x) is bounded by 1/x,
 * and thus decays to 0 for large x, since sum(x,{x=0,1..,n}) diverges
 * as log(n), we need to be careful about convergence.  This is
 * typically done by using a windowing function, which also makes
 * the sum over a finite number of input samples.
 *
 * The next problem is computational:  sinc(), and especially
 * sinc() multiplied by a non-trivial windowing function is expensive
 * to calculate, and also difficult to find SIMD optimizations.  Since
 * the time increment on input and output is different, it is not
 * possible to use a FIR filter, because the taps would have to be
 * recalculated for every t.
 *
 * To get around the expense of calculating sinc() for every point,
 * we pre-calculate sinc() at a number of points, and then interpolate
 * for the values we want in calculations.  The interpolation method
 * chosen is bi-cubic, which requires both the evalated function and
 * its derivative at every pre-sampled point.  Also, if the sampled
 * points are spaced commensurate with the input delta_t, we notice
 * that the interpolating weights are the same for every input point.
 * This decreases the number of operations to 4 multiplies and 4 adds
 * for each tap, regardless of the complexity of the filtering function.
 * 
 * At this point, it is possible to rearrange the problem as the sum
 * of 4 properly weghted FIR filters.  Typical SIMD computation units
 * are highly optimized for FIR filters, making long filter lengths
 * reasonable.
 */

static functable_t *ft;

double out_tmp[10000];

void gst_resample_sinc_ft_s16(gst_resample_t * r)
{
	double *ptr;
	signed short *o_ptr;
	int i;
	/*int j; */
	double c0, c1;
	/*double a; */
	double start_f, start_x;
	int start;
	double center;
	/*double weight; */
	double x, d;
	double scale;
	int n = 4;

	scale = r->i_inc;	/* cutoff at 22050 */
	/*scale = 1.0;		// cutoff at 24000 */
	/*scale = r->i_inc * 0.5;	// cutoff at 11025 */

	if(!ft){
		ft = malloc(sizeof(*ft));
		memset(ft,0,sizeof(*ft));

		ft->len = (r->filter_length + 2) * n;
		ft->offset = 1.0 / n;
		ft->start = - ft->len * 0.5 * ft->offset;

		ft->func_x = functable_sinc;
		ft->func_dx = functable_dsinc;
		ft->scale = M_PI * scale;

		ft->func2_x = functable_window_std;
		ft->func2_dx = functable_window_dstd;
		ft->scale2 = 1.0 / r->halftaps;
	
		functable_init(ft);

		/*printf("len=%d offset=%g start=%g\n",ft->len,ft->offset,ft->start); */
	}

	ptr = r->buffer;
	o_ptr = (signed short *) r->o_buf;

	center = r->o_start;
	start_x = center - r->halftaps;
	start_f = floor(start_x);
	start_x -= start_f;
	start = start_f;
	for (i = 0; i < r->o_samples; i++) {
		/*start_f = floor(center - r->halftaps); */
/*printf("%d: a=%g start=%d end=%d\n",i,a,start,start+r->filter_length-1); */
		x = start_f - center;
		d = 1;
		c0 = 0;
		c1 = 0;
/*#define slow */
#ifdef slow
		for (j = 0; j < r->filter_length; j++) {
			weight = functable_eval(ft,x)*scale;
			/*weight = sinc(M_PI * scale * x)*scale*r->i_inc; */
			/*weight *= window_func(x / r->halftaps); */
			c0 += weight * ptr[(start + j + r->filter_length)*2 + 0];
			c1 += weight * ptr[(start + j + r->filter_length)*2 + 1];
			x += d;
		}
#else
		functable_fir2(ft,
			&c0,&c1,
			x, n,
			ptr+(start + r->filter_length)*2,
			r->filter_length);
		c0 *= scale;
		c1 *= scale;
#endif

		out_tmp[2 * i + 0] = c0;
		out_tmp[2 * i + 1] = c1;
		center += r->o_inc;
		start_x += r->o_inc;
		while(start_x>=1.0){
			start_f++;
			start_x -= 1.0;
			start++;
		}
	}

	if(r->channels==2){
		conv_short_double(r->o_buf,out_tmp,2 * r->o_samples);
	}else{
		conv_short_double_sstr(r->o_buf,out_tmp,r->o_samples,2 * sizeof(double));
	}
}

/********
 ** float code below
 ********/


void gst_resample_nearest_float(gst_resample_t * r)
{
	float *i_ptr, *o_ptr;
	int i_count = 0;
	double a;
	int i;

	i_ptr = (float *) r->i_buf;
	o_ptr = (float *) r->o_buf;

	a = r->o_start;
	i_count = 0;
#define SCALE_LOOP(COPY,INC) \
	for (i = 0; i < r->o_samples; i++) {	\
		COPY;							\
		a += r->o_inc;						\
		while (a >= 1) {				\
			a -= 1;						\
			i_ptr+=INC;					\
			i_count++;					\
		}								\
		o_ptr+=INC;						\
   	}

	switch (r->channels) {
	case 1:
		SCALE_LOOP(o_ptr[0] = i_ptr[0], 1);
		break;
	case 2:
		SCALE_LOOP(o_ptr[0] = i_ptr[0];
			   o_ptr[1] = i_ptr[1], 2);
		break;
	default:
	{
		int n, n_chan = r->channels;

		SCALE_LOOP(for (n = 0; n < n_chan; n++) o_ptr[n] =
			   i_ptr[n], n_chan);
	}
	}
	if (i_count != r->i_samples) {
		printf("handled %d in samples (expected %d)\n", i_count,
		       r->i_samples);
	}
}

void gst_resample_bilinear_float(gst_resample_t * r)
{
	float *i_ptr, *o_ptr;
	int o_count = 0;
	double b;
	int i;
	double acc0, acc1;

	i_ptr = (float *) r->i_buf;
	o_ptr = (float *) r->o_buf;

	acc0 = r->acc[0];
	acc1 = r->acc[1];
	b = r->i_start;
	for (i = 0; i < r->i_samples; i++) {
		b += r->i_inc;
		/*printf("in %d\n",i_ptr[0]); */
		if(b>=2){
			printf("not expecting b>=2\n");
		}
		if (b >= 1) {
			acc0 += (1.0 - (b-r->i_inc)) * i_ptr[0];
			acc1 += (1.0 - (b-r->i_inc)) * i_ptr[1];

			o_ptr[0] = acc0;
			/*printf("out %d\n",o_ptr[0]); */
			o_ptr[1] = acc1;
			o_ptr += 2;
			o_count++;

			b -= 1.0;

			acc0 = b * i_ptr[0];
			acc1 = b * i_ptr[1];
		} else {
			acc0 += i_ptr[0] * r->i_inc;
			acc1 += i_ptr[1] * r->i_inc;
		}
		i_ptr += 2;
	}
	r->acc[0] = acc0;
	r->acc[1] = acc1;

	if (o_count != r->o_samples) {
		printf("handled %d out samples (expected %d)\n", o_count,
		       r->o_samples);
	}
}

void gst_resample_sinc_slow_float(gst_resample_t * r)
{
	float *i_ptr, *o_ptr;
	int i, j;
	double c0, c1;
	double a;
	int start;
	double center;
	double weight;

	if (!r->buffer) {
		int size = r->filter_length * sizeof(float) * r->channels;

		printf("gst_resample temp buffer\n");
		r->buffer = malloc(size);
		memset(r->buffer, 0, size);
	}

	i_ptr = (float *) r->i_buf;
	o_ptr = (float *) r->o_buf;

	a = r->i_start;
#define GETBUF(index,chan) (((index)<0) \
			? ((float *)(r->buffer))[((index)+r->filter_length)*2+(chan)] \
			: i_ptr[(index)*2+(chan)])
	{
		double sinx, cosx, sind, cosd;
		double x, d;
		double t;

		for (i = 0; i < r->o_samples; i++) {
			start = floor(a) - r->filter_length;
			center = a - r->halftaps;
			x = M_PI * (start - center) * r->o_inc;
			sinx = sin(M_PI * (start - center) * r->o_inc);
			cosx = cos(M_PI * (start - center) * r->o_inc);
			d = M_PI * r->o_inc;
			sind = sin(M_PI * r->o_inc);
			cosd = cos(M_PI * r->o_inc);
			c0 = 0;
			c1 = 0;
			for (j = 0; j < r->filter_length; j++) {
				weight = (x==0)?1:(sinx/x);
/*printf("j %d sin %g cos %g\n",j,sinx,cosx); */
/*printf("j %d sin %g x %g sinc %g\n",j,sinx,x,weight); */
				c0 += weight * GETBUF((start + j), 0);
				c1 += weight * GETBUF((start + j), 1);
				t = cosx * cosd - sinx * sind;
				sinx = cosx * sind + sinx * cosd;
				cosx = t;
				x += d;
			}
			o_ptr[0] = c0;
			o_ptr[1] = c1;
			o_ptr += 2;
			a += r->o_inc;
		}
	}
#undef GETBUF

	memcpy(r->buffer,
	       i_ptr + (r->i_samples - r->filter_length) * r->channels,
	       r->filter_length * sizeof(float) * r->channels);
}

/* only works for channels == 2 ???? */
void gst_resample_sinc_float(gst_resample_t * r)
{
	double *ptr;
	float *o_ptr;
	int i, j;
	double c0, c1;
	double a;
	int start;
	double center;
	double weight;
	double x0, x, d;
	double scale;

	ptr = (double *) r->buffer;
	o_ptr = (float *) r->o_buf;

	/* scale provides a cutoff frequency for the low
	 * pass filter aspects of sinc().  scale=M_PI
	 * will cut off at the input frequency, which is
	 * good for up-sampling, but will cause aliasing
	 * for downsampling.  Downsampling needs to be
	 * cut off at o_rate, thus scale=M_PI*r->i_inc. */
	/* actually, it needs to be M_PI*r->i_inc*r->i_inc.
	 * Need to research why. */
	scale = M_PI*r->i_inc;
	for (i = 0; i < r->o_samples; i++) {
		a = r->o_start + i * r->o_inc;
		start = floor(a - r->halftaps);
/*printf("%d: a=%g start=%d end=%d\n",i,a,start,start+r->filter_length-1); */
		center = a;
		/*x = M_PI * (start - center) * r->o_inc; */
		/*d = M_PI * r->o_inc; */
		/*x = (start - center) * r->o_inc; */
		x0 = (start - center) * r->o_inc;
		d = r->o_inc;
		c0 = 0;
		c1 = 0;
		for (j = 0; j < r->filter_length; j++) {
			x = x0 + d * j;
			weight = sinc(x*scale*r->i_inc)*scale/M_PI;
			weight *= window_func(x/r->halftaps*r->i_inc);
			c0 += weight * ptr[(start + j + r->filter_length)*2 + 0];
			c1 += weight * ptr[(start + j + r->filter_length)*2 + 1];
		}
		o_ptr[0] = c0;
		o_ptr[1] = c1;
		o_ptr += 2;
	}
}

void gst_resample_sinc_ft_float(gst_resample_t * r)
{
	double *ptr;
	float *o_ptr;
	int i;
	/*int j; */
	double c0, c1;
	/*double a; */
	double start_f, start_x;
	int start;
	double center;
	/*double weight; */
	double x, d;
	double scale;
	int n = 4;

	scale = r->i_inc;	/* cutoff at 22050 */
	/*scale = 1.0;		// cutoff at 24000 */
	/*scale = r->i_inc * 0.5;	// cutoff at 11025 */

	if(!ft){
		ft = malloc(sizeof(*ft));
		memset(ft,0,sizeof(*ft));

		ft->len = (r->filter_length + 2) * n;
		ft->offset = 1.0 / n;
		ft->start = - ft->len * 0.5 * ft->offset;

		ft->func_x = functable_sinc;
		ft->func_dx = functable_dsinc;
		ft->scale = M_PI * scale;

		ft->func2_x = functable_window_std;
		ft->func2_dx = functable_window_dstd;
		ft->scale2 = 1.0 / r->halftaps;
	
		functable_init(ft);

		/*printf("len=%d offset=%g start=%g\n",ft->len,ft->offset,ft->start); */
	}

	ptr = r->buffer;
	o_ptr = (float *) r->o_buf;

	center = r->o_start;
	start_x = center - r->halftaps;
	start_f = floor(start_x);
	start_x -= start_f;
	start = start_f;
	for (i = 0; i < r->o_samples; i++) {
		/*start_f = floor(center - r->halftaps); */
/*printf("%d: a=%g start=%d end=%d\n",i,a,start,start+r->filter_length-1); */
		x = start_f - center;
		d = 1;
		c0 = 0;
		c1 = 0;
/*#define slow */
#ifdef slow
		for (j = 0; j < r->filter_length; j++) {
			weight = functable_eval(ft,x)*scale;
			/*weight = sinc(M_PI * scale * x)*scale*r->i_inc; */
			/*weight *= window_func(x / r->halftaps); */
			c0 += weight * ptr[(start + j + r->filter_length)*2 + 0];
			c1 += weight * ptr[(start + j + r->filter_length)*2 + 1];
			x += d;
		}
#else
		functable_fir2(ft,
			&c0,&c1,
			x, n,
			ptr+(start + r->filter_length)*2,
			r->filter_length);
		c0 *= scale;
		c1 *= scale;
#endif

		out_tmp[2 * i + 0] = c0;
		out_tmp[2 * i + 1] = c1;
		center += r->o_inc;
		start_x += r->o_inc;
		while(start_x>=1.0){
			start_f++;
			start_x -= 1.0;
			start++;
		}
	}

	if(r->channels==2){
		conv_float_double(r->o_buf,out_tmp,2 * r->o_samples);
	}else{
		conv_float_double_sstr(r->o_buf,out_tmp,r->o_samples,2 * sizeof(double));
	}
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstresample",
  "Resampling routines for use in audio plugins",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
);

