#include <gst/gst.h>

guint
gstsharp_gst_pad_get_stream_lock_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstPad, stream_rec_lock);
}
