#include <gst/gst.h>

uint
gstsharp_gst_pad_get_stream_lock_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstPad, stream_rec_lock);
}
