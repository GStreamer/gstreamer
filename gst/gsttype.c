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

struct _GstTypeFindInfo {
  GstTypeFindFunc typefindfunc; /* typefind function */

  GstPlugin *plugin;            /* the plugin with this typefind function */
};

#define MAX_COST 999999

struct _gst_type_node
{
  int iNode;
  int iDist;
  int iPrev;
};

typedef struct _gst_type_node gst_type_node;

static gboolean 	gst_type_typefind_dummy		(GstBuffer *buffer, gpointer priv);

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

  //g_print("gsttype: type register %s\n", factory->mime);
  id = gst_type_find_by_mime (factory->mime);
  
  if (!id) {
    type = g_new0 (GstType, 1);

    type->id = 		_gst_maxtype++;
    type->mime = 	factory->mime;
    type->exts = 	factory->exts;
    //type->typefindfunc = factory->typefindfunc;
    type->srcs =	NULL;
    type->sinks = 	NULL;
    type->converters = 	g_hash_table_new (NULL, NULL);
    _gst_types = 	g_list_prepend (_gst_types, type);

    id = type->id;

  } else {
    type = gst_type_find_by_id (id);
    /* now we want to try to merge the types and return the original */

    /* FIXME: do extension merging here, not that easy */

    /* if there is no existing typefind function, try to use new one  */
    /*
    if ((type->typefindfunc == gst_type_typefind_dummy || 
         type->typefindfunc == NULL) && factory->typefindfunc)
      type->typefindfunc = factory->typefindfunc;
      */
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

static void 
gst_type_dump_converter (gpointer key, 
		         gpointer value, 
			 gpointer data) 
{
  GList *walk = (GList *)value;
  GstElementFactory *factory;
  guint16 id = GPOINTER_TO_UINT (key);
  GstType *type = gst_type_find_by_id (id);
  
  g_print ("\ngsttype:    %u (%s), ", type->id, type->mime);

  while (walk) {
    factory = (GstElementFactory *) walk->data;
    g_print("\"%s\" ", factory->name);
    walk = g_list_next (walk);
  }
}

/**
 * gst_type_dump:
 *
 * dumps the current type system
 */
void 
gst_type_dump(void) 
{
  GList *walk = _gst_types;
  GstType *type;

  g_print ("gst_type_dump() : \n");

  while (walk) {
    type = (GstType *)walk->data;

    g_print ("gsttype: %d (%s)", type->id, type->mime);
    g_hash_table_foreach (type->converters, gst_type_dump_converter, NULL); 
    g_print ("\n");

    walk = g_list_next (walk);
  }
}

static void 
gst_type_handle_src (guint16 id, GstElementFactory *src, gboolean remove) 
{
  GList *walk;
  GstType *type = gst_type_find_by_id (id);

  g_return_if_fail (type != NULL);
  g_return_if_fail (src != NULL);

  if (remove) 
    type->srcs = g_list_remove (type->srcs, src);
  else 
    type->srcs = g_list_prepend (type->srcs, src);

  // find out if the element has to be indexed in the matrix
  walk = src->padtemplates;

  while (walk) {
    GstPadTemplate *template;

    template = (GstPadTemplate *) walk->data;

    if (template->direction == GST_PAD_SINK) {
      GstType *type2;
      GList *converters;
      GList *orig;
      GstCaps *caps;

      caps = template->caps;

      if (caps)
        type2 = gst_type_find_by_id (caps->id);
      else
	goto next;

      converters = (GList *)g_hash_table_lookup (type2->converters, GUINT_TO_POINTER ((guint)id));
      orig = converters;

      while (converters) {
        if (converters->data == src) {
	  break;
        }
        converters = g_list_next (converters);
      }

      if (remove) 
        orig = g_list_remove (orig, src);
      else if (!converters)
        orig = g_list_prepend (orig, src);

      g_hash_table_insert (type2->converters, GUINT_TO_POINTER ((guint)id), orig);
    }
next: 
    walk = g_list_next (walk);
  }
}

/**
 * gst_type_add_src:
 * @id: the type id to add the source factory to
 * @src: the source factory for the type
 *
 * register the src factory as being a source for the
 * given type id
 */
void 
_gst_type_add_src (guint16 id, GstElementFactory *src) 
{
  gst_type_handle_src (id, src, FALSE);
}

/**
 * gst_type_remove_src:
 * @id: the type id to add the source factory to
 * @src: the source factory for the type
 *
 * register the src factory as being a source for the
 * given type id
 */
void 
_gst_type_remove_src (guint16 id, GstElementFactory *src) 
{
  gst_type_handle_src (id, src, TRUE);
}

static void 
gst_type_handle_sink (guint16 id, GstElementFactory *sink, gboolean remove) 
{
  GList *walk;
  GstType *type = gst_type_find_by_id (id);

  g_return_if_fail (type != NULL);
  g_return_if_fail (sink != NULL);

  if (remove) 
    type->sinks = g_list_remove (type->sinks, sink);
  else 
    type->sinks = g_list_prepend (type->sinks, sink);

  // find out if the element has to be indexed in the matrix
  walk = sink->padtemplates;

  while (walk) {
    GstPadTemplate *template;

    template = (GstPadTemplate *) walk->data;

    if (template->direction == GST_PAD_SRC) {
      guint16 id2;
      GList *converters;
      GList *orig;
      GstCaps *caps;

      caps = template->caps;

      if (caps)
        id2 = caps->id;
      else
	goto next;

      converters = (GList *)g_hash_table_lookup (type->converters, GUINT_TO_POINTER ((guint)id2));
      orig = converters;

      while (converters) {
        if (converters->data == sink) {
	  break;
        }
        converters = g_list_next (converters);
      }

      if (remove) 
        orig = g_list_remove (orig, sink);
      else if (!converters) 
        orig = g_list_prepend (orig, sink);

      g_hash_table_insert (type->converters, GUINT_TO_POINTER ((guint)id2), orig);
    }
next: 
    walk = g_list_next (walk);
  }
}

/**
 * gst_type_add_sink:
 * @id: the type id to add the sink factory to
 * @sink: the sink factory for the type
 *
 * register the sink factory as being a sink for the
 * given type id
 */
void 
_gst_type_add_sink (guint16 id, GstElementFactory *sink) 
{
  gst_type_handle_sink (id, sink, FALSE);
}

/**
 * gst_type_remove_sink:
 * @id: the type id to remove the sink factory from
 * @sink: the sink factory for the type
 *
 * remove the sink factory as being a sink for the
 * given type id
 */
void 
_gst_type_remove_sink (guint16 id, GstElementFactory *sink) 
{
  gst_type_handle_sink (id, sink, TRUE);
}

/**
 * gst_type_get_srcs:
 * @id: the id to fetch the source factories for
 *
 * return a list of elementfactories that source 
 * the given type id
 *
 * Returns: a list of elementfactories
 */
GList*
gst_type_get_srcs (guint16 id) 
{
  GstType *type = gst_type_find_by_id (id);

  g_return_val_if_fail (type != NULL, NULL);

  return type->srcs;
}

/**
 * gst_type_get_sinks:
 * @id: the id to fetch the sink factories for
 *
 * return a list of elementfactories that sink 
 * the given type id
 *
 * Returns: a list of elementfactories
 */
GList*
gst_type_get_sinks (guint16 id) 
{
  GstType *type = gst_type_find_by_id (id);

  g_return_val_if_fail (type != 0, NULL);

  return type->sinks;
}

/*
 * An implementation of Dijkstra's shortest path
 * algorithm to find the best set of GstElementFactories
 * to connnect two GstTypes
 *
 **/
static GList*
gst_type_enqueue (GList *queue, gint iNode, gint iDist, gint iPrev) 
{
  gst_type_node *node = g_malloc (sizeof (gst_type_node));

  node->iNode = iNode;
  node->iDist = iDist;
  node->iPrev = iPrev;

  queue = g_list_append (queue, node);

  return queue;
}

static GList*
gst_type_dequeue (GList *queue, gint *iNode, gint *iDist, gint *iPrev) 
{
  GList *head;
  gst_type_node *node;

  head = g_list_first (queue);
     
  if (head) {
    node = (gst_type_node *)head->data;
    *iNode = node->iNode;
    *iPrev = node->iPrev;
    *iDist = node->iDist;
    head = g_list_remove (queue, node);
  }

  return head;
}

static GList*
construct_path (gst_type_node *rgnNodes, gint chNode)
{
  guint src = chNode;
  guint current = rgnNodes[chNode].iPrev;
  GList *factories = NULL;
  GstType *type;
  GList *converters;

  g_print ("gsttype: constructed mime path ");
  while (current != MAX_COST)
  {
    type = gst_type_find_by_id (current);
    converters = (GList *)g_hash_table_lookup (type->converters, GUINT_TO_POINTER (src));

    g_print ("(%d %d)", src, current);
    factories = g_list_prepend (factories, converters->data);
    src = current;
    current = rgnNodes[current].iPrev;
  }
  g_print("\n");
  return factories;
}

static guint 
gst_type_find_cost (gint src, gint dest) 
{
  GstType *type = gst_type_find_by_id (src);

  GList *converters = (GList *)g_hash_table_lookup (type->converters, GUINT_TO_POINTER (dest));

  // FIXME do something very clever here...
  if (converters) return 1;
  return MAX_COST;
}

/**
 * gst_type_get_sink_to_src:
 * @sinkid: the id of the sink
 * @srcid: the id of the source
 *
 * return a list of elementfactories that convert the source
 * type id to the sink type id
 *
 * Returns: a list of elementfactories
 */
GList*
gst_type_get_sink_to_src (guint16 sinkid, guint16 srcid) 
{
  gst_type_node *rgnNodes;
  GList *queue = NULL;
  gint iNode, iDist, iPrev, i, iCost;

  if (sinkid == srcid) {
    //FIXME return an identity element
    return NULL;
  }
  else {
    rgnNodes = g_malloc (sizeof (gst_type_node) * _gst_maxtype);

    for (i=0; i< _gst_maxtype; i++) {
      rgnNodes[i].iNode = i;
      rgnNodes[i].iDist = MAX_COST;
      rgnNodes[i].iPrev = MAX_COST;
    }
    rgnNodes[sinkid].iDist = 0;
    rgnNodes[sinkid].iPrev = MAX_COST;

    queue = gst_type_enqueue (queue, sinkid, 0, MAX_COST);

    while (g_list_length (queue) > 0) {

      queue = gst_type_dequeue (queue, &iNode, &iDist, &iPrev);
      
      for (i=0; i< _gst_maxtype; i++) {
	iCost = gst_type_find_cost (iNode, i);
        if (iCost != MAX_COST) {
          if((MAX_COST == rgnNodes[i].iDist) ||
	     (rgnNodes[i].iDist > (iCost + iDist))) {
            rgnNodes[i].iDist = iDist + iCost;
            rgnNodes[i].iPrev = iNode;

            queue = gst_type_enqueue (queue, i, iDist + iCost, iNode);
	  }
	}
      }
    }
  }

  return construct_path (rgnNodes, srcid);
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
 * gst_type_save_thyself:
 * @type: the type to save
 * @parent: the parent node to save into
 *
 * save a type into an XML representation
 *
 * Returns: the new xmlNodePtr
 */
xmlNodePtr 
gst_type_save_thyself (GstType *type, xmlNodePtr parent) 
{
  xmlNewChild (parent, NULL, "mime", type->mime);
  
  return parent;
}

/**
 * gst_type_load_thyself:
 * @parent: the parent node with the xml information
 *
 * load a type from an XML representation
 *
 * Returns: the loaded type id
 */
guint16 
gst_type_load_thyself (xmlNodePtr parent) 
{
  xmlNodePtr field = parent->childs;
  guint16 typeid = 0;

  while (field) {
    if (!strcmp (field->name, "mime")) {
      typeid = gst_type_find_by_mime (xmlNodeGetContent (field));
      if (!typeid) {
        GstTypeFactory *factory = g_new0 (GstTypeFactory, 1);

        factory->mime = g_strdup (xmlNodeGetContent (field));
        typeid = gst_type_register (factory);
      }
      return typeid;
    }
    field = field->next;
  }

  return typeid;
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

static gboolean 
gst_type_typefind_dummy (GstBuffer *buffer, gpointer priv)
{
  GstType *type = (GstType *)priv;
  guint16 typeid;
  g_print ("gsttype: need to load typefind function\n");

  type->typefindfuncs = NULL;
  gst_plugin_load_typefactory (type->mime);
  typeid = gst_type_find_by_mime (type->mime);
  type = gst_type_find_by_id (typeid);

  /*
  if (type->typefindfunc) {
    return type->typefindfunc (buffer, type);
  }
  */

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
      //factory->typefindfunc = gst_type_typefind_dummy;
    }
    field = field->next;
  }

  return factory;
}

