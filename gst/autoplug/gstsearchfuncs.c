/* GStreamer
 * Copyright (C) 1999-2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000-2002 Wim Taymans <wtay@chello.be>
 *
 * gstsearchfuncs.c: functions needed when doing searches while
 *                   autoplugging
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstsearchfuncs.h"

/* FIXME: "evil hack" alarm, we need a better way to get a category in here */
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_AUTOPLUG_ATTEMPT);
#define GST_CAT_DEFAULT GST_CAT_AUTOPLUG_ATTEMPT

/* function that really misses in GLib
 * though the GLib version should take a function as argument...
 */
static void
g_list_free_list_and_elements (GList *list)
{
  GList *walk = list;
  
  while (walk)
  {
    g_free (walk->data);
    walk = g_list_next (walk);
  }
  g_list_free (list);
}
/**
 * gst_autoplug_caps_intersect:
 * @src: a source #GstCaps
 * @sink: the sink #GstCaps
 *
 * Checks if the given caps have a non-null intersection.
 *
 * Returns: TRUE, if both caps intersect.
 */
gboolean
gst_autoplug_caps_intersect (const GstCaps *src, const GstCaps *sink)
{
  GstCaps *caps;

  /* get an intersection */
  caps = gst_caps_intersect (src, sink);
  
  /* if the caps can't link, there is no intersection */
  if (gst_caps_is_empty (caps)) {
    gst_caps_free (caps);
    return FALSE;
  }
  
  /* hurrah, we can link, now remove the intersection */
  gst_caps_free (caps);
  return TRUE;
}

/**
 * gst_autoplug_can_connect_src:
 * @fac: factory to connect to
 * @src: caps to check
 *
 * Checks if a factory's sink can connect to the given caps
 *
 * Returns: #GstPadTemplate that can connect to the given caps
 */
GstPadTemplate *
gst_autoplug_can_connect_src (GstElementFactory *fac, const GstCaps *src)
{
  GList *templs;
  
  templs = fac->padtemplates;
  
  while (templs)
  {
    if ((GST_PAD_TEMPLATE_DIRECTION (templs->data) == GST_PAD_SINK) && 
	gst_autoplug_caps_intersect (src, 
		                     GST_PAD_TEMPLATE_CAPS (templs->data)))
    {
      return GST_PAD_TEMPLATE (templs->data);
    }
    templs = g_list_next (templs);
  }
  
  return NULL;  
}
/**
 * gst_autoplug_can_connect_sink:
 * @fac: factory to connect to
 * @sink: caps to check
 *
 * Checks if a factory's src can connect to the given caps
 *
 * Returns: #GstPadTemplate that can connect to the given caps
 */
GstPadTemplate *
gst_autoplug_can_connect_sink (GstElementFactory *fac, const GstCaps *sink)
{
  GList *templs;
  
  templs = fac->padtemplates;
  
  while (templs)
  {
    GstCaps *caps = GST_PAD_TEMPLATE_CAPS (templs->data);
    if ((GST_PAD_TEMPLATE_DIRECTION (templs->data) == GST_PAD_SRC) &&
	gst_autoplug_caps_intersect (caps, sink))
    {
      return GST_PAD_TEMPLATE (templs->data);
    }
    templs = g_list_next (templs);
  }

  return NULL;  
}
GstPadTemplate *
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
          if (gst_autoplug_caps_intersect (gst_pad_template_get_caps (srctemp), 
              gst_pad_template_get_caps (desttemp))) {
            GST_DEBUG ("factory \"%s\" can connect with factory \"%s\"", 
                       GST_OBJECT_NAME (src), GST_OBJECT_NAME (dest));
            return desttemp;
         }
      }

      desttemps = g_list_next (desttemps);
    }
    srctemps = g_list_next (srctemps);
  }
  GST_DEBUG ("factory \"%s\" cannot connect with factory \"%s\"", 
             GST_OBJECT_NAME (src), GST_OBJECT_NAME (dest));
  return NULL;
}

/* returns TRUE if the factory has padtemplates with the specified direction */
gboolean
gst_autoplug_factory_has_direction (GstElementFactory *fac, GstPadDirection dir)
{
  GList *templs = fac->padtemplates;
  
  while (templs)
  {
    if (GST_PAD_TEMPLATE_DIRECTION (templs->data) == dir)
    {
      return TRUE;
    }
    templs = g_list_next (templs);
  }
  
  return FALSE;
}

/* Decisions are based on the padtemplates. 
 * These functions return a new list so be sure to free it.
 */
