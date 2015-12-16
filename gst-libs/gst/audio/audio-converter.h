/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *           (C) 2015 Wim Taymans <wim.taymans@gmail.com>
 *
 * audioconverter.h: audio format conversion library
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

#ifndef __GST_AUDIO_CONVERTER_H__
#define __GST_AUDIO_CONVERTER_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>

typedef struct _GstAudioConverter GstAudioConverter;

/**
 * GST_AUDIO_CONVERTER_OPT_DITHER_METHOD:
 *
 * #GST_TYPE_AUDIO_DITHER_METHOD, The dither method to use when
 * changing bit depth.
 * Default is #GST_AUDIO_DITHER_NONE.
 */
#define GST_AUDIO_CONVERTER_OPT_DITHER_METHOD   "GstAudioConverter.dither-method"

/**
 * GST_AUDIO_CONVERTER_OPT_NOISE_SHAPING_METHOD:
 *
 * #GST_TYPE_AUDIO_NOISE_SHAPING_METHOD, The noise shaping method to use
 * to mask noise from quantization errors.
 * Default is #GST_AUDIO_NOISE_SHAPING_NONE.
 */
#define GST_AUDIO_CONVERTER_OPT_NOISE_SHAPING_METHOD   "GstAudioConverter.noise-shaping-method"

/**
 * GST_AUDIO_CONVERTER_OPT_QUANTIZATION:
 *
 * #G_TYPE_UINT, The quantization amount. Components will be
 * quantized to multiples of this value.
 * Default is 1
 */
#define GST_AUDIO_CONVERTER_OPT_QUANTIZATION   "GstAudioConverter.quantization"


/**
 * GstAudioConverterFlags:
 * @GST_AUDIO_CONVERTER_FLAG_NONE: no flag
 * @GST_AUDIO_CONVERTER_FLAG_SOURCE_WRITABLE: the source is writable and can be
 *    used as temporary storage during conversion.
 *
 * Extra flags passed to gst_audio_converter_samples().
 */
typedef enum {
  GST_AUDIO_CONVERTER_FLAG_NONE            = 0,
  GST_AUDIO_CONVERTER_FLAG_SOURCE_WRITABLE = (1 << 0)
} GstAudioConverterFlags;

GstAudioConverter *  gst_audio_converter_new             (GstAudioInfo *in_info,
                                                          GstAudioInfo *out_info,
                                                          GstStructure *config);

void                 gst_audio_converter_free            (GstAudioConverter * convert);

gboolean             gst_audio_converter_set_config      (GstAudioConverter * convert, GstStructure *config);
const GstStructure * gst_audio_converter_get_config      (GstAudioConverter * convert);

gsize                gst_audio_converter_get_out_frames  (GstAudioConverter *convert,
                                                          gsize in_frames);
gsize                gst_audio_converter_get_in_frames   (GstAudioConverter *convert,
                                                          gsize out_frames);

gsize                gst_audio_converter_get_max_latency (GstAudioConverter *convert);


gboolean             gst_audio_converter_samples         (GstAudioConverter * convert,
                                                          GstAudioConverterFlags flags,
                                                          gpointer in[], gsize in_samples,
                                                          gpointer out[], gsize out_samples,
                                                          gsize *in_consumed, gsize *out_produced);

#endif /* __GST_AUDIO_CONVERTER_H__ */
