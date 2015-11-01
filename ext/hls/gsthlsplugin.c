#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gsthls.h"
#include "gsthlsdemux.h"
#include "gsthlssink.h"

GST_DEBUG_CATEGORY (hls_debug);

static gboolean
hls_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (hls_debug, "hls", 0, "HTTP Live Streaming (HLS)");

  if (!gst_element_register (plugin, "hlsdemux", GST_RANK_PRIMARY,
          GST_TYPE_HLS_DEMUX) || FALSE)
    return FALSE;

  if (!gst_hls_sink_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    hls,
    "HTTP Live Streaming (HLS)",
    hls_init, VERSION, GST_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
