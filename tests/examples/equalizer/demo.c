#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gtk/gtk.h>

#define NBANDS 10

static GtkWidget *drawingarea = NULL;
static guint spect_height = 128;
static guint spect_bands = 256;
static gfloat height_scale = 2.0;

static void
on_window_destroy (GObject * object, gpointer user_data)
{
  drawingarea = NULL;
  gtk_main_quit ();
}

static gboolean
on_configure_event (GtkWidget * widget, GdkEventConfigure * event,
    gpointer user_data)
{
  GstElement *spectrum = GST_ELEMENT (user_data);

  /*GST_INFO ("%d x %d", event->width, event->height); */
  spect_height = event->height;
  height_scale = event->height / 64.0;
  spect_bands = event->width;

  g_object_set (G_OBJECT (spectrum), "bands", spect_bands, NULL);
  return FALSE;
}

/* control gains */
static void
on_gain_changed (GtkRange * range, gpointer user_data)
{
  GstObject *band = GST_OBJECT (user_data);
  gdouble value = gtk_range_get_value (range);

  g_object_set (band, "gain", value, NULL);
}

/* control bandwidths */
static void
on_bandwidth_changed (GtkRange * range, gpointer user_data)
{
  GstObject *band = GST_OBJECT (user_data);
  gdouble value = gtk_range_get_value (range);

  g_object_set (band, "bandwidth", value, NULL);
}

/* control frequency */
static void
on_freq_changed (GtkRange * range, gpointer user_data)
{
  GstObject *band = GST_OBJECT (user_data);
  gdouble value = gtk_range_get_value (range);

  /* hbox */
  GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (range));

  /* frame */
  GtkWidget *parent_parent = gtk_widget_get_parent (parent);
  gchar *label = g_strdup_printf ("%d Hz", (int) (value + 0.5));

  gtk_frame_set_label (GTK_FRAME (parent_parent), label);
  g_free (label);

  g_object_set (band, "freq", value, NULL);
}

/* draw frequency spectrum as a bunch of bars */
static void
draw_spectrum (gfloat * data)
{
  gint i;
  GdkRectangle rect = { 0, 0, spect_bands, spect_height };
  cairo_t *cr;

  if (!drawingarea)
    return;

  gdk_window_begin_paint_rect (gtk_widget_get_window (drawingarea), &rect);
  cr = gdk_cairo_create (gtk_widget_get_window (drawingarea));

  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_rectangle (cr, 0, 0, spect_bands, spect_height);
  cairo_fill (cr);
  for (i = 0; i < spect_bands; i++) {
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_rectangle (cr, i, -data[i], 1, spect_height + data[i]);
    cairo_fill (cr);
  }
  cairo_destroy (cr);

  gdk_window_end_paint (gtk_widget_get_window (drawingarea));
}