GList *
gst_autoplug_factories_sinks (GList *factories)
{
  GList *ret = NULL;
  
  while (factories)
  {
    if (gst_autoplug_factory_has_sink (factories->data))
      ret = g_list_prepend (ret, factories->data);
    factories = g_list_next (factories);
  }
  return ret;  
}
GList *
gst_autoplug_factories_srcs (GList *factories)
{
  GList *ret = NULL;
  
  while (factories)
  {
    if (gst_autoplug_factory_has_src (factories->data))
      ret = g_list_prepend (ret, factories->data);
    factories = g_list_next (factories);
  }
  return ret;  
}
GList *  
gst_autoplug_factories_filters (GList *factories)
{
  GList *ret = NULL;
  
  while (factories)
  {
    /* if you want it faster do src/sink check at once, don't call two functions */
    if (gst_autoplug_factory_has_src (factories->data) && gst_autoplug_factory_has_sink (factories->data))
      ret = g_list_prepend (ret, factories->data);
    factories = g_list_next (factories);
  }
  return ret;  
}


static gint 
gst_autoplug_rank_compare (const GstElementFactory *a, const GstElementFactory *b)
{
	if (GST_PLUGIN_FEATURE (a)->rank > GST_PLUGIN_FEATURE (b)->rank) return -1;
	return (GST_PLUGIN_FEATURE (a)->rank < GST_PLUGIN_FEATURE (b)->rank) ? 1 : 0;
}

/* returns all factories which have sinks with non-NULL caps and srcs with
 * any caps. also only returns factories with a non-zero rank, and sorts by 
 * rank descending.
 */
GList *
gst_autoplug_factories_filters_with_sink_caps (GList *factories)
{
  GList *ret = NULL;
  GstElementFactory *factory;
  GList *templs;

  while (factories)
  {
    factory = (GstElementFactory *) factories->data;
    templs = factory->padtemplates;

    if (GST_PLUGIN_FEATURE (factory)->rank > 0){
      gboolean have_src = FALSE;
      gboolean have_sink = FALSE;

      while (templs)
      {
        if (GST_PAD_TEMPLATE_DIRECTION (templs->data) == GST_PAD_SRC)
        {
          have_src = TRUE;
        }  
        if ((GST_PAD_TEMPLATE_DIRECTION (templs->data) == GST_PAD_SINK) && (GST_PAD_TEMPLATE_CAPS (templs->data) != NULL))
        {
          have_sink = TRUE;
        }
        if (have_src && have_sink)
        {
          ret = g_list_prepend (ret, factory);
          break;
        }
        templs = g_list_next (templs);
      }
    }
    factories = g_list_next (factories);
  }
  return g_list_sort(ret, (GCompareFunc)gst_autoplug_rank_compare);
}



/* returns all factories which have a maximum of maxtemplates GstPadTemplates in direction dir
 */
GList *
gst_autoplug_factories_at_most_templates(GList *factories, GstPadDirection dir, guint maxtemplates)
{
  GList *ret = NULL;
  
  while (factories)
  {
    guint count = 0;
    GList *templs = ((GstElementFactory *) factories->data)->padtemplates;

    while (templs)
    {
      if (GST_PAD_TEMPLATE_DIRECTION (templs->data) == dir)
      {
        count++;
      }
      if (count > maxtemplates)
        break;
      templs = g_list_next (templs);
    }
    if (count <= maxtemplates)
      ret = g_list_prepend (ret, factories->data);
    
    factories = g_list_next (factories);
  }
  return ret;
}
/*********************************************************************
 *
 * SHORTEST PATH ALGORITHM
 */
/**
 * gst_autoplug_sp:
 * @src_caps: a #GstCaps to plug from.
 * @sink_caps: the #GstCaps to plug to.
 * @factories: a #GList containing all allowed #GstElementFactory entries.
 *
 * Finds the shortest path of elements that together make up a possible
 * connection between the source and sink caps.
 *
 * Returns: a #GList of #GstElementFactory items which have to be connected 
 * to get the shortest path.
 */
