#include <gst/gst.h>

guint
gstsharp_gst_structure_get_parent_refcount_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstStructure, parent_refcount);
}
