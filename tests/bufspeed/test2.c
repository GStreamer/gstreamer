#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstBuffer *buf;
  guint i;

  gst_init (&argc, &argv);

  for (i = 0; i < 5000000; i++) {
    buf = gst_buffer_new ();
    gst_buffer_unref (buf);
  }

  return 0;
}
