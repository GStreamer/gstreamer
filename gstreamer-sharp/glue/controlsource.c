#include <gst/gst.h>
#include <gst/controller/gstcontrolsource.h>

guint
gst__controllersharp_gst__controller_controlsource_get_get_value_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstControlSource, get_value);
}

const gchar *__gtype_prefix = "__gtksharp_";
#define HAS_PREFIX(a) (*((guint64 *)(a)) == *((guint64 *) __gtype_prefix))

static GObjectClass *
get_threshold_class (GObject * obj)
{
  GObjectClass *klass;
  GType gtype = G_TYPE_FROM_INSTANCE (obj);

  while (HAS_PREFIX (g_type_name (gtype)))
    gtype = g_type_parent (gtype);
  klass = g_type_class_peek (gtype);
  if (klass == NULL)
    klass = g_type_class_ref (gtype);
  return klass;
}

gboolean
gst__controllersharp_gst__controller_controlsource_base_bind (GstControlSource *
    csource, GParamSpec * pspec)
{
  GstControlSourceClass *parent =
      (GstControlSourceClass *) get_threshold_class (G_OBJECT (csource));
  if (parent->bind)
    return parent->bind (csource, pspec);
  return FALSE;
}

void
gst__controllersharp_gst__controller_controlsource_override_bind (GType gtype,
    gpointer cb)
{
  GstControlSourceClass *klass = g_type_class_peek (gtype);
  if (!klass)
    klass = g_type_class_ref (gtype);
  ((GstControlSourceClass *) klass)->bind = cb;
}
