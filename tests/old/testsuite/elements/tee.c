/*
 * test for tee element
 * this tests for proxying of caps from tee sink to src's in various situations
 * it also tests if you get a good, unique pad when requesting a third one
 * which shows a bug in 0.3.2 :
 * request pad, get 0
 * request pad, get 1
 * remove pad 0, 
 * request pad, get 1 (number of pads), already exists, assert fail
 *
 * thomas@apestaart.org
 * originally written for 0.3.2
 */

#include <gst/gst.h>
#include <property.h>

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
  GstStructure *structure = NULL;
  GstPad *pad = NULL;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  g_print ("Creating pipeline\n");
  pipeline = gst_pipeline_new ("pipeline");

  g_print ("Connecting signals to pipeline\n");
  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (property_change_callback), NULL);
    
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

  /* link input part */
  g_print ("Linking input elements\n");
  gst_pad_link (gst_element_get_pad (src, "src"),
                gst_element_get_pad (tee, "sink"));
   
  /* request one pad from tee */
  g_print ("Requesting first pad\n");
  tee_src1 = gst_element_get_request_pad (tee, "src%d");
  gst_bin_add (GST_BIN (pipeline), sink1);
  gst_pad_link (tee_src1, gst_element_get_pad (sink1, "sink"));

  /* set to play */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  /* pause and request another pad */
  g_print ("Requesting second pad\n");
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  tee_src2 = gst_element_get_request_pad (tee, "src%d");
  gst_bin_add (GST_BIN (pipeline), sink2);
  gst_pad_link (tee_src2, gst_element_get_pad (sink2, "sink"));
  
  /* now we have two fakesinks linked, iterate */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  /* now we try setting caps on the src pad */
  /* FIXME: should we set to pause here ? */
  src_caps = gst_caps_from_string ("audio/raw, format=(s)\"int\", "
      "rate=(i)44100");

  g_assert (src_caps != NULL);
  g_print ("Setting caps on fakesrc's src pad\n");
  pad = gst_element_get_pad (src, "src");
  if ((gst_pad_try_set_caps (pad, src_caps)) <= 0) {
    g_print ("Could not set caps !\n");
  }

  /* now iterate and see if it proxies caps ok */
  gst_bin_iterate (GST_BIN (pipeline));
  sink_caps = gst_pad_get_caps (gst_element_get_pad (sink1, "sink"));
  if (sink_caps && gst_caps_is_fixed (sink_caps)) {
    structure = gst_caps_get_structure (sink_caps, 0);
  }else {
    structure = NULL;
    g_print ("sink_caps is not fixed\n");
  }
  if (structure == NULL || !(gst_structure_has_field (structure, "rate"))) {
    g_print ("Hm, rate has not been propagated to sink1.\n"); 
    return 1;
  } else {
    int rate;
    gst_structure_get_int (structure, "rate", &rate);
    g_print ("Rate of pad on sink1 : %d\n", rate);
  }
  sink_caps = gst_pad_get_caps (gst_element_get_pad (sink2, "sink"));
  structure = gst_caps_get_structure (sink_caps, 0);
  if (structure != NULL && ! (gst_structure_has_field (structure, "rate"))) {
    g_print ("Hm, rate has not been propagated to sink2.\n"); 
    return 1;
  } else {
    int rate;
    gst_structure_get_int (structure, "rate", &rate);
    g_print ("Rate of pad on sink2 : %d\n", rate);
  }
   
  /* remove the first one, iterate */
  g_print ("Removing first sink\n");
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_pad_unlink (tee_src1, gst_element_get_pad (sink1, "sink"));
  gst_bin_remove (GST_BIN (pipeline), sink1);

  /* only second fakesink linked, iterate */
  g_print ("Doing 1 iteration\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_bin_iterate (GST_BIN (pipeline));

  /* request another pad */
  g_print ("Requesting third pad\n");
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  /* in 0.3.2 the next statement gives an assert error */
  tee_src1 = gst_element_get_request_pad (tee, "src%d");
  
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Done !\n");
  return 0;
}
