
#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;

  gst_init (&argc, &argv);

  plugin = gst_plugin_find ("testplugin");
  g_assert (plugin != NULL);

  g_print ("testplugin: %s\n", plugin->name);

  return 0;
}
