#include <gst/gst.h>

#define ITERS 100000
#include <stdlib.h>
#include "mem.h"

static GstElement*
create_element (gchar *padname, GstPadDirection dir)
{
  GstElement *element;
  GstPad *pad;

  element = gst_element_new ();
  pad = gst_pad_new (padname, dir);
  gst_element_add_pad (element, pad);

  return element;
}

int
main (int argc, gchar *argv[])
{
  GstElement *element;
  GstElement *element2;
  GstPad *pad;
  long usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;


  g_print ("starting element with pad test with %d iterations\n", iters);
  usage1 = vmsize();

  element = create_element ("sink", GST_PAD_SINK);
  pad = gst_element_get_pad (element, "sink");
  g_assert (GST_OBJECT_FLOATING (element));
  g_assert (!GST_OBJECT_FLOATING (pad));
  g_assert (gst_pad_get_parent (pad) == element);
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/addpad/unref new element %ld\n", vmsize()-usage1);

  for (i=0; i<iters; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/addpad/unref %d elements %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters/2; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    element2 = create_element ("src", GST_PAD_SRC);
    gst_element_connect (element, "sink", element2, "src");
    g_assert (GST_PAD_CONNECTED (gst_element_get_pad (element2, "src")));
    g_assert (GST_PAD_CONNECTED (gst_element_get_pad (element, "sink")));
    gst_object_unref (GST_OBJECT (element));
    g_assert (!GST_PAD_CONNECTED (gst_element_get_pad (element2, "src")));
    gst_object_unref (GST_OBJECT (element2));
  }
  g_print ("create/connect/unref %d elements %ld\n", iters, vmsize()-usage1);

  element = create_element ("sink", GST_PAD_SINK);
  pad = gst_element_get_pad (element, "sink");
  gst_element_remove_pad (element, pad);
  g_assert (gst_element_get_pad (element, "sink") == NULL);

  g_print ("pad removal ok %ld\n", vmsize()-usage1);
  for (i=0; i<iters/2; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    pad = gst_element_get_pad (element, "sink");
    gst_element_remove_pad (element, pad);
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("pad removal loop %d  %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters/2; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    pad = gst_element_get_pad (element, "sink");
    gst_object_ref (GST_OBJECT (pad));
    gst_element_remove_pad (element, pad);
    g_assert (gst_pad_get_parent (pad) == NULL);
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("pad removal and test loop %d  %ld\n", iters, vmsize()-usage1);

  element = create_element ("sink", GST_PAD_SINK);
  pad = gst_element_get_pad (element, "sink");
  gst_object_destroy (GST_OBJECT (element));
  g_assert (GST_OBJECT_DESTROYED (element));
  g_assert (gst_element_get_pad (element, "sink") == NULL);
  gst_object_unref (GST_OBJECT (element));

  g_print ("pad destroy/removal ok %ld\n", vmsize()-usage1);

  for (i=0; i<iters/2; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    pad = gst_element_get_pad (element, "sink");
    gst_object_destroy (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("pad destroy/removal loop %d %ld\n", iters, vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return (vmsize()-usage1 ? -1 : 0);
}
