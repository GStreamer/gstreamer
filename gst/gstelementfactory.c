/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gstelement.h>


/* global list of registered elementfactories */
GList* _gst_elementfactories;

void _gst_elementfactory_initialize() {
  _gst_elementfactories = NULL;
}

/**
 * gst_elementfactory_register:
 * @elementfactory: factory to register
 *
 * Adds the elementfactory to the global list, so it can be retrieved by
 * name.
 */
void gst_elementfactory_register(GstElementFactory *elementfactory) {
  g_return_if_fail(elementfactory != NULL);

  _gst_elementfactories = g_list_prepend(_gst_elementfactories,elementfactory);
}

/**
 * gst_elementfactory_find:
 * @name: name of factory to find
 *
 * Search for an elementfactory of the given name.
 *
 * Returns: #GstElementFactory if found, NULL otherwise
 */
GstElementFactory *gst_elementfactory_find(gchar *name) {
  GList *walk = _gst_elementfactories;
  GstElementFactory *factory;

  while (walk) {
    factory = (GstElementFactory *)(walk->data);
    if (!strcmp(name,factory->name))
      return factory;
    walk = g_list_next(walk);
  }

  return NULL;
}

/**
 * gst_elementfactory_get_list:
 *
 * Get the global list of elementfactories.
 *
 * Returns: <type>GList</type> of type #GstElementFactory
 */
GList *gst_elementfactory_get_list() {
  return _gst_elementfactories;
}


/**
 * gst_elementfactory_new:
 * @name: name of new elementfactory
 * @type: GtkType of new element
 * @details: #GstElementDetails structure with element details
 *
 * Create a new elementfactory capable of insantiating objects of the
 * given type.
 *
 * Returns: new elementfactory
 */
GstElementFactory *gst_elementfactory_new(gchar *name,GtkType type,
                                          GstElementDetails *details) {
  GstElementFactory *factory = g_new0(GstElementFactory, 1);
  factory->name = g_strdup(name);
  factory->type = type;
  factory->details = details;
  return factory;
}

/**
 * gst_elementfactory_create:
 * @factory: factory to instantiate
 * @name: name of new element
 *
 * Create a new element of the type defined by the given elementfactory.
 * It wll be given the name supplied, since all elements require a name as
 * their first argument.
 *
 * Returns: new #GstElement
 */
GstElement *gst_elementfactory_create(GstElementFactory *factory,
                                      gchar *name) {
  GstElement *element;
  GstElementClass *oclass;

  g_return_val_if_fail(factory != NULL, NULL);
  g_return_val_if_fail(factory->type != 0, NULL);

  // create an instance of the element
  element = GST_ELEMENT(gtk_type_new(factory->type));
  g_assert(element != NULL);

  // attempt to set the elemenfactory class pointer if necessary
  oclass = GST_ELEMENT_CLASS(GTK_OBJECT(element)->klass);
  if (oclass->elementfactory == NULL)
    oclass->elementfactory = factory;

  gst_element_set_name(GST_ELEMENT(element),name);

  return element;
}

GstElement *gst_elementfactory_make(gchar *factoryname,gchar *name) {
  GstElementFactory *factory;
  GstElement *element;

  factory = gst_elementfactory_find(factoryname);
  if (factory == NULL) return NULL;
  element = gst_elementfactory_create(factory,name);
  return element;
}
