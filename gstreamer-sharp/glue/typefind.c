#include <gst/gst.h>

typedef guint8 *(*PeekFunction) (gpointer data, gint64 offset, guint size);
typedef void (*SuggestFunction) (gpointer data, guint probabilty,
    GstCaps * caps);
typedef guint64 (*GetLengthFunction) (gpointer data);

GstTypeFind *
gstsharp_gst_type_find_new (PeekFunction peek, SuggestFunction suggest,
    GetLengthFunction get_length)
{
  GstTypeFind *ret = g_new0 (GstTypeFind, 1);

  ret->peek = peek;
  ret->suggest = suggest;
  ret->get_length = get_length;

  return ret;
}
