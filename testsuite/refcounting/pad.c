#include <gst/gst.h>

#define ITERS 100
#include <stdlib.h>
#include "mem.h"

int
main (int argc, gchar * argv[])
{
  GstPad *pad;
  GstPadTemplate *padtempl;
  long usage1;
  gint i, iters;

  gst_alloc_trace_set_flags_all (GST_ALLOC_TRACE_LIVE);

  gst_init (&argc, &argv);

  g_mem_profile ();

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  //gst_alloc_trace_print_all ();

  g_print ("starting pad test\n");
  usage1 = vmsize ();

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/unref new pad %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/unref %d pads %ld\n", iters, vmsize () - usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  g_assert (GST_OBJECT_FLOATING (pad));
  gst_object_ref (GST_OBJECT (pad));
  gst_object_sink (GST_OBJECT (pad));
  g_assert (!GST_OBJECT_FLOATING (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/ref/sink/unref new pad %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_sink (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/sink/unref %d pads %ld\n", iters, vmsize () - usage1);

#if 0
  pad = gst_pad_new ("padname", GST_PAD_SINK);
  g_assert (!GST_OBJECT_DESTROYED (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_assert (GST_OBJECT_DESTROYED (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/destroy/unref pad %ld\n", vmsize () - usage1);
#endif

#if 0
  for (i = 0; i < iters; i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/destroy/unref %d pads %ld\n", iters, vmsize () - usage1);
#endif

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_ref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/ref/unref/unref pad %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/unref/unref %d pads %ld\n", iters, vmsize () - usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_ref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
#if 0
  gst_object_unref (GST_OBJECT (pad));
#endif
  g_print ("create/ref/destroy/unref/unref pad %ld\n", vmsize () - usage1);

#if 0
  for (i = 0; i < iters; i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    //gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/destroy/unref/unref %d pads %ld\n", iters,
      vmsize () - usage1);
#endif

#if 0
  for (i = 0; i < iters; i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_pad_set_name (pad, "testing123");
    gst_object_unref (GST_OBJECT (pad));
    gst_pad_set_name (pad, "testing123");
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/destroy/unref/unref %d pads %ld with name\n", iters,
      vmsize () - usage1);
#endif

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  for (i = 0; i < iters; i++) {
    gst_pad_set_name (pad, "testing");
  }
  gst_object_unref (GST_OBJECT (pad));
  g_print ("set name %d times %ld\n", iters, vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    padtempl =
        gst_pad_template_new ("sink%d", GST_PAD_SINK, GST_PAD_SOMETIMES, NULL);
    gst_object_unref (GST_OBJECT (padtempl));
  }
  g_print ("%d padtemplates create/unref %ld\n", iters, vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    padtempl =
        gst_pad_template_new ("sink%d", GST_PAD_SINK, GST_PAD_SOMETIMES, NULL);
    pad = gst_pad_new_from_template (padtempl, "sink1");
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("%d pads create/unref from padtemplate %ld\n", iters,
      vmsize () - usage1);

  g_print ("leaked: %ld\n", vmsize () - usage1);

  //gst_alloc_trace_print_all ();

  return vmsize () - usage1;
}
