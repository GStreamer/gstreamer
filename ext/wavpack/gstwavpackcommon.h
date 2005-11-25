#include <gst/gst.h>
#include <wavpack/wavpack.h>

gboolean gst_wavpack_read_header (WavpackHeader *header, guint8 *buf);
