/* this tests if the GstThread is ok after removing all elements from it
 * in PAUSED rather than NULL state.  Currently it crashes with a mutex
 * error
 */

#include <gst/gst.h>

int
main (int argc, char **argv)
{
  GstElement *thread, *pipeline;
  GstElement *src, *sink, *queue;
  int i;

  gst_init (&argc, &argv);

  pipeline = gst_element_factory_make ("pipeline", "pipeline");

  src = gst_element_factory_make ("fakesrc", "src");
  g_assert (src);

  gst_bin_add (GST_BIN (pipeline), src);

  thread = gst_element_factory_make ("thread", "thread");
  g_assert (thread);
  sink = gst_element_factory_make ("fakesink", "sink");
  g_assert (sink);
  queue = gst_element_factory_make ("queue", "queue");
  g_assert (queue);

  gst_bin_add_many (GST_BIN (thread), queue, sink, NULL);

  gst_bin_add (GST_BIN (pipeline), thread);

  if (!gst_element_link_many (src, queue, sink, NULL))
    g_assert_not_reached ();


  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  for (i = 0; i < 100; i++) {
    if (!gst_bin_iterate (GST_BIN (pipeline)))
      g_assert_not_reached ();
    g_print ("%d\n", i);
  }

  if (gst_element_set_state (pipeline, GST_STATE_PAUSED) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  gst_bin_remove_many (GST_BIN (thread), queue, sink, NULL);

  if (gst_element_set_state (thread, GST_STATE_NULL) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  gst_bin_remove (GST_BIN (pipeline), thread);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();


  return 0;
}
