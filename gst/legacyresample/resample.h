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

#include "functable.h"
#include "buffer.h"

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#ifdef WIN32
#define rint(x) (floor((x)+0.5))  
#endif 

typedef enum {
        RESAMPLE_FORMAT_S16 = 0,
        RESAMPLE_FORMAT_S32,
        RESAMPLE_FORMAT_F32,
        RESAMPLE_FORMAT_F64
} ResampleFormat;

typedef void (*ResampleCallback) (void *);

typedef struct _ResampleState ResampleState;

struct _ResampleState {
        /* parameters */

        int n_channels;
        ResampleFormat format;

        int filter_length;

        double i_rate;
        double o_rate;

        int method;

        /* internal parameters */

        int need_reinit;

        double halftaps;

        /* filter state */

        unsigned char *o_buf;
        int o_size;

        AudioresampleBufferQueue *queue;
        int eos;
        int started;

        int sample_size;

        unsigned char *buffer;
        int buffer_len;
        int buffer_filled;

        double i_start;
        double o_start;

        double i_inc;
        double o_inc;

        double sinc_scale;

        double i_end;
        double o_end;

        int i_samples;
        int o_samples;

        //void *i_buf;

        Functable *ft;

        double *out_tmp;
};

void resample_init (void);
void resample_cleanup (void);

ResampleState *resample_new (void);
void resample_free (ResampleState *state);

void resample_add_input_data (ResampleState * r, void *data, int size,
    ResampleCallback free_func, void *closure);
void resample_input_eos (ResampleState *r);
void resample_input_flush (ResampleState *r);
void resample_input_pushthrough (ResampleState *r);

int resample_get_output_size_for_input (ResampleState * r, int size);
int resample_get_input_size_for_output (ResampleState * r, int size);

int resample_get_output_size (ResampleState *r);
int resample_get_output_data (ResampleState *r, void *data, int size);

void resample_set_filter_length (ResampleState *r, int length);
void resample_set_input_rate (ResampleState *r, double rate);
void resample_set_output_rate (ResampleState *r, double rate);
void resample_set_n_channels (ResampleState *r, int n_channels);
void resample_set_format (ResampleState *r, ResampleFormat format);
void resample_set_method (ResampleState *r, int method);
int resample_format_size (ResampleFormat format);

void resample_scale_ref (ResampleState * r);
void resample_scale_functable (ResampleState * r);

#endif /* __RESAMPLE_H__ */

