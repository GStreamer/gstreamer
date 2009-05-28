#include <gst/gst.h>
#include <gst/base/gstadapter.h>

GstBuffer *
gstsharp_gst_adapter_peek_buffer (GstAdapter *adapter, guint size) {
  GstBuffer *ret = gst_buffer_new_and_try_alloc (size);

  if (ret == NULL)
    return NULL;

  gst_adapter_copy (adapter, GST_BUFFER_DATA (ret), 0, size);
  return ret;
}
