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

/* TODO:
 * probably should set up a hash table for the type id's, since currently
 * it's a rather pathetic linear search.  Eventually there may be dozens
 * of id's, but in reality there are only so many instances of lookup, so
 * I'm not overly worried yet...
 */

#include <string.h>

#include "gstdebug.h"
#include "gsttype.h"
#include "gstplugin.h"

/* global list of registered types */
GList *_gst_types;
guint16 _gst_maxtype;

struct _GstTypeFindInfo {
  GstTypeFindFunc typefindfunc; /* typefind function */

  GstPlugin *plugin;            /* the plugin with this typefind function */
};

static GstCaps* 	gst_type_typefind_dummy		(GstBuffer *buffer, gpointer priv);

void 
_gst_type_initialize (void) 
{
  _gst_types = NULL;
  _gst_maxtype = 1;		/* type 0 is undefined */
}

/**
 * gst_type_register:
 * @factory: the type factory to register
 *
 * register a new type factory to the system
 *
 * Returns: the new type id
 */
guint16 
gst_type_register (GstTypeFactory *factory) 
{
  guint16 id;
  GstType *type;

  g_return_val_if_fail (factory != NULL, 0);

  DEBUG("type register %s\n", factory->mime);
  id = gst_type_find_by_mime (factory->mime);
  
  if (!id) {
    type = g_new0 (GstType, 1);

    type->id = 		_gst_maxtype++;
    type->mime = 	factory->mime;
    type->exts = 	factory->exts;
    _gst_types = 	g_list_prepend (_gst_types, type);

    id = type->id;

  } else {
    type = gst_type_find_by_id (id);
    /* now we want to try to merge the types and return the original */

    /* FIXME: do extension merging here, not that easy */

    /* if there is no existing typefind function, try to use new one  */
  }
  if (factory->typefindfunc) {
    type->typefindfuncs = g_slist_prepend (type->typefindfuncs, factory->typefindfunc);
  }

  return id;
}

static 
guint16 gst_type_find_by_mime_func (gchar *mime) 
{
  GList *walk;
  GstType *type;
  gint typelen,mimelen;
  gchar *search, *found;

  g_return_val_if_fail (mime != NULL, 0);

  walk = _gst_types;
//  DEBUG("searching for '%s'\n",mime);
  mimelen = strlen (mime);
  while (walk) {
    type = (GstType *)walk->data;
    search = type->mime;
//    DEBUG("checking against '%s'\n",search);
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
 * find the type id of a given mime type
 *
 * Returns: the type id
 */
guint16 
gst_type_find_by_mime (gchar *mime) 
{
  return gst_type_find_by_mime_func (mime);
}

/**
 * gst_type_find_by_ext:
 * @ext: the extension to find
 *
 * find the type id of a given extention
 *
 * Returns: the type id
 */
guint16 
gst_type_find_by_ext (gchar *ext) 
{
  //FIXME
  g_warning ("gsttype: find_by_ext not implemented");
  return 0;
}

/**
 * gst_type_find_by_id:
 * @id: the type id to lookup
 *
 * find the type of a given type id
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
 * return a list of all registered types
 *
 * Returns: a list of GstTypes
 */
GList*
gst_type_get_list (void) 
{
  return _gst_types;
}

/**
 * gst_typefactory_save_thyself:
 * @factory: the type factory to save
 * @parent: the parent node to save into
 *
 * save a typefactory into an XML representation
 *
 * Returns: the new xmlNodePtr
 */
xmlNodePtr 
gst_typefactory_save_thyself (GstTypeFactory *factory, xmlNodePtr parent) 
{
  xmlNewChild (parent, NULL, "mime", factory->mime);
  if (factory->exts) {
    xmlNewChild (parent, NULL, "extensions", factory->exts);
  }
  if (factory->typefindfunc) {
    xmlNewChild (parent, NULL, "typefind", NULL);
  }
  
  return parent;
}

static GstCaps * 
gst_type_typefind_dummy (GstBuffer *buffer, gpointer priv)
{
  GstType *type = (GstType *)priv;
  guint16 typeid;
  GSList *funcs;

  DEBUG ("gsttype: need to load typefind function for %s\n", type->mime);

  type->typefindfuncs = NULL;
  gst_plugin_load_typefactory (type->mime);
  typeid = gst_type_find_by_mime (type->mime);
  type = gst_type_find_by_id (typeid);

  funcs = type->typefindfuncs;

  while (funcs) {
    GstTypeFindFunc func = (GstTypeFindFunc) funcs->data;

    if (func) {
       GstCaps *res = func (buffer, type);
       if (res) return res;
    }

    funcs = g_slist_next (funcs);
  }

  return FALSE;
}

/**
 * gst_typefactory_load_thyself:
 * @parent: the parent node to load from
 *
 * load a typefactory from an XML representation
 *
 * Returns: the new typefactory
 */
GstTypeFactory*
gst_typefactory_load_thyself (xmlNodePtr parent) 
{

  GstTypeFactory *factory = g_new0 (GstTypeFactory, 1);
  xmlNodePtr field = parent->childs;
  factory->typefindfunc = NULL;

  while (field) {
    if (!strcmp (field->name, "mime")) {
      factory->mime = g_strdup (xmlNodeGetContent (field));
    }
    else if (!strcmp (field->name, "extensions")) {
      factory->exts = g_strdup (xmlNodeGetContent (field));
    }
    else if (!strcmp (field->name, "typefind")) {
      factory->typefindfunc = gst_type_typefind_dummy;
    }
    field = field->next;
  }

  return factory;
}

