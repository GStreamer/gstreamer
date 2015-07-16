#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstdashdemux.h"

static gboolean
dashdemux_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "dashdemux", GST_RANK_PRIMARY,
      GST_TYPE_DASH_DEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dashdemux,
    "DASH demuxer plugin",
    dashdemux_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
