/*
 * this tests that chain-based pads don't pull.
 */

#include <gst/gst.h>

typedef struct _GstTestSink
{
  GstElement parent;
  GstPad *sinkpad;
} GstTestSink;

typedef GstElementClass GstTestSinkClass;

static void
gst_test_sink_class_init (GstTestSinkClass * klass)
{
}

static void
gst_test_sink_base_init (gpointer klass)
{
}

static void
gst_test_sink_chain (GstPad * pad, GstData * data)
{
  data = gst_pad_pull (pad);
}

static void
gst_test_sink_init (GstTestSink * sink)
{
  sink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (sink->sinkpad, gst_test_sink_chain);
  gst_element_add_pad (GST_ELEMENT (sink), sink->sinkpad);
}

GST_BOILERPLATE (GstTestSink, gst_test_sink, GstElement, GST_TYPE_ELEMENT);

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *fakesrc, *testsink;
  gint n;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("p");
  fakesrc = gst_element_factory_make ("fakesrc", "src");
  testsink = g_object_new (gst_test_sink_get_type (), NULL);
  gst_object_set_name (GST_OBJECT (testsink), "sink");
  gst_bin_add_many (GST_BIN (pipeline), fakesrc, testsink, NULL);
  gst_element_link (fakesrc, testsink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  for (n = 0; n < 100; n++) {
    if (!gst_bin_iterate (GST_BIN (pipeline)))
      break;
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
