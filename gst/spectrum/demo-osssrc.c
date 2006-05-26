#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gtk/gtk.h>

#define DEFAULT_AUDIOSRC "alsasrc"
#define SPECT_BANDS 256

static GtkWidget *drawingarea = NULL;

static void
spectrum_chain (GstElement * sink, GstBuffer * buf, GstPad * pad,
    gpointer unused)
{
  gint i;
  guchar *data = buf->data;
  gint width = GST_BUFFER_SIZE (buf);
  GdkRectangle rect = { 0, 0, width, 50 };

  gdk_window_begin_paint_rect (drawingarea->window, &rect);
  gdk_draw_rectangle (drawingarea->window, drawingarea->style->black_gc,
      TRUE, 0, 0, width, 50);
  for (i = 0; i < width; i++) {
    gdk_draw_rectangle (drawingarea->window, drawingarea->style->white_gc,
        TRUE, i, 64 - data[i], 1, data[i]);
  }
  gdk_window_end_paint (drawingarea->window);
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src, *spectrum, *sink;

  GtkWidget *appwindow;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  src = gst_element_factory_make (DEFAULT_AUDIOSRC, "src");
  g_object_set (G_OBJECT (src), "blocksize", (gulong) 1024 * 2, NULL);

  spectrum = gst_element_factory_make ("spectrum", "spectrum");
  g_object_set (G_OBJECT (spectrum), "width", SPECT_BANDS, "threshold", -80,
      NULL);

  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);

  g_signal_connect (sink, "handoff", G_CALLBACK (spectrum_chain), NULL);

  gst_bin_add_many (GST_BIN (bin), src, spectrum, sink, NULL);
  if (!gst_element_link_many (src, spectrum, sink, NULL)) {
    fprintf (stderr, "cant link elements\n");
    exit (1);
  }

  appwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  drawingarea = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (drawingarea), SPECT_BANDS, 64);
  gtk_container_add (GTK_CONTAINER (appwindow), drawingarea);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (bin, GST_STATE_PLAYING);
  gtk_main ();
  gst_element_set_state (bin, GST_STATE_NULL);

  gst_object_unref (bin);

  return 0;
}
