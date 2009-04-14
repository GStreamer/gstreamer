#include <gst/gst.h>

const gchar *
gstsharp_gst_static_pad_template_get_name_template (const GstStaticPadTemplate *templ) {
  return templ->name_template;
}

GstPadDirection
gstsharp_gst_static_pad_template_get_direction (const GstStaticPadTemplate *templ) {
  return templ->direction;
}

GstPadPresence
gstsharp_gst_static_pad_template_get_presence (const GstStaticPadTemplate *templ) {
  return templ->presence;
}

GstStaticPadTemplate *
gstsharp_gst_static_pad_template_new (const gchar *name_template, GstPadDirection direction, GstPadPresence presence, const gchar *caps) {
  GstStaticPadTemplate *ret = g_new0 (GstStaticPadTemplate, 1);
  ret->name_template = g_strdup (name_template);
  ret->direction = direction;
  ret->presence = presence;
  ret->static_caps.string = g_strdup (caps);

  return ret;
}
