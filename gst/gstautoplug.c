/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstautoplug.c: Autoplugging of pipelines
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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstautoplug.h"

static void     gst_autoplug_class_init (GstAutoplugClass *klass);
static void     gst_autoplug_init       (GstAutoplug *autoplug);

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

static GstObjectClass *parent_class = NULL;

GtkType gst_autoplug_get_type(void) {
  static GtkType autoplug_type = 0;

  if (!autoplug_type) {
    static const GtkTypeInfo autoplug_info = {
      "GstAutoplug",
      sizeof(GstElement),
      sizeof(GstElementClass),
      (GtkClassInitFunc)gst_autoplug_class_init,
      (GtkObjectInitFunc)gst_autoplug_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    autoplug_type = gtk_type_unique(GST_TYPE_AUTOPLUG,&autoplug_info);
  }
  return autoplug_type;
}

static void
gst_autoplug_class_init(GstAutoplugClass *klass) {
  parent_class = gtk_type_class(GST_TYPE_OBJECT);
}

static void gst_autoplug_init(GstAutoplug *autoplug) {
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
	if (gst_caps_list_check_compatibility (srctemp->caps, desttemp->caps)) {
	  GST_INFO (0,"factory \"%s\" can connect with factory \"%s\"", src->name, dest->name);
          return TRUE;
	}
      }

      desttemps = g_list_next (desttemps);
    }
    srctemps = g_list_next (srctemps);
  }
  GST_INFO (0,"factory \"%s\" cannot connect with factory \"%s\"", src->name, dest->name);
  return FALSE;
}
	
static GList*
gst_autoplug_elementfactory_get_list (gpointer data)
{
  return gst_elementfactory_get_list ();
}

typedef struct {
  GList *src;
  GList *sink;
} caps_struct;

#define IS_CAPS(cap) (((cap) == caps->src) || (cap) == caps->sink)

static guint 
gst_autoplug_caps_find_cost (gpointer src, gpointer dest, gpointer data) 
{
  caps_struct *caps = (caps_struct *)data;
  gboolean res;

  if (IS_CAPS (src) && IS_CAPS (dest)) {
    res = gst_caps_list_check_compatibility ((GList *)src, (GList *)dest);
    //GST_INFO (0,"caps %d to caps %d %d", ((GstCaps *)src)->id, ((GstCaps *)dest)->id, res);
  }
  else if (IS_CAPS (src)) {
    res = gst_elementfactory_can_sink_caps_list ((GstElementFactory *)dest, (GList *)src);
    //GST_INFO (0,"factory %s to src caps %d %d", ((GstElementFactory *)dest)->name, ((GstCaps *)src)->id, res);
  }
  else if (IS_CAPS (dest)) {
    res = gst_elementfactory_can_src_caps_list ((GstElementFactory *)src, (GList *)dest);
    //GST_INFO (0,"factory %s to sink caps %d %d", ((GstElementFactory *)src)->name, ((GstCaps *)dest)->id, res);
  }
  else {
    res = gst_autoplug_can_match ((GstElementFactory *)src, (GstElementFactory *)dest);
  }

  if (res) 
    return 1;
  else 
    return GST_AUTOPLUG_MAX_COST;
}

/**
 * gst_autoplug_caps:
 * @srccaps: the source caps
 * @sinkcaps: the sink caps
 *
 * Perform autoplugging between the two given caps.
 *
 * Returns: a list of elementfactories that can connect
 * the two caps
 */
GList*
gst_autoplug_caps (GstCaps *srccaps, GstCaps *sinkcaps) 
{
  caps_struct caps;

  caps.src = g_list_prepend (NULL,srccaps);
  caps.sink = g_list_prepend (NULL,sinkcaps);

  GST_INFO (0,"autoplugging two caps structures");

  return gst_autoplug_func (caps.src, caps.sink, 
      	  		    gst_autoplug_elementfactory_get_list, 
			    gst_autoplug_caps_find_cost,
			    &caps);
}

/**
 * gst_autoplug_caps_list:
 * @srccaps: the source caps list
 * @sinkcaps: the sink caps list
 *
 * Perform autoplugging between the two given caps lists.
 *
 * Returns: a list of elementfactories that can connect
 * the two caps lists
 */
GList*
gst_autoplug_caps_list (GList *srccaps, GList *sinkcaps) 
{
  caps_struct caps;

  caps.src = srccaps;
  caps.sink = sinkcaps;

  GST_INFO (0,"autoplugging two caps list structures");

  return gst_autoplug_func (caps.src, caps.sink, 
      	  		    gst_autoplug_elementfactory_get_list, 
			    gst_autoplug_caps_find_cost,
			    &caps);
}

/**
 * gst_autoplug_pads:
 * @srcpad: the source pad
 * @sinkpad: the sink pad
 *
 * Perform autoplugging between the two given pads
 *
 * Returns: a list of elementfactories that can connect
 * the two pads
 */
GList*
gst_autoplug_pads (GstPad *srcpad, GstPad *sinkpad) 
{
  caps_struct caps;

  caps.src = srcpad->caps;
  caps.sink = sinkpad->caps;

  GST_INFO (0,"autoplugging two caps structures");

  return gst_autoplug_func (caps.src, caps.sink, 
      	  		    gst_autoplug_elementfactory_get_list, 
			    gst_autoplug_caps_find_cost,
			    &caps);
}
static gint
find_factory (gst_autoplug_node *rgnNodes, gpointer factory)
{
  gint i=0;

  while (rgnNodes[i].iNode) {
    if (rgnNodes[i].iNode == factory) return i;
    i++;  
  }
  return 0;
}

static GList*
construct_path (gst_autoplug_node *rgnNodes, gpointer factory)
{
  GstElementFactory *current;
  GList *factories = NULL;
  
  current = rgnNodes[find_factory(rgnNodes, factory)].iPrev;

  GST_INFO (0,"factories found in autoplugging (reversed order)");

  while (current != NULL)
  { 
    gpointer next = NULL;
    
    next = rgnNodes[find_factory(rgnNodes, current)].iPrev;
    if (next) {
      factories = g_list_prepend (factories, current);
      GST_INFO (0,"factory: \"%s\"", current->name);
    }
    current = next;
  }
  return factories;
}

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
      rgnNodes[i].iDist = GST_AUTOPLUG_MAX_COST;
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
      if (iCost != GST_AUTOPLUG_MAX_COST) {
        if((GST_AUTOPLUG_MAX_COST == rgnNodes[i].iDist) ||
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

