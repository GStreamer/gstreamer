#include <gst/gst.h>

int main(int argc,char *argv[]) 
{
  GstElement *bin, *element;
  gint i=10000;

  free (malloc(8)); /* -lefence */

  gst_init (&argc, &argv);

  g_mem_chunk_info ();

  bin = gst_pipeline_new ("pipeline");

  while (i--)
  {
    fprintf (stderr, "+");

    element = gst_elementfactory_make ("tee", "tee");
    if (!element) 
      break;

    gst_element_request_pad_by_name (element, "src%d");

    gst_bin_add (GST_BIN (bin), element);
    gst_bin_remove (GST_BIN (bin), element);

  }
  fprintf (stderr, "+\n");

  gst_object_unref (GST_OBJECT (bin));

  g_mem_chunk_info ();
}
