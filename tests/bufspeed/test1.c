#include "gstbuffer.h"

int 
main (int argc, char *argv[])
{
  GstBuffer *buf;
  guint i;
  
  g_thread_init (NULL);
  gtk_init (&argc, &argv);
  _gst_buffer_initialize ();

  for (i=0; i<5000000; i++) {
    buf = gst_buffer_new ();
    gst_buffer_unref (buf);
  }

  return 0;
}
