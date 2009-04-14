#include <gst/gst.h>

const gchar *
gstsharp_gst_static_caps_get_string (const GstStaticCaps * caps)
{
  return caps->string;
}

GstStaticCaps *
gstsharp_gst_static_caps_new (const gchar * string)
{
  GstStaticCaps *caps = g_new0 (GstStaticCaps, 1);

  caps->string = g_strdup (string);

  return caps;
}
