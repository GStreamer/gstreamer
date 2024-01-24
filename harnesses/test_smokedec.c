/* GStreamer
 * harness for smokedec
 */


#include <stdlib.h>
#include <glib.h>
#include <gst/gst.h>
#include "smokecodec.h"
#include "smokeformat.h"

int LLVMFuzzerTestOneInput(const char *data, size_t size)
{
  SmokeCodecInfo *info;
  unsigned int frame_size = size;
  unsigned char *output = 0;

  smokecodec_decode_new(&info);

  smokecodec_decode(info, (const unsigned char*) data, (unsigned int) frame_size, output);
  return 0;
}
