/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gststaticautoplug.c: A static Autoplugger of pipelines
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

#include "gststaticautoplugrender.h"

#include <gst/gst.h>

#define GST_AUTOPLUG_MAX_COST 999999

typedef guint   (*GstAutoplugCostFunction) (gpointer src, gpointer dest, gpointer data);
typedef GList*  (*GstAutoplugListFunction) (gpointer data);


static void     	gst_static_autoplug_render_class_init	(GstStaticAutoplugRenderClass *klass);
static void     	gst_static_autoplug_render_init	(GstStaticAutoplugRender *autoplug);

static GList*		gst_autoplug_func		(gpointer src, gpointer sink,
						 	 GstAutoplugListFunction list_function,
						 	 GstAutoplugCostFunction cost_function,
						 	 gpointer data);



static GstElement*	gst_static_autoplug_to_render	(GstAutoplug *autoplug, 
						  	 GstCaps *srccaps, GstElement *target, va_list args);

static GstAutoplugClass *parent_class = NULL;

GtkType gst_static_autoplug_render_get_type(void)
{
  static GtkType static_autoplug_type = 0;

  if (!static_autoplug_type) {
    static const GtkTypeInfo static_autoplug_info = {
      "GstStaticAutoplugRender",
      sizeof(GstElement),
      sizeof(GstElementClass),
      (GtkClassInitFunc)gst_static_autoplug_render_class_init,
      (GtkObjectInitFunc)gst_static_autoplug_render_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    static_autoplug_type = gtk_type_unique (GST_TYPE_AUTOPLUG, &static_autoplug_info);
  }
  return static_autoplug_type;
}

static void
gst_static_autoplug_render_class_init(GstStaticAutoplugRenderClass *klass)
{
  GstAutoplugClass *gstautoplug_class;

  gstautoplug_class = (GstAutoplugClass*) klass;

  parent_class = gtk_type_class(GST_TYPE_AUTOPLUG);

  gstautoplug_class->autoplug_to_renderers = gst_static_autoplug_to_render;
}

static void gst_static_autoplug_render_init(GstStaticAutoplugRender *autoplug) {
}

GstPlugin*
plugin_init (GModule *module)
{
  GstPlugin *plugin;
  GstAutoplugFactory *factory;

  plugin = gst_plugin_new("gststaticautoplugrender", GST_VERSION_MAJOR, GST_VERSION_MINOR);
  g_return_val_if_fail(plugin != NULL,NULL);

  gst_plugin_set_longname (plugin, "A static autoplugger");

  factory = gst_autoplugfactory_new ("staticrender",
		  "A static autoplugger, it constructs the complete element before running it",
		  gst_static_autoplug_render_get_type ());

  if (factory != NULL) {
     gst_plugin_add_autoplugger (plugin, factory);
  }
  return plugin;
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
	if (gst_caps_check_compatibility (GST_PADTEMPLATE_CAPS (srctemp), GST_PADTEMPLATE_CAPS (desttemp))) {
	  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT,
			  "factory \"%s\" can connect with factory \"%s\"", src->name, dest->name);
          return TRUE;
	}
      }

      desttemps = g_list_next (desttemps);
    }
    srctemps = g_list_next (srctemps);
  }
  GST_DEBUG (GST_CAT_AUTOPLUG_ATTEMPT,
		  "factory \"%s\" cannot connect with factory \"%s\"", src->name, dest->name);
  return FALSE;
}

static gboolean
gst_autoplug_pads_autoplug_func (GstElement *src, GstPad *pad, GstElement *sink)
{
  GList *sinkpads;
  gboolean connected = FALSE;

  GST_DEBUG (0,"gstpipeline: autoplug pad connect function for %s %s:%s to \"%s\"\n",
		  GST_ELEMENT_NAME (src), GST_DEBUG_PAD_NAME(pad), GST_ELEMENT_NAME(sink));

  sinkpads = gst_element_get_pad_list(sink);
  while (sinkpads) {
    GstPad *sinkpad = (GstPad *)sinkpads->data;

    // if we have a match, connect the pads
    if (gst_pad_get_direction(sinkpad) == GST_PAD_SINK &&
        !GST_PAD_CONNECTED (pad) && !GST_PAD_CONNECTED(sinkpad))
    {
      if ((connected = gst_pad_connect (pad, sinkpad))) {
	break;
      }
      else {
        GST_DEBUG (0,"pads incompatible %s, %s\n", GST_PAD_NAME (pad), GST_PAD_NAME (sinkpad));
      }
    }
    sinkpads = g_list_next(sinkpads);
  }

  if (!connected) {
    GST_DEBUG (0,"gstpipeline: no path to sinks for type\n");
  }
  return connected;
}

