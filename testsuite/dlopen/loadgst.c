
#include <gst/gst.h>


void
do_test (void)
{
  GstElement *pipeline;
  int i;
  gboolean ret;

  gst_init (NULL, NULL);

  pipeline = gst_parse_launch ("fakesrc ! fakesink", NULL);
  g_assert (pipeline != NULL);


  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  for (i = 0; i < 100; i++) {
    ret = gst_bin_iterate (GST_BIN (pipeline));
    g_assert (ret);
    g_print ("%s", (i & 1) ? "+" : "-");
  }
  g_print ("\n");

  gst_object_unref (GST_OBJECT (pipeline));
}
