
#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;

  gst_init (&argc, &argv);

  plugin = gst_plugin_find ("testplugin");
  g_assert (plugin != NULL);

  g_print ("testplugin: %p %s\n", plugin, plugin->name);

  plugin = gst_plugin_find ("testplugin2");
  g_assert (plugin != NULL);

  g_print ("testplugin2: %p %s\n", plugin, plugin->name);

  return 0;
}
