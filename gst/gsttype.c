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


#include <gst/gst.h>


/* global list of registered types */
GList *_gst_types;
guint16 _gst_maxtype;


void _gst_type_initialize() {
  _gst_types = NULL;
  _gst_maxtype = 1;		/* type 0 is undefined */

//  gst_type_audio_register();
}

guint16 gst_type_register(GstTypeFactory *factory) {
  guint16 id;
  GstType *type;

  g_return_if_fail(factory != NULL);

//  id = gst_type_find_by_mime(factory->mime);
  id = 0;
  if (!id) {
    type = (GstType *)malloc(sizeof(GstType));

    type->id = _gst_maxtype++;
    type->mime = factory->mime;
    type->exts = factory->exts;
    type->typefindfunc = factory->typefindfunc;
    type->srcs = NULL;
    type->sinks = NULL;
    _gst_types = g_list_prepend(_gst_types,type);

    id = type->id;
  } else {
    type = gst_type_find_by_id(id);
    /* now we want to try to merge the types and return the original */

    /* FIXME: do extension merging here, not that easy */

    /* if there is no existing typefind function, try to use new one  */
    if (!type->typefindfunc && factory->typefindfunc)
      type->typefindfunc = factory->typefindfunc;
  }

  return id;
}

guint16 gst_type_find_by_mime(gchar *mime) {
  GList *walk = _gst_types;
  GstType *type;
  gint typelen,mimelen;
  gchar *search, *found;

//  DEBUG("searching for '%s'\n",mime);
  mimelen = strlen(mime);
  while (walk) {
    type = (GstType *)walk->data;
    search = type->mime;
//    DEBUG("checking against '%s'\n",search);
    typelen = strlen(search);
    while ((search - type->mime) < typelen) {
      found = strstr(search,mime);
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
    walk = g_list_next(walk);
  }

  return 0;
}

GstType *gst_type_find_by_id(guint16 id) {
  GList *walk = _gst_types;
  GstType *type;

  while (walk) {
    type = (GstType *)walk->data;
    if (type->id == id)
      return type;
    walk = g_list_next(walk);
  }

  return NULL;
}

void gst_type_add_src(guint16 id,GstElementFactory *src) {
  GstType *type = gst_type_find_by_id(id);

  g_return_if_fail(type != NULL);
  g_return_if_fail(src != NULL);

  type->srcs = g_list_prepend(type->srcs,src);
}

void gst_type_add_sink(guint16 id,GstElementFactory *sink) {
  GstType *type = gst_type_find_by_id(id);

  g_return_if_fail(type != NULL);
  g_return_if_fail(sink != NULL);

  type->sinks = g_list_prepend(type->sinks,sink);
}

GList *gst_type_get_srcs(guint16 id) {
  GstType *type = gst_type_find_by_id(id);

  g_return_if_fail(type != NULL);

  return type->srcs;
}

GList *gst_type_get_sinks(guint16 id) {
  GstType *type = gst_type_find_by_id(id);

  g_return_if_fail(type != 0);

  return type->sinks;
}

GList *gst_type_get_list() {
  return _gst_types;
}
