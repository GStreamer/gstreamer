#include <gst/gst.h>

#define ITERS 100
#include <stdlib.h>
#include "mem.h"

int
main (int argc, gchar * argv[])
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
  usage1 = vmsize ();

  element = gst_element_factory_make ("fakesink", NULL);;
  g_assert (GST_IS_ELEMENT (element));
  pad = gst_element_get_pad (element, "sink");
  g_assert (GST_IS_PAD (pad));
  g_assert (GST_OBJECT_FLOATING (element));
  g_assert (!GST_OBJECT_FLOATING (pad));
  g_assert (gst_pad_get_parent (pad) == element);
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/addpad/unref 1 new element: %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesink", NULL);;
    g_assert (GST_IS_ELEMENT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/unref %d elements: %ld\n", iters, vmsize () - usage1);

  for (i = 0; i < iters / 2; i++) {
    element = gst_element_factory_make ("fakesink", NULL);
    g_assert (GST_IS_ELEMENT (element));
    element2 = gst_element_factory_make ("fakesrc", NULL);
    g_assert (GST_IS_ELEMENT (element2));
    gst_element_link_pads (element2, "src", element, "sink");
    g_assert (GST_PAD_IS_LINKED (gst_element_get_pad (element2, "src")));
    g_assert (GST_PAD_IS_LINKED (gst_element_get_pad (element, "sink")));
    gst_object_unref (GST_OBJECT (element));
    g_assert (!GST_PAD_IS_LINKED (gst_element_get_pad (element2, "src")));
    gst_object_unref (GST_OBJECT (element2));
  }
  g_print ("create/link/unref %d element duos: %ld\n", iters / 2,
      vmsize () - usage1);

  element = gst_element_factory_make ("fakesink", NULL);;
  g_assert (GST_IS_ELEMENT (element));
  pad = gst_element_get_pad (element, "sink");
  g_assert (GST_IS_PAD (pad));
  gst_element_remove_pad (element, pad);
  g_assert (gst_element_get_pad (element, "sink") == NULL);
  gst_object_unref (GST_OBJECT (element));

  g_print ("pad removal on one element: %ld\n", vmsize () - usage1);

  for (i = 0; i < iters / 2; i++) {
    element = gst_element_factory_make ("fakesink", NULL);;
    g_assert (GST_IS_ELEMENT (element));
    pad = gst_element_get_pad (element, "sink");
    g_assert (GST_IS_PAD (pad));
    gst_element_remove_pad (element, pad);
    g_assert (gst_element_get_pad (element, "sink") == NULL);
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("pad removal loop on %d elements: %ld\n", iters / 2,
      vmsize () - usage1);

  for (i = 0; i < iters / 2; i++) {
    element = gst_element_factory_make ("fakesink", NULL);;
    g_assert (GST_IS_ELEMENT (element));
    pad = gst_element_get_pad (element, "sink");
    g_assert (GST_IS_PAD (pad));
    gst_object_ref (GST_OBJECT (pad));
    gst_element_remove_pad (element, pad);
    g_assert (gst_pad_get_parent (pad) == NULL);
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("pad ref/removal/test loop on %d elements: %ld\n", iters / 2,
      vmsize () - usage1);

  element = gst_element_factory_make ("fakesink", NULL);;
  g_assert (GST_IS_ELEMENT (element));
  pad = gst_element_get_pad (element, "sink");
  g_assert (GST_IS_PAD (pad));
  gst_object_unref (GST_OBJECT (element));

  g_print ("pad unref on one element: %ld\n", vmsize () - usage1);

  for (i = 0; i < iters / 2; i++) {
    element = gst_element_factory_make ("fakesink", NULL);
    g_assert (GST_IS_ELEMENT (element));
    pad = gst_element_get_pad (element, "sink");
    g_assert (GST_IS_PAD (pad));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("pad unref loop on %d elements: %ld\n", iters / 2,
      vmsize () - usage1);

  g_print ("leaked: %ld\n", vmsize () - usage1);

  return 0;
}