/* receive spectral data from element message */
static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "spectrum") == 0) {
      gfloat *spect = g_new (gfloat, spect_bands);
      const GValue *list;
      const GValue *value;
      guint i;

      list = gst_structure_get_value (s, "magnitude");
      for (i = 0; i < spect_bands; ++i) {
        value = gst_value_list_get_value (list, i);
        spect[i] = height_scale * g_value_get_float (value);
      }
      draw_spectrum (spect);
      g_free (spect);
    }
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src, *capsfilter, *equalizer, *spectrum, *audioconvert, *sink;
  GstCaps *caps;
  GstBus *bus;
  GtkWidget *appwindow, *vbox, *hbox, *widget;
  int i;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  bin = gst_pipeline_new ("bin");

  /* White noise */
  src = gst_element_factory_make ("audiotestsrc", "src");
  g_object_set (G_OBJECT (src), "wave", 5, "volume", 0.8, NULL);

  /* Force float32 samples */
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  caps =
      gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "F32LE",
      NULL);
  g_object_set (capsfilter, "caps", caps, NULL);

  equalizer = gst_element_factory_make ("equalizer-nbands", "equalizer");
  g_object_set (G_OBJECT (equalizer), "num-bands", NBANDS, NULL);

  spectrum = gst_element_factory_make ("spectrum", "spectrum");
  g_object_set (G_OBJECT (spectrum), "bands", spect_bands, "threshold", -80,
      "message", TRUE, "interval", 500 * GST_MSECOND, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");

  sink = gst_element_factory_make ("autoaudiosink", "sink");

  gst_bin_add_many (GST_BIN (bin), src, capsfilter, equalizer, spectrum,
      audioconvert, sink, NULL);
  if (!gst_element_link_many (src, capsfilter, equalizer, spectrum,
          audioconvert, sink, NULL)) {
    fprintf (stderr, "can't link elements\n");
    exit (1);
  }

  bus = gst_element_get_bus (bin);
  gst_bus_add_watch (bus, message_handler, NULL);
  gst_object_unref (bus);

  appwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (G_OBJECT (appwindow), "destroy",
      G_CALLBACK (on_window_destroy), NULL);
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

  drawingarea = gtk_drawing_area_new ();
  gtk_widget_set_size_request (drawingarea, spect_bands, spect_height);
  g_signal_connect (G_OBJECT (drawingarea), "configure-event",
      G_CALLBACK (on_configure_event), (gpointer) spectrum);
  gtk_box_pack_start (GTK_BOX (vbox), drawingarea, TRUE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 20);

  for (i = 0; i < NBANDS; i++) {
    GObject *band;
    gdouble freq;
    gdouble bw;
    gdouble gain;
    gchar *label;
    GtkWidget *frame, *scales_hbox;

    band = gst_child_proxy_get_child_by_index (GST_CHILD_PROXY (equalizer), i);
    g_assert (band != NULL);
    g_object_get (band, "freq", &freq, NULL);
    g_object_get (band, "bandwidth", &bw, NULL);
    g_object_get (band, "gain", &gain, NULL);

    label = g_strdup_printf ("%d Hz", (int) (freq + 0.5));
    frame = gtk_frame_new (label);
    g_free (label);

    scales_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    widget = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL,
        -24.0, 12.0, 0.5);
    gtk_scale_set_draw_value (GTK_SCALE (widget), TRUE);
    gtk_scale_set_value_pos (GTK_SCALE (widget), GTK_POS_TOP);
    gtk_range_set_value (GTK_RANGE (widget), gain);
    gtk_widget_set_size_request (widget, 25, 150);
    g_signal_connect (G_OBJECT (widget), "value-changed",
        G_CALLBACK (on_gain_changed), (gpointer) band);
    gtk_box_pack_start (GTK_BOX (scales_hbox), widget, FALSE, FALSE, 0);

    widget = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL,
        0.0, 20000.0, 5.0);
    gtk_scale_set_draw_value (GTK_SCALE (widget), TRUE);
    gtk_scale_set_value_pos (GTK_SCALE (widget), GTK_POS_TOP);
    gtk_range_set_value (GTK_RANGE (widget), bw);
    gtk_widget_set_size_request (widget, 25, 150);
    g_signal_connect (G_OBJECT (widget), "value-changed",
        G_CALLBACK (on_bandwidth_changed), (gpointer) band);
    gtk_box_pack_start (GTK_BOX (scales_hbox), widget, TRUE, TRUE, 0);

    widget = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL,
        20.0, 20000.0, 5.0);
    gtk_scale_set_draw_value (GTK_SCALE (widget), TRUE);
    gtk_scale_set_value_pos (GTK_SCALE (widget), GTK_POS_TOP);
    gtk_range_set_value (GTK_RANGE (widget), freq);
    gtk_widget_set_size_request (widget, 25, 150);
    g_signal_connect (G_OBJECT (widget), "value-changed",
        G_CALLBACK (on_freq_changed), (gpointer) band);
    gtk_box_pack_start (GTK_BOX (scales_hbox), widget, TRUE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (frame), scales_hbox);

    gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
  }

  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER (appwindow), vbox);
  gtk_widget_show_all (appwindow);

  gst_element_set_state (bin, GST_STATE_PLAYING);
  gtk_main ();
  gst_element_set_state (bin, GST_STATE_NULL);

  gst_object_unref (bin);

  return 0;
}
