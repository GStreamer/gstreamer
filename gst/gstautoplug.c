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

#include "gstdebug.h"
#include "gstautoplug.h"

#define MAX_COST 999999

typedef guint 	(*GstAutoplugCostFunction) (gpointer src, gpointer dest, gpointer data);
typedef GList* 	(*GstAutoplugListFunction) (gpointer data);

static GList* 	gst_autoplug_func 	(gpointer src, gpointer sink, 
		   			 GstAutoplugListFunction list_function,
		   			 GstAutoplugCostFunction cost_function,
					 gpointer data);


struct _gst_autoplug_node
{
  gpointer iNode;
  gpointer iPrev;
  gint iDist;
};

typedef struct _gst_autoplug_node gst_autoplug_node;

static GList*
gst_autoplug_enqueue (GList *queue, gpointer iNode, gint iDist, gpointer iPrev) 
{
  gst_autoplug_node *node = g_malloc (sizeof (gst_autoplug_node));

  node->iNode = iNode;
  node->iDist = iDist;
  node->iPrev = iPrev;

  queue = g_list_append (queue, node);

  return queue;
}

static GList*
gst_autoplug_dequeue (GList *queue, gpointer *iNode, gint *iDist, gpointer *iPrev) 
{
  GList *head;
  gst_autoplug_node *node;

  head = g_list_first (queue);
     
  if (head) {
    node = (gst_autoplug_node *)head->data;
    *iNode = node->iNode;
    *iPrev = node->iPrev;
    *iDist = node->iDist;
    head = g_list_remove (queue, node);
  }

  return head;
}

static gint
find_factory (gst_autoplug_node *rgnNodes, GstElementFactory *factory)
{
  gint i=0;

  while (rgnNodes[i].iNode) {
    if (rgnNodes[i].iNode == factory) return i;
    i++;  
  }

  return 0;
}

static GList*
construct_path (gst_autoplug_node *rgnNodes, GstElementFactory *factory)
{
  GstElementFactory *current;
  GList *factories = NULL;
  
  current = rgnNodes[find_factory(rgnNodes, factory)].iPrev;

  while (current != NULL)
  { 
    gpointer next;
    next = rgnNodes[find_factory(rgnNodes, current)].iPrev;
    if (next) factories = g_list_prepend (factories, current);
    current = next;
  }

  return factories;
}

static gboolean 
gst_autoplug_can_match (GstElementFactory *src, GstElementFactory *dest) 
{
  GList *srctemps, *desttemps;

  srctemps = src->padtemplates;

  while (srctemps) {
    GstPadTemplate *srctemp = (GstPadTemplate *)srctemps->data;

    desttemps = dest->padtemplates;

    while (desttemps) {
      GstPadTemplate *desttemp = (GstPadTemplate *)desttemps->data;

      if (srctemp->direction == GST_PAD_SRC &&
          desttemp->direction == GST_PAD_SINK) {
	if (gst_caps_check_compatibility (srctemp->caps, desttemp->caps)) {
	  DEBUG ("gstautoplug: \"%s\" connects with \"%s\"\n", src->name, dest->name);
          return TRUE;
	}
	else {
	  DEBUG ("gstautoplug: \"%s\" does not connect with \"%s\"\n", src->name, dest->name);
	}
      }

      desttemps = g_list_next (desttemps);
    }
    srctemps = g_list_next (srctemps);
  }
  return FALSE;
}
	
static GList*
gst_autoplug_elementfactory_get_list (gpointer data)
{
  return gst_elementfactory_get_list ();
}

static guint 
gst_autoplug_elementfactory_find_cost (gpointer src, gpointer dest, gpointer data) 
{
  if (gst_autoplug_can_match ((GstElementFactory *)src, (GstElementFactory *)dest)) {
    return 1;
  }
  return MAX_COST;
}


GList*
gst_autoplug_factories (GstElementFactory *srcfactory, GstElementFactory *sinkfactory) 
{
  return gst_autoplug_func (srcfactory, sinkfactory, 
      	  		    gst_autoplug_elementfactory_get_list, 
			    gst_autoplug_elementfactory_find_cost,
			    NULL);
}

typedef struct {
  GstCaps *src;
  GstCaps *sink;
} caps_struct;

