#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

static GList *rate_pads = NULL;
static GList *seekable_elements = NULL;

static GstElement *pipeline;
static GtkAdjustment *adjustment;
static gboolean stats = FALSE;
static guint64 duration;

static guint update_id;

//#define SOURCE "gnomevfssrc"
#define SOURCE "filesrc"

#define UPDATE_INTERVAL 500

static GstElement *
make_spider_pipeline (const gchar * location, gboolean thread)
{
  GstElement *pipeline;
  GstElement *src, *decoder, *audiosink, *videosink, *a_thread, *v_thread,
      *a_queue, *v_queue;

  if (thread) {
    pipeline = gst_thread_new ("app");
  } else {
    pipeline = gst_pipeline_new ("app");
  }


  src = gst_element_factory_make (SOURCE, "src");
  decoder = gst_element_factory_make ("spider", "decoder");
  a_thread = gst_thread_new ("a_thread");
  a_queue = gst_element_factory_make ("queue", "a_queue");
  audiosink = gst_element_factory_make ("osssink", "a_sink");
  //g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);

  v_thread = gst_thread_new ("v_thread");
  v_queue = gst_element_factory_make ("queue", "v_queue");
  videosink = gst_element_factory_make ("xvideosink", "v_sink");
  //g_object_set (G_OBJECT (audiosink), "sync", FALSE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (a_thread), a_queue);
  gst_bin_add (GST_BIN (a_thread), audiosink);
  gst_bin_add (GST_BIN (v_thread), v_queue);
  gst_bin_add (GST_BIN (v_thread), videosink);
  gst_bin_add (GST_BIN (pipeline), a_thread);
  gst_bin_add (GST_BIN (pipeline), v_thread);

  gst_element_link (src, decoder);
  gst_element_link (v_queue, videosink);
  gst_element_link (decoder, v_queue);
  gst_element_link (a_queue, audiosink);
  gst_element_link (decoder, a_queue);

  seekable_elements = g_list_prepend (seekable_elements, videosink);
  seekable_elements = g_list_prepend (seekable_elements, audiosink);
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (audiosink, "sink"));
  rate_pads =
      g_list_prepend (rate_pads, gst_element_get_pad (videosink, "sink"));

  return pipeline;
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

  return g_strdup_printf ("%02lld:%02lld:%02lld",
      seconds / 60, seconds % 60, subseconds % 100);
}

typedef struct
{
  const gchar *name;
  const GstFormat format;
} seek_format;

static seek_format seek_formats[] = {
  {"tim", GST_FORMAT_TIME},
  {"byt", GST_FORMAT_BYTES},
  {"buf", GST_FORMAT_BUFFERS},
  {"def", GST_FORMAT_DEFAULT},
  {NULL, 0},
};

G_GNUC_UNUSED static void
query_rates (void)
{
  GList *walk = rate_pads;

  while (walk) {
    GstPad *pad = GST_PAD (walk->data);
    gint i = 0;

    g_print ("rate/sec  %8.8s: ", GST_PAD_NAME (pad));
    while (seek_formats[i].name) {
      gint64 value;
      GstFormat format;

      format = seek_formats[i].format;

      if (gst_pad_convert (pad, GST_FORMAT_TIME, GST_SECOND, &format, &value)) {
	g_print ("%s %13lld | ", seek_formats[i].name, value);
      } else {
	g_print ("%s %13.13s | ", seek_formats[i].name, "*NA*");
      }

      i++;
    }
    g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));

    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_durations ()
{
  GList *walk = seekable_elements;

  while (walk) {
    GstElement *element = GST_ELEMENT (walk->data);
    gint i = 0;

    g_print ("durations %8.8s: ", GST_ELEMENT_NAME (element));
    while (seek_formats[i].name) {
      gboolean res;
      gint64 value;
      GstFormat format;

      format = seek_formats[i].format;
      res = gst_element_query (element, GST_QUERY_TOTAL, &format, &value);
      if (res) {
	g_print ("%s %13lld | ", seek_formats[i].name, value);
      } else {
	g_print ("%s %13.13s | ", seek_formats[i].name, "*NA*");
      }
      i++;
    }
    g_print (" %s\n", GST_ELEMENT_NAME (element));
    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_positions ()
{
  GList *walk = seekable_elements;

  while (walk) {
    GstElement *element = GST_ELEMENT (walk->data);
    gint i = 0;

    g_print ("positions %8.8s: ", GST_ELEMENT_NAME (element));
    while (seek_formats[i].name) {
      gboolean res;
      gint64 value;
      GstFormat format;

      format = seek_formats[i].format;
      res = gst_element_query (element, GST_QUERY_POSITION, &format, &value);
      if (res) {
	g_print ("%s %13lld | ", seek_formats[i].name, value);
      } else {
	g_print ("%s %13.13s | ", seek_formats[i].name, "*NA*");
      }
      i++;
    }
    g_print (" %s\n", GST_ELEMENT_NAME (element));
    walk = g_list_next (walk);
  }
}

static gboolean
update_scale (gpointer data)
{
  GstClock *clock;
  guint64 position;
  GstFormat format = GST_FORMAT_TIME;

  duration = 0;
  clock = gst_bin_get_clock (GST_BIN (pipeline));

  if (seekable_elements) {
    GstElement *element = GST_ELEMENT (seekable_elements->data);

    gst_element_query (element, GST_QUERY_TOTAL, &format, &duration);
  }
  position = gst_clock_get_time (clock);

  if (stats) {
    g_print ("clock:                  %13llu  (%s)\n", position,
	gst_object_get_name (GST_OBJECT (clock)));
    query_durations ();
    query_positions ();
    query_rates ();
  }
  if (duration > 0) {
    gtk_adjustment_set_value (adjustment, position * 100.0 / duration);
  }

  return TRUE;
}

static gboolean
iterate (gpointer data)
{
  gboolean res = TRUE;

  res = gst_bin_iterate (GST_BIN (data));
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
  GList *walk = seekable_elements;

  while (walk) {
    GstElement *seekable = GST_ELEMENT (walk->data);

    g_print ("seek to %lld on element %s\n", real, GST_ELEMENT_NAME (seekable));
    s_event = gst_event_new_seek (GST_FORMAT_TIME |
	GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, real);

    res = gst_element_send_event (seekable, s_event);

    walk = g_list_next (walk);
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (!GST_FLAG_IS_SET (pipeline, GST_BIN_SELF_SCHEDULABLE))
    gtk_idle_add ((GtkFunction) iterate, pipeline);
  update_id =
      gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);

  return FALSE;
}

static void
play_cb (GtkButton * button, gpointer data)
{
  if (gst_element_get_state (pipeline) != GST_STATE_PLAYING) {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    if (!GST_FLAG_IS_SET (pipeline, GST_BIN_SELF_SCHEDULABLE))
      gtk_idle_add ((GtkFunction) iterate, pipeline);
    update_id =
	gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);
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
    gtk_timeout_remove (update_id);
  }
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *hbox, *vbox,
      *play_button, *pause_button, *stop_button, *hscale;
  gboolean threaded = FALSE;
  struct poptOption options[] = {
    {"threaded", 't', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &threaded, 0,
	"Run the pipeline in a toplevel thread", NULL},
    {"stats", 's', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &stats, 0,
	"Show element stats", NULL},
    POPT_TABLEEND
  };

  gst_init_with_popt_table (&argc, &argv, options);
  gtk_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  pipeline = make_spider_pipeline (argv[1], threaded);

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

  gtk_main ();

  return 0;
}
