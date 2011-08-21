#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstfragmented.h"
#include "gsthlsdemux.h"
#include "gsthlssink.h"

GST_DEBUG_CATEGORY (fragmented_debug);

static gboolean
fragmented_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (fragmented_debug, "fragmented", 0, "fragmented");

  if (!gst_element_register (plugin, "hlsdemux", GST_RANK_PRIMARY,
          GST_TYPE_HLS_DEMUX) || FALSE)
    return FALSE;

  if (!gst_hls_sink_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    fragmented,
    "Fragmented streaming plugins",
    fragmented_init, VERSION, "LGPL", PACKAGE_NAME, "http://www.gstreamer.org/")
