
#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;
  gboolean loaded;

  gst_init (&argc, &argv);

  plugin = gst_plugin_new (".libs/libtestplugin.so");
  g_assert (plugin != NULL);

  loaded = gst_plugin_load_plugin (plugin);
  g_assert (loaded == TRUE);

  g_print ("testplugin: %d, %s\n", loaded, plugin->name);

  return 0;
}
