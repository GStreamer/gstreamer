
#define GST_PLUGIN_STATIC 

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "testplugin",
  "a plugin for testing",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
);

static gboolean
plugin2_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "testplugin2",
  "a second plugin for testing",
  plugin2_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
);

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;

  gst_init (&argc, &argv);

  plugin = gst_registry_pool_find_plugin ("testplugin");
  g_assert (plugin != NULL);

  g_print ("testplugin: %p %s\n", plugin, gst_plugin_get_name(plugin));

  plugin = gst_registry_pool_find_plugin ("testplugin2");
  g_assert (plugin != NULL);

  g_print ("testplugin2: %p %s\n", plugin, gst_plugin_get_name(plugin));

  return 0;
}
