/*
 * test for fakesrc and fakesink element
 * thomas@apestaart.org
 * originally written for 0.3.2
 */

#include <gst/gst.h>
#include "property.h"

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

  el = (GstElement *) gst_element_factory_make (element, name);
  if (el == NULL) {
    fprintf (stderr, "Could not create element %s (%s) !\n", name, element);
    return NULL;
  } else
    return el;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline = NULL;
  GstElement *src, *sink;
  gint retval = 0;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  g_print ("Creating pipeline\n");
  pipeline = gst_pipeline_new ("pipeline");

  g_print ("Connecting signals to pipeline\n");
  g_signal_connect (pipeline, "deep_notify",
      G_CALLBACK (property_change_callback), NULL);
  g_print ("Creating elements\n");
  if (!(src = element_create ("src", "fakesrc")))
    return 1;
  g_object_set (G_OBJECT (src), "sizetype", 2, NULL);
  if (!(sink = element_create ("sink", "fakesink")))
    return 1;

  /* add */
  g_print ("Adding elements to bin\n");
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  /* link */
  g_print ("Linking elements\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we expect this to give an error */
  if (gst_bin_iterate (GST_BIN (pipeline)) != FALSE) {
    g_warning
        ("Iterating a bin with unlinked elements should return FALSE !\n");
    retval = 1;
  }

  gst_pad_link (gst_element_get_pad (src, "src"),
      gst_element_get_pad (sink, "sink"));

  /* set to play */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* we expect this to work */
  if (gst_bin_iterate (GST_BIN (pipeline)) != TRUE) {
    g_error ("Iterating a bin with linked elements should return TRUE !\n");
    retval = 1;
  }

  g_print ("Done !\n");
  return retval;
}
