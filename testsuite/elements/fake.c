/*
 * test for fakesrc and fakesink element
 * this tests for proxying of caps
 * thomas@apestaart.org
 */

#include <gst/gst.h>
#include "events.h"

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
  GstElement *src, *sink;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  g_print ("Creating pipeline\n");
  pipeline = gst_pipeline_new ("pipeline");

  g_signal_connect (G_OBJECT (pipeline), "event", G_CALLBACK (event_func), NULL);
 g_print ("Creating elements\n");
  if (!(src = element_create ("src", "fakesrc"))) return 1;
  g_object_set (G_OBJECT (src), "sizetype", 2, NULL);
  if (!(sink = element_create ("sink", "fakesink"))) return 1;
 
  /* add */
  g_print ("Adding elements to bin\n");
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  /* connect */
  g_print ("Connecting elements\n");
  gst_pad_connect (gst_element_get_pad (src, "src"),
      		   gst_element_get_pad (sink, "sink"));
   
  /* set to play */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  g_print ("Done !\n");
  return 0;
}

