#include <gst/gst.h>

gboolean playing = TRUE;

static void
eos_signal_element (GstElement *element)
{
  g_print ("element eos received from \"%s\"\n", gst_element_get_name (element));
}

static void
eos_signal (GstElement *element)
{
  g_print ("eos received from \"%s\"\n", gst_element_get_name (element));

  playing = FALSE;
}

int
main(int argc,char *argv[])
{
  GstBin *pipeline, *bin;
  GstElement *src,*identity,*sink;
  GstElement *src2,*identity2,*sink2;

  gst_init(&argc,&argv);

  pipeline = GST_BIN(gst_pipeline_new("pipeline"));
  g_return_val_if_fail(pipeline != NULL, 1);

  src = gst_elementfactory_make("fakesrc","src");
  g_object_set (G_OBJECT (src), "num_buffers", 1, NULL);
  g_return_val_if_fail(src != NULL, 2);

  identity = gst_elementfactory_make("identity","identity");
  g_return_val_if_fail(identity != NULL, 3);

  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(sink != NULL, 4);

  bin = GST_BIN(gst_bin_new("bin"));
  g_return_val_if_fail(bin != NULL, 1);

  gst_bin_add(bin,GST_ELEMENT(src));
  gst_bin_add(bin,GST_ELEMENT(identity));
  gst_bin_add(bin,GST_ELEMENT(sink));

  gst_bin_add(pipeline,GST_ELEMENT(bin));

  gst_element_connect(src,"src",identity,"sink");
  gst_element_connect(identity,"src",sink,"sink");

  src2 = gst_elementfactory_make("fakesrc","src2");
  g_object_set (G_OBJECT (src2), "num_buffers", 3, NULL);
  g_return_val_if_fail(src2 != NULL, 2);

  identity2 = gst_elementfactory_make("identity","identity2");
  g_return_val_if_fail(identity2 != NULL, 3);

  sink2 = gst_elementfactory_make("fakesink","sink2");
  g_return_val_if_fail(sink2 != NULL, 4);

  gst_bin_add(pipeline,GST_ELEMENT(src2));
  gst_bin_add(pipeline,GST_ELEMENT(identity2));
  gst_bin_add(pipeline,GST_ELEMENT(sink2));

  gst_element_connect(src2,"src",identity2,"sink");
  gst_element_connect(identity2,"src",sink2,"sink");

  g_signal_connectc (G_OBJECT (src), "eos", eos_signal_element, NULL, FALSE);
  g_signal_connectc (G_OBJECT (pipeline), "eos", eos_signal, NULL, FALSE);

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while (playing)
    gst_bin_iterate(pipeline);

  exit (0);
}
