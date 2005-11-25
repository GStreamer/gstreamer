#include "gstwavpackcommon.h"
#include <string.h>

gboolean
gst_wavpack_read_header (WavpackHeader * header, guint8 * buf)
{
  g_memmove (header, buf, sizeof (WavpackHeader));
  little_endian_to_native (header, WavpackHeaderFormat);

  if (strncmp (header->ckID, "wvpk", 4))
    return FALSE;
  else
    return TRUE;
}
