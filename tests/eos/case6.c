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
  GstBin *pipeline, *thread;
  GstElement *queue,*identity,*sink;
  GstElement *src2, *identity2;

  gst_init(&argc,&argv);

  pipeline = GST_BIN(gst_pipeline_new("pipeline"));
  g_return_val_if_fail(pipeline != NULL, 1);

  src2 = gst_elementfactory_make("fakesrc","src2");
  g_object_set (G_OBJECT (src2), "num_buffers", 4, NULL);
  g_return_val_if_fail(src2 != NULL, 2);

  identity2 = gst_elementfactory_make("identity","identity2");
  g_return_val_if_fail(identity2 != NULL, 3);

  queue = gst_elementfactory_make("queue","queue");
  g_object_set (G_OBJECT (queue), "max_level", 1, NULL);
  g_return_val_if_fail(queue != NULL, 4);

  gst_element_connect(src2,"src",identity2,"sink");
  gst_element_connect(identity2,"src",queue,"sink");

  gst_bin_add(pipeline,GST_ELEMENT(src2));
  gst_bin_add(pipeline,GST_ELEMENT(identity2));
  gst_bin_add(pipeline,GST_ELEMENT(queue));

  identity = gst_elementfactory_make("identity","identity");
  //g_object_set (G_OBJECT (identity), "sleep_time", 1000000, NULL);
  g_return_val_if_fail(identity != NULL, 3);

  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(sink != NULL, 3);

  thread = GST_BIN(gst_thread_new("thread"));
  g_return_val_if_fail(thread != NULL, 1);

  gst_bin_add(thread,GST_ELEMENT(identity));
  gst_bin_add(thread,GST_ELEMENT(sink));

  gst_bin_add(pipeline,GST_ELEMENT(thread));

  gst_element_connect(queue,"src",identity,"sink");
  gst_element_connect(identity,"src",sink,"sink");

  g_signal_connectc (G_OBJECT (src2), "eos", eos_signal_element, NULL, FALSE);
  g_signal_connectc (G_OBJECT (queue), "eos", eos_signal_element, NULL, FALSE);
  g_signal_connectc (G_OBJECT (pipeline), "eos", eos_signal, NULL, FALSE);
  g_signal_connectc (G_OBJECT (thread), "eos", eos_signal_element, NULL, FALSE);

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while (gst_bin_iterate(pipeline));

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_NULL);

  exit (0);
}
