#include <gst/gst.h>

guint
gstsharp_gst_index_entry_get_id_description_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.id.description);
}

guint
gstsharp_gst_index_entry_get_assoc_nassocs_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.assoc.nassocs);
}

guint
gstsharp_gst_index_entry_get_assoc_assocs_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.assoc.assocs);
}

guint
gstsharp_gst_index_entry_get_assoc_flags_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.assoc.flags);
}

guint
gstsharp_gst_index_entry_get_object_key_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.object.key);
}

guint
gstsharp_gst_index_entry_get_object_type_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.object.type);
}

guint
gstsharp_gst_index_entry_get_object_object_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.object.object);
}

guint
gstsharp_gst_index_entry_get_format_format_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.format.format);
}

guint
gstsharp_gst_index_entry_get_format_key_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstIndexEntry, data.format.key);
}
