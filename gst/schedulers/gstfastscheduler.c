/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstscheduler.c: Default scheduling code for most cases
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

/*#define GST_DEBUG_ENABLED */
#include <gst/gst.h>
#include <../cothreads.h>

typedef struct _GstSchedulerChain GstSchedulerChain;

#define GST_PAD_THREADSTATE(pad)		(cothread_state*) (GST_PAD_CAST (pad)->sched_private)
#define GST_ELEMENT_THREADSTATE(elem)		(cothread_state*) (GST_ELEMENT_CAST (elem)->sched_private)
#define GST_BIN_THREADCONTEXT(bin)		(cothread_context*) (GST_BIN_CAST (bin)->sched_private)

#define GST_ELEMENT_COTHREAD_STOPPING			GST_ELEMENT_SCHEDULER_PRIVATE1
#define GST_ELEMENT_IS_COTHREAD_STOPPING(element)	GST_FLAG_IS_SET((element), GST_ELEMENT_COTHREAD_STOPPING)
#define GST_ELEMENT_INTERRUPTED				GST_ELEMENT_SCHEDULER_PRIVATE2
#define GST_ELEMENT_IS_INTERRUPTED(element)		GST_FLAG_IS_SET((element), GST_ELEMENT_INTERRUPTED)

typedef struct _GstFastScheduler GstFastScheduler;
typedef struct _GstFastSchedulerClass GstFastSchedulerClass;

struct _GstSchedulerChain {
  GstFastScheduler *sched;

  GList *disabled;

  GList *elements;
  gint num_elements;

  GstElement *entry;

  GList *cothreaded_elements;
  gint num_cothreaded;

  gboolean schedule;
};

#define GST_TYPE_FAST_SCHEDULER \
  (gst_fast_scheduler_get_type())
#define GST_FAST_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FAST_SCHEDULER,GstFastScheduler))
#define GST_FAST_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FAST_SCHEDULER,GstFastSchedulerClass))
#define GST_IS_FAST_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FAST_SCHEDULER))
#define GST_IS_FAST_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FAST_SCHEDULER))

#define GST_FAST_SCHEDULER_CAST(sched)	((GstFastScheduler *)(sched))

typedef enum {
  GST_FAST_SCHEDULER_STATE_NONE,
  GST_FAST_SCHEDULER_STATE_STOPPED,
  GST_FAST_SCHEDULER_STATE_ERROR,
  GST_FAST_SCHEDULER_STATE_RUNNING,
} GstFastSchedulerState;

struct _GstFastScheduler {
  GstScheduler parent;

  GList *elements;
  gint num_elements;

  GList *chains;
  gint num_chains;

  GstFastSchedulerState state;

};

struct _GstFastSchedulerClass {
  GstSchedulerClass parent_class;
};

static GType _gst_fast_scheduler_type = 0;

static void 		gst_fast_scheduler_class_init 		(GstFastSchedulerClass * klass);
static void 		gst_fast_scheduler_init 		(GstFastScheduler * scheduler);

static void 		gst_fast_scheduler_dispose 		(GObject *object);

static void 		gst_fast_scheduler_setup 		(GstScheduler *sched);
static void 		gst_fast_scheduler_reset 		(GstScheduler *sched);
static void		gst_fast_scheduler_add_element		(GstScheduler *sched, GstElement *element);
static void     	gst_fast_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
static GstElementStateReturn  
			gst_fast_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
static void 		gst_fast_scheduler_lock_element 	(GstScheduler *sched, GstElement *element);
static void 		gst_fast_scheduler_unlock_element 	(GstScheduler *sched, GstElement *element);
static void 		gst_fast_scheduler_yield 		(GstScheduler *sched, GstElement *element);
static gboolean		gst_fast_scheduler_interrupt 		(GstScheduler *sched, GstElement *element);
static void 		gst_fast_scheduler_error	 	(GstScheduler *sched, GstElement *element);
static void     	gst_fast_scheduler_pad_connect		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static void     	gst_fast_scheduler_pad_disconnect 	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static GstPad*  	gst_fast_scheduler_pad_select 		(GstScheduler *sched, GList *padlist);
static GstSchedulerState
			gst_fast_scheduler_iterate    		(GstScheduler *sched);
/* defined but not used
static void     	gst_fast_scheduler_show  		(GstScheduler *sched);
*/
static GstSchedulerClass *parent_class = NULL;

