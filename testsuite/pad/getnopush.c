/*
 * this tests that get-based pads don't push.
 */

#include <gst/gst.h>

typedef struct _GstTestSrc
{
  GstElement parent;
  GstPad *srcpad;
} GstTestSrc;

typedef GstElementClass GstTestSrcClass;

static void
gst_test_src_class_init (GstTestSrcClass * klass)
{
}
static void
gst_test_src_base_init (gpointer klass)
{
}

static GstData *
gst_test_src_get (GstAction * action, GstRealPad * pad)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_INTERRUPT);
  gst_event_ref (event);
  gst_pad_push (GST_PAD (pad), GST_DATA (event));

  return GST_DATA (event);
}

static void
gst_test_src_init (GstTestSrc * src)
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_src_pad_set_action_handler (src->srcpad, gst_test_src_get);
  gst_real_pad_set_initially_active (GST_REAL_PAD (src->srcpad), TRUE);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);
}

GST_BOILERPLATE (GstTestSrc, gst_test_src, GstElement, GST_TYPE_ELEMENT);

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *testsrc, *fakesink;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("p");
  testsrc = g_object_new (gst_test_src_get_type (), NULL);
  fakesink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), testsrc, fakesink, NULL);
  gst_element_link (testsrc, fakesink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_bin_iterate (GST_BIN (pipeline));

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
