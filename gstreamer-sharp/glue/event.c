#include <gst/gst.h>

guint
gstsharp_gst_event_get_timestamp_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstEvent, timestamp);
}

guint
gstsharp_gst_event_get_src_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstEvent, src);
}
