#include <gnome.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func(gpointer data);

GtkWidget *drawingarea;

int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElementFactory *srcfactory;
  GstElement *src;
  GstElementFactory *wavefactory;
  GstElement *wave;

  GtkWidget *appwindow;

  _gst_plugin_spew = TRUE;

  gst_init(&argc,&argv);
  gst_plugin_load("libsmoothwave.so");
  gnome_init("Wave","0.0.1",argc,argv);

  bin = gst_bin_new("bin");

  srcfactory = gst_elementfactory_find("audiosrc");
  g_return_val_if_fail(srcfactory != NULL, -1);
  wavefactory = gst_elementfactory_find("smoothwave");
  g_return_val_if_fail(wavefactory != NULL, -1);

  src = gst_elementfactory_create(srcfactory,"src");
  gtk_object_set(GTK_OBJECT(src),"bytes_per_read",(gulong)2048,NULL);
  wave = gst_elementfactory_create(wavefactory,"wave");
  gtk_object_set(GTK_OBJECT(wave),"width",256,"height",100,NULL);


  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(wave));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(wave,"sink"));

  appwindow = gnome_app_new("wave","Wave");
  gnome_app_set_contents(GNOME_APP(appwindow),gst_util_get_widget_arg(GTK_OBJECT(wave),"widget"));
  gtk_widget_show_all(appwindow);

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_READY);
  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  g_idle_add(idle_func,bin);

  gtk_main();
}

gboolean idle_func(gpointer data) {
  gst_bin_iterate(GST_BIN(data));
  return TRUE;
}
