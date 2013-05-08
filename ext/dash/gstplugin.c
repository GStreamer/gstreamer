#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstfragmented.h"
#include "gstdashdemux.h"

GST_DEBUG_CATEGORY (fragmented_debug);

static gboolean
fragmented_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (fragmented_debug, "dashdemux", 0, "dashdemux");

  if (!gst_element_register (plugin, "dashdemux", GST_RANK_PRIMARY,
          GST_TYPE_DASH_DEMUX) || FALSE)
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "fragmented",
    "Fragmented streaming plugins",
    fragmented_init, VERSION, "LGPL", PACKAGE_NAME, "http://www.gstreamer.org/")


