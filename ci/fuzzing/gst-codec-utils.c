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

/* Codec header parsing fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/pbutils/codec-utils.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/codec-utils.h>

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

  if (size == 0)
    return 0;

  /* H.264 AVCDecoderConfigurationRecord */
  {
    guint8 profile, flags, level;
    gst_codec_utils_h264_get_profile_flags_level (data, (guint) size,
        &profile, &flags, &level);
  }

  /* H.265 profile-tier-level */
  {
    GstCaps *caps = gst_caps_new_empty_simple ("video/x-h265");
    gst_codec_utils_h265_caps_set_level_tier_and_profile (caps, data,
        (guint) size);
    gst_caps_unref (caps);
  }

  /* H.266 profile-tier-level */
  {
    GstCaps *caps = gst_caps_new_empty_simple ("video/x-h266");
    gst_codec_utils_h266_caps_set_level_tier_and_profile (caps, data,
        (guint) size);
    gst_caps_unref (caps);
  }

  /* Opus header parsing with fixed  "OpusHead" + version=1 */
  if (size >= 10) {
    gsize opus_size = 9 + size;
    guint8 *opus_data = g_malloc (opus_size);
    memcpy (opus_data, "OpusHead\x01", 9);
    memcpy (opus_data + 9, data, size);
    GstBuffer *buf = gst_buffer_new_wrapped (opus_data, opus_size);
    guint32 rate;
    guint8 channels, family, stream_count, coupled_count;
    guint8 channel_mapping[256];
    guint16 pre_skip;
    gint16 output_gain;
    gst_codec_utils_opus_parse_header (buf, &rate, &channels, &family,
        &stream_count, &coupled_count, channel_mapping, &pre_skip,
        &output_gain);
    gst_buffer_unref (buf);
  }

  /* AV1 codec configuration record */
  {
    GstBuffer *buf =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
        size, 0, size, NULL, NULL);
    GstCaps *caps = gst_codec_utils_av1_create_caps_from_av1c (buf);
    if (caps != NULL)
      gst_caps_unref (caps);
    gst_buffer_unref (buf);
  }

  return 0;
}
