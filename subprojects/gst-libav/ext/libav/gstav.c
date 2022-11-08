/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <gst/gst.h>

#include "gstav.h"
#include "gstavutils.h"
#include "gstavcfg.h"

#ifdef GST_LIBAV_ENABLE_GPL
#define LICENSE "GPL"
#else
#define LICENSE "LGPL"
#endif

GST_DEBUG_CATEGORY (ffmpeg_debug);

static GMutex gst_avcodec_mutex;

/*
 * Check for FFmpeg-provided libavcodec/format
 */
static inline gboolean
gst_ffmpeg_avcodec_is_ffmpeg (void)
{
  guint av_version = avcodec_version ();

  GST_DEBUG ("Using libavcodec version %d.%d.%d",
      av_version >> 16, (av_version & 0x00ff00) >> 8, av_version & 0xff);

  /* FFmpeg *_MICRO versions start at 100 and Libav's at 0 */
  if ((av_version & 0xff) < 100)
    return FALSE;

  return TRUE;
}

int
gst_ffmpeg_avcodec_open (AVCodecContext * avctx, const AVCodec * codec)
{
  int ret;

  g_mutex_lock (&gst_avcodec_mutex);
  ret = avcodec_open2 (avctx, codec, NULL);
  g_mutex_unlock (&gst_avcodec_mutex);

  return ret;
}

int
gst_ffmpeg_avcodec_close (AVCodecContext * avctx)
{
  int ret;

  g_mutex_lock (&gst_avcodec_mutex);
  ret = avcodec_close (avctx);
  g_mutex_unlock (&gst_avcodec_mutex);

  return ret;
}

int
gst_ffmpeg_av_find_stream_info (AVFormatContext * ic)
{
  int ret;

  g_mutex_lock (&gst_avcodec_mutex);
  ret = avformat_find_stream_info (ic, NULL);
  g_mutex_unlock (&gst_avcodec_mutex);

  return ret;
}

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_ffmpeg_log_callback (void *ptr, int level, const char *fmt, va_list vl)
{
  GstDebugLevel gst_level;
  gint len = strlen (fmt);
  gchar *fmt2 = NULL;

  switch (level) {
    case AV_LOG_QUIET:
      gst_level = GST_LEVEL_NONE;
      break;
    case AV_LOG_ERROR:
      gst_level = GST_LEVEL_ERROR;
      break;
    case AV_LOG_INFO:
      gst_level = GST_LEVEL_INFO;
      break;
    case AV_LOG_DEBUG:
      gst_level = GST_LEVEL_DEBUG;
      break;
    default:
      gst_level = GST_LEVEL_INFO;
      break;
  }

  /* remove trailing newline as it gets already appended by the logger */
  if (fmt[len - 1] == '\n') {
    fmt2 = g_strdup (fmt);
    fmt2[len - 1] = '\0';
  }

  gst_debug_log_valist (ffmpeg_debug, gst_level, "", "", 0, NULL,
      fmt2 ? fmt2 : fmt, vl);

  g_free (fmt2);
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ffmpeg_debug, "libav", 0, "libav elements");

  /* Bail if not FFmpeg. We can no longer ensure operation with Libav */
  if (!gst_ffmpeg_avcodec_is_ffmpeg ()) {
    GST_ERROR_OBJECT (plugin,
        "Incompatible, non-FFmpeg libavcodec/format found");
    return FALSE;
  }
#ifndef GST_DISABLE_GST_DEBUG
  av_log_set_callback (gst_ffmpeg_log_callback);
#endif

  gst_ffmpeg_init_pix_fmt_info ();

  /* build global ffmpeg param/property info */
  gst_ffmpeg_cfg_init ();

  gst_ffmpegaudenc_register (plugin);
  gst_ffmpegvidenc_register (plugin);
  gst_ffmpegauddec_register (plugin);
  gst_ffmpegviddec_register (plugin);
  gst_ffmpegdemux_register (plugin);
  gst_ffmpegmux_register (plugin);
  gst_ffmpegdeinterlace_register (plugin);
  gst_ffmpegvidcmp_register (plugin);

  /* Now we can return the pointer to the newly created Plugin object. */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    libav,
    "All libav codecs and formats (" LIBAV_SOURCE ")",
    plugin_init, PACKAGE_VERSION, LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
