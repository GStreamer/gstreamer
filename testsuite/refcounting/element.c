#include <gst/gst.h>

#define ITERS 100
#include <stdlib.h>

int
main (int argc, gchar * argv[])
{
  GstElement *element;
  int usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  g_print ("starting test\n");

  usage1 = gst_alloc_trace_live_all ();
  //gst_alloc_trace_print_all ();

  element = gst_element_factory_make ("fakesrc", NULL);
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/unref new element %d\n",
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/unref %d elements %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  element = gst_element_factory_make ("fakesrc", NULL);
  g_assert (GST_OBJECT_FLOATING (element));
  gst_object_ref (GST_OBJECT (element));
  gst_object_sink (GST_OBJECT (element));
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/ref/sink/unref new element %d\n",
      gst_alloc_trace_live_all () - usage1);


  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_object_sink (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/ref/sink/unref %d elements %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

#if 0
  element = gst_element_factory_make ("fakesrc", NULL);
  g_assert (!GST_OBJECT_DESTROYED (element));
  gst_object_unref (GST_OBJECT (element));
  g_assert (GST_OBJECT_DESTROYED (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/destroy/unref new element %d\n",
      gst_alloc_trace_live_all () - usage1);
#endif

#if 0
  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/destroy/unref %d element %d\n", iters,
      gst_alloc_trace_live_all () - usage1);
#endif

  element = gst_element_factory_make ("fakesrc", NULL);
  gst_object_ref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("create/ref/unref/unref new element %d\n",
      gst_alloc_trace_live_all () - usage1);

  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("create/ref/unref/unref %d element %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

#if 0
  element = gst_element_factory_make ("fakesrc", NULL);
  gst_object_ref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  gst_object_unref (GST_OBJECT (element));
  g_print ("craete/ref/destroy/unref/unref new element %d\n",
      gst_alloc_trace_live_all () - usage1);
#endif

#if 0
  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("craete/ref/destroy/unref/unref %d elements %d\n", iters,
      gst_alloc_trace_live_all () - usage1);
#endif

#if 0
  for (i = 0; i < iters; i++) {
    element = gst_element_factory_make ("fakesrc", NULL);
    gst_object_ref (GST_OBJECT (element));
    gst_element_set_name (element, "testing123");
    gst_object_unref (GST_OBJECT (element));
    gst_element_set_name (element, "testing123");
    gst_object_unref (GST_OBJECT (element));
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("craete/ref/destroy/unref/unref %d elements with name %d\n", iters,
      gst_alloc_trace_live_all () - usage1);
#endif

  element = gst_element_factory_make ("fakesrc", NULL);
  for (i = 0; i < iters; i++) {
    gst_element_set_name (element, "testing");
  }
  gst_object_unref (GST_OBJECT (element));
  g_print ("set name %d times %d\n", iters,
      gst_alloc_trace_live_all () - usage1);

  g_print ("leaked: %d\n", gst_alloc_trace_live_all () - usage1);

  return (gst_alloc_trace_live_all () - usage1 ? -1 : 0);
}
