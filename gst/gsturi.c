/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsturi.c: register URI handlers
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

#include "gst_private.h"

#include "gsturi.h"
#include "gstregistrypool.h"
#include "gstinfo.h"

static void 		gst_uri_handler_class_init 	(GstURIHandlerClass *klass);
static void 		gst_uri_handler_init 		(GstURIHandler *factory);

static GstPluginFeatureClass *parent_class = NULL;
/* static guint gst_uri_handler_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_uri_handler_get_type (void)
{
  static GType urihandler_type = 0;

  if (!urihandler_type) {
    static const GTypeInfo urihandler_info = {
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
    urihandler_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE,
                                               "GstURIHandler", &urihandler_info, 0);
  }
  return urihandler_type;
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
 * @name: the name of the feature
 * @uri: the uri to register
 * @longdesc: a description for this uri
 * @element: an element that can handle the uri
 * @property: the property on the element to set the uri
 *
 * Creates a plugin feature to register an element that can
 * handle the given uri on the given property.
 *
 * Returns: the new urihandler
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
 * @name: the name of the urihandler to find
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

/*
 * this is a straight copy from glib 2.2
 * remove this function when glib 2.2 is sufficiently widespread and
 * then change to using the regular g_str_has_prefix
 */
static gboolean
g_str_has_prefix_glib22 (gchar *haystack, gchar *needle)
{
  if (haystack == NULL && needle == NULL) {
    return TRUE;
  }
  if (haystack == NULL || needle == NULL) {
    return FALSE;
  }
  if (strncmp (haystack, needle, strlen (needle)) == 0) {
    return TRUE;
  }
  return FALSE;
}

/**
 * gst_uri_handler_uri_filter:
 * @feature: the feature to inspect
 * @uri: the name of the uri to match
 *
 * Check if the given pluginfeature is a uri hanler and that
 * it can handle the given uri.
 *
 * Returns: TRUE if the feature can handle the uri.
 */
gboolean
gst_uri_handler_uri_filter (GstPluginFeature *feature, const gchar *uri)
{
  if (G_OBJECT_TYPE (feature) == GST_TYPE_URI_HANDLER) {
    GstURIHandler *handler = GST_URI_HANDLER (feature);
  
    if (g_str_has_prefix_glib22 ((gchar *) uri, handler->uri)) {
      return TRUE;
    }
  }
  return FALSE;
}

/**
 * gst_uri_handler_find_by_uri:
 * @uri: the uri to find a handler for
 *
 * Find a URIHandler for the given uri.
 *
 * Returns: a GstURIHandler that can handle the given uri.
 */
GstURIHandler*
gst_uri_handler_find_by_uri (const gchar *uri)
{
  GList *walk;
  GstURIHandler *result = NULL;
  
  g_return_val_if_fail (uri != NULL, NULL);

  walk = gst_registry_pool_feature_filter (
		  (GstPluginFeatureFilter) gst_uri_handler_uri_filter, TRUE, (gpointer) uri);

  if (walk) {
    result = GST_URI_HANDLER (walk->data);
  }
  g_list_free (walk);

  return result;
}

/**
 * gst_uri_handler_create:
 * @handler: the uri handler to use
 * @name: the name of the element
 *
 * Create an element with the given name from the given handler.
 *
 * Returns: a new element associated with the handler.
 */
GstElement*
gst_uri_handler_create (GstURIHandler *handler, const gchar *name)
{
  GstElement *element = NULL;

  g_return_val_if_fail (handler != NULL, NULL);

  element = gst_element_factory_make (handler->element, name);

  return element;
}

/**
 * gst_uri_handler_make_by_uri:
 * @uri: the uri
 * @name: the name of the element
 *
 * Create an element with the given name that can handle the given
 * uri. This function will also use set the uri on the element.
 *
 * Returns: a new element that can handle the uri.
 */
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



