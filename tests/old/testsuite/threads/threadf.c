#include <gst/gst.h>

/* threadf.c
 * this tests if we can make a GThread and construct and interate a pipeline
 * inside it
 * used to fail because of cothread ctx key not being reset on context
 * destroy
 */

#define MAX_IDENTITIES 29
#define RUNS_PER_IDENTITY 5

gboolean running = FALSE;
gboolean done = FALSE;

static void
construct_pipeline (GstElement * pipeline, gint identities)
{
  GstElement *src, *sink;
  GstElement *identity = NULL;
  GstElement *from;
  int i;

  identity = NULL;
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
    if (!(gst_element_link (from, identity)))
      g_print ("Warning: can't link identity with previous element\n");
    from = identity;
  }
  gst_element_link (identity, sink);

  g_object_set (G_OBJECT (src), "num_buffers", 10, "sizetype", 3, NULL);
}

static void
thread (void)
{
  int runs = MAX_IDENTITIES * RUNS_PER_IDENTITY;
  int i;
  GstElement *pipeline;

  for (i = 30; i < runs; ++i) {
    pipeline = gst_pipeline_new ("main_pipeline");
    g_assert (pipeline);

    g_print ("Run %d, using %d identities\n", i, i / RUNS_PER_IDENTITY + 1);
    construct_pipeline (pipeline, i / RUNS_PER_IDENTITY + 1);
    if (!gst_element_set_state (pipeline, GST_STATE_PLAYING))
      g_print ("WARNING: can't set pipeline to play\n");
    while (gst_bin_iterate (GST_BIN (pipeline)))
      g_print ("+");
    g_print ("\n");
    g_print ("Unreffing pipeline\n");
    g_object_unref (G_OBJECT (pipeline));
  }
  done = TRUE;
}

int
main (gint argc, gchar * argv[])
{
  done = FALSE;

  g_thread_init (NULL);
  gst_init (&argc, &argv);

  g_thread_create ((GThreadFunc) thread, NULL, FALSE, NULL);
  g_print ("main: created GThread\n");
  while (!done)
    g_usleep (G_USEC_PER_SEC);
  g_print ("main: done\n");
  return 0;
}
