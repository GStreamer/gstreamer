#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

static GstElement *src, *mad, *osssink, *pipeline;

static void 
set_seek (GtkAdjustment *adj, gpointer data)
{
  g_print ("%f\n", adj->value);

  gst_pad_send_event (gst_element_get_pad (mad, "src"), 
		  gst_event_new_seek (GST_SEEK_BYTEOFFSET_SET, adj->value*1000, TRUE));

}

static void
play_cb (GtkButton * button, gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_idle_add ((GtkFunction) gst_bin_iterate, pipeline);
}

static void
pause_cb (GtkButton * button, gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
}

static void
stop_cb (GtkButton * button, gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);

  gst_element_set_state (pipeline, GST_STATE_READY);
}

int
main (int argc, char **argv)
{
  guint channels;
  GtkWidget *window, *hbox, *play_button, *pause_button, *stop_button, *quit_button, *hscale;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <mp3-filename>\n", argv[0]);
    exit (-1);
  }

  src = gst_element_factory_make ("filesrc", "filesrc");
  mad = gst_element_factory_make ("mad", "mad");
  osssink = gst_element_factory_make ("osssink", "osssink");

  g_object_set (G_OBJECT (src), "location", argv[1], NULL);
  g_object_set (G_OBJECT (osssink), "fragment", 0x00180008, NULL);
  g_object_get (G_OBJECT (osssink), "channels", &channels, NULL);

  pipeline = gst_pipeline_new ("app");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), mad);
  gst_bin_add (GST_BIN (pipeline), osssink);

  gst_element_connect (src, mad);
  gst_element_connect (mad, osssink);


  /* initialize gui elements ... */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new (FALSE, 0);
  play_button = gtk_button_new_with_label ("play");
  pause_button = gtk_button_new_with_label ("pause");
  stop_button = gtk_button_new_with_label ("stop");
  quit_button = gtk_button_new_with_label ("quit");

  hscale = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 0.01, 100.0, 0.1, 0.0, 0.0)));
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);

  gtk_signal_connect(GTK_OBJECT(gtk_range_get_adjustment(GTK_RANGE(hscale))),
                             "value_changed", G_CALLBACK (set_seek), osssink);

  /* do the packing stuff ... */
  gtk_window_set_default_size (GTK_WINDOW (window), 96, 96);
  gtk_container_add (GTK_CONTAINER (window), hbox);
  gtk_box_pack_start (GTK_BOX (hbox), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), quit_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), hscale, TRUE, TRUE, 2);

  /* connect things ... */
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), pipeline);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), pipeline);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), pipeline);
  g_signal_connect (G_OBJECT (quit_button), "clicked", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show (play_button);
  gtk_widget_show (pause_button);
  gtk_widget_show (stop_button);
  gtk_widget_show (quit_button);
  gtk_widget_show (hscale);
  gtk_widget_show (hbox);
  gtk_widget_show (window);


  gtk_main ();

  return 0;
}
