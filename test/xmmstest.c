#include <gnome.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func(gpointer data);

int 
main (int argc,char *argv[]) 
{
  GstElement *bin;
  GstElement *src;
  GstElement *audiosink;

  gst_init(&argc,&argv);

  bin = gst_bin_new("bin");

  //src = gst_elementfactory_make("XMMS_INPUT_mpeg_layer_1/2/3_player_1.2.4", "xmms_plugin");
  //src = gst_elementfactory_make("XMMS_INPUT_oggvorbis_player_0.1", "xmms_plugin");
  src = gst_elementfactory_make("XMMS_INPUT_mikmod_player_1.2.4", "xmms_plugin");
  //src = gst_elementfactory_make("XMMS_INPUT_tone_generator_1.2.4", "xmms_plugin");
  g_return_val_if_fail(src != NULL, -1);

  gtk_object_set (GTK_OBJECT (src), "location", argv[1], NULL);
  //gtk_object_set (GTK_OBJECT (src), "filename", "tone://1000", NULL);

  audiosink = gst_elementfactory_make("audiosink", "audiosink");
  g_return_val_if_fail(audiosink != NULL, -1);

  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(audiosink));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  g_idle_add(idle_func, bin);

  gtk_main();

  return 0;
}

gboolean 
idle_func (gpointer data) 
{
  gst_bin_iterate(GST_BIN(data));

  return TRUE;
}
