#include <gst/gst.h>

#define TAILLE 100

int
main (int argc, char *argv[])
{
  GstElement *bin, *src, *dec, *sink;
  int i, j;

  gst_init (&argc, &argv);

  free (malloc (8));		/* -lefence */

  for (i = 0; i < TAILLE; i++) {
    bin = gst_pipeline_new ("pipeline");
    src = gst_element_factory_make ("fakesrc", "source");
    dec = gst_element_factory_make ("identity", "decoder");
    sink = gst_element_factory_make ("fakesink", "sink");
    gst_bin_add_many (GST_BIN (bin), src, dec, sink, NULL);
    gst_element_link_many (src, dec, sink, NULL);
    gst_element_set_state (bin, GST_STATE_PLAYING);
    for (j = 0; j < 30; j++)
      gst_bin_iterate (GST_BIN (bin));
    gst_element_set_state (bin, GST_STATE_PAUSED);
  }

  return 0;
}
