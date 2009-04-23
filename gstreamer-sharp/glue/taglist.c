#include <gst/gst.h>

void
gstsharp_gst_tag_list_add_value (GstTagList * list, GstTagMergeMode mode,
    const gchar * tag, const GValue * v)
{
  gst_tag_list_add_values (list, mode, tag, v, NULL);
}
