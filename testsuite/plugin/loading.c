
#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;
  gboolean loaded = FALSE;
  gint numplugins;
  GError *error = NULL;

  gst_init (&argc, &argv);

  numplugins = g_list_length (gst_registry_pool_plugin_list ());
  g_print ("%d plugins loaded\n", numplugins);
  g_mem_chunk_info ();

  plugin = gst_registry_pool_find_plugin ("testplugin");
  g_assert (plugin != NULL);

  g_print ("%d features in plugin\n", g_list_length (gst_plugin_get_feature_list (plugin)));


  g_print ("testplugin: %p  loaded: %s\n", plugin, (gst_plugin_is_loaded (plugin) ? "true": "false"));
  
  loaded = gst_plugin_load_plugin (plugin, &error);
  if (error)
  {
    g_print ("ERROR loading plug-in: %s\n", error->message);
    g_free (error);
  }
  g_assert (loaded == TRUE);

  numplugins = g_list_length (gst_registry_pool_plugin_list ());
  g_print ("%d plugins loaded\n", numplugins);

  g_mem_chunk_info ();

  plugin = gst_registry_pool_find_plugin ("testplugin");
  g_assert (plugin != NULL);
  g_print ("testplugin: %p  loaded: %s\n", plugin, (gst_plugin_is_loaded (plugin) ? "true": "false"));

  g_print ("%d features in plugin\n", g_list_length (gst_plugin_get_feature_list (plugin)));

  loaded = gst_plugin_load_plugin (plugin, &error);
  if (error)
  {
    g_print ("ERROR loading plug-in: %s\n", error->message);
    g_free (error);
  }
  g_assert (loaded == TRUE);

  numplugins = g_list_length (gst_registry_pool_plugin_list ());
  g_print ("%d plugins loaded\n", numplugins);

  g_print ("%d features in plugin\n", g_list_length (gst_plugin_get_feature_list (plugin)));

  g_mem_chunk_info ();

  plugin = gst_registry_pool_find_plugin ("testplugin");
  g_assert (plugin != NULL);
  g_print ("testplugin: %p  loaded: %s\n", plugin, (gst_plugin_is_loaded (plugin) ? "true": "false"));

  return 0;
}
