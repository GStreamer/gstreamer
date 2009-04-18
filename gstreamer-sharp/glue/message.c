#include <gst/gst.h>

guint
gstsharp_gst_message_get_timestamp_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMessage, timestamp);
}

guint
gstsharp_gst_message_get_src_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstMessage, src);
}