static GType
gst_fast_scheduler_get_type (void)
{
  if (!_gst_fast_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstFastSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_fast_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstFastScheduler),
      0,
      (GInstanceInitFunc) gst_fast_scheduler_init,
      NULL
    };

    _gst_fast_scheduler_type = g_type_register_static (GST_TYPE_SCHEDULER, "GstFastScheduler", &scheduler_info, 0);
  }
  return _gst_fast_scheduler_type;
}

static void
gst_fast_scheduler_class_init (GstFastSchedulerClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstSchedulerClass *gstscheduler_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstscheduler_class = (GstSchedulerClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_SCHEDULER);

  gobject_class->dispose	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_dispose);

  gstscheduler_class->setup 		= GST_DEBUG_FUNCPTR (gst_fast_scheduler_setup);
  gstscheduler_class->reset	 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_reset);
  gstscheduler_class->add_element 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_add_element);
  gstscheduler_class->remove_element 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_remove_element);
  gstscheduler_class->state_transition 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_state_transition);
  gstscheduler_class->lock_element 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_lock_element);
  gstscheduler_class->unlock_element 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_unlock_element);
  gstscheduler_class->yield	 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_yield);
  gstscheduler_class->interrupt 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_interrupt);
  gstscheduler_class->error	 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_error);
  gstscheduler_class->pad_connect 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_pad_connect);
  gstscheduler_class->pad_disconnect 	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_pad_disconnect);
  gstscheduler_class->pad_select	= GST_DEBUG_FUNCPTR (gst_fast_scheduler_pad_select);
  gstscheduler_class->iterate 		= GST_DEBUG_FUNCPTR (gst_fast_scheduler_iterate);
}

static void
gst_fast_scheduler_init (GstFastScheduler *scheduler)
{
  scheduler->elements = NULL;
  scheduler->num_elements = 0;
  scheduler->chains = NULL;
  scheduler->num_chains = 0;
}

static void
gst_fast_scheduler_dispose (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstSchedulerFactory *factory;

  gst_plugin_set_longname (plugin, "A fast scheduler");

  factory = gst_scheduler_factory_new ("fast",
	                              "A fast scheduler, it uses cothreads",
		                      gst_fast_scheduler_get_type());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register scheduler: fast");
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstfastscheduler",
  plugin_init
};

static int
gst_fast_scheduler_loopfunc_wrapper (int argc, char *argv[])
{
  GstElement *element = GST_ELEMENT_CAST (argv);
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);

  GST_DEBUG_ENTER ("(%d,'%s')", argc, name);

  do {
    GST_DEBUG (GST_CAT_DATAFLOW, "calling loopfunc %s for element %s\n",
	       GST_DEBUG_FUNCPTR_NAME (element->loopfunc), name);
    (element->loopfunc) (element);
    GST_DEBUG (GST_CAT_DATAFLOW, "element %s ended loop function\n", name);

  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
  GST_FLAG_UNSET (element, GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE ("(%d,'%s')", argc, name);
  return 0;
}

static void 
gst_fast_scheduler_chainfunc_proxy (GstPad *pad, GstBuffer *buffer)
{
  GstElement *element = GST_PAD_PARENT (pad);

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  GST_RPAD_BUFPEN (pad) = buffer;

  while (GST_RPAD_BUFPEN (pad) != NULL) {
    if (GST_ELEMENT_THREADSTATE (element)) {
      cothread_switch (GST_ELEMENT_THREADSTATE (element));
    }
    else {
      g_assert_not_reached();
    }
  }
  GST_DEBUG_LEAVE ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));
}

static GstBuffer* 
gst_fast_scheduler_getfunc_proxy (GstPad *pad)
{
  GstElement *element = GST_PAD_PARENT (pad);
  GstPad *peer = GST_PAD_CAST (GST_RPAD_PEER (pad));
  GstBuffer *buf;

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  while (GST_RPAD_BUFPEN (peer) == NULL) {
    if (GST_ELEMENT_THREADSTATE (element)) {
      cothread_switch (GST_ELEMENT_THREADSTATE (element));
    }
    else {
      g_assert_not_reached();
    }
  }

  GST_DEBUG_LEAVE ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));
  buf = GST_RPAD_BUFPEN (peer);
  GST_RPAD_BUFPEN (peer) = NULL;

  return buf;
}

