/* GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __SPEEX_RESAMPLER_WRAPPER_H__
#define __SPEEX_RESAMPLER_WRAPPER_H__

#define SPEEX_RESAMPLER_QUALITY_MAX 10
#define SPEEX_RESAMPLER_QUALITY_MIN 0
#define SPEEX_RESAMPLER_QUALITY_DEFAULT 4
#define SPEEX_RESAMPLER_QUALITY_VOIP 3
#define SPEEX_RESAMPLER_QUALITY_DESKTOP 5

enum
{
  RESAMPLER_ERR_SUCCESS = 0,
  RESAMPLER_ERR_ALLOC_FAILED = 1,
  RESAMPLER_ERR_BAD_STATE = 2,
  RESAMPLER_ERR_INVALID_ARG = 3,
  RESAMPLER_ERR_PTR_OVERLAP = 4,

  RESAMPLER_ERR_MAX_ERROR
};

typedef struct SpeexResamplerState_ SpeexResamplerState;

SpeexResamplerState *resample_float_resampler_init (guint32 nb_channels,
    guint32 in_rate, guint32 out_rate, gint quality, gint * err);
SpeexResamplerState *resample_int_resampler_init (guint32 nb_channels,
    guint32 in_rate, guint32 out_rate, gint quality, gint * err);

#define resample_resampler_destroy resample_int_resampler_destroy
void resample_resampler_destroy (SpeexResamplerState * st);

int resample_float_resampler_process_interleaved_float (SpeexResamplerState *
    st, const gfloat * in, guint32 * in_len, gfloat * out, guint32 * out_len);
int resample_int_resampler_process_interleaved_int (SpeexResamplerState * st,
    const gint16 * in, guint32 * in_len, gint16 * out, guint32 * out_len);

int resample_float_resampler_set_rate (SpeexResamplerState * st,
    guint32 in_rate, guint32 out_rate);
int resample_int_resampler_set_rate (SpeexResamplerState * st,
    guint32 in_rate, guint32 out_rate);

void resample_float_resampler_get_rate (SpeexResamplerState * st,
    guint32 * in_rate, guint32 * out_rate);
void resample_int_resampler_get_rate (SpeexResamplerState * st,
    guint32 * in_rate, guint32 * out_rate);

void resample_float_resampler_get_ratio (SpeexResamplerState * st,
    guint32 * ratio_num, guint32 * ratio_den);
void resample_int_resampler_get_ratio (SpeexResamplerState * st,
    guint32 * ratio_num, guint32 * ratio_den);

int resample_float_resampler_set_quality (SpeexResamplerState * st,
    gint quality);
int resample_int_resampler_set_quality (SpeexResamplerState * st, gint quality);

int resample_float_resampler_reset_mem (SpeexResamplerState * st);
int resample_int_resampler_reset_mem (SpeexResamplerState * st);

#define resample_resampler_strerror resample_int_resampler_strerror
const char *resample_resampler_strerror (gint err);

#endif /* __SPEEX_RESAMPLER_WRAPPER_H__ */
