/* GStreamer
 * harness for smokedec
 */


#include <stdlib.h>
#include <glib.h>
#include <gst/gst.h>
//#include "smokecodec.h"
//#include "smokeformat.h"
//#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const char *data, size_t size)
{
  //FuzzedDataProvider fdata(data, size);
  SmokeCodecInfo *info;
  unsigned int frame_size;
  unsigned char *output = 0;

  smokecodec_decode_new(&info);

  smokecodec_decode(info, (const unsigned char*) data, (unsigned int) frame_size, output);
  //smokecodec_encode()?
  return 0;
}
