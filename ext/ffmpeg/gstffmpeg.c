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

#include "config.h"
#include <gst/gst.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#include <avformat.h>
#else
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#endif

extern gboolean gst_ffmpegdemux_register (GstPlugin *plugin);
extern gboolean gst_ffmpegdec_register (GstPlugin *plugin);
extern gboolean gst_ffmpegenc_register (GstPlugin *plugin);
extern gboolean gst_ffmpegmux_register (GstPlugin *plugin);
extern gboolean gst_ffmpegcsp_register (GstPlugin *plugin);
	
extern URLProtocol gstreamer_protocol;

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  avcodec_init ();
  avcodec_register_all ();
  av_register_all ();

  gst_ffmpegenc_register (plugin);
  gst_ffmpegdec_register (plugin);
  /*gst_ffmpegdemux_register (plugin);*/
  /*gst_ffmpegmux_register (plugin);*/
  gst_ffmpegcsp_register (plugin);

  /*register_protocol (&gstreamer_protocol);*/

  /* Now we can return the pointer to the newly created Plugin object. */
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ffmpeg",
  "All FFMPEG codecs",
  plugin_init,
  FFMPEG_VERSION,
  "LGPL",
  "FFMpeg",
  "http://ffmpeg.sourceforge.net/"
)
