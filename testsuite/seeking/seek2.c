#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>

static GstElement *src, *decoder, *osssink, *pipeline;
static guint64 duration, position;
static GtkAdjustment *adjustment;

static guint update_id;
static gboolean block;

static gchar*
format_value (GtkScale *scale,
	      gdouble   value)
{
  gint real = value * duration / 100;
  gint seconds = (gint) real/1000000;
  gint subseconds = (gint) real/10000;

  return g_strdup_printf ("%02d:%02d:%02d",
                          seconds/60, 
			  seconds%60, 
			  subseconds%100);
}
static gboolean
update_scale (gpointer data) 
{
  GstEvent *event;
  gboolean res;

  event = gst_event_new_duration (GST_SEEK_FORMAT_TIME);
  res = gst_pad_send_event (gst_element_get_pad (decoder, "src"), event);
  if (res) {
    duration = GST_EVENT_DURATION_VALUE (event);
  }
  gst_event_free (event);

  event = gst_event_new_position (GST_SEEK_FORMAT_TIME);
  res = gst_pad_send_event (gst_element_get_pad (decoder, "src"), event);
  if (res) {
    position = GST_EVENT_POSITION_VALUE (event);
  }
  gst_event_free (event);

  if (duration > 0) {
    block = TRUE;
    gtk_adjustment_set_value (adjustment, position * 100.0 / duration);
    block = FALSE;
  }

  return TRUE;
}

static gboolean
iterate (gpointer data)
{
  gboolean res;

  res = gst_bin_iterate (GST_BIN (data));
  if (!res) {
    gtk_timeout_remove (update_id);
  }
  return res;
}

static gboolean
start_seek (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gtk_timeout_remove (update_id);

  return FALSE;
}

static gboolean
stop_seek (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  gint real = gtk_range_get_value (GTK_RANGE (widget)) * duration / 100;
  gboolean res;

  res = gst_pad_send_event (gst_element_get_pad (decoder, "src"), 
		  gst_event_new_seek (GST_SEEK_FORMAT_TIME |
			  	      GST_SEEK_METHOD_SET |
				      GST_SEEK_FLAG_FLUSH, real));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_idle_add ((GtkFunction) iterate, pipeline);
  update_id = gtk_timeout_add (100, (GtkFunction) update_scale, pipeline);

  return FALSE;
}

static void
play_cb (GtkButton * button, gpointer data)
{
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_idle_add ((GtkFunction) iterate, pipeline);
  update_id = gtk_timeout_add (100, (GtkFunction) update_scale, pipeline);
}

static void
pause_cb (GtkButton * button, gpointer data)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gtk_timeout_remove (update_id);
}

static void
stop_cb (GtkButton * button, gpointer data)
{
  gst_element_set_state (pipeline, GST_STATE_READY);
  gtk_timeout_remove (update_id);
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
  decoder = gst_element_factory_make ("vorbisdec", "decoder");
  osssink = gst_element_factory_make ("osssink", "osssink");

  g_object_set (G_OBJECT (src), "location", argv[1], NULL);
  g_object_set (G_OBJECT (osssink), "fragment", 0x00180008, NULL);
  g_object_get (G_OBJECT (osssink), "channels", &channels, NULL);

  pipeline = gst_pipeline_new ("app");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), osssink);

  gst_element_connect (src, decoder);
  gst_element_connect (decoder, osssink);


  /* initialize gui elements ... */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new (FALSE, 0);
  play_button = gtk_button_new_with_label ("play");
  pause_button = gtk_button_new_with_label ("pause");
  stop_button = gtk_button_new_with_label ("stop");
  quit_button = gtk_button_new_with_label ("quit");

  adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.00, 100.0, 0.1, 1.0, 1.0));
  hscale = gtk_hscale_new (adjustment);
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);

  gtk_signal_connect(GTK_OBJECT(hscale),
                             "button_press_event", G_CALLBACK (start_seek), osssink);
  gtk_signal_connect(GTK_OBJECT(hscale),
                             "button_release_event", G_CALLBACK (stop_seek), osssink);
  gtk_signal_connect(GTK_OBJECT(hscale),
                             "format_value", G_CALLBACK (format_value), osssink);

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
