#include <gst/gst.h>

int 
main (int argc, char *argv[])
{
  GstBuffer *buf;
  guint i;
  
  gst_init (&argc, &argv);

  for (i=0; i<5000000; i++) {
    /* buffer API has changed, use default pool with 1024 size buffers */
    buf = gst_buffer_new (NULL, 1024);
    /*
    gst_buffer_unref (buf);
    */
    gst_data_unref (GST_DATA (buf));
  }

  return 0;
}
