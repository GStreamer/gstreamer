#include <gst/gst.h>

guint
gstsharp_gst_task_get_cond_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstTask, cond);
}
