#include <gst/gst.h>

#define ITERS 100000
#include <stdlib.h>
#include "mem.h"

int
main (int argc, gchar *argv[])
{
  GstElement *element;
  long usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  g_print ("starting test\n");
  usage1 = vmsize();

  element = gst_element_factory_make ("fakesrc", NULL);
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/unref new element %ld\n", vmsize()-usage1);

  for (i=0; i<iters;i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/unref %d elements %ld\n", iters, vmsize()-usage1);

  element = gst_element_factory_make ("fakesrc", NULL);
  g_assert (GST_OBJECT_FLOATING (element));
  gst_object_ref (GST_OBJECT (element));
  gst_object_sink (GST_OBJECT (element));
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/ref/sink/unref new element %ld\n", vmsize()-usage1);


  for (i=0; i<iters;i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_object_sink (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/ref/sink/unref %d elements %ld\n", iters, vmsize()-usage1);

  element = gst_element_factory_make ("fakesrc", NULL);
  g_assert (!GST_OBJECT_DESTROYED (element));
  gst_object_destroy (GST_OBJECT (element));
  g_assert (GST_OBJECT_DESTROYED (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/destroy/unref new element %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_destroy (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/destroy/unref %d element %ld\n", iters, vmsize()-usage1);

  element = gst_element_factory_make ("fakesrc", NULL);
  gst_object_ref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/ref/unref/unref new element %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/ref/unref/unref %d element %ld\n", iters, vmsize()-usage1);

  element = gst_element_factory_make ("fakesrc", NULL);
  gst_object_ref (GST_OBJECT (element));
  gst_object_destroy (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("craete/ref/destroy/unref/unref new element %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_object_destroy (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("craete/ref/destroy/unref/unref %d elements %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_element_set_name (element, "testing123");
    gst_object_destroy (GST_OBJECT (element));
    gst_element_set_name (element, "testing123");
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("craete/ref/destroy/unref/unref %d elements with name %ld\n", iters, vmsize()-usage1);

  element = gst_element_factory_make ("fakesrc", NULL);
  for (i=0; i<iters;i++) {
    gst_element_set_name (element, "testing");
  }
  gst_object_unref (GST_OBJECT (element));
  g_print ("set name %d times %ld\n", iters, vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return (vmsize()-usage1 ? -1 : 0);
}
