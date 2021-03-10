/* Copyright (C) <2018> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
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

/**
 * SECTION:element-wpesrc
 * @title: wpesrc
 *
 * The wpesrc element is used to produce a video texture representing a web page
 * rendered off-screen by WPE.
 *
 * Starting from WPEBackend-FDO 1.6.x, software rendering support is available. This
 * features allows wpesrc to be used on machines without GPU, and/or for testing
 * purpose. To enable it, set the `LIBGL_ALWAYS_SOFTWARE=true` environment
 * variable and make sure `video/x-raw, format=BGRA` caps are negotiated by the
 * wpesrc element.
 *
 * ## Example launch lines
 *
 * |[
 * gst-launch-1.0 -v wpesrc location="https://gstreamer.freedesktop.org" ! queue ! glimagesink
 * ]|
 * Shows the GStreamer website homepage
 *
 * |[
 * LIBGL_ALWAYS_SOFTWARE=true gst-launch-1.0 -v wpesrc num-buffers=50 location="https://gstreamer.freedesktop.org" ! videoconvert ! pngenc ! multifilesink location=/tmp/snapshot-%05d.png
 * ]|
 * Saves the first 50 video frames generated for the GStreamer website as PNG files in /tmp.
 *
 * |[
 * gst-play-1.0 --videosink gtkglsink wpe://https://gstreamer.freedesktop.org
 * ]|
 * Shows the GStreamer website homepage as played with GstPlayer in a GTK+ window.
 *
 * |[
 * gst-launch-1.0  glvideomixer name=m sink_1::zorder=0 ! glimagesink wpesrc location="file:///home/phil/Downloads/plunk/index.html" draw-background=0 ! m. videotestsrc ! queue ! glupload ! glcolorconvert ! m.
 * ]|
 * Composite WPE with a video stream in a single OpenGL scene.
 *
 * |[
 * gst-launch-1.0 glvideomixer name=m sink_1::zorder=0 sink_0::height=818 sink_0::width=1920 ! gtkglsink wpesrc location="file:///home/phil/Downloads/plunk/index.html" draw-background=0 ! m. uridecodebin uri="http://192.168.1.44/Sintel.2010.1080p.mkv" name=d d. ! queue ! glupload ! glcolorconvert ! m.
 * ]|
 * Composite WPE with a video stream, sink_0 pad properties have to match the video dimensions.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwpevideosrc.h"
#include "gstwpesrcbin.h"

GST_DEBUG_CATEGORY (wpe_video_src_debug);
GST_DEBUG_CATEGORY (wpe_view_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (wpe_video_src_debug, "wpesrc", 0, "WPE Source");
  GST_DEBUG_CATEGORY_INIT (wpe_view_debug, "wpeview", 0, "WPE Threaded View");

  gboolean result = gst_element_register (plugin, "wpevideosrc", GST_RANK_NONE,
      GST_TYPE_WPE_VIDEO_SRC);
  result &= gst_element_register(plugin, "wpesrc", GST_RANK_NONE, GST_TYPE_WPE_SRC);
  return result;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    wpe, "WPE src plugin", plugin_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN)
