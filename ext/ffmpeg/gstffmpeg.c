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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#include <avformat.h>
#else
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#endif

#include "gstffmpeg.h"

GST_DEBUG_CATEGORY (ffmpeg_debug);

static GStaticMutex gst_avcodec_mutex = G_STATIC_MUTEX_INIT;


int
gst_ffmpeg_avcodec_open (AVCodecContext *avctx, AVCodec *codec) {
  int ret;

  g_static_mutex_lock (&gst_avcodec_mutex);
  ret = avcodec_open (avctx, codec);
  g_static_mutex_unlock (&gst_avcodec_mutex);
  
  return ret;
}

int
gst_ffmpeg_avcodec_close (AVCodecContext *avctx) {
  int ret;
  
  g_static_mutex_lock (&gst_avcodec_mutex);
  ret = avcodec_close (avctx);
  g_static_mutex_unlock (&gst_avcodec_mutex);
  
  return ret;
}

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_ffmpeg_log_callback (void * ptr, int level, const char * fmt, va_list vl)
{
  GstDebugLevel gst_level;

  if (_shut_up_I_am_probing)
    return;

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

  gst_debug_log_valist (ffmpeg_debug, gst_level, "", "", 0, NULL, fmt, vl);
}
#endif

#ifndef GST_DISABLE_GST_DEBUG
gboolean _shut_up_I_am_probing = FALSE;
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ffmpeg_debug, "ffmpeg", 0, "FFmpeg elements");
#ifndef GST_DISABLE_GST_DEBUG

  av_log_set_callback (gst_ffmpeg_log_callback);
#endif
  av_register_all ();

  gst_ffmpegenc_register (plugin);
  gst_ffmpegdec_register (plugin);
  gst_ffmpegdemux_register (plugin);
  gst_ffmpegdeinterlace_register (plugin);
  gst_ffmpegscale_register (plugin);
#if 0
  gst_ffmpegmux_register (plugin);
  gst_ffmpegcsp_register (plugin);
#endif

  register_protocol (&gstreamer_protocol);

  /* Now we can return the pointer to the newly created Plugin object. */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ffmpeg",
    "All FFMPEG codecs",
    plugin_init,
    GST_FFMPEG_VERSION, "LGPL", "FFMpeg", "http://ffmpeg.sourceforge.net/")


