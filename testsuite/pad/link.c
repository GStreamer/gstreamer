/*
 * Test that:
 *  - get-based sources can return data, loop-based sources can push.
 *  - chain-based filters receive/push, loop-based filters can pull/push.
 *  - chain-based sinks receive, loop-based sinks pull.
 */

#include <gst/gst.h>

/*
 * Scary type code.
 */

typedef struct _GstTestElement
{
  GstElement parent;
  GstPad *srcpad, *sinkpad;
} GstTestSrc, GstTestFilter, GstTestSink, GstTestElement;

typedef GstElementClass GstTestSrcClass, GstTestFilterClass, GstTestSinkClass,
    GstTestElementClass;

#define gst_test_src_class_init gst_test_element_class_init
#define gst_test_filter_class_init gst_test_element_class_init
#define gst_test_sink_class_init gst_test_element_class_init

#define gst_test_src_base_init gst_test_element_base_init
#define gst_test_filter_base_init gst_test_element_base_init
#define gst_test_sink_base_init gst_test_element_base_init

static void
gst_test_element_class_init (GstTestElementClass * klass)
{
}
static void
gst_test_element_base_init (gpointer klass)
{
}

/*
 * Actual element code.
 */

gboolean loop = FALSE;

static GstData *
gst_test_src_get (GstPad * pad)
{
  return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
}

static void
gst_test_src_loop (GstElement * element)
{
  GstTestSrc *src = (GstTestElement *) element;

  gst_pad_push (src->srcpad, gst_test_src_get (src->srcpad));
}

static void
gst_test_src_init (GstTestElement * src)
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  if (loop) {
    gst_element_set_loop_function (GST_ELEMENT (src), gst_test_src_loop);
  } else {
    gst_pad_set_get_function (src->srcpad, gst_test_src_get);
  }
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

  GST_FLAG_SET (src, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_test_filter_chain (GstPad * pad, GstData * data)
{
  GstTestFilter *filter = (GstTestElement *) gst_pad_get_parent (pad);

  gst_pad_push (filter->srcpad, data);
}

static void
gst_test_filter_loop (GstElement * element)
{
  GstTestFilter *filter = (GstTestElement *) element;

  gst_test_filter_chain (filter->sinkpad, gst_pad_pull (filter->sinkpad));
}

static void
gst_test_filter_init (GstTestElement * filter)
{
  filter->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  if (loop) {
    gst_element_set_loop_function (GST_ELEMENT (filter), gst_test_filter_loop);
  } else {
    gst_pad_set_chain_function (filter->sinkpad, gst_test_filter_chain);
  }
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  GST_FLAG_SET (filter, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_test_sink_chain (GstPad * pad, GstData * data)
{
  gst_data_unref (data);
}

static void
gst_test_sink_loop (GstElement * element)
{
  GstTestSink *sink = (GstTestElement *) element;

  gst_test_sink_chain (sink->sinkpad, gst_pad_pull (sink->sinkpad));
}

static void
gst_test_sink_init (GstTestElement * sink)
{
  sink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  if (loop) {
    gst_element_set_loop_function (GST_ELEMENT (sink), gst_test_sink_loop);
  } else {
    gst_pad_set_chain_function (sink->sinkpad, gst_test_sink_chain);
  }
  gst_element_add_pad (GST_ELEMENT (sink), sink->sinkpad);

  GST_FLAG_SET (sink, GST_ELEMENT_EVENT_AWARE);
}

#define parent_class src_parent_class
GST_BOILERPLATE (GstTestSrc, gst_test_src, GstElement, GST_TYPE_ELEMENT);
#undef parent_class
#define parent_class filter_parent_class
GST_BOILERPLATE (GstTestFilter, gst_test_filter, GstElement, GST_TYPE_ELEMENT);
#undef parent_class
#define parent_class sink_parent_class
GST_BOILERPLATE (GstTestSink, gst_test_sink, GstElement, GST_TYPE_ELEMENT);
#undef parent_class

/*
 * Actual test.
 */

static void
cb_error (GstElement * element)
{
  g_assert_not_reached ();
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *src, *filter, *sink;
  gint n, r;
  gboolean res;

  gst_init (&argc, &argv);

  for (r = 0; r < 2; r++) {
    pipeline = gst_pipeline_new ("p");
    g_signal_connect (pipeline, "error", G_CALLBACK (cb_error), NULL);
    src = g_object_new (gst_test_src_get_type (), NULL);
    gst_object_set_name (GST_OBJECT (src), "src");
    filter = g_object_new (gst_test_filter_get_type (), NULL);
    gst_object_set_name (GST_OBJECT (filter), "filter");
    sink = g_object_new (gst_test_sink_get_type (), NULL);
    gst_object_set_name (GST_OBJECT (sink), "sink");
    gst_bin_add_many (GST_BIN (pipeline), src, filter, sink, NULL);
    res = gst_element_link (src, filter);
    g_assert (res);
    res = gst_element_link (filter, sink);
    g_assert (res);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    for (n = 0; n < 100; n++) {
      if (!gst_bin_iterate (GST_BIN (pipeline)))
        g_assert_not_reached ();
    }

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);

    /* switch element types */
    g_print ("Loop=%s done\n", loop ? "true" : "false");
    loop = !loop;
  }

  return 0;
}
