#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

static GstElement *playbin = NULL;
static GstElement *pipeline;
static guint64 duration;
static GtkAdjustment *adjustment;
static GtkWidget *hscale;
static gboolean verbose = FALSE;

static guint update_id;

#define UPDATE_INTERVAL 500

static GstElement *
make_playerbin_pipeline (const gchar * location)
{
  playbin = gst_element_factory_make ("playbin", "player");
  g_assert (playbin);

  g_object_set (G_OBJECT (playbin), "uri", location, NULL);

  return playbin;
}

static gchar *
format_value (GtkScale * scale, gdouble value)
{
  gint64 real;
  gint64 seconds;
  gint64 subseconds;

  real = value * duration / 100;
  seconds = (gint64) real / GST_SECOND;
  subseconds = (gint64) real / (GST_SECOND / 100);

  return g_strdup_printf ("%02" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT ":%02"
      G_GINT64_FORMAT, seconds / 60, seconds % 60, subseconds % 100);
}

static gboolean
update_scale (gpointer data)
{
  GstClock *clock;
  guint64 position;
  GstFormat format = GST_FORMAT_TIME;
  gboolean res;

  duration = 0;
  clock = gst_bin_get_clock (GST_BIN (pipeline));

  res = gst_element_query (playbin, GST_QUERY_TOTAL, &format, &duration);
  if (!res)
    duration = 0;
  res = gst_element_query (playbin, GST_QUERY_POSITION, &format, &position);
  if (!res)
    position = 0;

  if (position >= duration)
    duration = position;

  if (duration > 0) {
    gtk_adjustment_set_value (adjustment, position * 100.0 / duration);
    gtk_widget_queue_draw (hscale);
  }

  return TRUE;
}

static gboolean
iterate (gpointer data)
{
  gboolean res;

  if (!GST_FLAG_IS_SET (GST_OBJECT (data), GST_BIN_SELF_SCHEDULABLE)) {
    res = gst_bin_iterate (GST_BIN (data));
  } else {
    g_usleep (UPDATE_INTERVAL);
    res = gst_element_get_state (GST_ELEMENT (data)) == GST_STATE_PLAYING;
  }

  if (!res) {
    gtk_timeout_remove (update_id);
    g_print ("stopping iterations\n");
  }
  return res;
}

static gboolean
start_seek (GtkWidget * widget, GdkEventButton * event, gpointer user_data)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gtk_timeout_remove (update_id);

  return FALSE;
}

static gboolean
stop_seek (GtkWidget * widget, GdkEventButton * event, gpointer user_data)
{
  gint64 real = gtk_range_get_value (GTK_RANGE (widget)) * duration / 100;
  gboolean res;
  GstEvent *s_event;

  g_print ("seek to %" G_GINT64_FORMAT " on element %s\n", real,
      gst_element_get_name (playbin));
  s_event =
      gst_event_new_seek (GST_FORMAT_TIME | GST_SEEK_METHOD_SET |
      GST_SEEK_FLAG_FLUSH, real);

  res = gst_element_send_event (playbin, s_event);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_idle_add ((GtkFunction) iterate, pipeline);
  update_id =
      gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);

  return FALSE;
}

static void
print_media_info (GstElement * playbin)
{
  GList *streaminfo;
  GList *s;

  g_print ("have media info now\n");

  /* get info about the stream */
  g_object_get (G_OBJECT (playbin), "stream-info", &streaminfo, NULL);

  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    gint type;
    gboolean mute;

    g_object_get (obj, "type", &type, NULL);
    g_object_get (obj, "mute", &mute, NULL);

    g_print ("%d %d\n", type, mute);
  }
}

static void
play_cb (GtkButton * button, gpointer data)
{
  if (gst_element_get_state (pipeline) != GST_STATE_PLAYING) {
    GstElementStateReturn res;

    res = gst_element_set_state (pipeline, GST_STATE_PAUSED);
    if (res == GST_STATE_SUCCESS) {
      print_media_info (playbin);

      res = gst_element_set_state (pipeline, GST_STATE_PLAYING);
      gtk_idle_add ((GtkFunction) iterate, pipeline);
      update_id =
          gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale,
          pipeline);
    } else {
      g_print ("failed playing\n");
    }
  }
}

static void
pause_cb (GtkButton * button, gpointer data)
{
  if (gst_element_get_state (pipeline) != GST_STATE_PAUSED) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gtk_timeout_remove (update_id);
  }
}

static void
stop_cb (GtkButton * button, gpointer data)
{
  if (gst_element_get_state (pipeline) != GST_STATE_READY) {
    gst_element_set_state (pipeline, GST_STATE_READY);
    gtk_adjustment_set_value (adjustment, 0.0);
    gtk_timeout_remove (update_id);
  }
}

static void
print_usage (int argc, char **argv)
{
  g_print ("usage: %s <uri>\n", argv[0]);
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *hbox, *vbox, *play_button, *pause_button, *stop_button;
  struct poptOption options[] = {
    {"verbose", 'v', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &verbose, 0,
        "Verbose properties", NULL},
    POPT_TABLEEND
  };

  gst_init_with_popt_table (&argc, &argv, options);
  gtk_init (&argc, &argv);

  if (argc != 2) {
    print_usage (argc, argv);
    exit (-1);
  }

  pipeline = make_playerbin_pipeline (argv[1]);
  g_assert (pipeline);

  /* initialize gui elements ... */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new (FALSE, 0);
  vbox = gtk_vbox_new (FALSE, 0);
  play_button = gtk_button_new_with_label ("play");
  pause_button = gtk_button_new_with_label ("pause");
  stop_button = gtk_button_new_with_label ("stop");

  adjustment =
      GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.00, 100.0, 0.1, 1.0, 1.0));
  hscale = gtk_hscale_new (adjustment);
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);

  gtk_signal_connect (GTK_OBJECT (hscale),
      "button_press_event", G_CALLBACK (start_seek), pipeline);
  gtk_signal_connect (GTK_OBJECT (hscale),
      "button_release_event", G_CALLBACK (stop_seek), pipeline);
  gtk_signal_connect (GTK_OBJECT (hscale),
      "format_value", G_CALLBACK (format_value), pipeline);

  /* do the packing stuff ... */
  gtk_window_set_default_size (GTK_WINDOW (window), 96, 96);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_container_add (GTK_CONTAINER (vbox), hbox);
  gtk_box_pack_start (GTK_BOX (hbox), play_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), pause_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (hbox), stop_button, FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (vbox), hscale, TRUE, TRUE, 2);

  /* connect things ... */
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb),
      pipeline);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb),
      pipeline);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb),
      pipeline);
  g_signal_connect (G_OBJECT (window), "delete_event", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show_all (window);

  if (verbose) {
    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_element_default_deep_notify), NULL);
  }
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error),
      NULL);

  gtk_main ();

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
