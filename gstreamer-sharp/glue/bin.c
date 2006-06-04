#include <gst/gstbin.h>
#include <gst/gstpipeline.h>
#include <gst/gstsegment.h>

guint
gstsharp_gst_bin_get_children_offset (void);

guint gstsharp_gst_bin_get_children_offset (void) {
	return (guint)G_STRUCT_OFFSET (GstBin, children);
}
