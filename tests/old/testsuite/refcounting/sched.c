#include <gst/gst.h>

gint i = 0;
GstElement *pipeline;
GstPadChainFunction oss_chain;

static GstElement *
make_and_check_element (gchar * type, gchar * name)
{
  GstElement *element = gst_element_factory_make (type, name);

  if (element == NULL) {
    g_warning
	("Could not run test, because element type \"%s\" is not installed. Please retry when it is. Assuming it works for now...",
	type);
    exit (1);
  }

  return element;
}

static void
create_pipeline (void)
{
  GstElement *src;
  GstElement *sink;
  GstElement *id;

  pipeline = gst_pipeline_new ("pipeline");
  src = make_and_check_element ("sinesrc", "src");
  /**
   * You need a sink with a loop-based element in here, if you want to kill opt, too.
   * Osssink (chain-based) only breaks the basic scheduler.
   */
  sink = make_and_check_element ("alsasink", "sink");


  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  /** 
   * now make the bug appear
   * I believe it has something to do with 2 chains being created in the scheduler
   * but I haven't looked at it yet
   * If you comment out the next 4 lines, everything works fine.
   * And no, it's not because of identity, you may use any other element.
   */
  gst_element_unlink (src, sink);
  id = make_and_check_element ("identity", "id");
  gst_bin_add (GST_BIN (pipeline), id);
  gst_element_link_many (src, id, sink, NULL);

  /* This pipeline will not be removed properly once we unref it */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

gint
main (gint argc, gchar * argv[])
{
  gst_init (&argc, &argv);
  create_pipeline ();

  while (i < 300) {
    /**
     * only inc i when it works, so the program hangs when _iterate returns false,
     * which it does after the first pipeline isn't unref'd properly and the next
     * osssink refuses to work.
     */
    if (gst_bin_iterate (GST_BIN (pipeline)))
      i++;
    if (i % 50 == 0) {
      gst_object_unref (GST_OBJECT (pipeline));
      create_pipeline ();
    }
  }
  return 0;
}
