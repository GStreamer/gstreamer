
#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstPlugin *plugin;
  GError *error = NULL;

  gst_init (&argc, &argv);

  plugin = gst_plugin_load_file (".libs/libtestplugin.so", &error);
  g_assert (plugin != NULL);
  if (error) {
    g_print ("ERROR loading plug-in: %s\n", error->message);
    g_free (error);
    return 1;
  }

  g_print ("testplugin: %s\n", gst_plugin_get_name (plugin));

  return 0;
}
