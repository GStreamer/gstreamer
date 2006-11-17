#include <stdio.h>
#include <stdlib.h>

#include "rfb.h"

int
main (int argc, char *argv[])
{
  RfbDecoder *decoder;

  // int fd = 0;

  decoder = rfb_decoder_new ();

  rfb_decoder_connect_tcp (decoder, "127.0.0.1", 5901);
  // rfb_decoder_use_file_descriptor (decoder, fd);

  while (!decoder->inited)
    rfb_decoder_iterate (decoder);

  rfb_decoder_send_update_request (decoder, FALSE, 0, 0, 100, 100);

  while (1) {
    rfb_decoder_iterate (decoder);
  }

  return 0;
}
