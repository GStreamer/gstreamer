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

#include "gsturi.h"
#include "gstregistry.h"
#include "gstlog.h"


static void 		gst_uri_handler_class_init 	(GstURIHandlerClass *klass);
static void 		gst_uri_handler_init 		(GstURIHandler *factory);

static GstPluginFeatureClass *parent_class = NULL;
/* static guint gst_uri_handler_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_uri_handler_get_type (void)
{
  static GType typefactory_type = 0;

  if (!typefactory_type) {
    static const GTypeInfo typefactory_info = {
      sizeof (GstURIHandlerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_uri_handler_class_init,
      NULL,
      NULL,
      sizeof(GstURIHandler),
      0,
      (GInstanceInitFunc) gst_uri_handler_init,
      NULL
    };
    typefactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE,
                                               "GstURIHandler", &typefactory_info, 0);
  }
  return typefactory_type;
}

static void
gst_uri_handler_class_init (GstURIHandlerClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);
}

static void
gst_uri_handler_init (GstURIHandler *factory)
{
}

/**
 * gst_uri_handler_new:
 * @definition: the definition to use
 *
 * Creata a new typefactory from the given definition.
 *
 * Returns: the new typefactory
 */
GstURIHandler*
gst_uri_handler_new (const gchar *name, 
		     const gchar *uri, const gchar *longdesc, 
		     const gchar *element, gchar *property)
{
  GstURIHandler *factory;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (uri != NULL, NULL);
  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (property != NULL, NULL);

  factory = gst_uri_handler_find (name);

  if (!factory) {
    factory = GST_URI_HANDLER (g_object_new (GST_TYPE_URI_HANDLER, NULL));
  }

  GST_PLUGIN_FEATURE_NAME (factory) = g_strdup (name);
  factory->uri = g_strdup (uri);
  factory->longdesc = g_strdup (longdesc);
  factory->element = g_strdup (element);
  factory->property = g_strdup (property);

  return factory;
}

/**
 * gst_uri_handler_find:
 * @name: the name of the typefactory to find
 *
 * Return the URIHandler with the given name. 
 *
 * Returns: a GstURIHandler with the given name;
 */
GstURIHandler*
gst_uri_handler_find (const gchar *name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_URI_HANDLER);
  if (feature)
    return GST_URI_HANDLER (feature);

  return NULL;
}

GstURIHandler*
gst_uri_handler_find_by_uri (const gchar *uri)
{
  GList *walk, *orig;
  GstURIHandler *handler = NULL;
  
  g_return_val_if_fail (uri != NULL, NULL);

  orig = walk = gst_registry_pool_feature_list (GST_TYPE_URI_HANDLER);

  while (walk) {
    handler = GST_URI_HANDLER (walk->data);

    if (g_str_has_prefix (uri, handler->uri))
      break;

    walk = g_list_next (walk);
  }
  g_list_free (orig);

  return handler;
}

GstElement*
gst_uri_handler_create (GstURIHandler *factory, const gchar *name)
{
  GstElement *element = NULL;

  g_return_val_if_fail (factory != NULL, NULL);

  element = gst_element_factory_make (factory->element, name);

  return element;
}

GstElement*
gst_uri_handler_make_by_uri (const gchar *uri, const gchar *name)
{
  GstElement *element = NULL;
  GstURIHandler *handler;

  g_return_val_if_fail (uri != NULL, NULL);

  handler = gst_uri_handler_find_by_uri (uri);
  if (handler) {
    element = gst_uri_handler_create (handler, name);
    if (element) {
      g_object_set (G_OBJECT (element), handler->property, uri, NULL);
    }
  }
  return element;
}



