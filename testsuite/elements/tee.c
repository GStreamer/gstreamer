/*
 * test for tee element
 * this tests for proxying of caps from tee sink to src's in various situations
 * thomas@apestaart.org
 * originally written for 0.3.2
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
  GstElement *tee, *src, *sink1, *sink2;
  GstPad *tee_src1, *tee_src2;
  GstCaps *src_caps = NULL;
  GstCaps *sink_caps = NULL;
  GstProps *props = NULL;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  g_print ("Creating pipeline\n");
  pipeline = gst_pipeline_new ("pipeline");
  g_signal_connect (G_OBJECT (pipeline), "event", G_CALLBACK (event_func), NULL);

  g_print ("Creating elements\n");
  if (!(tee = element_create ("tee", "tee"))) return 1;
  if (!(src = element_create ("src", "fakesrc"))) return 1;
  g_object_set (G_OBJECT (src), "sizetype", 2, NULL);
  if (!(sink1 = element_create ("sink1", "fakesink"))) return 1;
  if (!(sink2 = element_create ("sink2", "fakesink"))) return 1;
 
  /* add */
  g_print ("Adding elements to bin\n");
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), tee);

  /* connect input part */
  g_print ("Connecting input elements\n");
  gst_pad_connect (gst_element_get_pad (src, "src"),
      		   gst_element_get_pad (tee, "sink"));
   
  /* request one pad from tee */
  g_print ("Requesting first pad\n");
  tee_src1 = gst_element_request_pad_by_name (tee, "src%d");
  gst_bin_add (GST_BIN (pipeline), sink1);
  gst_pad_connect (tee_src1, gst_element_get_pad (sink1, "sink"));

  /* set to play */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  /* pause and request another pad */
  g_print ("Requesting second pad\n");
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  tee_src2 = gst_element_request_pad_by_name (tee, "src%d");
  gst_bin_add (GST_BIN (pipeline), sink2);
  gst_pad_connect (tee_src2, gst_element_get_pad (sink2, "sink"));
  
  /* now we have two fakesinks connected, iterate */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  /* now we try setting caps on the src pad */
  /* FIXME: should we set to pause here ? */
  src_caps = GST_CAPS_NEW (
      "input audio",
      "audio/raw",
        "format", GST_PROPS_STRING ("int"),
	"rate", GST_PROPS_INT (44100)
	);
  g_assert (src_caps != NULL);
  g_print ("Setting caps on fakesrc's src pad\n");
  if (! (gst_pad_try_set_caps (gst_element_get_pad (src, "src"), src_caps)))
  {
    g_print ("Could not set caps !\n");
  }

  /* now iterate and see if it proxies caps ok */
  gst_bin_iterate (GST_BIN (pipeline));
  sink_caps = gst_pad_get_caps (gst_element_get_pad (sink1, "sink"));
  props = gst_caps_get_props (sink_caps);
  if (! (gst_props_has_property (props, "rate")))
  {
    g_print ("Hm, rate has not been propagated to sink1.\n"); 
    return 1;
  }
  else
    g_print ("Rate of pad on sink1 : %d\n", gst_props_get_int (props, "rate"));
  sink_caps = gst_pad_get_caps (gst_element_get_pad (sink2, "sink"));
  props = gst_caps_get_props (sink_caps);
  if (! (gst_props_has_property (props, "rate")))
  {
    g_print ("Hm, rate has not been propagated to sink2.\n"); 
    return 1;
  }
  else
    g_print ("Rate of pad on sink2 : %d\n", gst_props_get_int (props, "rate"));
   
  /* remove the first one, iterate */
  g_print ("Removing first sink\n");
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_pad_disconnect (tee_src1, gst_element_get_pad (sink1, "sink"));
  gst_pad_destroy (tee_src1);
  gst_bin_remove (GST_BIN (pipeline), sink1);

  /* only second fakesink connected, iterate */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  g_print ("Done !\n");
  return 0;
}

