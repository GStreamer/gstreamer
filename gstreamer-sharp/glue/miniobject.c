// Thie file is mostly bits and pieces snipped from the gtk-sharp/glib/glue/object.c code 
// By Mike Kestner

#include <glib-object.h>
#include <gst/gstminiobject.h>

GType
gstsharp_get_type_id(GObject *obj)
{       
    return G_TYPE_FROM_INSTANCE(obj);
}

GType
gstsharp_register_type(gchar *name, GType parent)
{       
    GTypeQuery query;
    GTypeInfo info = { 0, NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL };

    g_type_query (parent, &query);

    info.class_size = query.class_size;
    info.instance_size = query.instance_size;

    return g_type_register_static(parent, name, &info, 0);
}
