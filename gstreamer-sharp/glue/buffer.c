#include <gst/gst.h>

guint
gstsharp_gst_buffer_refcount(GstBuffer * buf) 
{
	return GST_MINI_OBJECT_REFCOUNT_VALUE(buf);
}
