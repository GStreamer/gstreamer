#include <glib.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <string.h>

static GList *seekables = NULL;
static GList *rate_pads = NULL;

static GstElement *pipeline;
static guint64 duration, position;
static GtkAdjustment *adjustment;

static guint update_id;

#define UPDATE_INTERVAL 500

#define THREAD

typedef struct
{
  const gchar 	*padname;
  GstPad 	*target;
  GstElement 	*bin;
} dyn_connect;

static void
dynamic_connect (GstPadTemplate *templ, GstPad *newpad, gpointer data)
{
  dyn_connect *connect = (dyn_connect *) data;

  if (!strcmp (gst_pad_get_name (newpad), connect->padname)) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_bin_add (GST_BIN (pipeline), connect->bin);
    gst_pad_connect (newpad, connect->target);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    seekables = g_list_prepend (seekables, newpad);
    rate_pads = g_list_prepend (rate_pads, newpad);
  }
}

static void
setup_dynamic_connection (GstElement *element, const gchar *padname, GstPad *target, GstElement *bin)
{
  dyn_connect *connect;

  connect = g_new0 (dyn_connect, 1);
  connect->padname 	= g_strdup (padname);
  connect->target 	= target;
  connect->bin 		= bin;

  g_signal_connect (G_OBJECT (element), "new_pad", G_CALLBACK (dynamic_connect), connect);
}

static GstElement*
make_parse_pipeline (const gchar *location) 
{
  GstElement *pipeline;
  GstElement *src, *parser, *fakesink;
  GstPad *seekable;
  
  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make ("filesrc", "src");
  parser = gst_element_factory_make ("mpegparse", "parse");
  fakesink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (G_OBJECT (fakesink), "sync", TRUE, NULL);

  g_object_set (G_OBJECT (src), "location", location, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), parser);
  gst_bin_add (GST_BIN (pipeline), fakesink);

  gst_element_connect (src, parser);
  gst_element_connect (parser, fakesink);

  seekable = gst_element_get_pad (parser, "src");
  seekables = g_list_prepend (seekables, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (parser, "sink"));

  return pipeline;
}

static GstElement*
make_mp3_pipeline (const gchar *location) 
{
  GstElement *pipeline;
  GstElement *src, *decoder, *osssink;
  GstPad *seekable;
  
  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make ("filesrc", "src");
  decoder = gst_element_factory_make ("mad", "dec");
  osssink = gst_element_factory_make ("osssink", "sink");

  g_object_set (G_OBJECT (src), "location", location, NULL);
  g_object_set (G_OBJECT (osssink), "fragment", 0x00180008, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), decoder);
  gst_bin_add (GST_BIN (pipeline), osssink);

  gst_element_connect (src, decoder);
  gst_element_connect (decoder, osssink);

  seekable = gst_element_get_pad (decoder, "src");
  seekables = g_list_prepend (seekables, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (decoder, "sink"));

  return pipeline;
}

