#include <gst/gst.h>

#define ITERS 100
#include <stdlib.h>

static GstElement *
create_bin (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  g_assert (GST_IS_BIN (bin));
  element = gst_element_factory_make ("fakesrc", NULL);
  g_assert (GST_IS_ELEMENT (element));
  gst_element_set_name (element, "test1");
  gst_bin_add (GST_BIN (bin), element);
  element = gst_element_factory_make ("fakesrc", NULL);
  g_assert (GST_IS_ELEMENT (element));
  gst_element_set_name (element, "test2");
  gst_bin_add (GST_BIN (bin), element);

  return bin;
}

static GstElement *
create_bin_ghostpads (void)
{
  GstElement *bin;
  GstElement *element1, *element2;

  bin = gst_bin_new ("testbin");
  element1 = gst_element_factory_make ("identity", NULL);
  gst_bin_add (GST_BIN (bin), element1);
  element2 = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add (GST_BIN (bin), element2);
  gst_element_link_pads (element1, "src", element2, "sink");
  gst_element_add_ghost_pad (bin, gst_element_get_pad (element1, "sink"),
      "ghost_sink");

  return bin;
}

static void
add_remove_test1 (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_factory_make ("fakesrc", NULL);
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
  element = gst_element_factory_make ("fakesrc", NULL);
  gst_element_set_name (element, "test1");
  gst_object_ref (GST_OBJECT (element));
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_bin_remove (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  g_assert (!GST_OBJECT_DESTROYED (element));

  gst_object_unref (GST_OBJECT (element));
#if 0
  g_assert (GST_OBJECT_DESTROYED (element));
  gst_object_unref (GST_OBJECT (element));
#endif

  gst_object_unref (GST_OBJECT (bin));
}

#if 0
/* This code is bogus */
static void
add_remove_test3 (void)
{
  GstElement *bin;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_factory_make ("fakesrc", NULL);
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));

  gst_object_unref (GST_OBJECT (element));
  g_assert (gst_bin_get_by_name (GST_BIN (bin), "test1") == NULL);

  gst_object_unref (GST_OBJECT (bin));
}
#endif

#if 0
/* This code is bogus */
static void
add_remove_test4 (void)
{
  GstElement *bin, *bin2;
  GstElement *element;

  bin = gst_bin_new ("testbin");
  element = gst_element_factory_make ("fakesrc", NULL);
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (bin), element);
  g_assert (!GST_OBJECT_FLOATING (element));

  bin2 = create_bin ();
  g_assert (GST_OBJECT_FLOATING (bin2));
  gst_bin_add (GST_BIN (bin), bin2);
  g_assert (!GST_OBJECT_FLOATING (bin2));

  gst_object_unref (GST_OBJECT (bin2));
  g_assert (gst_bin_get_by_name (GST_BIN (bin), "testbin") == NULL);
  gst_object_unref (GST_OBJECT (element));
  g_assert (gst_bin_get_by_name (GST_BIN (bin), "test1") == NULL);

  gst_object_unref (GST_OBJECT (bin));
}
#endif

int
main (int argc, gchar * argv[])
{
  GstElement *bin;
  int usage1;
  gint i, iters;

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;


  g_print ("starting test\n");

  usage1 = gst_alloc_trace_live_all ();
  //gst_alloc_trace_print_all ();

  bin = gst_bin_new ("somebin");
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/unref new bin %d\n", gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    bin = gst_bin_new ("somebin");
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/unref %d bins %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  bin = gst_bin_new ("somebin");
  g_assert (GST_OBJECT_FLOATING (bin));
  gst_object_ref (GST_OBJECT (bin));
  gst_object_sink (GST_OBJECT (bin));
  g_assert (!GST_OBJECT_FLOATING (bin));
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/ref/sink/unref new bin %d\n",
      gst_alloc_trace_live_all () - usage1);


  for (i = 0; i < iters; i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_object_sink (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/ref/sink/unref %d bins %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  bin = gst_bin_new ("somebin");
  g_assert (!GST_OBJECT_DESTROYED (bin));
  gst_object_unref (GST_OBJECT (bin));
#if 0
  g_assert (GST_OBJECT_DESTROYED (bin));
  gst_object_unref (GST_OBJECT (bin));
#endif
  g_print ("create/destroy/unref new bin %d\n",
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    bin = gst_bin_new ("somebin");
    gst_object_unref (GST_OBJECT (bin));
#if 0
    gst_object_unref (GST_OBJECT (bin));
#endif
  }
  g_print ("create/destroy/unref %d bin %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  bin = gst_bin_new ("somebin");
  gst_object_ref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  g_print ("create/ref/unref/unref new bin %d\n",
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/ref/unref/unref %d bin %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  bin = gst_bin_new ("somebin");
  gst_object_ref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
  gst_object_unref (GST_OBJECT (bin));
#if 0
  gst_object_unref (GST_OBJECT (bin));
#endif
  g_print ("craete/ref/destroy/unref/unref new bin %d\n",
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
    gst_object_unref (GST_OBJECT (bin));
#if 0
    gst_object_unref (GST_OBJECT (bin));
#endif
  }
  g_print ("craete/ref/destroy/unref/unref %d bins %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    bin = gst_bin_new ("somebin");
    gst_object_ref (GST_OBJECT (bin));
    gst_element_set_name (bin, "testing123");
    gst_object_unref (GST_OBJECT (bin));
    gst_element_set_name (bin, "testing123");
    gst_object_unref (GST_OBJECT (bin));
#if 0
    gst_object_unref (GST_OBJECT (bin));
#endif
  }
  g_print ("craete/ref/destroy/unref/unref %d bins with name %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  bin = gst_bin_new ("somebin");
  for (i = 0; i < iters; i++) {
    gst_element_set_name (bin, "testing");
  }
  gst_object_unref (GST_OBJECT (bin));
  g_print ("set name %d times %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    bin = create_bin ();
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/unref %d bin with children %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters / 2; i++) {
    bin = create_bin_ghostpads ();
    gst_object_unref (GST_OBJECT (bin));
  }
  g_print ("create/unref %d bin with children and ghostpads %d\n", iters / 2,
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    add_remove_test1 ();
  }
  g_print ("add/remove test1 %d in bin %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    add_remove_test2 ();
  }
  g_print ("add/remove test2 %d in bin %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

#if 0
  for (i = 0; i < iters; i++) {
    add_remove_test3 ();
  }
  g_print ("add/destroy/remove test3 %d in bin %d\n", iters,
      gst_alloc_trace_live_all () - usage1);
#endif

#if 0
  for (i = 0; i < iters; i++) {
    add_remove_test4 ();
  }
  g_print ("add/destroy/remove test4 %d in bin %d\n", iters,
      gst_alloc_trace_live_all () - usage1);
#endif

  g_print ("leaked: %d\n", gst_alloc_trace_live_all () - usage1);

  //gst_alloc_trace_print_all ();

  return (gst_alloc_trace_live_all () - usage1 ? -1 : 0);
}
