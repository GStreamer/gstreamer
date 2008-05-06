/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * audioconvert.h: audio format conversion library
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

#ifndef __AUDIO_CONVERT_H__
#define __AUDIO_CONVERT_H__

#include <gst/gst.h>
#include <gst/audio/multichannel.h>

typedef enum
{
  DITHER_NONE = 0,
  DITHER_RPDF,
  DITHER_TPDF,
  DITHER_TPDF_HF
} DitherType;

typedef enum
{
  NOISE_SHAPING_NONE = 0,
  NOISE_SHAPING_ERROR_FEEDBACK,
  NOISE_SHAPING_SIMPLE,
  NOISE_SHAPING_MEDIUM,
  NOISE_SHAPING_HIGH
} NoiseShapingType;

typedef struct _AudioConvertCtx AudioConvertCtx;
typedef struct _AudioConvertFmt AudioConvertFmt;

struct _AudioConvertFmt
{
  /* general caps */
  gboolean is_int;
  gint endianness;
  gint width;
  gint rate;
  gint channels;
  GstAudioChannelPosition *pos;
  gboolean unpositioned_layout;

  /* int audio caps */
  gboolean sign;
  gint depth;

  gint unit_size;
};

typedef void (*AudioConvertUnpack) (gpointer src, gpointer dst, gint scale,
    gint count);
typedef void (*AudioConvertPack) (gpointer src, gpointer dst, gint scale,
    gint count);

typedef void (*AudioConvertMix) (AudioConvertCtx *, gpointer, gpointer, gint);
typedef void (*AudioConvertQuantize) (AudioConvertCtx * ctx, gpointer src,
    gpointer dst, gint count);

struct _AudioConvertCtx
{
  AudioConvertFmt in;
  AudioConvertFmt out;

  AudioConvertUnpack unpack;
  AudioConvertPack pack;

  /* channel conversion matrix, m[in_channels][out_channels].
   * If identity matrix, passthrough applies. */
  gfloat **matrix;
  /* temp storage for channelmix */
  gpointer tmp;

  gboolean in_default;
  gboolean mix_passthrough;
  gboolean out_default;

  gpointer tmpbuf;
  gint tmpbufsize;

  gint in_scale;
  gint out_scale;

  AudioConvertMix channel_mix;

  AudioConvertQuantize quantize;
  DitherType dither;
  NoiseShapingType ns;
  /* random number generate for dither noise */
  GRand *dither_random;
  /* last random number generated per channel for hifreq TPDF dither */
  gpointer last_random;
  /* contains the past quantization errors, error[out_channels][count] */
  gdouble *error_buf;
};

gboolean audio_convert_clean_fmt (AudioConvertFmt * fmt);

gboolean audio_convert_prepare_context (AudioConvertCtx * ctx,
    AudioConvertFmt * in, AudioConvertFmt * out, DitherType dither,
    NoiseShapingType ns);
gboolean audio_convert_get_sizes (AudioConvertCtx * ctx, gint samples,
    gint * srcsize, gint * dstsize);

gboolean audio_convert_clean_context (AudioConvertCtx * ctx);

gboolean audio_convert_convert (AudioConvertCtx * ctx, gpointer src,
    gpointer dst, gint samples, gboolean src_writable);

#endif /* __AUDIO_CONVERT_H__ */
