
#include <gst/gst.h>

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DESC (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "testplugin",
  plugin_init
);
