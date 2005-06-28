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
gst_test_src_get (GstPad * pad)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_INTERRUPT);
  gst_event_ref (event);
  gst_pad_push (pad, GST_DATA (event));

  return GST_DATA (event);
}

static void
gst_test_src_init (GstTestSrc * src)
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (src->srcpad, gst_test_src_get);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);
}

GST_BOILERPLATE (GstTestSrc, gst_test_src, GstElement, GST_TYPE_ELEMENT);

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *testsrc, *fakesink;
  gint n;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("p");
  testsrc = g_object_new (gst_test_src_get_type (), NULL);
  gst_object_set_name (GST_OBJECT (testsrc), "src");
  fakesink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), testsrc, fakesink, NULL);
  gst_element_link (testsrc, fakesink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  for (n = 0; n < 100; n++) {
    if (!gst_bin_iterate (GST_BIN (pipeline)))
      break;
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
