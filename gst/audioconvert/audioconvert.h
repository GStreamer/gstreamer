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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __AUDIO_CONVERT_H__
#define __AUDIO_CONVERT_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

#include "gstchannelmix.h"
#include "gstaudioquantize.h"

GST_DEBUG_CATEGORY_EXTERN (audio_convert_debug);
#define GST_CAT_DEFAULT (audio_convert_debug)

typedef struct _AudioConvertCtx AudioConvertCtx;

typedef void (*AudioConvertToF64) (gdouble *dst, const gint32 *src, gint count);

struct _AudioConvertCtx
{
  GstAudioInfo in;
  GstAudioInfo out;

  GstAudioFormat mix_format;
  GstChannelMix *mix;
  GstAudioQuantize *quant;

  gboolean in_default;
  gboolean mix_passthrough;
  gboolean quant_default;
  gboolean out_default;

  gpointer tmpbuf;
  gint tmpbufsize;

  gint out_scale;

  AudioConvertToF64 convert;
};

gboolean audio_convert_prepare_context (AudioConvertCtx * ctx,
    GstAudioInfo * in, GstAudioInfo * out,
    GstAudioDitherMethod dither, GstAudioNoiseShapingMethod ns);
gboolean audio_convert_get_sizes (AudioConvertCtx * ctx, gint samples,
    gint * srcsize, gint * dstsize);

gboolean audio_convert_clean_context (AudioConvertCtx * ctx);

gboolean audio_convert_convert (AudioConvertCtx * ctx, gpointer src,
    gpointer dst, gint samples, gboolean src_writable);

#endif /* __AUDIO_CONVERT_H__ */
