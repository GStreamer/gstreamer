#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "polypsink.h"

GST_DEBUG_CATEGORY (polyp_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstaudio"))
    return FALSE;

  if (!(gst_element_register (plugin, "polypsink", GST_RANK_NONE,
              GST_TYPE_POLYPSINK)))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (polyp_debug, "polyp", 0, "Polypaudio elements");
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "polypsink", "Polypaudio Element Plugins",
    plugin_init,
    VERSION,
    "LGPL", "polypaudio", "http://0pointer.de/lennart/projects/gst-polyp/")
