#include <gst/gst.h>

uint
gstsharp_gst_task_get_cond_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstTask, cond);
}

uint
gstsharp_gst_task_get_running_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstTask, running);
}
