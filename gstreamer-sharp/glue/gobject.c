#include <glib-object.h>

gint
gstsharp_g_closure_sizeof (void)
{
  return sizeof (GClosure);
}

GType
gstsharp_g_type_from_instance (GTypeInstance * instance)
{
  return G_TYPE_FROM_INSTANCE (instance);
}