GList *
gst_autoplug_sp (const GstCaps *srccaps, const GstCaps *sinkcaps, GList *factories)
{
  GList *factory_nodes = NULL;
  guint curcost = GST_AUTOPLUG_MAX_COST; /* below this cost, there is no path */
  GstAutoplugNode *bestnode = NULL; /* best (unconnected) endpoint currently */
  
  g_return_val_if_fail (srccaps != NULL, NULL);
  g_return_val_if_fail (sinkcaps != NULL, NULL);
  
  GST_INFO ("attempting to autoplug via shortest path from %"
      GST_PTR_FORMAT " to %" GST_PTR_FORMAT, srccaps, sinkcaps);

  /* wrap all factories as GstAutoplugNode 
   * initialize the cost */
  while (factories)
  {
    GstAutoplugNode *node = g_new0 (GstAutoplugNode, 1);
    node->prev = NULL;
    node->fac = (GstElementFactory *) factories->data;
    GST_DEBUG ("trying with %s", node->fac->details.longname);
    node->templ = gst_autoplug_can_connect_src (node->fac, srccaps);
    node->cost = (node->templ ? gst_autoplug_get_cost (node->fac) 
		              : GST_AUTOPLUG_MAX_COST);
    node->endpoint = gst_autoplug_can_connect_sink (node->fac, sinkcaps);
    if (node->templ && node->endpoint)
      GST_DEBUG ("%s makes connection possible",
		 node->fac->details.longname);
    else
      GST_DEBUG ("direct connection with %s not possible",
		 node->fac->details.longname);
    if ((node->endpoint != NULL) && 
	((bestnode == NULL) || (node->cost < bestnode->cost)))
    {
      bestnode = node;
    }
    factory_nodes = g_list_prepend (factory_nodes, node);
    /* make curcost the minimum cost of any plugin */
    curcost = node->cost < curcost ? node->cost : curcost;
    factories = g_list_next (factories);
  }
  
  /* check if we even have possible endpoints */
  if (bestnode == NULL)
  {
    GST_DEBUG ("no factory found that could connect to sink caps");
    g_list_free_list_and_elements (factory_nodes);
    return NULL;
  }
  
  /* iterate until we found the best path */
  while (curcost < GST_AUTOPLUG_MAX_COST)
  {
    GList *nodes = factory_nodes;
    guint nextcost = GST_AUTOPLUG_MAX_COST; /* next cost to check */
    GST_DEBUG ("iterating at current cost %d, bestnode %s at %d", curcost, GST_OBJECT_NAME (bestnode->fac), bestnode->cost);
    /* check if we already have a valid best connection to the sink */
    if (bestnode->cost <= curcost)
    {
      GList *ret;
      GST_DEBUG ("found a way to connect via %s", GST_OBJECT_NAME ((GstObject *) bestnode->fac));    
      /* enter all factories into the return list */
      ret = g_list_prepend (NULL, bestnode->fac);
      bestnode = bestnode->prev;
      while (bestnode != NULL)
      {
        ret = g_list_prepend (ret, bestnode->fac);
        bestnode = bestnode->prev;
      }
      g_list_free_list_and_elements (factory_nodes);
      return ret;
    }
    
    /* iterate over all factories we have
     * if they have the current cost, calculate if this
     * factory supplies shorter paths to other elements
     */
    while (nodes)
    {
      if (((GstAutoplugNode *) nodes->data)->cost == curcost)
      {
        /* now check all elements if we got a shorter path */
        GList *sinknodes = factory_nodes;
        GstAutoplugNode *srcnode = (GstAutoplugNode *) nodes->data;
        while (sinknodes)
        {
          GstAutoplugNode *sinknode = (GstAutoplugNode *) sinknodes->data;
          GstPadTemplate *templ;
          if ((sinknode->cost > srcnode->cost + gst_autoplug_get_cost (sinknode->fac)) && (templ = gst_autoplug_can_match(srcnode->fac, sinknode->fac)))
          {
            /* we got a shorter path
             * now enter that path to that node */
            sinknode->prev = srcnode;
            sinknode->templ = templ;
            sinknode->cost = srcnode->cost + gst_autoplug_get_cost (sinknode->fac);
            /* make sure to set which cost to view next */
            nextcost = (nextcost > sinknode->cost) ? sinknode->cost : nextcost;
            /* did we get a new best node? */
            if (sinknode->endpoint && (sinknode->cost < bestnode->cost))
            {
              bestnode = sinknode;
            }      
          }
          sinknodes = g_list_next (sinknodes);
        }
        /* FIXME: for speed remove the item we just iterated with from the factory_nodes
         * but don't free it yet and don't forget to free it.
         */
      }
      nodes = g_list_next (nodes);
    }
    curcost = nextcost;
  }
  
  GST_DEBUG ("found no path from source caps to sink caps");    
  g_list_free_list_and_elements (factory_nodes);
  return NULL;  
}
