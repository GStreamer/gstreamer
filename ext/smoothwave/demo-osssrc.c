#include <gtk/gtk.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func (gpointer data);

GtkWidget *drawingarea;

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElementFactory *srcfactory;
  GstElement *src;
  GstElementFactory *wavefactory;
  GstElement *wave;
  GtkWidget *wave_widget;
  GtkWidget *appwindow;

  gst_init (&argc, &argv);
  gst_plugin_load ("libsmoothwave.so");
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  srcfactory = gst_element_factory_find ("sinesrc");
  g_return_val_if_fail (srcfactory != NULL, -1);
  wavefactory = gst_element_factory_find ("smoothwave");
  g_return_val_if_fail (wavefactory != NULL, -1);

  src = gst_element_factory_create (srcfactory, "src");
  //g_object_set(G_OBJECT(src),"bytes_per_read",(gulong)2048,NULL);
  wave = gst_element_factory_create (wavefactory, "wave");
  g_object_set (G_OBJECT (wave), "width", 256, "height", 100, NULL);


  gst_bin_add (GST_BIN (bin), GST_ELEMENT (src));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (wave));

  gst_pad_link (gst_element_get_pad (src, "src"),
      gst_element_get_pad (wave, "sink"));

  appwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_object_get (G_OBJECT (wave), "widget", &wave_widget, NULL);
  gtk_container_add (GTK_CONTAINER (appwindow), wave_widget);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
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
