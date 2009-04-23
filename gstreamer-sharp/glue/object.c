#include <gst/gst.h>

guint
gstsharp_gst_object_get_lock_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstObject, lock);
}
