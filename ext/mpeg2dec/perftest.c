
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <gst/gst.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>

#include "output_video.h"
#include "gstmpeg2dec.h"


void
new_frame (char *buf, void *data, uint64_t timestamp)
{
  g_print (".");
}


int
main (int argc, char *argv[])
{
  int fd, count = 20000;
  mpeg2dec_t *decoder;
  char buffer[2048];

  fd = open (argv[1], O_RDONLY);

  gst_init (&argc, &argv);
  mpeg2_init (decoder, new_frame, NULL);

  while (read (fd, buffer, 2048) && count--) {
    mpeg2_decode_data (decoder, buffer, buffer + 2048);
  }
  g_print ("\n");
}
