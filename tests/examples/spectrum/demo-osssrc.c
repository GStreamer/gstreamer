#include <gnome.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

void spectrum_chain (GstPad * pad, GstData * _data);
gboolean idle_func (gpointer data);

GtkWidget *drawingarea;

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElementFactory *srcfactory;
  GstElement *src;
  GstElementFactory *spectrumfactory;
  GstElement *spectrum;
  GstPad *spectrumpad;

  GtkWidget *appwindow;

  _gst_plugin_spew = TRUE;

  gst_init (&argc, &argv);
  gnome_init ("Spectrum", "0.0.1", argc, argv);

  bin = gst_bin_new ("bin");

  srcfactory = gst_element_factory_find ("osssrc");
  spectrumfactory = gst_element_factory_find ("gstspectrum");

  src = gst_element_factory_create (srcfactory, "src");
  gtk_object_set (GTK_OBJECT (src), "bytes_per_read", (gulong) 1024, NULL);
  spectrum = gst_element_factory_create (spectrumfactory, "spectrum");
  gtk_object_set (GTK_OBJECT (spectrum), "width", 256, NULL);


  gst_bin_add (GST_BIN (bin), GST_ELEMENT (src));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (spectrum));

  gst_pad_link (gst_element_get_pad (src, "src"),
      gst_element_get_pad (spectrum, "sink"));

  spectrumpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (spectrumpad, spectrum_chain);

  gst_pad_link (gst_element_get_pad (spectrum, "src"), spectrumpad);

  appwindow = gnome_app_new ("spectrum", "Spectrum");
  drawingarea = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (drawingarea), 256, 32);
  gnome_app_set_contents (GNOME_APP (appwindow), drawingarea);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  g_idle_add (idle_func, src);

  gtk_main ();

  return 0;
}


void
spectrum_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  gint i;
  guchar *data = buf->data;

  gdk_draw_rectangle (drawingarea->window, drawingarea->style->black_gc,
      TRUE, 0, 0, GST_BUFFER_SIZE (buf), 25);
  for (i = 0; i < GST_BUFFER_SIZE (buf); i++) {
    gdk_draw_rectangle (drawingarea->window, drawingarea->style->white_gc,
        TRUE, i, 32 - data[i], 1, data[i]);
  }
  gst_buffer_unref (buf);
}

gboolean
idle_func (gpointer data)
{
  /*gst_src_push(GST_SRC(data)); */
  return TRUE;
}
