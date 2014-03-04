#include <gst/gst.h>

#define NUM_STREAM 13

typedef struct _App App;

struct _App
{
  GstElement *pipeline;
  GstElement *audiotestsrc[NUM_STREAM];
  GstElement *audioconvert[NUM_STREAM];
  GstElement *capsfilter[NUM_STREAM];
  GstElement *vorbisenc[NUM_STREAM];
  GstElement *oggmux[NUM_STREAM];
  GstElement *funnel;
  GstElement *demux;
  GstElement *stream_synchronizer;
  GstElement *queue[NUM_STREAM];
  GstElement *filesink[NUM_STREAM];

  gboolean pad_blocked[NUM_STREAM];
  GstPad *queue_srcpad[NUM_STREAM];
  gulong blocked_id[NUM_STREAM];
};

App s_app;

gint pad_added_cnt = 0;

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:{
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ERROR:{
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static void
set_blocked (App * app, gboolean blocked)
{
  gint i = 0;

  for (i = 0; i < NUM_STREAM; i++) {
    gst_pad_remove_probe (app->queue_srcpad[i], app->blocked_id[i]);
  }
}

static void
sink_do_reconfigure (App * app)
{
  gint i = 0;
  GstPad *filesink_sinkpad[NUM_STREAM];
  GstPad *sync_sinkpad[NUM_STREAM];
  GstPad *sync_srcpad[NUM_STREAM];
  GstIterator *it;
  GValue item = G_VALUE_INIT;

  for (i = 0; i < NUM_STREAM; i++) {
    sync_sinkpad[i] =
        gst_element_get_request_pad (app->stream_synchronizer, "sink_%u");
    it = gst_pad_iterate_internal_links (sync_sinkpad[i]);
    g_assert (it);
    gst_iterator_next (it, &item);
    sync_srcpad[i] = g_value_dup_object (&item);
    g_value_unset (&item);

    filesink_sinkpad[i] = gst_element_get_static_pad (app->filesink[i], "sink");

    gst_pad_link_full (app->queue_srcpad[i], sync_sinkpad[i],
        GST_PAD_LINK_CHECK_NOTHING);
    gst_pad_link_full (sync_srcpad[i], filesink_sinkpad[i],
        GST_PAD_LINK_CHECK_NOTHING);
  }
  gst_iterator_free (it);

}

static GstPadProbeReturn
blocked_cb (GstPad * blockedpad, GstPadProbeInfo * info, gpointer user_data)
{
  App *app = user_data;
  gint i = 0;
  gboolean all_pads_blocked = TRUE;

  for (i = 0; i < NUM_STREAM; i++) {
    if (blockedpad == app->queue_srcpad[i])
      app->pad_blocked[i] = TRUE;
  }

  for (i = 0; i < NUM_STREAM; i++) {
    if (app->queue_srcpad[i] == FALSE) {
      all_pads_blocked = FALSE;
      break;
    }
  }

  if (all_pads_blocked == TRUE) {
    sink_do_reconfigure (app);
    set_blocked (app, FALSE);
  }

  return GST_PAD_PROBE_OK;
}

static void
src_pad_added_cb (GstElement * demux, GstPad * pad, App * app)
{
  GstPad *queue_sinkpad[NUM_STREAM];

  queue_sinkpad[pad_added_cnt] =
      gst_element_get_static_pad (app->queue[pad_added_cnt], "sink");
  gst_pad_link_full (pad, queue_sinkpad[pad_added_cnt],
      GST_PAD_LINK_CHECK_NOTHING);

  app->queue_srcpad[pad_added_cnt] =
      gst_element_get_static_pad (app->queue[pad_added_cnt], "src");
  app->blocked_id[pad_added_cnt] =
      gst_pad_add_probe (app->queue_srcpad[pad_added_cnt],
      GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, blocked_cb, app, NULL);

  pad_added_cnt++;
}

gint
main (gint argc, gchar * argv[])
{
  App *app = &s_app;

  GMainLoop *loop = NULL;
  GstBus *bus;
  guint bus_watch_id;

  GstPad *funnel_sinkpad[NUM_STREAM];
  GstPad *funnel_srcpad;
  GstPad *demux_sinkpad;
  GstPad *oggmux_srcpad[NUM_STREAM];

  guint stream_cnt = 0;
  GstCaps *caps;

  gst_init (&argc, &argv);

  app->pipeline = gst_pipeline_new ("pipeline");

  for (stream_cnt = 0; stream_cnt < NUM_STREAM; stream_cnt++) {
    app->audiotestsrc[stream_cnt] =
        gst_element_factory_make ("audiotestsrc", NULL);
    app->audioconvert[stream_cnt] =
        gst_element_factory_make ("audioconvert", NULL);
    app->capsfilter[stream_cnt] = gst_element_factory_make ("capsfilter", NULL);
    app->vorbisenc[stream_cnt] = gst_element_factory_make ("vorbisenc", NULL);
    app->oggmux[stream_cnt] = gst_element_factory_make ("oggmux", NULL);
  }

  app->funnel = gst_element_factory_make ("funnel", NULL);
  app->demux = gst_element_factory_make ("streamiddemux", NULL);
  app->stream_synchronizer =
      gst_element_factory_make ("streamsynchronizer", NULL);

  caps = gst_caps_from_string ("audio/x-raw,channels=1;");

  stream_cnt = 0;

  for (stream_cnt = 0; stream_cnt < NUM_STREAM; stream_cnt++) {
    app->queue[stream_cnt] = gst_element_factory_make ("queue", NULL);
    app->filesink[stream_cnt] = gst_element_factory_make ("filesink", NULL);

    g_object_set (app->audiotestsrc[stream_cnt], "wave", stream_cnt,
        "num-buffers", 2000, NULL);
    g_object_set (app->capsfilter[stream_cnt], "caps", caps, NULL);
    g_object_set (app->filesink[stream_cnt], "location",
        g_strdup_printf ("filesink_%d.ogg", stream_cnt), NULL);
  }

  stream_cnt = 0;

  g_signal_connect (app->demux, "pad-added", G_CALLBACK (src_pad_added_cb),
      app);

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_element_get_bus (app->pipeline);
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  g_object_unref (bus);

  for (stream_cnt = 0; stream_cnt < NUM_STREAM; stream_cnt++) {
    gst_bin_add_many (GST_BIN (app->pipeline), app->audiotestsrc[stream_cnt],
        app->audioconvert[stream_cnt], app->capsfilter[stream_cnt],
        app->vorbisenc[stream_cnt], app->oggmux[stream_cnt],
        app->queue[stream_cnt], app->filesink[stream_cnt], NULL);
    if (stream_cnt == 0) {
      gst_bin_add_many (GST_BIN (app->pipeline), app->funnel, app->demux,
          app->stream_synchronizer, NULL);
    }
  }

  stream_cnt = 0;

  for (stream_cnt = 0; stream_cnt < NUM_STREAM; stream_cnt++) {
    gst_element_link_many (app->audiotestsrc[stream_cnt],
        app->audioconvert[stream_cnt], app->capsfilter[stream_cnt],
        app->vorbisenc[stream_cnt], app->oggmux[stream_cnt], NULL);
  }

  stream_cnt = 0;

  for (stream_cnt = 0; stream_cnt < NUM_STREAM; stream_cnt++) {
    funnel_sinkpad[stream_cnt] =
        gst_element_get_request_pad (app->funnel, "sink_%u");
    oggmux_srcpad[stream_cnt] =
        gst_element_get_static_pad (app->oggmux[stream_cnt], "src");
    gst_pad_link (oggmux_srcpad[stream_cnt], funnel_sinkpad[stream_cnt]);
  }

  funnel_srcpad = gst_element_get_static_pad (app->funnel, "src");

  demux_sinkpad = gst_element_get_static_pad (app->demux, "sink");
  gst_pad_link (funnel_srcpad, demux_sinkpad);

  gst_element_set_state (app->pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  gst_element_set_state (app->pipeline, GST_STATE_NULL);
  g_object_unref (app->pipeline);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}
