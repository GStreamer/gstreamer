#include <gst/gst.h>

static void
eos_signal_element (GstElement *element)
{
  g_print ("element eos received from \"%s\"\n", gst_element_get_name (element));
}

static void
eos_signal (GstElement *element)
{
  g_print ("eos received from \"%s\"\n", gst_element_get_name (element));

  gst_element_set_state (GST_ELEMENT (element), GST_STATE_NULL);

  g_print ("quiting main loop\n");
  //gdk_threads_enter ();
  //g_main_quit();
  //gdk_threads_leave ();
  g_print ("quited main loop\n");
}

int
main(int argc,char *argv[])
{
  GstBin *bin, *thread;
  GstElement *src,*identity,*sink;

  gst_init (&argc, &argv);

  thread = GST_BIN (gst_thread_new ("thread"));
  g_return_val_if_fail (thread != NULL, 1);

  bin = GST_BIN (gst_bin_new ("bin"));
  g_return_val_if_fail(bin != NULL, 1);

  src = gst_elementfactory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "num_buffers", 1, NULL);
  g_return_val_if_fail (src != NULL, 2);

  identity = gst_elementfactory_make ("identity", "identity");
  g_return_val_if_fail (identity != NULL, 3);
  sink = gst_elementfactory_make ("fakesink", "sink");
  g_return_val_if_fail (sink != NULL, 4);

  gst_bin_add(bin, GST_ELEMENT (src));
  gst_bin_add(bin, GST_ELEMENT (identity));
  gst_bin_add(bin, GST_ELEMENT (sink));

  gst_bin_add(thread, GST_ELEMENT (bin));

  gst_element_connect(src, "src", identity, "sink");
  gst_element_connect(identity, "src", sink, "sink");

  g_signal_connect (G_OBJECT (src), "eos",
		    G_CALLBACK (eos_signal_element), NULL);
  g_signal_connect (G_OBJECT (bin), "eos",
		    G_CALLBACK (eos_signal_element), NULL);
  g_signal_connect (G_OBJECT (thread), "eos",
		    G_CALLBACK (eos_signal), NULL);

  gst_element_set_state (GST_ELEMENT (thread), GST_STATE_PLAYING);

  //gdk_threads_enter ();
  //g_main();
  //gdk_threads_leave ();

  g_print ("quiting\n");

  exit (0);
}
