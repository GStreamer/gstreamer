
#include <gst/gst.h>

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
