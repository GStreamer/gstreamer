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

static GstElement*
create_bin_ghostpads (void)
{
  GstElement *bin;
  GstElement *element1, *element2;

  bin = gst_bin_new ("testbin");
  element1 = gst_element_new ();
  gst_element_set_name (element1, "test1");
  gst_element_add_pad (element1, gst_pad_new ("src1", GST_PAD_SRC));
  gst_bin_add (GST_BIN (bin), element1);
  element2 = gst_element_new ();
  gst_element_set_name (element2, "test2");
  gst_element_add_pad (element2, gst_pad_new ("sink1", GST_PAD_SINK));
  gst_bin_add (GST_BIN (bin), element2);
  gst_element_connect (element1, "src1", element2, "sink1");
  gst_element_add_ghost_pad (bin, gst_element_get_pad (element2, "sink1"), "sink1");

  return bin;
}

static void
add_remove_test1 (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_bin_remove (GST_BIN (bin), element);

  gst_object_unref (GST_OBJECT (bin));
}

static void
add_remove_test2 (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  gst_object_ref (GST_OBJECT (element));
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_bin_remove (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  g_assert (!GST_OBJECT_DESTROYED (element));

  gst_object_destroy (GST_OBJECT (element));
  g_assert (GST_OBJECT_DESTROYED (element));
  gst_object_unref (GST_OBJECT (element));

  gst_object_unref (GST_OBJECT (bin));
}

static void
add_remove_test3 (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));

  gst_object_destroy (GST_OBJECT (element));
  g_assert (gst_bin_get_by_name (GST_BIN (bin), "test1") == NULL);

  gst_object_unref (GST_OBJECT (bin));
}

static void
add_remove_test4 (void)
{
  GstElement *bin, *bin2;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));

  bin2 = create_bin ();
  g_assert (GST_OBJECT_FLOATING (bin2));
  gst_bin_add (GST_BIN (bin), bin2);
  g_assert (!GST_OBJECT_FLOATING (bin2));

  gst_object_destroy (GST_OBJECT (bin2));
  g_assert (gst_bin_get_by_name (GST_BIN (bin), "testbin") == NULL);
  gst_object_destroy (GST_OBJECT (element));
  g_assert (gst_bin_get_by_name (GST_BIN (bin), "test1") == NULL);

  gst_object_unref (GST_OBJECT (bin));
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

  for (i=0; i<iters/2;i++) {
    bin = create_bin_ghostpads();
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/unref %d bin with children and ghostpads %ld\n", iters/2, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    add_remove_test1();
  }
  g_print ("add/remove test1 %d in bin %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    add_remove_test2();
  }
  g_print ("add/remove test2 %d in bin %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    add_remove_test3();
  }
  g_print ("add/destroy/remove test3 %d in bin %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    add_remove_test4();
  }
  g_print ("add/destroy/remove test4 %d in bin %ld\n", iters, vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return (vmsize()-usage1 ? -1 : 0);
}
