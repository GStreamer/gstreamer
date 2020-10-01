/* GStreamer
 * Copyright (C) 2017, Collabora Ltd.
 *   Author:Justin Kim <justin.kim@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsrtsrc.h"
#include "gstsrtsink.h"

GST_DEBUG_CATEGORY_STATIC (gst_debug_srtlib);
GST_DEBUG_CATEGORY (gst_debug_srtobject);
#define GST_CAT_DEFAULT gst_debug_srtobject

#ifndef GST_REMOVE_DEPRECATED

#define GST_TYPE_SRT_CLIENT_SRC gst_srt_client_src_get_type()
#define GST_TYPE_SRT_SERVER_SRC gst_srt_server_src_get_type()

#define GST_TYPE_SRT_CLIENT_SINK gst_srt_client_sink_get_type()
#define GST_TYPE_SRT_SERVER_SINK gst_srt_server_sink_get_type()

typedef GstSRTSrc GstSRTClientSrc;
typedef GstSRTSrcClass GstSRTClientSrcClass;

typedef GstSRTSrc GstSRTServerSrc;
typedef GstSRTSrcClass GstSRTServerSrcClass;

typedef GstSRTSink GstSRTClientSink;
typedef GstSRTSinkClass GstSRTClientSinkClass;

typedef GstSRTSink GstSRTServerSink;
typedef GstSRTSinkClass GstSRTServerSinkClass;

static GType gst_srt_client_src_get_type (void);
static GType gst_srt_server_src_get_type (void);
static GType gst_srt_client_sink_get_type (void);
static GType gst_srt_server_sink_get_type (void);

G_DEFINE_TYPE (GstSRTClientSrc, gst_srt_client_src, GST_TYPE_SRT_SRC);
G_DEFINE_TYPE (GstSRTServerSrc, gst_srt_server_src, GST_TYPE_SRT_SRC);
G_DEFINE_TYPE (GstSRTClientSink, gst_srt_client_sink, GST_TYPE_SRT_SINK);
G_DEFINE_TYPE (GstSRTServerSink, gst_srt_server_sink, GST_TYPE_SRT_SINK);

static void
gst_srt_client_src_init (GstSRTClientSrc * src)
{
}

static void
gst_srt_client_src_class_init (GstSRTClientSrcClass * klass)
{
}

static void
gst_srt_server_src_init (GstSRTServerSrc * src)
{
}

static void
gst_srt_server_src_class_init (GstSRTServerSrcClass * klass)
{
}

static void
gst_srt_client_sink_init (GstSRTClientSink * sink)
{
}

static void
gst_srt_client_sink_class_init (GstSRTClientSinkClass * klass)
{
}

static void
gst_srt_server_sink_init (GstSRTServerSink * sink)
{
}

static void
gst_srt_server_sink_class_init (GstSRTServerSinkClass * klass)
{
}

#endif

#ifndef GST_DISABLE_GST_DEBUG
static void
gst_srt_log_handler (void *opaque, int level, const char *file, int line,
    const char *area, const char *message)
{
  GstDebugLevel gst_level;

  switch (level) {
    case LOG_CRIT:
      gst_level = GST_LEVEL_ERROR;
      break;

    case LOG_ERR:
      gst_level = GST_LEVEL_WARNING;
      break;

    case LOG_WARNING:
      gst_level = GST_LEVEL_INFO;
      break;

    case LOG_NOTICE:
      gst_level = GST_LEVEL_DEBUG;
      break;

    case LOG_DEBUG:
      gst_level = GST_LEVEL_LOG;
      break;

    default:
      gst_level = GST_LEVEL_FIXME;
      break;
  }

  if (G_UNLIKELY (gst_level <= _gst_debug_min)) {
    gst_debug_log (gst_debug_srtlib, gst_level, file, area, line, NULL, "%s",
        message);
  }
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_debug_srtobject, "srtobject", 0, "SRT Object");
  GST_DEBUG_CATEGORY_INIT (gst_debug_srtlib, "srtlib", 0, "SRT Library");

#ifndef GST_DISABLE_GST_DEBUG
  srt_setloghandler (NULL, gst_srt_log_handler);
  srt_setlogflags (SRT_LOGF_DISABLE_TIME | SRT_LOGF_DISABLE_THREADNAME |
      SRT_LOGF_DISABLE_SEVERITY | SRT_LOGF_DISABLE_EOL);
  srt_setloglevel (LOG_DEBUG);
#endif

  if (!gst_element_register (plugin, "srtsrc", GST_RANK_PRIMARY,
          GST_TYPE_SRT_SRC))
    return FALSE;

  if (!gst_element_register (plugin, "srtsink", GST_RANK_PRIMARY,
          GST_TYPE_SRT_SINK))
    return FALSE;

  /* deprecated */
#ifndef GST_REMOVE_DEPRECATED
  if (!gst_element_register (plugin, "srtclientsrc", GST_RANK_NONE,
          GST_TYPE_SRT_CLIENT_SRC))
    return FALSE;

  if (!gst_element_register (plugin, "srtserversrc", GST_RANK_NONE,
          GST_TYPE_SRT_SERVER_SRC))
    return FALSE;

  if (!gst_element_register (plugin, "srtclientsink", GST_RANK_NONE,
          GST_TYPE_SRT_CLIENT_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "srtserversink", GST_RANK_NONE,
          GST_TYPE_SRT_SERVER_SINK))
    return FALSE;
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    srt,
    "transfer data via SRT",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
