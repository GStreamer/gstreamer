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

/* Video library fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/video/video-hdr.c
 *   gst-libs/gst/video/video-color.c
 *   gst-libs/gst/video/video-info-dma.c
 *   gst-libs/gst/video/gstvideotimecode.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>

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

  /* HDR10+ binary parsing */
  {
    GstVideoHDR10Plus hdr10;
    gst_video_hdr_parse_hdr10_plus (data, size, &hdr10);
  }

  /* string-based metadata parsers */
  {
    gchar *str = g_strndup ((const gchar *) data, size);

    {
      GstVideoMasteringDisplayInfo minfo;
      gst_video_mastering_display_info_from_string (&minfo, str);
    }
    {
      GstVideoContentLightLevel linfo;
      gst_video_content_light_level_from_string (&linfo, str);
    }
    {
      GstVideoColorimetry cinfo;
      gst_video_colorimetry_from_string (&cinfo, str);
    }
    {
      guint64 modifier;
      gst_video_dma_drm_fourcc_from_string (str, &modifier);
    }
    {
      GstVideoTimeCode *tc = gst_video_time_code_new_from_string (str);
      if (tc)
        gst_video_time_code_free (tc);
    }
    {
      GstVideoTimeCodeInterval *tci =
          gst_video_time_code_interval_new_from_string (str);
      if (tci)
        gst_video_time_code_interval_free (tci);
    }

    g_free (str);
  }

  return 0;
}
