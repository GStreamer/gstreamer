#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *bin, *element;
  gint i = 1000;
  gint step = 100;


  free (malloc (8));            /* -lefence */

  gst_init (&argc, &argv);

  g_mem_chunk_info ();

  bin = gst_pipeline_new ("pipeline");

  while (i--) {
    GstPad *pad;

    if (i % step == 0)
      fprintf (stderr, "\r%10d", i);

    element = gst_element_factory_make ("tee", "tee");
    if (!element)
      break;

    pad = gst_element_get_request_pad (element, "src%d");

    gst_bin_add (GST_BIN (bin), element);
    gst_bin_remove (GST_BIN (bin), element);

  }
  fprintf (stderr, "+\n");

  gst_object_unref (GST_OBJECT (bin));

  g_mem_chunk_info ();
  return 0;
}