static gboolean
gst_fast_scheduler_cothreaded_element (GstBin * bin, GstElement *element)
{
  cothread_func wrapper_function;
  GList *pads;

  GST_DEBUG (GST_CAT_SCHEDULING, "element is using COTHREADS\n");

  g_assert (GST_BIN_THREADCONTEXT (bin) != NULL);

  wrapper_function = GST_DEBUG_FUNCPTR (gst_fast_scheduler_loopfunc_wrapper);

  if (GST_ELEMENT_THREADSTATE (element) == NULL) {
    GST_ELEMENT_THREADSTATE (element) = cothread_create (GST_BIN_THREADCONTEXT (bin));
    if (GST_ELEMENT_THREADSTATE (element) == NULL) {
      gst_element_error (element, "could not create cothread for \"%s\"", 
			  GST_ELEMENT_NAME (element), NULL);
      return FALSE;
    }
    GST_DEBUG (GST_CAT_SCHEDULING, "created cothread %p for '%s'\n", 
		   GST_ELEMENT_THREADSTATE (element),
		   GST_ELEMENT_NAME (element));
    cothread_setfunc (GST_ELEMENT_THREADSTATE (element), wrapper_function, 0, (char **) element);
    GST_DEBUG (GST_CAT_SCHEDULING, "set wrapper function for '%s' to &%s\n",
		 GST_ELEMENT_NAME (element), GST_DEBUG_FUNCPTR_NAME (wrapper_function));
  }

  pads = gst_element_get_pad_list (element);

  while (pads) {
    GstPad *pad = GST_PAD_CAST (pads->data);
    pads = g_list_next (pads);

    if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
      GST_DEBUG (GST_CAT_SCHEDULING, "setting gethandler to getfunc_proxy for %s:%s\n", GST_DEBUG_PAD_NAME (pad));
      GST_RPAD_GETHANDLER (pad) = gst_fast_scheduler_getfunc_proxy;
    }
    else {
      GST_DEBUG (GST_CAT_SCHEDULING, "setting chainhandler to chainfunc_proxy for %s:%s\n", GST_DEBUG_PAD_NAME (pad));
      GST_RPAD_CHAINHANDLER (pad) = gst_fast_scheduler_chainfunc_proxy;
    }
  }

  return TRUE;
}

static gboolean
gst_fast_scheduler_chained_element (GstBin *bin, GstElement *element) {
  GList *pads;
  GstPad *pad;

  GST_DEBUG (GST_CAT_SCHEDULING,"chain entered\n");

  // walk through all the pads
  pads = gst_element_get_pad_list (element);
  while (pads) {
    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);
    if (!GST_IS_REAL_PAD (pad)) 
      continue;

    if (GST_RPAD_DIRECTION (pad) == GST_PAD_SINK) {
      GST_DEBUG (GST_CAT_SCHEDULING,"copying chain function into chain handler for %s:%s\n",GST_DEBUG_PAD_NAME (pad));
      GST_RPAD_CHAINHANDLER (pad) = GST_RPAD_CHAINFUNC (pad);
    } else {
      GST_DEBUG (GST_CAT_SCHEDULING,"copying get function into get handler for %s:%s\n",GST_DEBUG_PAD_NAME (pad));
      if (!GST_RPAD_GETFUNC (pad)) 
        GST_RPAD_GETHANDLER (pad) = gst_fast_scheduler_getfunc_proxy;
      else 
        GST_RPAD_GETHANDLER (pad) = GST_RPAD_GETFUNC (pad);
    }
  }

  return TRUE;
}


static GstSchedulerChain *
gst_fast_scheduler_chain_new (GstFastScheduler * sched)
{
  GstSchedulerChain *chain = g_new (GstSchedulerChain, 1);

  /* initialize the chain with sane values */
  chain->sched = sched;
  chain->disabled = NULL;
  chain->elements = NULL;
  chain->num_elements = 0;
  chain->entry = NULL;
  chain->cothreaded_elements = NULL;
  chain->num_cothreaded = 0;
  chain->schedule = FALSE;

  /* add the chain to the schedulers' list of chains */
  sched->chains = g_list_prepend (sched->chains, chain);
  sched->num_chains++;

  GST_INFO (GST_CAT_SCHEDULING, "created new chain %p, now are %d chains in sched %p",
	    chain, sched->num_chains, sched);

  return chain;
}

static void
gst_fast_scheduler_chain_destroy (GstSchedulerChain * chain)
{
  GstFastScheduler *sched = chain->sched;


  /* remove the chain from the schedulers' list of chains */
  sched->chains = g_list_remove (sched->chains, chain);
  sched->num_chains--;

  /* destroy the chain */
  g_list_free (chain->disabled);	/* should be empty... */
  g_list_free (chain->elements);	/* ditto 	      */

  GST_INFO (GST_CAT_SCHEDULING, "destroyed chain %p, now are %d chains in sched %p", chain,
	    sched->num_chains, sched);

  g_free (chain);
}

