
#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;
  gboolean loaded;

  gst_init (&argc, &argv);

  gst_plugin_add_path (".");

  loaded = gst_plugin_load ("testplugin");
  g_assert (loaded == TRUE);

  plugin = gst_plugin_find ("testplugin");
  g_assert (plugin != NULL);

  g_print ("testplugin: %d, %s\n", loaded, plugin->name);

  return 0;
}
