#include <gst/gst.h>

#define ITERS 100000
#include "mem.h"

static void
print_pad_props (GstPad *pad)
{
  g_print ("name %s\n", gst_pad_get_name (pad));
  g_print ("flags 0x%08x\n", GST_FLAGS (pad));
}

int
main (int argc, gchar *argv[])
{
  GstPad *pad;
  GstPad *pad2;
  GstPadTemplate *padtempl;
  long usage1;
  gint i;

  gst_init (&argc, &argv);

  g_print ("creating new pad\n");
  pad = gst_pad_new ("padname", GST_PAD_SINK);
  usage1 = vmsize();
  print_pad_props (pad);
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));

  g_print ("creating new pad\n");
  pad = gst_pad_new ("padname", GST_PAD_SINK);
  g_assert (GST_OBJECT_FLOATING (pad));
  print_pad_props (pad);
  g_print ("sink new pad %ld\n", vmsize()-usage1);
  gst_object_ref (GST_OBJECT (pad));
  gst_object_sink (GST_OBJECT (pad));
  g_assert (!GST_OBJECT_FLOATING (pad));
  print_pad_props (pad);
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));

  for (i=0; i<ITERS;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("unref 100000 pad %ld\n", vmsize()-usage1);

  g_print ("creating new pad\n");
  pad = gst_pad_new ("padname", GST_PAD_SINK);
  g_assert (!GST_OBJECT_DESTROYED (pad));
  print_pad_props (pad);
  g_print ("destroy new pad %ld\n", vmsize()-usage1);
  gst_object_destroy (GST_OBJECT (pad));
  g_assert (GST_OBJECT_DESTROYED (pad));
  print_pad_props (pad);
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));
  
  for (i=0; i<ITERS;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_destroy (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("destroy/unref 100000 pad %ld\n", vmsize()-usage1);

  g_print ("creating new pad\n");
  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_ref (GST_OBJECT (pad));
  print_pad_props (pad);
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));
  
  for (i=0; i<ITERS;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("destroy/unref 100000 pad %ld\n", vmsize()-usage1);

  g_print ("creating new pad\n");
  pad = gst_pad_new ("padname", GST_PAD_SINK);
  gst_object_ref (GST_OBJECT (pad));
  print_pad_props (pad);
  gst_object_destroy (GST_OBJECT (pad));
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));
  g_print ("unref new pad %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (pad));
  
  for (i=0; i<ITERS;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_object_destroy (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("destroy/unref 100000 pad %ld\n", vmsize()-usage1);

  for (i=0; i<ITERS;i++) {
    pad = gst_pad_new ("padname", GST_PAD_SINK);
    gst_object_ref (GST_OBJECT (pad));
    gst_pad_set_name (pad, "testing123");
    gst_object_destroy (GST_OBJECT (pad));
    gst_pad_set_name (pad, "testing123");
    gst_object_unref (GST_OBJECT (pad));
    gst_object_unref (GST_OBJECT (pad));
  }
  g_print ("destroy/unref 100000 pad %ld\n", vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  for (i=0; i<ITERS;i++) {
    gst_pad_set_name (pad, "testing");
  }
  gst_object_unref (GST_OBJECT (pad));
  g_print ("destroy/unref 100000 pad %ld\n", vmsize()-usage1);

  g_print ("connecting pad %ld\n", vmsize()-usage1);

  pad = gst_pad_new ("padname", GST_PAD_SINK);
  pad2 = gst_pad_new ("padname2", GST_PAD_SRC);

  gst_pad_connect (pad, pad2);
  gst_pad_disconnect (pad, pad2);
  gst_pad_connect (pad2, pad);
  gst_pad_disconnect (pad2, pad);

  g_print ("padtemplates create/destroy %ld\n", vmsize()-usage1);

  for (i=0; i<ITERS;i++) {
    padtempl = gst_padtemplate_new ("sink%d", GST_PAD_SINK, GST_PAD_SOMETIMES, NULL);
    gst_object_unref (GST_OBJECT (padtempl));
  }

  g_print ("padtemplates create/destroy on pad %ld\n", vmsize()-usage1);

  for (i=0; i<ITERS;i++) {
    padtempl = gst_padtemplate_new ("sink%d", GST_PAD_SINK, GST_PAD_SOMETIMES, NULL);
    pad = gst_pad_new_from_template (padtempl, "sink1");
    gst_object_unref (GST_OBJECT (pad));
  }
  
  g_print ("leaked: %ld\n", vmsize()-usage1);

  return 0;
}
