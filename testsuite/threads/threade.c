#include <gst/gst.h>
#include <unistd.h>

/* threadc.c
 * this tests if we can make a GstBin and iterate it inside a GThread
 */

#define MAX_IDENTITIES 29
#define RUNS_PER_IDENTITY 5

gboolean running = FALSE;
gboolean done = FALSE;

static void
construct_pipeline (GstElement * pipeline, gint identities)
{
  GstElement *src, *sink, *identity = NULL;
  GstElement *from;
  int i;

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (src);
  g_assert (sink);
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  from = src;

  for (i = 0; i < identities; ++i) {
    identity = gst_element_factory_make ("identity", NULL);
    g_assert (identity);
    gst_bin_add (GST_BIN (pipeline), identity);
    gst_element_link (from, identity);
    from = identity;
  }
  gst_element_link (identity, sink);

  g_object_set (G_OBJECT (src), "num_buffers", 10, "sizetype", 3, NULL);
}

static void
iterator (GstElement * bin)
{
  gst_element_set_state (bin, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (bin)))
    g_print ("+");
  gst_element_set_state (bin, GST_STATE_NULL);
  g_print ("\n");
  done = TRUE;
}

int
main (gint argc, gchar * argv[])
{
  int runs = MAX_IDENTITIES * RUNS_PER_IDENTITY;
  int i;
  GstElement *pipeline;

  alarm (10);

  g_thread_init (NULL);
  gst_init (&argc, &argv);

  for (i = 0; i < runs; ++i) {
    pipeline = gst_pipeline_new ("main_pipeline");
    g_assert (pipeline);

    /* connect state change signal */
    construct_pipeline (pipeline, i / RUNS_PER_IDENTITY + 1);

    done = FALSE;
    g_thread_create ((GThreadFunc) iterator, pipeline, FALSE, NULL);
    g_print ("Created GThread\n");

    g_print ("Waiting for thread PLAYING->PAUSED\n");
    while (!done)               /* do nothing */
      ;
    running = FALSE;
    g_print ("Unreffing pipeline\n");
    g_object_unref (G_OBJECT (pipeline));
  }

  return 0;
}
