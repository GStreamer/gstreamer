/*
 * test for tee element
 * this tests for proxying of caps from tee sink to src's in various situations
 * thomas@apestaart.org
 */

#include <gst/gst.h>

GstElement *
element_create (char *name, char *element)
  /*
   * create the element
   * print an error if it can't be created
   * return NULL if it couldn't be created
   * return element if it did work
   */
{
  GstElement *el = NULL;

  el = (GstElement *) gst_elementfactory_make (element, name);
  if (el == NULL)
  {
    fprintf (stderr, "Could not create element %s (%s) !\n", name, element);
    return NULL;
  }
  else
    return el;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline = NULL;
  GstElement *tee, *src, *sink1, *sink2;
  GstPad *tee_src;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  g_print ("Creating pipeline\n");
  pipeline = gst_pipeline_new ("pipeline");
  //g_assert (GST_IS_PIPELINE (pipeline));

  g_print ("Creating elements\n");
  if (!(tee = element_create ("tee", "tee"))) return 1;
  if (!(src = element_create ("src", "fakesrc"))) return 1;
  if (!(sink1 = element_create ("sink1", "fakesink"))) return 1;
  if (!(sink2 = element_create ("sink2", "fakesink"))) return 1;

  /* add */
  g_print ("Adding elements to bin\n");
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), tee);
  
  /* request one pad from tee */
  tee_src = gst_element_request_pad_by_name (tee, "src%d");

  /* connect */
  gst_pad_connect (tee_src, gst_element_get_pad (sink1, "sink"));

  /* set to play */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  g_print ("Done !\n");
  return 0;
}

