
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE_STATIC (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "testplugin2",
  "another testplugin for testing",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_COPYRIGHT,
  GST_PACKAGE,
  GST_ORIGIN
);

