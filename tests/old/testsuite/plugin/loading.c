
#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstPlugin *plugin;
  gboolean loaded = FALSE;
  gint numplugins;

  gst_init (&argc, &argv);

  numplugins = g_list_length (gst_plugin_get_list ());
  g_print ("%d plugins loaded\n", numplugins);
  g_mem_chunk_info ();

  plugin = gst_plugin_find ("ossaudio");
  g_assert (plugin != NULL);

  g_print ("%d features in plugin\n", g_list_length (gst_plugin_get_feature_list (plugin)));


  g_print ("ossaudio: %p %d\n", plugin, gst_plugin_is_loaded (plugin));
  
  loaded = gst_plugin_load_plugin (plugin);
  g_assert (loaded == TRUE);

  numplugins = g_list_length (gst_plugin_get_list ());
  g_print ("%d plugins loaded\n", numplugins);

  g_mem_chunk_info ();

  plugin = gst_plugin_find ("ossaudio");
  g_assert (plugin != NULL);
  g_print ("ossaudio: %p %d\n", plugin, gst_plugin_is_loaded (plugin));

  g_print ("%d features in plugin\n", g_list_length (gst_plugin_get_feature_list (plugin)));

  loaded = gst_plugin_load_plugin (plugin);
  g_assert (loaded == TRUE);

  numplugins = g_list_length (gst_plugin_get_list ());
  g_print ("%d plugins loaded\n", numplugins);

  g_print ("%d features in plugin\n", g_list_length (gst_plugin_get_feature_list (plugin)));

  g_mem_chunk_info ();

  plugin = gst_plugin_find ("ossaudio");
  g_assert (plugin != NULL);
  g_print ("osssink: %p %d\n", plugin, gst_plugin_is_loaded (plugin));

  return 0;
}
