#include <unistd.h>
#include <gst/gst.h>



static gpointer
iterate_bin (GstBin * bin)
{
  while (TRUE) {
    gst_bin_iterate (bin);
  }
  return NULL;
}

int
main (int argc, char **argv)
{
  gint i;
  GstElement *bin;

  gst_init (&argc, &argv);

  for (i = 0; i < 20; i++) {
    bin = gst_element_factory_make ("bin", "bin");
    gst_scheduler_factory_make (NULL, GST_ELEMENT (bin));

    g_thread_create ((GThreadFunc) iterate_bin, bin, TRUE, NULL);
  }

  sleep (5);

  return 0;
}