static gboolean
gst_fast_scheduler_chain_enable_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "enabling element \"%s\" in chain %p, %d cothreaded elements", 
		  GST_ELEMENT_NAME (element), chain, chain->num_cothreaded);

  /* remove from disabled list */
  chain->disabled = g_list_remove (chain->disabled, element);

  /* add to elements list */
  chain->elements = g_list_prepend (chain->elements, element);

  /* reschedule the chain */
  if (element->loopfunc) {
    chain->cothreaded_elements = g_list_prepend (chain->cothreaded_elements, element);
    chain->num_cothreaded++;

    return gst_fast_scheduler_cothreaded_element (GST_BIN (GST_SCHEDULER (chain->sched)->parent), element);
  }
  else {
    if (element->numsinkpads == 0 || GST_ELEMENT_IS_DECOUPLED (element)) {
      chain->entry = element;
    }

    return gst_fast_scheduler_chained_element (GST_BIN (GST_SCHEDULER (chain->sched)->parent), element);
  }
}

static void
gst_fast_scheduler_chain_disable_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "disabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* remove from elements list */
  chain->elements = g_list_remove (chain->elements, element);

  /* add to disabled list */
  chain->disabled = g_list_prepend (chain->disabled, element);

  if (element->loopfunc) {
    chain->cothreaded_elements = g_list_remove (chain->cothreaded_elements, element);
    chain->num_cothreaded--;
  }
  else {
    if (chain->entry == element)
      chain->entry = NULL;
  }
}

static void
gst_fast_scheduler_chain_add_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* set the sched pointer for the element */
  element->sched = GST_SCHEDULER (chain->sched);

  /* add the element to the list of 'disabled' elements */
  chain->disabled = g_list_prepend (chain->disabled, element);
  chain->num_elements++;
}

static void
gst_fast_scheduler_chain_remove_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* if it's active, deactivate it */
  if (g_list_find (chain->elements, element)) {
    gst_fast_scheduler_chain_disable_element (chain, element);
  }
  /* we have to check for a threadstate here because a queue doesn't have one */
  if (GST_ELEMENT_THREADSTATE (element)) {
    cothread_free (GST_ELEMENT_THREADSTATE (element));
    GST_ELEMENT_THREADSTATE (element) = NULL;
  }

  /* remove the element from the list of elements */
  chain->disabled = g_list_remove (chain->disabled, element);
  chain->num_elements--;

  /* if there are no more elements in the chain, destroy the chain */
  if (chain->num_elements == 0)
    gst_fast_scheduler_chain_destroy (chain);
}

static void
gst_fast_scheduler_chain_elements (GstFastScheduler * sched, GstElement * element1, GstElement * element2)
{
  GList *chains;
  GstSchedulerChain *chain;
  GstSchedulerChain *chain1 = NULL, *chain2 = NULL;
  GstElement *element;

  /* first find the chains that hold the two  */
  chains = sched->chains;
  while (chains) {
    chain = (GstSchedulerChain *) (chains->data);
    chains = g_list_next (chains);

    if (g_list_find (chain->disabled, element1))
      chain1 = chain;
    else if (g_list_find (chain->elements, element1))
      chain1 = chain;

    if (g_list_find (chain->disabled, element2))
      chain2 = chain;
    else if (g_list_find (chain->elements, element2))
      chain2 = chain;
  }

  /* first check to see if they're in the same chain, we're done if that's the case */
  if ((chain1 != NULL) && (chain1 == chain2)) {
    GST_INFO (GST_CAT_SCHEDULING, "elements are already in the same chain");
    return;
  }

  /* now, if neither element has a chain, create one */
  if ((chain1 == NULL) && (chain2 == NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "creating new chain to hold two new elements");
    chain = gst_fast_scheduler_chain_new (sched);
    gst_fast_scheduler_chain_add_element (chain, element1);
    gst_fast_scheduler_chain_add_element (chain, element2);

    /* otherwise if both have chains already, join them */
  }
  else if ((chain1 != NULL) && (chain2 != NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "merging chain %p into chain %p", chain2, chain1);
    /* take the contents of chain2 and merge them into chain1 */
    chain1->disabled = g_list_concat (chain1->disabled, g_list_copy (chain2->disabled));
    chain1->elements = g_list_concat (chain1->elements, g_list_copy (chain2->elements));
    //chain1->cothreaded_elements = g_list_concat (chain1->cothreaded_elements, g_list_copy (chain2->cothreaded_elements));
    chain1->num_elements += chain2->num_elements;
    //chain1->num_cothreaded += chain2->num_cothreaded;

    gst_fast_scheduler_chain_destroy (chain2);

    /* otherwise one has a chain already, the other doesn't */
  }
  else {
    /* pick out which one has the chain, and which doesn't */
    if (chain1 != NULL)
      chain = chain1, element = element2;
    else
      chain = chain2, element = element1;

    GST_INFO (GST_CAT_SCHEDULING, "adding element to existing chain");
    gst_fast_scheduler_chain_add_element (chain, element);
  }
}


