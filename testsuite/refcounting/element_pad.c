#include <gst/gst.h>

#define ITERS 100000
#include "mem.h"

static void
print_pad_props (GstPad *pad)
{
  g_print ("name %s\n", gst_pad_get_name (pad));
  g_print ("flags 0x%08x\n", GST_FLAGS (pad));
}

static GstElement*
create_element (gchar *padname, GstPadDirection dir)
{
  GstElement *element;
  GstPad *pad;

  element = gst_element_new ();
  pad = gst_pad_new (padname, dir);
  gst_element_add_pad (element, pad);

  return element;
}

int
main (int argc, gchar *argv[])
{
  GstElement *element;
  GstElement *element2;
  GstPad *pad;
  long usage1;
  gint i;

  gst_init (&argc, &argv);

  g_print ("creating new element\n");
  element = gst_element_new ();
  usage1 = vmsize();

  pad = gst_pad_new ("sink", GST_PAD_SINK);
  print_pad_props (pad);
  gst_element_add_pad (element, pad);
  print_pad_props (pad);

  g_print ("unref new element %ld\n", vmsize()-usage1);
  gst_object_unref (GST_OBJECT (element));

  for (i=0; i<ITERS; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    gst_object_unref (GST_OBJECT (element));
  }
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  for (i=0; i<ITERS/2; i++) {
    element = create_element ("sink", GST_PAD_SINK);
    element2 = create_element ("src", GST_PAD_SRC);
    gst_element_connect (element, "sink", element2, "src");
    g_assert (GST_PAD_CONNECTED (gst_element_get_pad (element2, "src")));
    g_assert (GST_PAD_CONNECTED (gst_element_get_pad (element, "sink")));
    gst_object_unref (GST_OBJECT (element));
    g_assert (!GST_PAD_CONNECTED (gst_element_get_pad (element2, "src")));
    gst_object_unref (GST_OBJECT (element2));
  }
  g_print ("destroy/unref 100000 element %ld\n", vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return 0;
}
