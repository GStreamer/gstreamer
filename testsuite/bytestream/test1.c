#include <gst/gst.h>
#include "mem.h"

#define VM_THRES 1000

static guint8 count;

static void
handoff (GstElement *element, GstBuffer *buf, GstPad *pad, gpointer data)
{
  if (GST_IS_BUFFER (buf)) {
    gint i;
    guint8 *ptr = GST_BUFFER_DATA (buf);
    
    for (i=0; i<GST_BUFFER_SIZE (buf); i++) {
      if (*ptr++ != count++) {
	g_print ("data error!\n");
	return;
      }
    }
  }
  else {
    g_print ("not a buffer ! %p\n", buf);
  }
}

static void 
run_test (GstBin *pipeline, gint iters)
{
  gint vm = 0;
  gint maxiters = iters;

  count = 0;
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (iters) {
    gint newvm = vmsize();

    if (newvm - vm > VM_THRES) {
      g_print ("\r%d (delta %d)              ", newvm, newvm - vm);
      vm = newvm;
    }
    g_print ("\b\b\b\b\b\b%.3d%%  ", (gint)((maxiters-iters+1)*100.0/maxiters));
    gst_bin_iterate (pipeline);

    if (iters > 0) iters--;
  }
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}

int
main (int argc, char *argv[]) 
{
  GstElement *src;
  GstElement *sink;
  GstElement *bs;
  GstElement *pipeline;
  gint i, testnum = 1;

  gst_init (&argc, &argv);

  pipeline = gst_elementfactory_make ("pipeline", "pipeline");
  g_assert (pipeline);

  src = gst_elementfactory_make ("fakesrc", "src");
  g_assert (src);

  sink = gst_elementfactory_make ("fakesink", "sink");
  g_assert (sink);
  g_signal_connect (G_OBJECT (sink), "handoff", G_CALLBACK (handoff), NULL);

  bs = gst_elementfactory_make ("bstest", "bs");
  g_assert (bs);

  gst_element_connect (src, "src", bs, "sink");
  gst_element_connect (bs, "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), bs);
  gst_bin_add (GST_BIN (pipeline), sink);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("fixed size src, fixed size _read:\n");
  g_object_set (G_OBJECT (src), "data", 1, "sizetype", 2, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 1, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("fixed size src, random size _read:\n");
  g_object_set (G_OBJECT (src), "data", 1, "sizetype", 2, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 2, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("random size src, fixed size _read:\n");
  g_object_set (G_OBJECT (src), "data", 1, "sizetype", 3, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 1, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("random size src, random size _read:\n");
  g_object_set (G_OBJECT (src), "data", 1, "sizetype", 3, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 2, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  
  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("fixed size src as subbuffer, fixed size _read:\n");
  g_object_set (G_OBJECT (src), "data", 2, "sizetype", 2, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 1, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("fixed size src as subbuffer, random size _read:\n");
  g_object_set (G_OBJECT (src), "data", 2, "sizetype", 2, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 2, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("random size src as subbuffer, fixed size _read:\n");
  g_object_set (G_OBJECT (src), "data", 2, "sizetype", 3, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 1, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\nrunning test %d:\n", testnum++);
  g_print ("random size src as subbuffer, random size _read:\n");
  g_object_set (G_OBJECT (src), "data", 2, "sizetype", 3, "filltype", 5, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (bs),  "sizetype", 2, "silent", TRUE, NULL);
  g_object_set (G_OBJECT (sink), "dump", FALSE, "silent", TRUE, NULL);
  run_test (GST_BIN (pipeline), 50000);

  g_print ("\n\ndone\n");

}
