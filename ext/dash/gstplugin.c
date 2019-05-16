#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdashdemux.h"
#include "gstdashsink.h"

GST_DEBUG_CATEGORY (dash_debug);

static gboolean
dash_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (dash_debug, "DASH", 0, "HTTP Live Streaming (HLS)");

  if (!gst_element_register (plugin, "dashdemux", GST_RANK_PRIMARY,
          GST_TYPE_DASH_DEMUX) || FALSE)
    return FALSE;

  if (!gst_dash_sink_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dash,
    "DASH plugin", dash_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
