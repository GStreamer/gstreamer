/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsttype.c: Media-type management functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* TODO:
 * probably should set up a hash table for the type id's, since currently
 * it's a rather pathetic linear search.  Eventually there may be dozens
 * of id's, but in reality there are only so many instances of lookup, so
 * I'm not overly worried yet...
 */

#include "gst_private.h"

#include "gsttype.h"
#include "gstregistry.h"


/* global list of registered types */
static GList *_gst_types;
static guint16 _gst_maxtype;

static void 		gst_type_factory_class_init 	(GstTypeFactoryClass *klass);
static void 		gst_type_factory_init 		(GstTypeFactory *factory);

static GstCaps*		gst_type_type_find_dummy		(GstBuffer *buffer, gpointer priv);

static void 		gst_type_factory_unload_thyself 	(GstPluginFeature *feature);

static GstPluginFeatureClass *parent_class = NULL;
/* static guint gst_type_factory_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_type_factory_get_type (void)
{
  static GType typefactory_type = 0;

  if (!typefactory_type) {
    static const GTypeInfo typefactory_info = {
      sizeof (GstTypeFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_type_factory_class_init,
      NULL,
      NULL,
      sizeof(GstTypeFactory),
      0,
      (GInstanceInitFunc) gst_type_factory_init,
      NULL
    };
    typefactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE,
                                               "GstTypeFactory", &typefactory_info, 0);
  }
  return typefactory_type;
}

static void
gst_type_factory_class_init (GstTypeFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);

  gstpluginfeature_class->unload_thyself = GST_DEBUG_FUNCPTR (gst_type_factory_unload_thyself);

  _gst_types = NULL;
  _gst_maxtype = 1;		/* type 0 is undefined */

}

static void
gst_type_factory_init (GstTypeFactory *factory)
{
}

/**
 * gst_type_factory_new:
 * @definition: the definition to use
 *
 * Creata a new typefactory from the given definition.
 *
 * Returns: the new typefactory
 */
GstTypeFactory* 
gst_type_factory_new (GstTypeDefinition *definition)
{
  GstTypeFactory *factory;

  g_return_val_if_fail (definition != NULL, NULL);
  g_return_val_if_fail (definition->name != NULL, NULL);
  g_return_val_if_fail (definition->mime != NULL, NULL);

  factory = gst_type_factory_find (definition->name);

  if (!factory) {
    factory = GST_TYPE_FACTORY (g_object_new (GST_TYPE_TYPE_FACTORY, NULL));
  }


  GST_PLUGIN_FEATURE_NAME (factory) = g_strdup (definition->name);
  factory->mime = g_strdup (definition->mime);
  factory->exts = g_strdup (definition->exts);
  factory->typefindfunc = definition->typefindfunc;

  return factory;
}

/**
 * gst_type_register:
 * @factory: the type factory to register
 *
 * Register a new type factory to the system.
 *
 * Returns: the new type id
 */
guint16
gst_type_register (GstTypeFactory *factory)
{
  guint16 id;
  GstType *type;

  g_return_val_if_fail (factory != NULL, 0);

/*  GST_INFO (GST_CAT_TYPES,"type register %s", factory->mime); */
  id = gst_type_find_by_mime (factory->mime);

  if (!id) {
    type = g_new0 (GstType, 1);

    type->id =		_gst_maxtype++;
    type->mime =	factory->mime;
    type->exts =	factory->exts;
    type->factories = 	NULL;
    _gst_types =	g_list_prepend (_gst_types, type);

    id = type->id;

  } else {
    type = gst_type_find_by_id (id);
    /* now we want to try to merge the types and return the original */

    /* FIXME: do extension merging here, not that easy */

    /* if there is no existing typefind function, try to use new one  */
  }
  GST_DEBUG (GST_CAT_TYPES,"gsttype: %s(%p) gave new mime type '%s', id %d", 
		    GST_OBJECT_NAME (factory), factory, type->mime, type->id);
  type->factories = g_slist_prepend (type->factories, factory);

  return id;
}

static guint16 
gst_type_find_by_mime_func (const gchar *mime)
{
  GList *walk;
  GstType *type;
  gint typelen,mimelen;
  gchar *search, *found;

  g_return_val_if_fail (mime != NULL, 0);

  walk = _gst_types;
/*  GST_DEBUG (GST_CAT_TYPES,"searching for '%s'",mime); */
  mimelen = strlen (mime);
  while (walk) {
    type = (GstType *)walk->data;
    search = type->mime;
/*    GST_DEBUG (GST_CAT_TYPES,"checking against '%s'",search); */
    typelen = strlen (search);
    while ((search - type->mime) < typelen) {
      found = strstr (search, mime);
      /* if the requested mime is in the list */
      if (found) {
        if ((*(found + mimelen) == ' ') ||
            (*(found + mimelen) == ',') ||
            (*(found + mimelen) == '\0')) {
          return type->id;
        } else {
          search = found + mimelen;
        }
      } else
        search += mimelen;
    }
    walk = g_list_next (walk);
  }

  return 0;
}

/**
 * gst_type_find_by_mime:
 * @mime: the mime type to find
 *
 * Find the type id of a given mime type.
 *
 * Returns: the type id
 */
guint16
gst_type_find_by_mime (const gchar *mime)
{
  return gst_type_find_by_mime_func (mime);
}

/**
 * gst_type_find_by_ext:
 * @ext: the extension to find
 *
 * Find the type id of a given extention.
 *
 * Returns: the type id
 */
guint16
gst_type_find_by_ext (const gchar *ext)
{
  /* FIXME */
  g_warning ("gsttype: find_by_ext not implemented");
  return 0;
}

/**
 * gst_type_find_by_id:
 * @id: the type id to lookup
 *
 * Find the type of a given type id.
 *
 * Returns: the type
 */
GstType*
gst_type_find_by_id (guint16 id)
{
  GList *walk = _gst_types;
  GstType *type;

  while (walk) {
    type = (GstType *)walk->data;
    if (type->id == id)
      return type;
    walk = g_list_next (walk);
  }

  return NULL;
}

/**
 * gst_type_get_list:
 *
 * Return a list of all registered types.
 *
 * Returns: a list of GstTypes
 */
const GList*
gst_type_get_list (void)
{
  return _gst_types;
}

/**
 * gst_type_factory_find:
 * @name: the name of the typefactory to find
 *
 * Return the TypeFactory with the given name. 
 *
 * Returns: a GstTypeFactory with the given name;
 */
GstTypeFactory*
gst_type_factory_find (const gchar *name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_TYPE_FACTORY);
  if (feature)
    return GST_TYPE_FACTORY (feature);

  return NULL;
}

static void
gst_type_factory_unload_thyself (GstPluginFeature *feature)
{
  GstTypeFactory *factory;

  g_return_if_fail (GST_IS_TYPE_FACTORY (feature));

  factory = GST_TYPE_FACTORY (feature);

  if (factory->typefindfunc)
    factory->typefindfunc = gst_type_type_find_dummy;
}

static GstCaps*
gst_type_type_find_dummy (GstBuffer *buffer, gpointer priv)
{
  GstTypeFactory *factory = (GstTypeFactory *)priv;

  GST_DEBUG (GST_CAT_TYPES,"gsttype: need to load typefind function for %s", factory->mime);

  if (gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    if (factory->typefindfunc) {
       GstCaps *res = factory->typefindfunc (buffer, factory);
       if (res) 
         return res;
    }
  }

  return NULL;
}
