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

/* Video VBI/VANC parser fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/video/video-anc.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#define HEADER_SIZE 4

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

  GstVideoFormat fmt = (data[0] & 1)
      ? GST_VIDEO_FORMAT_v210 : GST_VIDEO_FORMAT_UYVY;
  guint32 pixel_width = ((data[0] >> 1) % 252) + 4;

  GstVideoInfo vinfo;
  gst_video_info_init (&vinfo);
  if (!gst_video_info_set_format (&vinfo, fmt, pixel_width, 1))
    return 0;

  gsize line_size = GST_VIDEO_INFO_PLANE_STRIDE (&vinfo, 0);

  const guint8 *payload = data + HEADER_SIZE;
  gsize payload_size = size - HEADER_SIZE;

  /* VBI parser */
  {
    guint8 *line_buf = g_malloc0 (line_size);
    memcpy (line_buf, payload, MIN (payload_size, line_size));

    GstVideoVBIParser *vbi = gst_video_vbi_parser_new (fmt, pixel_width);
    if (vbi) {
      GstVideoAncillary anc;
      gst_video_vbi_parser_add_line (vbi, line_buf);
      while (gst_video_vbi_parser_get_ancillary (vbi, &anc) ==
          GST_VIDEO_VBI_PARSER_RESULT_OK);

      gst_video_vbi_parser_free (vbi);
    }
    g_free (line_buf);
  }

  /* VBI encoder */
  {
    guint8 *line_buf = g_malloc0 (line_size);

    GstVideoVBIEncoder *enc = gst_video_vbi_encoder_new (fmt, pixel_width);
    if (enc) {
      guint8 DID = data[1];
      guint8 SDID = data[2];
      gboolean composite = (data[3] & 1) != 0;
      guint data_count = MIN ((guint) payload_size, 255U);

      gst_video_vbi_encoder_add_ancillary (enc, composite, DID, SDID,
          payload, data_count);
      gst_video_vbi_encoder_write_line (enc, line_buf);

      gst_video_vbi_encoder_free (enc);
    }
    g_free (line_buf);
  }

  return 0;
}
