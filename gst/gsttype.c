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
#include <string.h>


/* global list of registered types */
GList *_gst_types;
guint16 _gst_maxtype;

#define MAX_COST 999999

struct _gst_type_node
{
  int iNode;
  int iDist;
  int iPrev;
};
typedef struct _gst_type_node gst_type_node;

static gboolean gst_type_typefind_dummy(GstBuffer *buffer, gpointer priv);

/* we keep a (spase) matrix in the hashtable like:
 *
 *  type_id    list of factories hashed by src type_id   
 *
 *    1    ->    (1, factory1, factory2), (3, factory3)
 *    2    ->    NULL
 *    3    ->    (4, factory4)
 *    4    ->    NULL
 *
 *  That way, we can quickly find all factories that convert
 *  1 to 2.
 *
 **/

void _gst_type_initialize() {

  _gst_types = NULL;
  _gst_maxtype = 1;		/* type 0 is undefined */

//  gst_type_audio_register();
}

guint16 gst_type_register(GstTypeFactory *factory) {
  guint16 id;
  GstType *type;

  g_return_val_if_fail(factory != NULL, 0);

  id = gst_type_find_by_mime(factory->mime);
  if (!id) {
    type = g_new0(GstType, 1);

    type->id = _gst_maxtype++;
    type->mime = factory->mime;
    type->exts = factory->exts;
    type->typefindfunc = factory->typefindfunc;
    type->srcs = NULL;
    type->sinks = NULL;
    type->converters = g_hash_table_new(NULL, NULL);
    _gst_types = g_list_prepend(_gst_types,type);

    id = type->id;

  } else {
    type = gst_type_find_by_id(id);
    /* now we want to try to merge the types and return the original */

    /* FIXME: do extension merging here, not that easy */

    /* if there is no existing typefind function, try to use new one  */
    if ((type->typefindfunc == gst_type_typefind_dummy || 
         type->typefindfunc == NULL) && factory->typefindfunc)
      type->typefindfunc = factory->typefindfunc;
  }

  return id;
}

