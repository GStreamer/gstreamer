#include <gst/gst.h>

static gint looping;
static GstEvent *event;
static GstPad *pad;

static void
event_received (GObject * object, GstEvent * event, GstElement * pipeline)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT_DONE) {
    g_print ("segment done\n");
    if (--looping == 1) {
      event = gst_event_new_segment_seek (GST_FORMAT_DEFAULT |
          GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, 20, 25);
    } else {
      event = gst_event_new_segment_seek (GST_FORMAT_DEFAULT |
          GST_SEEK_METHOD_SET |
          GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT_LOOP, 50, 55);
    }
    gst_pad_send_event (pad, event);
  }
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc;
  GstElement *fakesink;
  guint64 value;
  GstFormat format;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  fakesrc = gst_element_factory_make ("fakesrc", "src");

  fakesink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), fakesrc);
  gst_bin_add (GST_BIN (pipeline), fakesink);

  gst_element_link_pads (fakesrc, "src", fakesink, "sink");

  gst_element_set_state (pipeline, GST_STATE_READY);

  pad = gst_element_get_pad (fakesrc, "src");

  g_print ("doing segment seek from 5 to 10\n");

  gst_pad_send_event (pad,
      gst_event_new_segment_seek (GST_FORMAT_DEFAULT |
          GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, 5, 10));

  format = GST_FORMAT_DEFAULT;

  gst_pad_query (pad, GST_QUERY_START, &format, &value);
  g_print ("configured for start   %" G_GINT64_FORMAT "\n", value);
  gst_pad_query (pad, GST_QUERY_SEGMENT_END, &format, &value);
  g_print ("configured segment end %" G_GINT64_FORMAT "\n", value);


  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  g_print
      ("doing segment seek from 50 to 55 with looping (2 times), then 20 to 25 without looping\n");
  looping = 3;

  event = gst_event_new_segment_seek (GST_FORMAT_DEFAULT |
      GST_SEEK_METHOD_SET |
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT_LOOP, 50, 55);
  gst_pad_send_event (pad, event);

  g_signal_connect (G_OBJECT (gst_element_get_pad (fakesink, "sink")),
      "event_received", G_CALLBACK (event_received), event);

  gst_pad_query (pad, GST_QUERY_START, &format, &value);
  g_print ("configured for start   %" G_GINT64_FORMAT "\n", value);
  gst_pad_query (pad, GST_QUERY_SEGMENT_END, &format, &value);
  g_print ("configured segment end %" G_GINT64_FORMAT "\n", value);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
