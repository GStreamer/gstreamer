#include <gst/gst.h>

#define ITERS 100000
#include <stdlib.h>
#include "mem.h"

int
main (int argc, gchar *argv[])
{
  GstPad *pad;
  GstPad *pad2;
  GstPadTemplate *padtempl;
  long usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  g_print ("starting pad test\n");
  usage1 = vmsize();

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/unref new pad %ld\n", vmsize()-usage1);

  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/unref %d pads %ld\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  g_assert (GST_OBJECT_FLOATING (pad));
  gst_object_ref (GST_OBJECT (pad));
  gst_object_sink (GST_OBJECT (pad));
  g_assert (!GST_OBJECT_FLOATING (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/ref/sink/unref new pad %ld\n", vmsize()-usage1);

  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_sink (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/sink/unref %d pads %ld\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  g_assert (!GST_OBJECT_DESTROYED (pad));
  gst_object_destroy (GST_OBJECT (pad));
  g_assert (GST_OBJECT_DESTROYED (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/destroy/unref pad %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_destroy (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/destroy/unref %d pads %ld\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_ref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/ref/unref/unref pad %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/unref/unref %d pads %ld\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_ref (GST_OBJECT (pad));
  gst_object_destroy (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad));
  g_print ("create/ref/destroy/unref/unref pad %ld\n", vmsize()-usage1);
  
  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_destroy (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/destroy/unref/unref %d pads %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_pad_set_name (pad, "testing123");
    gst_object_destroy (GST_OBJECT (pad));
    gst_pad_set_name (pad, "testing123");
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("create/ref/destroy/unref/unref %d pads %ld with name\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  for (i=0; i<iters;i++) {
    gst_pad_set_name (pad, "testing");
  }
  gst_object_unref (GST_OBJECT (pad));
  g_print ("set name %d times %ld\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  pad2 = gst_pad_new ("padname2", GST_PAD_SRC);

  gst_pad_connect (pad2, pad);
  g_assert (GST_PAD_CONNECTED (pad));
  g_assert (GST_PAD_CONNECTED (pad2));
  gst_pad_disconnect (pad2, pad);
  g_assert (!GST_PAD_CONNECTED (pad));
  g_assert (!GST_PAD_CONNECTED (pad2));
  g_print ("connect/disconnect pad %ld\n", vmsize()-usage1);
  gst_pad_connect (pad, pad2);
  g_assert (GST_PAD_CONNECTED (pad));
  g_assert (GST_PAD_CONNECTED (pad2));
  gst_pad_disconnect (pad, pad2);
  g_assert (!GST_PAD_CONNECTED (pad));
  g_assert (!GST_PAD_CONNECTED (pad2));
  g_print ("connect/disconnect pad wrong direction %ld\n", vmsize()-usage1);

  gst_object_unref (GST_OBJECT (pad));
  gst_object_unref (GST_OBJECT (pad2));

  for (i=0; i<iters;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    pad2 = gst_pad_new ("padname2", GST_PAD_SRC);
    gst_pad_connect (pad2, pad);
    gst_pad_disconnect (pad2, pad);
    gst_pad_connect (pad, pad2);
    gst_pad_disconnect (pad, pad2);
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad2));
  }
  g_print ("connect/disconnect %d pads %ld\n", iters, vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  pad2 = gst_pad_new ("padname2", GST_PAD_SRC);

  gst_pad_connect (pad2, pad);
  g_assert (GST_PAD_CONNECTED (pad2));
  g_assert (GST_PAD_CONNECTED (pad));

  gst_object_unref (GST_OBJECT (pad2));
  g_assert (!GST_PAD_CONNECTED (pad));
  g_assert (!GST_OBJECT_DESTROYED (pad));
  gst_object_unref (GST_OBJECT (pad));

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  pad2 = gst_pad_new ("padname2", GST_PAD_SRC);

  gst_pad_connect (pad2, pad);
  g_assert (GST_PAD_CONNECTED (pad2));
  g_assert (GST_PAD_CONNECTED (pad));

  gst_object_unref (GST_OBJECT (pad));
  g_assert (!GST_PAD_CONNECTED (pad2));
  g_assert (!GST_OBJECT_DESTROYED (pad2));
  gst_object_unref (GST_OBJECT (pad2));

  g_print ("pad unref effects on connect pad ok %ld\n", vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  pad2 = gst_pad_new ("padname2", GST_PAD_SRC);

  gst_pad_connect (pad2, pad);
  g_assert (GST_PAD_CONNECTED (pad2));
  g_assert (GST_PAD_CONNECTED (pad));

  gst_object_destroy (GST_OBJECT (pad2));
  g_assert (GST_OBJECT_DESTROYED (pad2));
  g_assert (!GST_OBJECT_DESTROYED (pad));
  g_assert (!GST_PAD_CONNECTED (pad));
  gst_object_unref (GST_OBJECT (pad2));
  g_assert (!GST_OBJECT_DESTROYED (pad));
  g_assert (!GST_PAD_CONNECTED (pad));
  gst_object_unref (GST_OBJECT (pad));

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  pad2 = gst_pad_new ("padname2", GST_PAD_SRC);

  gst_pad_connect (pad2, pad);
  g_assert (GST_PAD_CONNECTED (pad2));
  g_assert (GST_PAD_CONNECTED (pad));

  gst_object_destroy (GST_OBJECT (pad));
  g_assert (GST_OBJECT_DESTROYED (pad));
  g_assert (!GST_OBJECT_DESTROYED (pad2));
  g_assert (!GST_PAD_CONNECTED (pad2));
  gst_object_unref (GST_OBJECT (pad));
  g_assert (!GST_OBJECT_DESTROYED (pad2));
  g_assert (!GST_PAD_CONNECTED (pad2));
  gst_object_unref (GST_OBJECT (pad2));

  g_print ("pad destroy effects on connect pad ok %ld\n", vmsize()-usage1);

  for (i=0; i<iters;i++) {
    padtempl = gst_pad_template_new ("sink%d", GST_PAD_SINK, GST_PAD_SOMETIMES, NULL);
    gst_object_unref (GST_OBJECT (padtempl));
  }
  g_print ("%d padtemplates create/unref %ld\n", iters, vmsize()-usage1);

  for (i=0; i<iters;i++) {
    padtempl = gst_pad_template_new ("sink%d", GST_PAD_SINK, GST_PAD_SOMETIMES, NULL);
    pad = gst_pad_new_from_template (padtempl, "sink1");
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("%d pads create/unref from padtemplate %ld\n", iters, vmsize()-usage1);
  
  g_print ("leaked: %ld\n", vmsize()-usage1);

  return vmsize()-usage1;
}
