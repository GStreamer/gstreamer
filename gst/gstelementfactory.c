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
#include <gst/gstplugin.h>


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
  factory->src_types = NULL;
  factory->sink_types = NULL;
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

  factory = gst_plugin_load_elementfactory(factory->name);

  g_return_val_if_fail(factory->type != 0, NULL);

  // create an instance of the element
  element = GST_ELEMENT(gtk_type_new(factory->type));
  g_assert(element != NULL);
  gst_object_ref(GST_OBJECT(element));

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

  gst_plugin_load_elementfactory(factoryname);
  factory = gst_elementfactory_find(factoryname);
  if (factory == NULL) return NULL;
  element = gst_elementfactory_create(factory,name);
  return element;
}

void gst_elementfactory_add_src(GstElementFactory *elementfactory, guint16 id) {
  guint type = id;

  elementfactory->src_types = g_list_prepend(elementfactory->src_types, GUINT_TO_POINTER(type));
}

void gst_elementfactory_add_sink(GstElementFactory *elementfactory, guint16 id) {
  guint type = id;

  elementfactory->sink_types = g_list_prepend(elementfactory->sink_types, GUINT_TO_POINTER(type));
}

xmlNodePtr gst_elementfactory_save_thyself(GstElementFactory *factory, xmlNodePtr parent) {
  GList *types;
  xmlNodePtr subtree;

  xmlNewChild(parent,NULL,"name",factory->name);
  xmlNewChild(parent,NULL,"longname", factory->details->longname);
  xmlNewChild(parent,NULL,"class", factory->details->class);
  xmlNewChild(parent,NULL,"description", factory->details->description);
  xmlNewChild(parent,NULL,"version", factory->details->version);
  xmlNewChild(parent,NULL,"author", factory->details->author);
  xmlNewChild(parent,NULL,"copyright", factory->details->copyright);

  types = factory->src_types;
  if (types) {
    subtree = xmlNewChild(parent,NULL,"sources",NULL);
    while (types) {
      guint16 typeid = GPOINTER_TO_UINT(types->data);
      GstType *type = gst_type_find_by_id(typeid);

      gst_type_save_thyself(type, subtree);

      types = g_list_next(types);
    }
  }
  types = factory->sink_types;
  if (types) {
    subtree = xmlNewChild(parent,NULL,"sinks",NULL);
    while (types) {
      guint16 typeid = GPOINTER_TO_UINT(types->data);
      GstType *type = gst_type_find_by_id(typeid);

      gst_type_save_thyself(type, subtree);

      types = g_list_next(types);
    }
  }

  return parent;
}

GstElementFactory *gst_elementfactory_load_thyself(xmlNodePtr parent) {
  GstElementFactory *factory = g_new0(GstElementFactory, 1);
  xmlNodePtr children = parent->childs;
  factory->details = g_new0(GstElementDetails, 1);
  factory->sink_types = NULL;
  factory->src_types = NULL;

  while (children) {
    if (!strcmp(children->name, "name")) {
      factory->name = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "longname")) {
      factory->details->longname = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "class")) {
      factory->details->class = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "description")) {
      factory->details->description = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "version")) {
      factory->details->version = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "author")) {
      factory->details->author = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "copyright")) {
      factory->details->copyright = g_strdup(xmlNodeGetContent(children));
    }
    if (!strcmp(children->name, "sources")) {
      guint16 typeid = gst_type_load_thyself(children);

      gst_type_add_src(typeid, factory);
    }
    if (!strcmp(children->name, "sinks")) {
      guint16 typeid = gst_type_load_thyself(children);

      gst_type_add_sink(typeid, factory);
    }

    children = children->next;
  }

  return factory;
}