static GstElement*
make_avi_pipeline (const gchar *location) 
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *audiosink, *videosink;
  GstElement *a_queue = NULL, *audio_thread = NULL, *v_queue = NULL, *video_thread = NULL;
  GstPad *seekable;
  
  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make ("avidemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_connect (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make ("mad", "a_dec");
  audio_thread = gst_thread_new ("a_decoder_thread");
  audiosink = gst_element_factory_make ("osssink", "a_sink");
  g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);
  a_queue = gst_element_factory_make ("queue", "a_queue");
  gst_element_connect (a_decoder, a_queue);
  gst_element_connect (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), audio_thread);
  gst_bin_add (GST_BIN (audio_thread), a_queue);
  gst_bin_add (GST_BIN (audio_thread), audiosink);
  gst_element_set_state (audio_bin, GST_STATE_READY);

  setup_dynamic_connection (demux, "audio_00", gst_element_get_pad (a_decoder, "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekables = g_list_prepend (seekables, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  //v_decoder = gst_element_factory_make ("identity", "v_dec");
  v_decoder = gst_element_factory_make ("windec", "v_dec");
  video_thread = gst_thread_new ("v_decoder_thread");
  videosink = gst_element_factory_make ("xvideosink", "v_sink");
  //videosink = gst_element_factory_make ("fakesink", "v_sink");
  //g_object_set (G_OBJECT (videosink), "sync", TRUE, NULL);
  v_queue = gst_element_factory_make ("queue", "v_queue");
  g_object_set (G_OBJECT (v_queue), "max_level", 10, NULL);
  gst_element_connect (v_decoder, v_queue);
  gst_element_connect (v_queue, videosink);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), video_thread);
  gst_bin_add (GST_BIN (video_thread), v_queue);
  gst_bin_add (GST_BIN (video_thread), videosink);

  gst_element_set_state (video_bin, GST_STATE_READY);

  setup_dynamic_connection (demux, "video_00", gst_element_get_pad (v_decoder, "sink"), video_bin);

  seekable = gst_element_get_pad (v_queue, "src");
  seekables = g_list_prepend (seekables, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

static GstElement*
make_mpeg_pipeline (const gchar *location) 
{
  GstElement *pipeline, *audio_bin, *video_bin;
  GstElement *src, *demux, *a_decoder, *v_decoder, *audiosink, *videosink;
  GstElement *a_queue, *audio_thread, *v_queue, *video_thread;
  GstPad *seekable;
  
  pipeline = gst_pipeline_new ("app");

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", location, NULL);

  demux = gst_element_factory_make ("mpegdemux", "demux");
  g_object_set (G_OBJECT (demux), "sync", FALSE, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);
  gst_element_connect (src, demux);

  audio_bin = gst_bin_new ("a_decoder_bin");
  a_decoder = gst_element_factory_make ("mad", "a_dec");
  audio_thread = gst_thread_new ("a_decoder_thread");
  a_queue = gst_element_factory_make ("queue", "a_queue");
  audiosink = gst_element_factory_make ("osssink", "a_sink");
  g_object_set (G_OBJECT (audiosink), "fragment", 0x00180008, NULL);
  gst_element_connect (a_decoder, a_queue);
  gst_element_connect (a_queue, audiosink);
  gst_bin_add (GST_BIN (audio_bin), a_decoder);
  gst_bin_add (GST_BIN (audio_bin), audio_thread);
  gst_bin_add (GST_BIN (audio_thread), a_queue);
  gst_bin_add (GST_BIN (audio_thread), audiosink);

  setup_dynamic_connection (demux, "audio_00", gst_element_get_pad (a_decoder, "sink"), audio_bin);

  seekable = gst_element_get_pad (a_queue, "src");
  seekables = g_list_prepend (seekables, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (a_decoder, "sink"));

  video_bin = gst_bin_new ("v_decoder_bin");
  v_decoder = gst_element_factory_make ("mpeg2dec", "v_dec");
  video_thread = gst_thread_new ("v_decoder_thread");
  v_queue = gst_element_factory_make ("queue", "v_queue");
  videosink = gst_element_factory_make ("xvideosink", "v_sink");
  gst_element_connect (v_decoder, v_queue);
  gst_element_connect (v_queue, videosink);
  gst_bin_add (GST_BIN (video_bin), v_decoder);
  gst_bin_add (GST_BIN (video_bin), video_thread);
  gst_bin_add (GST_BIN (video_thread), v_queue);
  gst_bin_add (GST_BIN (video_thread), videosink);

  setup_dynamic_connection (demux, "video_00", gst_element_get_pad (v_decoder, "sink"), video_bin);

  seekable = gst_element_get_pad (v_queue, "src");
  seekables = g_list_prepend (seekables, seekable);
  rate_pads = g_list_prepend (rate_pads, seekable);
  rate_pads = g_list_prepend (rate_pads, gst_element_get_pad (v_decoder, "sink"));

  return pipeline;
}

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

typedef struct
{
  const gchar *name;
  const GstFormat format;
} seek_format;

static seek_format seek_formats[] = 
{
  { "tim",  GST_FORMAT_TIME    },
  { "byt",  GST_FORMAT_BYTES   },
  { "smp",  GST_FORMAT_SAMPLES },
  { "frm",  GST_FORMAT_FRAMES  },
  { "fld",  GST_FORMAT_FIELDS  },
  { "buf",  GST_FORMAT_BUFFERS },
  { "def",  GST_FORMAT_DEFAULT },
  { NULL, 0 }, 
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

      if (gst_pad_convert (pad, GST_FORMAT_TIME, GST_SECOND, 
		           &format, &value)) 
      {
        g_print ("%s %10lld | ", seek_formats[i].name, value);
      }
      else {
        g_print ("%s %10.10s | ", seek_formats[i].name, "*NA*");
      }

      i++;
    }
    g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));

    walk = g_list_next (walk);
  }
}

G_GNUC_UNUSED static void
query_durations (GstPad *pad)
{
  gint i = 0;

  g_print ("durations %8.8s: ", GST_PAD_NAME (pad));
  while (seek_formats[i].name) {
    gboolean res;
    gint64 value;
    GstFormat format;

    format = seek_formats[i].format;
    res = gst_pad_query (pad, GST_PAD_QUERY_TOTAL, &format, &value);
    if (res) {
      g_print ("%s %10lld | ", seek_formats[i].name, value);
      if (seek_formats[i].format == GST_FORMAT_TIME)
	duration = value;
    }
    else {
      g_print ("%s %10.10s | ", seek_formats[i].name, "*NA*");
    }
    i++;
  }
  g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));
}

