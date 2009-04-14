#include <gst/gst.h>

guint
gstsharp_gst_caps_get_refcount_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstCaps, refcount);
}
