#include <gst/gst.h>

uint
gstsharp_gst_index_entry_get_id_description_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.id.description);
}

uint
gstsharp_gst_index_entry_get_assoc_nassocs_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.assoc.nassocs);
}

uint
gstsharp_gst_index_entry_get_assoc_assocs_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.assoc.assocs);
}

uint
gstsharp_gst_index_entry_get_assoc_flags_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.assoc.flags);
}

uint
gstsharp_gst_index_entry_get_object_key_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.object.key);
}

uint
gstsharp_gst_index_entry_get_object_type_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.object.type);
}

uint
gstsharp_gst_index_entry_get_object_object_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.object.object);
}

uint
gstsharp_gst_index_entry_get_format_format_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.format.format);
}

uint
gstsharp_gst_index_entry_get_format_key_offset (void)
{
  return (uint) G_STRUCT_OFFSET (GstIndexEntry, data.format.key);
}