static guint16 gst_type_find_by_mime_func(gchar *mime) {
  GList *walk;
  GstType *type;
  gint typelen,mimelen;
  gchar *search, *found;


  walk = _gst_types;
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

guint16 gst_type_find_by_mime(gchar *mime) {
  guint16 typeid;

  typeid = gst_type_find_by_mime_func(mime);

  if (!typeid) {
    gst_plugin_load_typefactory(mime);
  }

  return gst_type_find_by_mime_func(mime);
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

static void gst_type_dump_converter(gpointer key, gpointer value, gpointer data) {
  GList *walk = (GList *)value;
  GstElementFactory *factory;
  
  g_print("gsttype: %u, (", GPOINTER_TO_UINT(key));

  while (walk) {
    factory = (GstElementFactory *) walk->data;
    g_print("%s, ", factory->name);
    walk = g_list_next(walk);
  }
  g_print("NULL)), ");
}

void gst_type_dump() {
  GList *walk = _gst_types;
  GstType *type;

  g_print("gst_type_dump() : \n");

  while (walk) {
    type = (GstType *)walk->data;

    g_print("gst_type: %d (%s) -> (", type->id, type->mime);
    g_hash_table_foreach(type->converters, gst_type_dump_converter, NULL); 
    g_print("NULL)\n");

    walk = g_list_next(walk);
  }
}

void gst_type_add_src(guint16 id,GstElementFactory *src) {
  GList *walk;
  GstType *type = gst_type_find_by_id(id);

  g_return_if_fail(type != NULL);
  g_return_if_fail(src != NULL);

  type->srcs = g_list_prepend(type->srcs,src);
  gst_elementfactory_add_src(src, id);

  // find out if the element has to be indexed in the matrix
  walk = src->sink_types;

  while (walk) {
    GstType *type2 = gst_type_find_by_id(GPOINTER_TO_UINT(walk->data));
    GList *converters = (GList *)g_hash_table_lookup(type2->converters, GUINT_TO_POINTER((guint)id));
    GList *orig = converters;

    while (converters) {
      if (converters->data == src) {
	break;
      }
      converters = g_list_next(converters);
    }

    if (!converters) {
      orig = g_list_prepend(orig, src);
      g_hash_table_insert(type2->converters, GUINT_TO_POINTER((guint)id), orig);
    }
    
    walk = g_list_next(walk);
  }
}

void gst_type_add_sink(guint16 id,GstElementFactory *sink) {
  GList *walk;
  GstType *type = gst_type_find_by_id(id);

  g_return_if_fail(type != NULL);
  g_return_if_fail(sink != NULL);

  type->sinks = g_list_prepend(type->sinks,sink);
  gst_elementfactory_add_sink(sink, id);

  // find out if the element has to be indexed in the matrix
  walk = sink->src_types;

  while (walk) {
    GList *converters = (GList *)g_hash_table_lookup(type->converters, walk->data);
    GList *orig = converters;

    while (converters) {
      if (converters->data == sink) {
	break;
      }
      converters = g_list_next(converters);
    }

    if (!converters) {
      orig = g_list_prepend(orig, sink);
      g_hash_table_insert(type->converters, walk->data, orig);
    }
    
    walk = g_list_next(walk);
  }
}

GList *gst_type_get_srcs(guint16 id) {
  GstType *type = gst_type_find_by_id(id);

  g_return_val_if_fail(type != NULL, NULL);

  return type->srcs;
}

GList *gst_type_get_sinks(guint16 id) {
  GstType *type = gst_type_find_by_id(id);

  g_return_val_if_fail(type != 0, NULL);

  return type->sinks;
}

/*
 * An implementation of Dijkstra's shortest path
 * algorithm to find the best set of GstElementFactories
 * to connnect two GstTypes
 *
 **/

static GList *gst_type_enqueue(GList *queue, gint iNode, gint iDist, gint iPrev) {
  gst_type_node *node = g_malloc(sizeof(gst_type_node));

  node->iNode = iNode;
  node->iDist = iDist;
  node->iPrev = iPrev;

  queue = g_list_append(queue, node);

  return queue;
}

static GList *gst_type_dequeue(GList *queue, gint *iNode, gint *iDist, gint *iPrev) {
  GList *head;
  gst_type_node *node;

  head = g_list_first(queue);
     
  if (head) {
    node = (gst_type_node *)head->data;
    *iNode = node->iNode;
    *iPrev = node->iPrev;
    *iDist = node->iDist;
    head = g_list_remove(queue, node);
  }

  return head;
}

static GList *construct_path (gst_type_node *rgnNodes, gint chNode)
{
  guint src = chNode;
  guint current = rgnNodes[chNode].iPrev;
  GList *factories = NULL;
  GstType *type;
  GList *converters;

  g_print("gsttype: constructed pad ");
  while (current != MAX_COST)
  {
    type = gst_type_find_by_id(current);
    converters = (GList *)g_hash_table_lookup(type->converters, GUINT_TO_POINTER(src));

    g_print("(%d %d)", src, current);
    factories = g_list_prepend(factories, converters->data);
    src = current;
    current = rgnNodes[current].iPrev;
  }
  g_print("\n");
  return factories;
}

static guint gst_type_find_cost(gint src, gint dest) {
  GstType *type = gst_type_find_by_id(src);

  GList *converters = (GList *)g_hash_table_lookup(type->converters, GUINT_TO_POINTER(dest));

  // FIXME do something very clever here...
  if (converters) return 1;
  return MAX_COST;
}

GList *gst_type_get_sink_to_src(guint16 sinkid, guint16 srcid) { 
  gst_type_node *rgnNodes;
  GList *queue = NULL;
  gint iNode, iDist, iPrev, i, iCost;

  if (sinkid == srcid) {
    //FIXME return an identity element
    return NULL;
  }
  else {
    rgnNodes = g_malloc(sizeof(gst_type_node) * _gst_maxtype);

    for (i=0; i< _gst_maxtype; i++) {
      rgnNodes[i].iNode = i;
      rgnNodes[i].iDist = MAX_COST;
      rgnNodes[i].iPrev = MAX_COST;
    }
    rgnNodes[sinkid].iDist = 0;
    rgnNodes[sinkid].iPrev = MAX_COST;

    queue = gst_type_enqueue(queue, sinkid, 0, MAX_COST);

    while (g_list_length(queue) > 0) {

      queue = gst_type_dequeue(queue, &iNode, &iDist, &iPrev);
      
      for (i=0; i< _gst_maxtype; i++) {
	iCost = gst_type_find_cost(iNode, i);
        if (iCost != MAX_COST) {
          if((MAX_COST == rgnNodes[i].iDist) ||
	     (rgnNodes[i].iDist > (iCost + iDist))) {
            rgnNodes[i].iDist = iDist + iCost;
            rgnNodes[i].iPrev = iNode;

            queue = gst_type_enqueue(queue, i, iDist + iCost, iNode);
	  }
	}
      }
    }
  }

  return construct_path(rgnNodes, srcid);
}

GList *gst_type_get_list() {
  return _gst_types;
}

xmlNodePtr gst_type_save_thyself(GstType *type, xmlNodePtr parent) {
  xmlNodePtr tree;

  tree = xmlNewChild(parent, NULL, "type", NULL);

  xmlNewChild(tree, NULL, "mime", type->mime);
  
  return tree;
}

guint16 gst_type_load_thyself(xmlNodePtr parent) {
  xmlNodePtr children = parent->childs;
  guint16 typeid = 0;

  while (children) {
    if (!strcmp(children->name, "type")) {
      xmlNodePtr field = children->childs;
      while (field) {
	if (!strcmp(field->name, "mime")) {
	  typeid = gst_type_find_by_mime(xmlNodeGetContent(field));
	  if (!typeid) {
            GstTypeFactory *factory = g_new0(GstTypeFactory, 1);

            factory->mime = g_strdup(xmlNodeGetContent(field));
	    typeid = gst_type_register(factory);
	  }
	  return typeid;
	}
	field = field->next;
      }
    }
    children = children->next;
  }

  return typeid;
}


xmlNodePtr gst_typefactory_save_thyself(GstTypeFactory *factory, xmlNodePtr parent) {

  xmlNewChild(parent, NULL, "mime", factory->mime);
  if (factory->exts) {
    xmlNewChild(parent, NULL, "extensions", factory->exts);
  }
  if (factory->typefindfunc) {
    xmlNewChild(parent, NULL, "typefind", NULL);
  }
  
  return parent;
}

static gboolean gst_type_typefind_dummy(GstBuffer *buffer, gpointer priv)
{
  GstType *type = (GstType *)priv;
  guint16 typeid;
  g_print("gsttype: need to load typefind function\n");

  type->typefindfunc = NULL;
  gst_plugin_load_typefactory(type->mime);
  typeid = gst_type_find_by_mime(type->mime);
  type = gst_type_find_by_id(typeid);

  if (type->typefindfunc) {
    return type->typefindfunc(buffer, type);
  }

  return FALSE;
}

GstTypeFactory *gst_typefactory_load_thyself(xmlNodePtr parent) {

  GstTypeFactory *factory = g_new0(GstTypeFactory, 1);
  xmlNodePtr field = parent->childs;
  factory->typefindfunc = NULL;

  while (field) {
    if (!strcmp(field->name, "mime")) {
      factory->mime = g_strdup(xmlNodeGetContent(field));
    }
    else if (!strcmp(field->name, "extensions")) {
      factory->exts = g_strdup(xmlNodeGetContent(field));
    }
    else if (!strcmp(field->name, "typefind")) {
      factory->typefindfunc = gst_type_typefind_dummy;
    }
    field = field->next;
  }

  return factory;
}

