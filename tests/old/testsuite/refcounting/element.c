#include <gst/gst.h>

#define ITERS 100000
#include "mem.h"

static void
print_element_props (GstElement *element)
{
  g_print ("name %s\n", gst_element_get_name (element));
  g_print ("flags 0x%08x\n", GST_FLAGS (element));
}

int
main (int argc, gchar *argv[])
{
  GstElement *element;
  long usage1, usage2;
  gint i;

  gst_init (&argc, &argv);

  g_print ("creating new element\n");
  element = gst_element_new ();
  usage1 = vmsize();
  print_element_props (element);
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));

  g_print ("creating new element\n");
  element = gst_element_new ();
  g_assert (GST_OBJECT_FLOATING (element));
  print_element_props (element);
  g_print ("sink new element %ld\n", vmsize()-usage1);
  gst_object_ref (GST_OBJECT (element));
  gst_object_sink (GST_OBJECT (element));
  g_assert (!GST_OBJECT_FLOATING (element));
  print_element_props (element);
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));

  for (i=0; i<ITERS;i++) {
    element = gst_element_new ();
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("unref 100000 element %ld\n", vmsize()-usage1);

  g_print ("creating new element\n");
  element = gst_element_new ();
  g_assert (!GST_OBJECT_DESTROYED (element));
  print_element_props (element);
  g_print ("destroy new element %ld\n", vmsize()-usage1);
  gst_object_destroy (GST_OBJECT (element));
  g_assert (GST_OBJECT_DESTROYED (element));
  print_element_props (element);
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));
  
  for (i=0; i<ITERS;i++) {
    element = gst_element_new ();
    gst_object_destroy (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  g_print ("creating new element\n");
  element = gst_element_new ();
  gst_object_ref (GST_OBJECT (element));
  print_element_props (element);
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));
  
  for (i=0; i<ITERS;i++) {
    element = gst_element_new ();
    gst_object_ref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  g_print ("creating new element\n");
  element = gst_element_new ();
  gst_object_ref (GST_OBJECT (element));
  print_element_props (element);
  gst_object_destroy (GST_OBJECT (element));
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));
  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));
  
  for (i=0; i<ITERS;i++) {
    element = gst_element_new ();
    gst_object_ref (GST_OBJECT (element));
    gst_object_destroy (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  for (i=0; i<ITERS;i++) {
    element = gst_element_new ();
    gst_object_ref (GST_OBJECT (element));
    gst_element_set_name (element, "testing123");
    gst_object_destroy (GST_OBJECT (element));
    gst_element_set_name (element, "testing123");
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  element = gst_element_new ();
  for (i=0; i<ITERS;i++) {
    gst_element_set_name (element, "testing");
  }
  gst_object_unref (GST_OBJECT (element));
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return 0;
}