static void
gst_autoplug_pads_autoplug (GstElement *src, GstElement *sink)
{
  GList *srcpads;
  gboolean connected = FALSE;

  srcpads = gst_element_get_pad_list(src);

  while (srcpads && !connected) {
    GstPad *srcpad = (GstPad *)srcpads->data;

    if (gst_pad_get_direction(srcpad) == GST_PAD_SRC) {
      connected = gst_autoplug_pads_autoplug_func (src, srcpad, sink);
      if (connected)
        break;
    }

    srcpads = g_list_next(srcpads);
  }

  if (!connected) {
    GST_DEBUG (0,"gstpipeline: delaying pad connections for \"%s\" to \"%s\"\n",
		    GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
    gtk_signal_connect(GTK_OBJECT(src),"new_pad",
                 GTK_SIGNAL_FUNC(gst_autoplug_pads_autoplug_func), sink);
    gtk_signal_connect(GTK_OBJECT(src),"new_ghost_pad",
                 GTK_SIGNAL_FUNC(gst_autoplug_pads_autoplug_func), sink);
  }
}

static GList*
gst_autoplug_elementfactory_get_list (gpointer data)
{
  return gst_elementfactory_get_list ();
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
    //GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"caps %d to caps %d %d", ((GstCaps *)src)->id, ((GstCaps *)dest)->id, res);
  }
  else if (IS_CAPS (src)) {
    res = gst_elementfactory_can_sink_caps ((GstElementFactory *)dest, (GstCaps *)src);
    //GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"factory %s to src caps %d %d", ((GstElementFactory *)dest)->name, ((GstCaps *)src)->id, res);
  }
  else if (IS_CAPS (dest)) {
    res = gst_elementfactory_can_src_caps ((GstElementFactory *)src, (GstCaps *)dest);
    //GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"factory %s to sink caps %d %d", ((GstElementFactory *)src)->name, ((GstCaps *)dest)->id, res);
  }
  else {
    res = gst_autoplug_can_match ((GstElementFactory *)src, (GstElementFactory *)dest);
  }

  if (res)
    return 1;
  else
    return GST_AUTOPLUG_MAX_COST;
}

static GstElement*
gst_static_autoplug_to_render (GstAutoplug *autoplug, GstCaps *srccaps, GstElement *target, va_list args)
{
  caps_struct caps;
  GstElement *targetelement;
  GstElement *result = NULL, *srcelement = NULL;
  GList **factories;
  GList *chains = NULL;
  GList *endelements = NULL;
  guint numsinks = 0, i;
  gboolean have_common = FALSE;

  targetelement = target;

  /*
   * We first create a list of elements that are needed
   * to convert the srcpad caps to the different sinkpad caps.
   * and add the list of elementfactories to a list (chains).
   */
  caps.src  = srccaps;

  while (targetelement) {
    GList *elements;
    GstRealPad *pad;
    GstPadTemplate *templ;

    pad = GST_PAD_REALIZE (gst_element_get_pad_list (targetelement)->data);
    templ = GST_PAD_PADTEMPLATE (pad);

    caps.sink = GST_PADTEMPLATE_CAPS (templ);

    GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"autoplugging two caps structures");

    elements =  gst_autoplug_func (caps.src, caps.sink,
				   gst_autoplug_elementfactory_get_list,
				   gst_autoplug_caps_find_cost,
				   &caps);

    if (elements) {
      chains = g_list_append (chains, elements);
      endelements = g_list_append (endelements, targetelement);
      numsinks++;
    }
    else {
    }

    targetelement = va_arg (args, GstElement *);
  }

  /*
   * If no list could be found the pipeline cannot be autoplugged and
   * we return a NULL element
   */
  if (numsinks == 0)
    return NULL;

  /*
   * We now have a list of lists. We will turn this into an array
   * of lists, this will make it much more easy to manipulate it
   * in the next steps.
   */
  factories = g_new0 (GList *, numsinks);

  for (i = 0; chains; i++) {
    GList *elements = (GList *) chains->data;

    factories[i] = elements;

    chains = g_list_next (chains);
  }
  //FIXME, free the list

  result = gst_bin_new ("autoplug_bin");

  /*
   * We now hav a list of lists that is probably like:
   *
   *  !
   *  A -> B -> C
   *  !
   *  A -> D -> E
   *
   * we now try to find the common elements (A) and add them to
   * the bin. We remove them from both lists too.
   */
  while (factories[0]) {
    GstElementFactory *factory;
    GstElement *element;

    // fase 3: add common elements
    factory = (GstElementFactory *) (factories[0]->data);

    // check to other paths for matching elements (factories)
    for (i=1; i<numsinks; i++) {
      if (factory != (GstElementFactory *) (factories[i]->data)) {
	goto differ;
      }
    }

    GST_DEBUG (0,"common factory \"%s\"\n", factory->name);

    element = gst_elementfactory_create (factory, factory->name);
    gst_bin_add (GST_BIN(result), element);

    if (srcelement != NULL) {
      gst_autoplug_pads_autoplug (srcelement, element);
    }
    // this is the first element, find a good ghostpad
    else {
      GList *pads;

      pads = gst_element_get_pad_list (element);

      while (pads) {
	GstPad *pad = GST_PAD (pads->data);
	GstPadTemplate *templ = GST_PAD_PADTEMPLATE (pad);

	if (gst_caps_check_compatibility (srccaps, GST_PADTEMPLATE_CAPS (templ))) {
          gst_element_add_ghost_pad (result, pad, "sink");
	  break;
	}

	pads = g_list_next (pads);
      }
    }
    gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (element));

    srcelement = element;

    // advance the pointer in all lists
    for (i=0; i<numsinks; i++) {
      factories[i] = g_list_next (factories[i]);
    }

    have_common = TRUE;
  }

