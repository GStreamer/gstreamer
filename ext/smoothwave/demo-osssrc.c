#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gtk/gtk.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func (gpointer data);

GtkWidget *drawingarea;

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src;
  GstElement *wave;
  GstElement *ximage;

  gst_init (&argc, &argv);
  gst_plugin_load ("libsmoothwave.so");
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  src = gst_element_factory_make ("sinesrc", "src");
  wave = gst_element_factory_make ("smoothwave", "wave");
  ximage = gst_element_factory_make (DEFAULT_VIDEOSINK, "sink");
  g_return_val_if_fail (src != NULL, -1);
  g_return_val_if_fail (wave != NULL, -1);
  g_return_val_if_fail (ximage != NULL, -1);


  gst_bin_add_many (GST_BIN (bin), src, wave, ximage, NULL);
  g_return_val_if_fail (gst_element_link_many (src, wave, ximage,
          NULL) != FALSE, -1);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  g_idle_add (idle_func, bin);

  gtk_main ();

  return 0;
}

gboolean
idle_func (gpointer data)
{
  gst_bin_iterate (GST_BIN (data));
  return TRUE;
}
