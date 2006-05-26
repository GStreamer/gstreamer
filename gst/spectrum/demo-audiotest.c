#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gtk/gtk.h>

#define SPECT_BANDS 256

static GtkWidget *drawingarea = NULL;
static gint ct = 0;

static void
on_frequency_changed (GtkRange * range, gpointer user_data)
{
  GstElement *machine = GST_ELEMENT (user_data);
  gdouble value = gtk_range_get_value (range);

  g_object_set (machine, "freq", value, NULL);
}

static void
spectrum_chain (GstElement * sink, GstBuffer * buf, GstPad * pad,
    gpointer unused)
{
  ct = (ct + 1) & 0x15;
  if (!ct) {
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
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src, *spectrum, *sink;

  GtkWidget *appwindow, *vbox, *widget;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  src = gst_element_factory_make ("audiotestsrc", "src");
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
  vbox = gtk_vbox_new (FALSE, 6);

  widget = gtk_hscale_new_with_range (50.0, 20000.0, 10);
  gtk_scale_set_draw_value (GTK_SCALE (widget), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE (widget), GTK_POS_TOP);
  gtk_range_set_value (GTK_RANGE (widget), 440.0);
  g_signal_connect (G_OBJECT (widget), "value-changed",
      G_CALLBACK (on_frequency_changed), (gpointer) src);
  gtk_container_add (GTK_CONTAINER (vbox), widget);

  drawingarea = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (drawingarea), SPECT_BANDS, 64);
  gtk_container_add (GTK_CONTAINER (vbox), drawingarea);

  gtk_container_add (GTK_CONTAINER (appwindow), vbox);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (bin, GST_STATE_PLAYING);
  gtk_main ();
  gst_element_set_state (bin, GST_STATE_NULL);

  gst_object_unref (bin);

  return 0;
}
