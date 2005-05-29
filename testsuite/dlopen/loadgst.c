
#include <gst/gst.h>


void
do_test (void)
{
  GstElement *pipeline;

  gst_init (NULL, NULL);

  pipeline = gst_parse_launch ("fakesrc num-buffers=100 ! fakesink", NULL);
  g_assert (pipeline != NULL);


  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  gst_object_unref (GST_OBJECT (pipeline));
}
