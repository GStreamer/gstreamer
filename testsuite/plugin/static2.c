
#define GST_PLUGIN_STATIC 

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

static gboolean
plugin2_init (GModule *module, GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DESC (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "testplugin2",
  plugin2_init
);

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;

  gst_init (&argc, &argv);

  plugin = gst_registry_pool_find_plugin ("testplugin");
  g_assert (plugin != NULL);

  g_print ("testplugin: %p %s\n", plugin, plugin->name);

  plugin = gst_registry_pool_find_plugin ("testplugin2");
  g_assert (plugin != NULL);

  g_print ("testplugin2: %p %s\n", plugin, plugin->name);

  return 0;
}