/* find the chain within the scheduler that holds the element, if any */
static GstSchedulerChain *
gst_fast_scheduler_find_chain (GstFastScheduler * sched, GstElement * element)
{
  GList *chains;
  GstSchedulerChain *chain;

  GST_INFO (GST_CAT_SCHEDULING, "searching for element \"%s\" in chains",
	    GST_ELEMENT_NAME (element));

  chains = sched->chains;
  while (chains) {
    chain = (GstSchedulerChain *) (chains->data);
    chains = g_list_next (chains);

    if (g_list_find (chain->elements, element))
      return chain;
    if (g_list_find (chain->disabled, element))
      return chain;
  }

  return NULL;
}

static void
gst_fast_scheduler_chain_recursive_add (GstSchedulerChain * chain, GstElement * element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;

  /* add the element to the chain */
  gst_fast_scheduler_chain_add_element (chain, element);

  GST_DEBUG (GST_CAT_SCHEDULING, "recursing on element \"%s\"\n", GST_ELEMENT_NAME (element));
  /* now go through all the pads and see which peers can be added */
  pads = element->pads;
  while (pads) {
    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);

    GST_DEBUG (GST_CAT_SCHEDULING, "have pad %s:%s, checking for valid peer\n",
	       GST_DEBUG_PAD_NAME (pad));
    /* if the peer exists and could be in the same chain */
    if (GST_PAD_PEER (pad)) {
      GST_DEBUG (GST_CAT_SCHEDULING, "has peer %s:%s\n", GST_DEBUG_PAD_NAME (GST_PAD_PEER (pad)));
      peerelement = GST_PAD_PARENT (GST_PAD_PEER (pad));
      if (GST_ELEMENT_SCHED (GST_PAD_PARENT (pad)) == GST_ELEMENT_SCHED (peerelement)) {
	GST_DEBUG (GST_CAT_SCHEDULING, "peer \"%s\" is valid for same chain\n",
		   GST_ELEMENT_NAME (peerelement));
	/* if it's not already in a chain, add it to this one */
	if (gst_fast_scheduler_find_chain (chain->sched, peerelement) == NULL) {
	  gst_fast_scheduler_chain_recursive_add (chain, peerelement);
	}
      }
    }
  }
}

/*
 * Entry points for this scheduler.
 */
static void
gst_fast_scheduler_setup (GstScheduler *sched)
{
  GstBin *bin = GST_BIN (sched->parent);

  /* first create thread context */
  if (GST_BIN_THREADCONTEXT (bin) == NULL) {
    GST_DEBUG (GST_CAT_SCHEDULING, "initializing cothread context\n");
    GST_BIN_THREADCONTEXT (bin) = cothread_context_init ();
  }
}

static void
gst_fast_scheduler_reset (GstScheduler *sched)
{
  cothread_context *ctx;
  GList *elements = GST_FAST_SCHEDULER_CAST (sched)->elements;

  while (elements) {
    GST_ELEMENT_THREADSTATE (elements->data) = NULL;
    elements = g_list_next (elements);
  }
  
  ctx = GST_BIN_THREADCONTEXT (GST_SCHEDULER_PARENT (sched));

  cothread_context_free (ctx);
  
  GST_BIN_THREADCONTEXT (GST_SCHEDULER_PARENT (sched)) = NULL;
}

