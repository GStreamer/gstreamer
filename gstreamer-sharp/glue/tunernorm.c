#include <gst/interfaces/tunernorm.h>

guint
gst__interfacessharp_gst__interfaces_tunernorm_get_framerate_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstTunerNorm, framerate);
}
