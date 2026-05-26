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

/* Video converter fuzzing target
 *
 * Exercises:
 *   gst-libs/gst/video/video-converter.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#define HEADER_SIZE 6

static const GstVideoFormat formats[] = {
  GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_YV12,
  GST_VIDEO_FORMAT_NV12, GST_VIDEO_FORMAT_NV21,
  GST_VIDEO_FORMAT_YUY2, GST_VIDEO_FORMAT_UYVY,
  GST_VIDEO_FORMAT_AYUV, GST_VIDEO_FORMAT_RGB,
  GST_VIDEO_FORMAT_BGR, GST_VIDEO_FORMAT_RGBA,
  GST_VIDEO_FORMAT_BGRA, GST_VIDEO_FORMAT_ARGB,
  GST_VIDEO_FORMAT_ABGR, GST_VIDEO_FORMAT_GRAY8,
  GST_VIDEO_FORMAT_GRAY16_LE, GST_VIDEO_FORMAT_RGB16,
};

#define N_FORMATS G_N_ELEMENTS(formats)

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

  GstVideoFormat in_fmt = formats[data[0] % N_FORMATS];
  GstVideoFormat out_fmt = formats[data[1] % N_FORMATS];

  /* Random size */
  guint in_width = (guint) data[2] + 1;
  guint in_height = (guint) data[3] + 1;
  guint out_width = (guint) data[4] + 1;
  guint out_height = (guint) data[5] + 1;

  GstVideoInfo in_info, out_info;
  gst_video_info_init (&in_info);
  gst_video_info_init (&out_info);
  if (!gst_video_info_set_format (&in_info, in_fmt, in_width, in_height))
    return 0;
  if (!gst_video_info_set_format (&out_info, out_fmt, out_width, out_height))
    return 0;

  const guint8 *payload = data + HEADER_SIZE;
  gsize payload_size = size - HEADER_SIZE;
  gsize in_size = GST_VIDEO_INFO_SIZE (&in_info);
  gsize out_size = GST_VIDEO_INFO_SIZE (&out_info);

  if (in_size == 0 || out_size == 0)
    return 0;

  GstBuffer *in_buf = gst_buffer_new_allocate (NULL, in_size, NULL);
  GstBuffer *out_buf = gst_buffer_new_allocate (NULL, out_size, NULL);
  if (!in_buf || !out_buf) {
    if (in_buf)
      gst_buffer_unref (in_buf);
    if (out_buf)
      gst_buffer_unref (out_buf);
    return 0;
  }

  /* Fill input buffer with fuzz data (zero-padded if shorter) */
  {
    GstMapInfo map;
    if (gst_buffer_map (in_buf, &map, GST_MAP_WRITE)) {
      memset (map.data, 0, map.size);
      memcpy (map.data, payload, MIN (payload_size, map.size));
      gst_buffer_unmap (in_buf, &map);
    }
  }

  GstVideoFrame in_frame = GST_VIDEO_FRAME_INIT;
  GstVideoFrame out_frame = GST_VIDEO_FRAME_INIT;
  gboolean in_mapped = FALSE, out_mapped = FALSE;

  if (gst_video_frame_map (&in_frame, &in_info, in_buf, GST_MAP_READ)) {
    in_mapped = TRUE;
    if (gst_video_frame_map (&out_frame, &out_info, out_buf, GST_MAP_WRITE)) {
      out_mapped = TRUE;
      GstVideoConverter *conv =
          gst_video_converter_new (&in_info, &out_info, NULL);
      if (conv) {
        gst_video_converter_frame (conv, &in_frame, &out_frame);
        gst_video_converter_free (conv);
      }
    }
  }

  if (out_mapped)
    gst_video_frame_unmap (&out_frame);
  if (in_mapped)
    gst_video_frame_unmap (&in_frame);

  gst_buffer_unref (in_buf);
  gst_buffer_unref (out_buf);

  return 0;
}
