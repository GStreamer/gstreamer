#include <gst/gst.h>
#include "mem.h"

#define VM_THRES 1000

typedef struct
{
  gchar *desc;
  gint src_data;
  gint src_sizetype;
  gint src_filltype;
  gboolean src_silent;

  gint bs_sizetype;
  gint bs_accesstype;
  gboolean bs_silent;

  gboolean sink_dump;
  gboolean sink_silent;
} TestParam;

static TestParam params[] = {
  {"fixed size src, fixed size _read", 			1, 2, 5, TRUE,   1, 1, TRUE,   FALSE, TRUE },
  {"fixed size src, random size _read", 		1, 2, 5, TRUE,   2, 1, TRUE,   FALSE, TRUE },
  {"random size src, fixed size _read", 		1, 3, 5, TRUE,   1, 1, TRUE,   FALSE, TRUE },
  {"random size src, random size _read", 		1, 3, 5, TRUE,   2, 1, TRUE,   FALSE, TRUE },
  {"fixed size subbuffer, fixed size _read", 		2, 2, 5, TRUE,   1, 1, TRUE,   FALSE, TRUE },
  {"fixed size subbuffer, random size _read", 		2, 2, 5, TRUE,   2, 1, TRUE,   FALSE, TRUE },
  {"random size subbuffer, fixed size _read", 		2, 3, 5, TRUE,   1, 1, TRUE,   FALSE, TRUE },
  {"random size subbuffer, random size _read", 		2, 3, 5, TRUE,   2, 1, TRUE,   FALSE, TRUE },
  {"fixed size src, fixed size _peek_read", 		1, 2, 5, TRUE,   1, 2, TRUE,   FALSE, TRUE },
  {"fixed size src, random size _peek_read", 		1, 2, 5, TRUE,   2, 2, TRUE,   FALSE, TRUE },
  {"random size src, fixed size _peek_read", 		1, 3, 5, TRUE,   1, 2, TRUE,   FALSE, TRUE },
  {"random size src, random size _peek_read", 		1, 3, 5, TRUE,   2, 2, TRUE,   FALSE, TRUE },
  {"fixed size subbuffer, fixed size _peek_read", 	2, 2, 5, TRUE,   1, 2, TRUE,   FALSE, TRUE },
  {"fixed size subbuffer, random size _peek_read", 	2, 2, 5, TRUE,   2, 2, TRUE,   FALSE, TRUE },
  {"random size subbuffer, fixed size _peek_read", 	2, 3, 5, TRUE,   1, 2, TRUE,   FALSE, TRUE },
  {"random size subbuffer, random size _peek_read", 	2, 3, 5, TRUE,   2, 2, TRUE,   FALSE, TRUE },
  {"fixed size src, fixed size _peek_readrand", 	1, 2, 5, TRUE,   1, 3, TRUE,   FALSE, TRUE },
  {"fixed size src, random size _peek_readrand", 	1, 2, 5, TRUE,   2, 3, TRUE,   FALSE, TRUE },
  {"random size src, fixed size _peek_readrand", 	1, 3, 5, TRUE,   1, 3, TRUE,   FALSE, TRUE },
  {"random size src, random size _peek_readrand", 	1, 3, 5, TRUE,   2, 3, TRUE,   FALSE, TRUE },
  {"fixed size subbuffer, fixed size _peek_readrand", 	2, 2, 5, TRUE,   1, 3, TRUE,   FALSE, TRUE },
  {"fixed size subbuffer, random size _peek_readrand", 	2, 2, 5, TRUE,   2, 3, TRUE,   FALSE, TRUE },
  {"random size subbuffer, fixed size _peek_readrand", 	2, 3, 5, TRUE,   1, 3, TRUE,   FALSE, TRUE },
  {"random size subbuffer, random size _peek_readrand",	2, 3, 5, TRUE,   2, 3, TRUE,   FALSE, TRUE },
  {NULL, 2, 3, 5, TRUE,   2, 2, TRUE,   FALSE, TRUE }
};

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
  gint prev_percent = -1;

  count = 0;
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (iters) {
    gint newvm = vmsize();
    gint percent;

    percent = (gint)((maxiters-iters+1)*100.0/maxiters);

    if (percent != prev_percent || newvm - vm > VM_THRES) {
      g_print ("\r%d (delta %d) %.3d%%               ", newvm, newvm - vm, percent);
      prev_percent = percent;
      vm = newvm;
    }
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
  gint testnum = 0;

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

  while (params[testnum].desc) {
    g_print ("\n\nrunning test %d:\n", testnum+1);
    g_print ("%s\n", params[testnum].desc);

    g_object_set (G_OBJECT (src), "data", params[testnum].src_data, 
		                  "sizetype", params[testnum].src_sizetype, 
				  "filltype", params[testnum].src_filltype, 
				  "silent", params[testnum].src_silent, NULL);

    g_object_set (G_OBJECT (bs),  "sizetype", params[testnum].bs_sizetype, 
		    		  "accesstype", params[testnum].bs_accesstype, 
				  "silent", TRUE, NULL);

    g_object_set (G_OBJECT (sink), "dump", params[testnum].sink_dump, 
		     		   "silent", params[testnum].sink_silent, NULL);

    run_test (GST_BIN (pipeline), 50000);

    testnum++;
  }

  g_print ("\n\ndone\n");

  return 0;

}