static void
gst_fast_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;
  GstSchedulerChain *chain;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);

  /* if it's already in this scheduler, don't bother doing anything */
  if (GST_ELEMENT_SCHED (element) == sched)
    return;

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to scheduler", GST_ELEMENT_NAME (element));

  /* if the element already has a different scheduler, remove the element from it */
  if (GST_ELEMENT_SCHED (element)) {
    gst_fast_scheduler_remove_element (GST_ELEMENT_SCHED (element), element);
  }

  /* set the sched pointer in the element itself */
  GST_ELEMENT_SCHED (element) = sched;

  /* only deal with elements after this point, not bins */
  /* exception is made for Bin's that are schedulable, like the autoplugger */
  if (GST_IS_BIN (element) && !GST_FLAG_IS_SET (element, GST_BIN_SELF_SCHEDULABLE))
    return;

  /* first add it to the list of elements that are to be scheduled */
  bsched->elements = g_list_prepend (bsched->elements, element);
  bsched->num_elements++;

  /* create a chain to hold it, and add */
  chain = gst_fast_scheduler_chain_new (bsched);
  gst_fast_scheduler_chain_add_element (chain, element);

  /* set the sched pointer in all the pads */
  pads = element->pads;
  while (pads) {
    pad = GST_PAD (pads->data);
    pads = g_list_next (pads);

    /* we only operate on real pads */
    if (!GST_IS_REAL_PAD (pad))
      continue;

    /* set the pad's sched pointer */
    gst_pad_set_sched (pad, sched);

    /* if the peer element exists and is a candidate */
    if (GST_PAD_PEER (pad)) {
      peerelement = GST_PAD_PARENT (GST_PAD_PEER (pad));
      if (GST_ELEMENT_SCHED (element) == GST_ELEMENT_SCHED (peerelement)) {
	GST_INFO (GST_CAT_SCHEDULING, "peer is in same scheduler, chaining together");
	/* make sure that the two elements are in the same chain */
	gst_fast_scheduler_chain_elements (bsched, element, peerelement);
      }
    }
  }
}

static void
gst_fast_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerChain *chain;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);

  if (g_list_find (bsched->elements, element)) {
    GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from scheduler",
	      GST_ELEMENT_NAME (element));

    /* find what chain the element is in */
    chain = gst_fast_scheduler_find_chain (bsched, element);

    /* remove it from its chain */
    gst_fast_scheduler_chain_remove_element (chain, element);

    /* remove it from the list of elements */
    bsched->elements = g_list_remove (bsched->elements, element);
    bsched->num_elements--;

    /* unset the scheduler pointer in the element */
    GST_ELEMENT_SCHED (element) = NULL;
  }
}

static GstElementStateReturn
gst_fast_scheduler_state_transition (GstScheduler *sched, GstElement *element, gint transition)
{
  GstSchedulerChain *chain;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);

  /* check if our parent changed state */
  if (GST_SCHEDULER_PARENT (sched) == element) {
    GST_INFO (GST_CAT_SCHEDULING, "parent \"%s\" changed state", GST_ELEMENT_NAME (element));
    if (transition == GST_STATE_PLAYING_TO_PAUSED) {
      GST_INFO (GST_CAT_SCHEDULING, "setting scheduler state to stopped");
      GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_STOPPED;
    }
    else if (transition == GST_STATE_PAUSED_TO_PLAYING) {
      GST_INFO (GST_CAT_SCHEDULING, "setting scheduler state to running");
      GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_RUNNING;
    }
    else {
      GST_INFO (GST_CAT_SCHEDULING, "no interesting state change, doing nothing");
    }
  }
  else if (transition == GST_STATE_PLAYING_TO_PAUSED ||
           transition == GST_STATE_PAUSED_TO_PLAYING) {
    /* find the chain the element is in */
    chain = gst_fast_scheduler_find_chain (bsched, element);

    /* remove it from the chain */
    if (chain) {
      if (transition == GST_STATE_PLAYING_TO_PAUSED) {
        gst_fast_scheduler_chain_disable_element (chain, element);
      }
      else if (transition == GST_STATE_PAUSED_TO_PLAYING) {
        if (!gst_fast_scheduler_chain_enable_element (chain, element)) {
          GST_INFO (GST_CAT_SCHEDULING, "could not enable element \"%s\"", GST_ELEMENT_NAME (element));
          return GST_STATE_FAILURE;
        }
      }
    }
    else {
      GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" not found in any chain, no state change", GST_ELEMENT_NAME (element));
    }
  }

  return GST_STATE_SUCCESS;
}

static void
gst_fast_scheduler_lock_element (GstScheduler * sched, GstElement * element)
{
  if (GST_ELEMENT_THREADSTATE (element))
    cothread_lock (GST_ELEMENT_THREADSTATE (element));
}

static void
gst_fast_scheduler_unlock_element (GstScheduler * sched, GstElement * element)
{
  if (GST_ELEMENT_THREADSTATE (element))
    cothread_unlock (GST_ELEMENT_THREADSTATE (element));
}

static void
gst_fast_scheduler_yield (GstScheduler *sched, GstElement *element)
{
  if (GST_ELEMENT_IS_COTHREAD_STOPPING (element)) {
    cothread_switch (cothread_current_main ());
  }
}

static gboolean
gst_fast_scheduler_interrupt (GstScheduler *sched, GstElement *element)
{
  if (cothread_current () != cothread_current_main()) {
    cothread_switch (cothread_current_main ());
    return FALSE;
  }
  GST_FLAG_SET (element, GST_ELEMENT_INTERRUPTED);
  return TRUE;
}

