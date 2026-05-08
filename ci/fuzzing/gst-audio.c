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

/* Audio caps fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/audio/audio-info.c
 *   gst-libs/gst/audio/audio-channels.c
 *   gst-libs/gst/audio/gstdsd.c
 *   gst-libs/gst/audio/gstaudioringbuffer.c
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

static const gint rates[] = {
  8000, 11025, 16000, 22050, 32000, 44100, 48000, 96000
};

#define N_RATES G_N_ELEMENTS(rates)

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

  gint in_ch = (data[2] % 8) + 1;
  gint in_rate = rates[data[4] % N_RATES];

  const guint8 *audio_data = data + HEADER_SIZE;
  gsize audio_size = size - HEADER_SIZE;

  /* fuzz-derived caps parsers */
  {
    gchar *fuzz_str = g_strndup ((const gchar *) data, size);

    {
      GstAudioInfo info;
      GstCaps *caps = gst_caps_new_simple ("audio/x-raw",
          "format", G_TYPE_STRING, "S16LE",
          "layout", G_TYPE_STRING, fuzz_str,
          "rate", G_TYPE_INT, in_rate,
          "channels", G_TYPE_INT, in_ch,
          NULL);
      gst_audio_info_from_caps (&info, caps);
      gst_caps_unref (caps);
    }
    {
      GstAudioInfo *info;
      GstCaps *caps = gst_caps_new_simple ("audio/x-raw",
          "format", G_TYPE_STRING, "S16LE",
          "layout", G_TYPE_STRING, fuzz_str,
          "rate", G_TYPE_INT, in_rate,
          "channels", G_TYPE_INT, in_ch,
          NULL);
      info = gst_audio_info_new_from_caps (caps);
      if (info)
        gst_audio_info_free (info);
      gst_caps_unref (caps);
    }
    {
      GstDsdInfo dsd_info;
      GstCaps *caps = gst_caps_new_simple ("audio/x-dsd",
          "format", G_TYPE_STRING, "DSDU8",
          "layout", G_TYPE_STRING, fuzz_str,
          "rate", G_TYPE_INT, in_rate,
          "channels", G_TYPE_INT, in_ch,
          NULL);
      gst_dsd_info_from_caps (&dsd_info, caps);
      gst_caps_unref (caps);
    }
    {
      static const gchar *const mimes[] = {
        "audio/x-raw", "audio/x-alaw", "audio/x-mulaw",
        "audio/x-iec958", "audio/x-ac3", "audio/x-dts",
        "audio/mpeg", "audio/x-flac",
      };
      const gchar *mime = mimes[(data[5] >> 3) % G_N_ELEMENTS (mimes)];
      GstAudioRingBufferSpec spec = { 0 };
      GstCaps *caps;

      spec.latency_time = 10000;

      if (g_str_equal (mime, "audio/x-raw")) {
        caps = gst_caps_new_simple (mime,
            "format", G_TYPE_STRING, "S16LE",
            "layout", G_TYPE_STRING, "interleaved",
            "rate", G_TYPE_INT, in_rate, "channels", G_TYPE_INT, in_ch, NULL);
      } else if (g_str_equal (mime, "audio/mpeg")) {
        caps = gst_caps_new_simple (mime,
            "mpegversion", G_TYPE_INT, 1,
            "mpegaudioversion", G_TYPE_INT, 1,
            "stream-format", G_TYPE_STRING, fuzz_str,
            "rate", G_TYPE_INT, in_rate, "channels", G_TYPE_INT, in_ch, NULL);
      } else {
        caps = gst_caps_new_simple (mime,
            "rate", G_TYPE_INT, in_rate, "channels", G_TYPE_INT, in_ch, NULL);
      }
      gst_audio_ring_buffer_parse_caps (&spec, caps);
      if (spec.caps)
        gst_caps_unref (spec.caps);
      gst_caps_unref (caps);
    }

    g_free (fuzz_str);
  }

  /* channel positions from mask */
  {
    if (audio_size >= sizeof (guint64)) {
      guint64 mask;
      GstAudioChannelPosition positions[64];

      memcpy (&mask, audio_data, sizeof (guint64));
      gst_audio_channel_positions_from_mask (in_ch, mask, positions);
    }
  }

  return 0;
}
