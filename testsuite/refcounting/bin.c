#include <gst/gst.h>

#define ITERS 100000
#include <stdlib.h>
#include "mem.h"

static GstElement*
create_bin (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  gst_bin_add (GST_BIN (bin), element);
  element = gst_element_new ();
  gst_element_set_name (element, "test2");
  gst_bin_add (GST_BIN (bin), element);

  return bin;
}

int
main (int argc, gchar *argv[])
{
  GstElement *bin;
  long usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  g_print ("starting test\n");
  usage1 = vmsize();

  bin = gst_bin_new ("somebin");
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/unref new bin %ld\n", vmsize()-usage1);

  for (i=0; i<iters;i++) {
    bin = gst_bin_new ("somebin");
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/unref %d bins %ld\n", iters, vmsize()-usage1);

  bin = gst_bin_new ("somebin");
  g_assert (GST_OBJECT_FLOATING (bin));
  gst_object_ref (GST_OBJECT (bin));
  gst_object_sink (GST_OBJECT (bin));
  g_assert (!GST_OBJECT_FLOATING (bin));
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/ref/sink/unref new bin %ld\n", vmsize()-usage1);


  for (i=0; i<iters;i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_object_sink (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/ref/sink/unref %d bins %ld\n", iters, vmsize()-usage1);

  bin = gst_bin_new ("somebin");
  g_assert (!GST_OBJECT_DESTROYED (bin));
  gst_object_destroy (GST_OBJECT (bin));
  g_assert (GST_OBJECT_DESTROYED (bin));
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/destroy/unref new bin %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    bin = gst_bin_new ("somebin");
    gst_object_destroy (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/destroy/unref %d bin %ld\n", iters, vmsize()-usage1);

  bin = gst_bin_new ("somebin");
  gst_object_ref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/ref/unref/unref new bin %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/ref/unref/unref %d bin %ld\n", iters, vmsize()-usage1);

  bin = gst_bin_new ("somebin");
  gst_object_ref (GST_OBJECT (bin));
  gst_object_destroy (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  g_print ("craete/ref/destroy/unref/unref new bin %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_object_destroy (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("craete/ref/destroy/unref/unref %d bins %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_element_set_name (bin, "testing123");
    gst_object_destroy (GST_OBJECT (bin));
    gst_element_set_name (bin, "testing123");
    gst_object_unref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("craete/ref/destroy/unref/unref %d bins with name %ld\n", iters, vmsize()-usage1);

  bin = gst_bin_new ("somebin");
  for (i=0; i<iters;i++) {
    gst_element_set_name (bin, "testing");
  }
  gst_object_unref (GST_OBJECT (bin));
  g_print ("set name %d times %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    bin = create_bin();
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/unref %d bin with children %ld\n", iters, vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return (vmsize()-usage1 ? -1 : 0);
}