static void
gst_fast_scheduler_error (GstScheduler *sched, GstElement *element)
{
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);
  GstSchedulerChain *chain;
    
  chain = gst_fast_scheduler_find_chain (bsched, element);
  if (chain)
    gst_fast_scheduler_chain_disable_element (chain, element);

  GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_ERROR;

  gst_fast_scheduler_interrupt (sched, element);
}

static void
gst_fast_scheduler_pad_connect (GstScheduler * sched, GstPad *srcpad, GstPad *sinkpad)
{
  GstElement *srcelement, *sinkelement;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);

  srcelement = GST_PAD_PARENT (srcpad);
  g_return_if_fail (srcelement != NULL);
  sinkelement = GST_PAD_PARENT (sinkpad);
  g_return_if_fail (sinkelement != NULL);

  GST_INFO (GST_CAT_SCHEDULING, "have pad connected callback on %s:%s to %s:%s",
	    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
  GST_DEBUG (GST_CAT_SCHEDULING, "srcpad sched is %p, sinkpad sched is %p\n",
	     GST_ELEMENT_SCHED (srcelement), GST_ELEMENT_SCHED (sinkelement));

  if (GST_ELEMENT_SCHED (srcelement) == GST_ELEMENT_SCHED (sinkelement)) {
    GST_INFO (GST_CAT_SCHEDULING, "peer %s:%s is in same scheduler, chaining together",
	      GST_DEBUG_PAD_NAME (sinkpad));
    gst_fast_scheduler_chain_elements (bsched, srcelement, sinkelement);
  }
}

static void
gst_fast_scheduler_pad_disconnect (GstScheduler * sched, GstPad * srcpad, GstPad * sinkpad)
{
  GstElement *element1, *element2;
  GstSchedulerChain *chain1, *chain2;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);

  GST_INFO (GST_CAT_SCHEDULING, "disconnecting pads %s:%s and %s:%s",
	    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* we need to have the parent elements of each pad */
  element1 = GST_ELEMENT_CAST (GST_PAD_PARENT (srcpad));
  element2 = GST_ELEMENT_CAST (GST_PAD_PARENT (sinkpad));

  /* first task is to remove the old chain they belonged to.
   * this can be accomplished by taking either of the elements,
   * since they are guaranteed to be in the same chain
   * FIXME is it potentially better to make an attempt at splitting cleaner??
   */
  chain1 = gst_fast_scheduler_find_chain (bsched, element1);
  chain2 = gst_fast_scheduler_find_chain (bsched, element2);

  if (chain1 != chain2) {
    /* elements not in the same chain don't need to be separated */
    GST_INFO (GST_CAT_SCHEDULING, "elements not in the same chain");
    return;
  }

  if (chain1) {
    GST_INFO (GST_CAT_SCHEDULING, "destroying chain");
    gst_fast_scheduler_chain_destroy (chain1);

    /* now create a new chain to hold element1 and build it from scratch */
    chain1 = gst_fast_scheduler_chain_new (bsched);
    gst_fast_scheduler_chain_recursive_add (chain1, element1);
  }

  /* check the other element to see if it landed in the newly created chain */
  if (gst_fast_scheduler_find_chain (bsched, element2) == NULL) {
    /* if not in chain, create chain and build from scratch */
    chain2 = gst_fast_scheduler_chain_new (bsched);
    gst_fast_scheduler_chain_recursive_add (chain2, element2);
  }
}

static GstPad *
gst_fast_scheduler_pad_select (GstScheduler * sched, GList * padlist)
{
  GstPad *pad = NULL;

  GST_INFO (GST_CAT_SCHEDULING, "imlement me!!");

  return pad;
}

