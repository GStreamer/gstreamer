#include <gst/controller/gstcontroller.h>

guint
gst__controllersharp_gst__controller_controller_get_properties_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstController, properties);
}

guint
gst__controllersharp_gst__controller_controller_get_object_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstController, object);
}