#define IS_CAPS(cap) (((cap) == caps->src) || (cap) == caps->sink)

static guint 
gst_autoplug_caps_find_cost (gpointer src, gpointer dest, gpointer data) 
{
  caps_struct *caps = (caps_struct *)data;
  gboolean res;

  if (IS_CAPS (src) && IS_CAPS (dest)) {
    res = gst_caps_check_compatibility ((GstCaps *)src, (GstCaps *)dest);
    DEBUG ("caps %d to caps %d %d\n", ((GstCaps *)src)->id, ((GstCaps *)dest)->id, res);
  }
  else if (IS_CAPS (src)) {
    res = gst_elementfactory_can_sink_caps ((GstElementFactory *)dest, src);
    DEBUG ("factory %s to sink caps %d %d\n", ((GstElementFactory *)dest)->name, ((GstCaps *)src)->id, res);
  }
  else if (IS_CAPS (dest)) {
    res = gst_elementfactory_can_src_caps ((GstElementFactory *)src, dest);
    DEBUG ("factory %s to src caps %d %d\n", ((GstElementFactory *)src)->name, ((GstCaps *)dest)->id, res);
  }
  else {
    res = gst_autoplug_can_match ((GstElementFactory *)src, (GstElementFactory *)dest);
  }

  if (res) return 1;
  return MAX_COST;
}

GList*
gst_autoplug_caps (GstCaps *srccaps, GstCaps *sinkcaps) 
{
  caps_struct caps;

  caps.src = srccaps;
  caps.sink = sinkcaps;

  return gst_autoplug_func (srccaps, sinkcaps, 
      	  		    gst_autoplug_elementfactory_get_list, 
			    gst_autoplug_caps_find_cost,
			    &caps);
}

GList*
gst_autoplug_elements (GstElement *src, GstElement *sink) 
{
  return NULL;
}

GList*
gst_autoplug_caps_to_factory (GstCaps *srccaps, GstElementFactory *sinkfactory) 
{
  return NULL;
}

GList*
gst_autoplug_factory_to_caps (GstElementFactory *srcfactory, GstCaps *sinkcaps) 
{
  return NULL;
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
static GList*
gst_autoplug_func (gpointer src, gpointer sink, 
		   GstAutoplugListFunction list_function,
		   GstAutoplugCostFunction cost_function,
		   gpointer data)
{
  gst_autoplug_node *rgnNodes;
  GList *queue = NULL;
  gpointer iNode, iPrev;
  gint iDist, i, iCost;

  GList *elements = g_list_copy (list_function(data));
  GList *factories;
  guint num_factories;
  
  DEBUG ("%p %p\n", src, sink);

  elements = g_list_append (elements, sink);
  elements = g_list_append (elements, src);
  
  factories = elements;
  
  num_factories = g_list_length (factories);

  rgnNodes = g_new0 (gst_autoplug_node, num_factories+1);

  for (i=0; i< num_factories; i++) {
    gpointer fact = factories->data;

    rgnNodes[i].iNode = fact;
    rgnNodes[i].iPrev = NULL;

    if (fact == src) {
      rgnNodes[i].iDist = 0;
    }
    else {
      rgnNodes[i].iDist = MAX_COST;
    }

    factories = g_list_next (factories);
  }
  rgnNodes[num_factories].iNode = NULL;

  queue = gst_autoplug_enqueue (queue, src, 0, NULL);

  while (g_list_length (queue) > 0) {
    GList *factories2 = elements;

    queue = gst_autoplug_dequeue (queue, &iNode, &iDist, &iPrev);
     
    for (i=0; i< num_factories; i++) {
      gpointer current = factories2->data;
 	
      iCost = cost_function (iNode, current, data);
      if (iCost != MAX_COST) {
        if((MAX_COST == rgnNodes[i].iDist) ||
           (rgnNodes[i].iDist > (iCost + iDist))) {
          rgnNodes[i].iDist = iDist + iCost;
          rgnNodes[i].iPrev = iNode;

          queue = gst_autoplug_enqueue (queue, current, iDist + iCost, iNode);
        }
      }

      factories2 = g_list_next (factories2);
    }
  }

  return construct_path (rgnNodes, sink);
}