static GstSchedulerState
gst_fast_scheduler_iterate (GstScheduler * sched)
{
  GstBin *bin = GST_BIN (sched->parent);
  GList *chains;
  GstSchedulerChain *chain;
  gint scheduled = 0;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);
  GstSchedulerState state;

  GST_DEBUG_ENTER ("(\"%s\")", GST_ELEMENT_NAME (bin));

  /* step through all the chains */
  chains = bsched->chains;

  if (chains == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "no chains!\n");

    state = GST_SCHEDULER_STATE_STOPPED;

    goto exit;
  }

  while (chains) {
    chain = (GstSchedulerChain *) (chains->data);
    chains = g_list_next (chains);

    if (chain->elements == NULL) {
      continue;
    }

    //if (chain->num_cothreaded > 1) {
    if (FALSE) {
      g_warning ("this scheduler can only deal with 1 cothreaded element in a chain");

      state = GST_SCHEDULER_STATE_ERROR;

      goto exit;
    }
    else if (chain->num_cothreaded != 0) {
      /* we just pick the first cothreaded element */
      GstElement *entry = GST_ELEMENT (chain->cothreaded_elements->data);
	  
      GST_DEBUG (GST_CAT_DATAFLOW, "starting iteration via cothreads\n");

      GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);

      GST_DEBUG (GST_CAT_DATAFLOW, "set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
		   GST_ELEMENT_NAME (entry), entry);

      if (GST_ELEMENT_THREADSTATE (entry)) {
        cothread_switch (GST_ELEMENT_THREADSTATE (entry));
      }
      else {
	GST_DEBUG (GST_CAT_DATAFLOW, "cothread switch not possible, element has no threadstate\n");

        GST_DEBUG (GST_CAT_DATAFLOW, "leaving (%s)\n", GST_ELEMENT_NAME (bin));

	state = GST_SCHEDULER_STATE_ERROR;

        goto exit;
      }

      GST_DEBUG (GST_CAT_SCHEDULING, "loopfunc of element %s ended\n", GST_ELEMENT_NAME (entry));

      scheduled++;
    }
    else {
      GstElement *entry = chain->entry;
      if (entry) {
        GList *pads = gst_element_get_pad_list (entry);

        GST_DEBUG (GST_CAT_DATAFLOW, "starting chained iteration\n");

        while (pads) {
          GstPad *pad = GST_PAD_CAST (pads->data);
	  pads = g_list_next (pads);

	  if (GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
	    GstBuffer *buf;

	    buf = GST_RPAD_GETFUNC (pad) (pad);
	    if (GST_ELEMENT_IS_INTERRUPTED (entry)) {
              GST_FLAG_UNSET (entry, GST_ELEMENT_INTERRUPTED);
	      break;
	    }
            gst_pad_push (pad, buf);
            scheduled++;
	  }
        }
      }
      else {
        GST_INFO (GST_CAT_DATAFLOW, "no entry found!!");

        state = GST_SCHEDULER_STATE_ERROR;
        goto exit;
      }
    }

    state = GST_SCHEDULER_STATE (sched);

    if (state != GST_SCHEDULER_STATE_RUNNING) {
      GST_INFO (GST_CAT_DATAFLOW, "scheduler is not running, in state %d", state);

      goto exit;
    }
  }

  GST_DEBUG (GST_CAT_DATAFLOW, "leaving (%s)\n", GST_ELEMENT_NAME (bin));

  if (scheduled == 0) {
    GST_INFO (GST_CAT_DATAFLOW, "nothing was scheduled, return STOPPED");
    state = GST_SCHEDULER_STATE_STOPPED;
  }
  else {
    GST_INFO (GST_CAT_DATAFLOW, "scheduler still running, return RUNNING");
    state = GST_SCHEDULER_STATE_RUNNING;
  }

exit:
  GST_DEBUG (GST_CAT_DATAFLOW, "leaving (%s) %d\n", GST_ELEMENT_NAME (bin), state);
  
  return state;
}

/* defined but not used
static void
gst_fast_scheduler_show (GstScheduler * sched)
{
  GList *chains, *elements;
  GstElement *element;
  GstSchedulerChain *chain;
  GstFastScheduler *bsched = GST_FAST_SCHEDULER (sched);

  if (sched == NULL) {
    g_print ("scheduler doesn't exist for this element\n");
    return;
  }

  g_return_if_fail (GST_IS_SCHEDULER (sched));

  g_print ("SCHEDULER DUMP FOR MANAGING BIN \"%s\"\n", GST_ELEMENT_NAME (sched->parent));

  g_print ("scheduler has %d elements in it: ", bsched->num_elements);
  elements = bsched->elements;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);

    g_print ("%s, ", GST_ELEMENT_NAME (element));
  }
  g_print ("\n");

  g_print ("scheduler has %d chains in it\n", bsched->num_chains);
  chains = bsched->chains;
  while (chains) {
    chain = (GstSchedulerChain *) (chains->data);
    chains = g_list_next (chains);

    g_print ("%p: ", chain);

    elements = chain->disabled;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      elements = g_list_next (elements);

      g_print ("!%s, ", GST_ELEMENT_NAME (element));
    }

    elements = chain->elements;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      elements = g_list_next (elements);

      g_print ("%s, ", GST_ELEMENT_NAME (element));
    }
    g_print ("\n");
  }
}
*/
