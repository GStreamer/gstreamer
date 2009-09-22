#include <gst/gst.h>

void
gstsharp_gst_buffer_set_data (GstBuffer * buffer, guint8 * new_data, guint size)
{
  g_return_if_fail (gst_buffer_is_writable (buffer));

  if (buffer->malloc_data && buffer->free_func)
    buffer->free_func (buffer->malloc_data);
  else if (buffer->malloc_data)
    g_free (buffer->malloc_data);

  buffer->malloc_data = buffer->data = new_data;
  buffer->size = size;
  buffer->free_func = g_free;
}

guint
gstsharp_gst_buffer_get_size_offset (void)
{
	return (guint)G_STRUCT_OFFSET (GstBuffer, size);
}

guint
gstsharp_gst_buffer_get_data_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstBuffer, data);
}

guint
gstsharp_gst_buffer_get_timestamp_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstBuffer, timestamp);
}

guint
gstsharp_gst_buffer_get_duration_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstBuffer, duration);
}

guint
gstsharp_gst_buffer_get_offset_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstBuffer, offset);
}

guint
gstsharp_gst_buffer_get_offset_end_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstBuffer, offset_end);
}