G_GNUC_UNUSED static void
query_positions (GstPad *pad)
{
  gint i = 0;

  g_print ("positions %8.8s: ", GST_PAD_NAME (pad));
  while (seek_formats[i].name) {
    gboolean res;
    gint64 value;
    GstFormat format;

    format = seek_formats[i].format;
    res = gst_pad_query (pad, GST_PAD_QUERY_POSITION, &format, &value);
    if (res) {
      g_print ("%s %10lld | ", seek_formats[i].name, value);
      if (seek_formats[i].format == GST_FORMAT_TIME)
	position = value;
    }
    else {
      g_print ("%s %10.10s | ", seek_formats[i].name, "*NA*");
    }
    i++;
  }
  g_print (" %s:%s\n", GST_DEBUG_PAD_NAME (pad));
}

static gboolean
update_scale (gpointer data) 
{
  GList *walk = seekables;
  GstClock *clock;

  clock = gst_bin_get_clock (GST_BIN (pipeline));

  g_print ("clock:                  %10llu\n", gst_clock_get_time (clock));

  while (walk) {
    GstPad *pad = GST_PAD (walk->data);

    query_durations (pad);
    query_positions (pad);

    walk = g_list_next (walk);
  }
  query_rates ();

  if (duration > 0) {
    gtk_adjustment_set_value (adjustment, position * 100.0 / duration);
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
    g_print ("stopping iterations\n");
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
  gint64 real = gtk_range_get_value (GTK_RANGE (widget)) * duration / 100;
  gboolean res;
  GstEvent *s_event;
  GList *walk = seekables;

  while (walk) {
    GstPad *seekable = GST_PAD (walk->data);

    g_print ("seek to %lld on pad %s:%s\n", real, GST_DEBUG_PAD_NAME (seekable));
    s_event = gst_event_new_seek (GST_FORMAT_TIME |
			   	  GST_SEEK_METHOD_SET |
				  GST_SEEK_FLAG_FLUSH, real);

    res = gst_pad_send_event (seekable, s_event);
    gst_event_free (s_event);

    walk = g_list_next (walk);
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_idle_add ((GtkFunction) iterate, pipeline);
  update_id = gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);

  return FALSE;
}

static void
play_cb (GtkButton * button, gpointer data)
{
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gtk_idle_add ((GtkFunction) iterate, pipeline);
  update_id = gtk_timeout_add (UPDATE_INTERVAL, (GtkFunction) update_scale, pipeline);
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
  GtkWidget *window, *hbox, *vbox, 
            *play_button, *pause_button, *stop_button, 
	    *hscale;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  if (argc != 3) {
    g_print ("usage: %s <type 0=mp3 1=avi 2=mpeg1 3=mpegparse> <filename>\n", argv[0]);
    exit (-1);
  }

  if (atoi (argv[1]) == 0) 
    pipeline = make_mp3_pipeline (argv[2]);
  else if (atoi (argv[1]) == 1) 
    pipeline = make_avi_pipeline (argv[2]);
  else if (atoi (argv[1]) == 2) 
    pipeline = make_mpeg_pipeline (argv[2]);
  else 
    pipeline = make_parse_pipeline (argv[2]);

  /* initialize gui elements ... */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  hbox = gtk_hbox_new (FALSE, 0);
  vbox = gtk_vbox_new (FALSE, 0);
  play_button = gtk_button_new_with_label ("play");
  pause_button = gtk_button_new_with_label ("pause");
  stop_button = gtk_button_new_with_label ("stop");

  adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.00, 100.0, 0.1, 1.0, 1.0));
  hscale = gtk_hscale_new (adjustment);
  gtk_scale_set_digits (GTK_SCALE (hscale), 2);
  gtk_range_set_update_policy (GTK_RANGE (hscale), GTK_UPDATE_CONTINUOUS);

  gtk_signal_connect(GTK_OBJECT(hscale),
                             "button_press_event", G_CALLBACK (start_seek), pipeline);
  gtk_signal_connect(GTK_OBJECT(hscale),
                             "button_release_event", G_CALLBACK (stop_seek), pipeline);
  gtk_signal_connect(GTK_OBJECT(hscale),
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
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), pipeline);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), pipeline);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), pipeline);
  g_signal_connect (G_OBJECT (window), "delete_event", gtk_main_quit, NULL);

  /* show the gui. */
  gtk_widget_show_all (window);

  gtk_main ();

  //g_mem_chunk_info();

  return 0;
}
