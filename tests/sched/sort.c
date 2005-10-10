#include <gst/gst.h>

static GstElement *
make_pipeline1 ()
{
  GstElement *fakesrc, *fakesink;
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  fakesink = gst_element_factory_make ("fakesink", "fake_sink");
  g_assert (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  return pipeline;
}

static GstElement *
make_pipeline2 ()
{
  GstElement *fakesrc, *tee, *fakesink1, *fakesink2;
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  tee = gst_element_factory_make ("tee", "tee");
  g_assert (tee != NULL);

  fakesink1 = gst_element_factory_make ("fakesink", "fake_sink1");
  g_assert (fakesink1 != NULL);

  fakesink2 = gst_element_factory_make ("fakesink", "fake_sink2");
  g_assert (fakesink2 != NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, tee, fakesink1, fakesink2,
      NULL);
  gst_element_link (fakesrc, tee);
  gst_element_link (tee, fakesink1);
  gst_element_link (tee, fakesink2);

  return pipeline;
}

static GstElement *
make_pipeline3 ()
{
  GstElement *fakesrc, *tee, *identity, *fakesink1, *fakesink2;
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  tee = gst_element_factory_make ("tee", "tee");
  g_assert (tee != NULL);

  identity = gst_element_factory_make ("identity", "identity");
  g_assert (identity != NULL);

  fakesink1 = gst_element_factory_make ("fakesink", "fake_sink1");
  g_assert (fakesink1 != NULL);

  fakesink2 = gst_element_factory_make ("fakesink", "fake_sink2");
  g_assert (fakesink2 != NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, tee, identity,
      fakesink1, fakesink2, NULL);
  gst_element_link (fakesrc, tee);
  gst_element_link (tee, identity);
  gst_element_link (identity, fakesink1);
  gst_element_link (tee, fakesink2);

  return pipeline;
}

static GstElement *
make_pipeline4 ()
{
  GstElement *fakesrc, *tee, *identity, *fakesink1, *fakesink2;
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  fakesrc = gst_element_factory_make ("fakesrc", "fake_source");
  g_assert (fakesrc != NULL);

  tee = gst_element_factory_make ("tee", "tee");
  g_assert (tee != NULL);

  identity = gst_element_factory_make ("identity", "identity");
  g_assert (identity != NULL);

  fakesink1 = gst_element_factory_make ("fakesink", "fake_sink1");
  g_assert (fakesink1 != NULL);

  fakesink2 = gst_element_factory_make ("fakesink", "fake_sink2");
  g_assert (fakesink2 != NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, tee, identity,
      fakesink1, fakesink2, NULL);
  gst_element_link (fakesrc, tee);
  gst_element_link (identity, fakesink1);

  return pipeline;
}

static void
print_elem (GstElement * elem, gpointer unused)
{
  g_print ("----> %s\n", GST_ELEMENT_NAME (elem));
  gst_object_unref (elem);
}

int
main (int argc, gchar * argv[])
{
  GstElement *bin;
  GstIterator *it;

  gst_init (&argc, &argv);

  g_print ("pipeline 1\n");
  bin = make_pipeline1 ();
  it = gst_bin_iterate_sorted (GST_BIN (bin));
  gst_iterator_foreach (it, (GFunc) print_elem, NULL);
  gst_iterator_free (it);

  g_print ("pipeline 2\n");
  bin = make_pipeline2 ();
  it = gst_bin_iterate_sorted (GST_BIN (bin));
  gst_iterator_foreach (it, (GFunc) print_elem, NULL);
  gst_iterator_free (it);

  g_print ("pipeline 3\n");
  bin = make_pipeline3 ();
  it = gst_bin_iterate_sorted (GST_BIN (bin));
  gst_iterator_foreach (it, (GFunc) print_elem, NULL);
  gst_iterator_free (it);

  g_print ("pipeline 4\n");
  bin = make_pipeline4 ();
  it = gst_bin_iterate_sorted (GST_BIN (bin));
  gst_iterator_foreach (it, (GFunc) print_elem, NULL);
  gst_iterator_free (it);

  return 0;
}