differ:

  // loop over all the sink elements
  for (i = 0; i < numsinks; i++) {
    GstElement *thesrcelement = srcelement;
    GstElement *thebin = GST_ELEMENT(result);
    GstElement *sinkelement;
    gboolean use_thread;

    sinkelement = GST_ELEMENT (endelements->data);
    endelements = g_list_next (endelements);

    use_thread = have_common;

    while (factories[i] || sinkelement) {
      // fase 4: add other elements...
      GstElementFactory *factory;
      GstElement *element;

      if (factories[i]) {
        factory = (GstElementFactory *)(factories[i]->data);

        GST_DEBUG (0,"factory \"%s\"\n", factory->name);
        element = gst_elementfactory_create(factory, factory->name);
      }
      else {
	element = sinkelement;
	sinkelement = NULL;
      }

      // this element suggests the use of a thread, so we set one up...
      if (GST_ELEMENT_IS_THREAD_SUGGESTED(element) || use_thread) {
        GstElement *queue;
        GstPad *srcpad;

	use_thread = FALSE;

        GST_DEBUG (0,"sugest new thread for \"%s\" %08x\n", GST_ELEMENT_NAME (element), GST_FLAGS(element));

	// create a new queue and add to the previous bin
        queue = gst_elementfactory_make("queue", g_strconcat("queue_", GST_ELEMENT_NAME(element), NULL));
        GST_DEBUG (0,"adding element \"%s\"\n", GST_ELEMENT_NAME (element));
        gst_bin_add(GST_BIN(thebin), queue);
        gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (queue));

	// this will be the new bin for all following elements
        thebin = gst_elementfactory_make("thread", g_strconcat("thread_", GST_ELEMENT_NAME(element), NULL));

        srcpad = gst_element_get_pad(queue, "src");

        gst_autoplug_pads_autoplug(thesrcelement, queue);

	GST_DEBUG (0,"adding element %s\n", GST_ELEMENT_NAME (element));
        gst_bin_add(GST_BIN(thebin), element);
        gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (element));
	GST_DEBUG (0,"adding element %s\n", GST_ELEMENT_NAME (thebin));
        gst_bin_add(GST_BIN(result), thebin);
        gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (thebin));
        thesrcelement = queue;
      }
      // no thread needed, easy case
      else {
	GST_DEBUG (0,"adding element %s\n", GST_ELEMENT_NAME (element));
        gst_bin_add(GST_BIN(thebin), element);
        gst_autoplug_signal_new_object (GST_AUTOPLUG (autoplug), GST_OBJECT (element));
      }
      gst_autoplug_pads_autoplug(thesrcelement, element);

      // this element is now the new source element
      thesrcelement = element;

      factories[i] = g_list_next(factories[i]);
    }
  }

  return result;
}

/*
 * shortest path algorithm
 *
 */
struct _gst_autoplug_node
{
  gpointer iNode;
  gpointer iPrev;
  gint iDist;
};

typedef struct _gst_autoplug_node gst_autoplug_node;

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

  GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"factories found in autoplugging (reversed order)");

  while (current != NULL)
  {
    gpointer next = NULL;

    next = rgnNodes[find_factory(rgnNodes, current)].iPrev;
    if (next) {
      factories = g_list_prepend (factories, current);
      GST_INFO (GST_CAT_AUTOPLUG_ATTEMPT,"factory: \"%s\"", current->name);
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
        if ((GST_AUTOPLUG_MAX_COST == rgnNodes[i].iDist) ||
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

