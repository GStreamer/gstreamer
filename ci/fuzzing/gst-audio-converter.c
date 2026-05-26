/*
 * Copyright 2026 Google Inc.
 * author: Arthur SC Chan <arthur.chan@adalogics.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Audio converter/resampler/quantize fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/audio/audio-converter.c
 *   gst-libs/gst/audio/audio-resampler.c
 *   gst-libs/gst/audio/audio-quantize.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

#define HEADER_SIZE 8
#define MAX_FRAMES  4096
#define MAX_RESAMPLE_BYTES (16 * 1024 * 1024)

/* All formats exercised by the converter path */
static const GstAudioFormat all_formats[] = {
  GST_AUDIO_FORMAT_S8, GST_AUDIO_FORMAT_U8,
  GST_AUDIO_FORMAT_S16LE, GST_AUDIO_FORMAT_U16LE,
  GST_AUDIO_FORMAT_S24LE, GST_AUDIO_FORMAT_S32LE,
  GST_AUDIO_FORMAT_U32LE, GST_AUDIO_FORMAT_F32LE,
  GST_AUDIO_FORMAT_F64LE,
};

#define N_ALL_FORMATS G_N_ELEMENTS(all_formats)

/* Resampler only accepts these four formats */
static const GstAudioFormat resamp_formats[] = {
  GST_AUDIO_FORMAT_S16, GST_AUDIO_FORMAT_S32,
  GST_AUDIO_FORMAT_F32, GST_AUDIO_FORMAT_F64,
};

#define N_RESAMP_FORMATS G_N_ELEMENTS(resamp_formats)

static const gint rates[] = {
  100, 997, 7919,
  8000, 11025, 16000, 22050,
  32000, 44100, 48000, 96000,
  8009, 22051, 44101
};

#define N_RATES G_N_ELEMENTS(rates)

/* Channel counts the audio converter's mix matrix supports out of the box. */
static const gint mixable_channels[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

#define N_MIXABLE_CHANNELS G_N_ELEMENTS(mixable_channels)

static void
custom_logger (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message, gpointer unused_data)
{
  if (log_level & G_LOG_LEVEL_CRITICAL) {
    g_printerr ("CRITICAL ERROR : %s\n", message);
    abort ();
  } else if (log_level & G_LOG_LEVEL_WARNING) {
    g_printerr ("WARNING : %s\n", message);
  }
}

int
LLVMFuzzerTestOneInput (const guint8 * data, size_t size)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
    g_log_set_default_handler (custom_logger, NULL);
    gst_init (NULL, NULL);
    initialized = TRUE;
  }

  if (size < HEADER_SIZE)
    return 0;

  GstAudioFormat in_fmt = all_formats[data[0] % N_ALL_FORMATS];
  GstAudioFormat out_fmt = all_formats[data[1] % N_ALL_FORMATS];

  /* Picked from the supported mix-matrix set so the converter actually runs. */
  gint in_ch = mixable_channels[data[2] % N_MIXABLE_CHANNELS];
  gint in_rate = rates[data[3] % N_RATES];
  gint out_rate = rates[data[4] % N_RATES];
  GstAudioResamplerMethod rm = (GstAudioResamplerMethod) ((data[5] >> 4) % 5);
  GstAudioFormat resamp_fmt = resamp_formats[data[5] % N_RESAMP_FORMATS];
  GstAudioDitherMethod dither = (GstAudioDitherMethod) ((data[6] >> 4) % 4);
  GstAudioNoiseShapingMethod ns =
      (GstAudioNoiseShapingMethod) ((data[6] & 0x0f) % 5);
  guint quantizer = (guint) data[6] + 1;

  const guint8 *audio_data = data + HEADER_SIZE;
  gsize audio_size = size - HEADER_SIZE;

  /* audio converter */
  {
    gint out_ch = mixable_channels[data[7] % N_MIXABLE_CHANNELS];
    GstAudioInfo in_info, out_info;
    GstAudioConverter *conv;

    gst_audio_info_set_format (&in_info, in_fmt, in_rate, in_ch, NULL);
    gst_audio_info_set_format (&out_info, out_fmt, out_rate, out_ch, NULL);
    conv = gst_audio_converter_new (0, &in_info, &out_info, NULL);
    if (conv) {
      if (audio_size > 0) {
        gpointer out = NULL;
        gsize out_size = 0;
        gst_audio_converter_convert (conv, 0, (gpointer) audio_data,
            audio_size, &out, &out_size);
        g_free (out);
      }
      gst_audio_converter_free (conv);
    }
  }

  /* audio resampler */
  {
    GstAudioResampler *resampler;

    resampler = gst_audio_resampler_new (rm, 0, resamp_fmt, in_ch,
        in_rate, out_rate, NULL);
    if (resampler) {
      const GstAudioFormatInfo *finfo = gst_audio_format_get_info (resamp_fmt);
      gsize frame_size = (finfo->width / 8) * in_ch;
      if (frame_size > 0 && audio_size >= frame_size) {
        gsize in_frames = MIN (audio_size / frame_size, MAX_FRAMES);
        gsize out_frames =
            gst_audio_resampler_get_out_frames (resampler, in_frames);
        if (out_frames > 0) {
          gsize out_buf_size = out_frames * (finfo->width / 8) * in_ch;
          /* Cap allocation to keep the fuzzer alive on extreme rate ratios. */
          if (out_buf_size > 0 && out_buf_size <= MAX_RESAMPLE_BYTES) {
            gpointer out_buf = g_malloc0 (out_buf_size);
            gpointer in_ptrs[1] = { (gpointer) audio_data };
            gpointer out_ptrs[1] = { out_buf };
            gst_audio_resampler_resample (resampler, in_ptrs, in_frames,
                out_ptrs, out_frames);
            g_free (out_buf);
          }
        }
      }
      gst_audio_resampler_free (resampler);
    }
  }

  /* audio quantize */
  {
    GstAudioQuantize *quant = gst_audio_quantize_new (dither, ns, 0,
        GST_AUDIO_FORMAT_S32, 1, quantizer);
    if (quant) {
      const gsize frame_size = sizeof (gint32);
      if (audio_size >= frame_size) {
        guint frames = (guint) MIN (audio_size / frame_size, MAX_FRAMES);
        gpointer out_buf = g_malloc0 (frames * frame_size);
        gpointer in_ptrs[1] = { (gpointer) audio_data };
        gpointer out_ptrs[1] = { out_buf };
        gst_audio_quantize_samples (quant, in_ptrs, out_ptrs, frames);
        g_free (out_buf);
      }
      gst_audio_quantize_free (quant);
    }
  }

  return 0;
}
