#include <gst/gst.h>

void
gstsharp_gst_index_factory_set_plugin (GstIndexFactory * factory,
    GstPlugin * plugin)
{
  if (!plugin)
    return;

  GST_PLUGIN_FEATURE (factory)->plugin_name = g_strdup (plugin->desc.name);
  GST_PLUGIN_FEATURE (factory)->loaded = TRUE;
}
