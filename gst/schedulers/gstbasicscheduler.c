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

typedef struct _GstSchedulerChain GstSchedulerChain;

struct _GstSchedulerChain {
  GstScheduler *sched;

  GList *disabled;

  GList *elements;
  gint num_elements;

  GstElement *entry;

  gint cothreaded_elements;
  gboolean schedule;
};

static GType _gst_basic_scheduler_type = 0;

static void 	gst_basic_scheduler_class_init 		(GstSchedulerClass * klass);
static void 	gst_basic_scheduler_init 		(GstScheduler * scheduler);

static void 	gst_basic_scheduler_dispose 		(GObject *object);

static void 	gst_basic_scheduler_setup 		(GstScheduler *sched);
static void 	gst_basic_scheduler_reset 		(GstScheduler *sched);
static void	gst_basic_scheduler_add_element		(GstScheduler *sched, GstElement *element);
static void     gst_basic_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
static void     gst_basic_scheduler_enable_element	(GstScheduler *sched, GstElement *element);
static void     gst_basic_scheduler_disable_element	(GstScheduler *sched, GstElement *element);
static void 	gst_basic_scheduler_lock_element 	(GstScheduler *sched, GstElement *element);
static void 	gst_basic_scheduler_unlock_element 	(GstScheduler *sched, GstElement *element);
static void     gst_basic_scheduler_pad_connect		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static void     gst_basic_scheduler_pad_disconnect 	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static GstPad*  gst_basic_scheduler_pad_select 		(GstScheduler *sched, GList *padlist);
static gboolean gst_basic_scheduler_iterate    		(GstScheduler *sched);

static void     gst_basic_scheduler_show  		(GstScheduler *sched);

static GstSchedulerClass *parent_class = NULL;

static GType
gst_basic_scheduler_get_type (void)
{
  if (!_gst_basic_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_basic_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstScheduler),
      0,
      (GInstanceInitFunc) gst_basic_scheduler_init,
      NULL
    };

    _gst_basic_scheduler_type = g_type_register_static (GST_TYPE_SCHEDULER, "GstBasicScheduler", &scheduler_info, 0);
  }
  return _gst_basic_scheduler_type;
}

static void
gst_basic_scheduler_class_init (GstSchedulerClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_SCHEDULER);

  gobject_class->dispose	= GST_DEBUG_FUNCPTR (gst_basic_scheduler_dispose);

  klass->setup 			= GST_DEBUG_FUNCPTR (gst_basic_scheduler_setup);
  klass->reset	 		= GST_DEBUG_FUNCPTR (gst_basic_scheduler_reset);
  klass->add_element 		= GST_DEBUG_FUNCPTR (gst_basic_scheduler_add_element);
  klass->remove_element 	= GST_DEBUG_FUNCPTR (gst_basic_scheduler_remove_element);
  klass->enable_element 	= GST_DEBUG_FUNCPTR (gst_basic_scheduler_enable_element);
  klass->disable_element 	= GST_DEBUG_FUNCPTR (gst_basic_scheduler_disable_element);
  klass->lock_element 		= GST_DEBUG_FUNCPTR (gst_basic_scheduler_lock_element);
  klass->unlock_element 	= GST_DEBUG_FUNCPTR (gst_basic_scheduler_unlock_element);
  klass->pad_connect 		= GST_DEBUG_FUNCPTR (gst_basic_scheduler_pad_connect);
  klass->pad_disconnect 	= GST_DEBUG_FUNCPTR (gst_basic_scheduler_pad_disconnect);
  klass->pad_select 		= GST_DEBUG_FUNCPTR (gst_basic_scheduler_pad_select);
  klass->iterate 		= GST_DEBUG_FUNCPTR (gst_basic_scheduler_iterate);
}

static void
gst_basic_scheduler_init (GstScheduler *scheduler)
{
}

static void
gst_basic_scheduler_dispose (GObject *object)
{
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstSchedulerFactory *factory;

  gst_plugin_set_longname (plugin, "A basic scheduler");

  factory = gst_schedulerfactory_new ("basic",
	                              "A basic scheduler, it uses cothreads",
		                      gst_basic_scheduler_get_type());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register scheduler: basic");
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstbasicscheduler",
  plugin_init
};

static int
gst_basic_scheduler_loopfunc_wrapper (int argc, char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
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

static int
gst_basic_scheduler_chain_wrapper (int argc, char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);

  GST_DEBUG_ENTER ("(\"%s\")", name);

  GST_DEBUG (GST_CAT_DATAFLOW, "stepping through pads\n");

  do {
    GList *pads = element->pads;

    while (pads) {
      GstPad *pad = GST_PAD (pads->data);
      GstRealPad *realpad;

      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD (pad))
	continue;
      realpad = GST_REAL_PAD (pad);
      if (GST_RPAD_DIRECTION (realpad) == GST_PAD_SINK) {
	GstBuffer *buf;

	GST_DEBUG (GST_CAT_DATAFLOW, "pulling data from %s:%s\n", name, GST_PAD_NAME (pad));
	buf = gst_pad_pull (pad);
	if (buf) {
	  if (GST_IS_EVENT (buf) && !GST_ELEMENT_IS_EVENT_AWARE (element)) {
	    /*gst_pad_event_default (pad, GST_EVENT (buf)); */
	    gst_pad_send_event (pad, GST_EVENT (buf));
	  }
	  else {
	    GST_DEBUG (GST_CAT_DATAFLOW, "calling chain function of %s:%s\n", name,
		       GST_PAD_NAME (pad));
	    GST_RPAD_CHAINFUNC (realpad) (pad, buf);
	  }
	}
	GST_DEBUG (GST_CAT_DATAFLOW, "calling chain function of %s:%s done\n", name,
		   GST_PAD_NAME (pad));
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
  GST_FLAG_UNSET (element, GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE ("(%d,'%s')", argc, name);
  return 0;
}

static int
gst_basic_scheduler_src_wrapper (int argc, char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  GList *pads;
  GstRealPad *realpad;
  GstBuffer *buf = NULL;
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);

  GST_DEBUG_ENTER ("(%d,\"%s\")", argc, name);

  do {
    pads = element->pads;
    while (pads) {
      if (!GST_IS_REAL_PAD (pads->data))
	continue;
      realpad = (GstRealPad *) (pads->data);
      pads = g_list_next (pads);
      if (GST_RPAD_DIRECTION (realpad) == GST_PAD_SRC) {
	GST_DEBUG (GST_CAT_DATAFLOW, "calling _getfunc for %s:%s\n", GST_DEBUG_PAD_NAME (realpad));
	if (realpad->regiontype != GST_REGION_VOID) {
	  g_return_val_if_fail (GST_RPAD_GETREGIONFUNC (realpad) != NULL, 0);
/*          if (GST_RPAD_GETREGIONFUNC(realpad) == NULL)		   */	
/*            fprintf(stderr,"error, no getregionfunc in \"%s\"\n", name); */
/*          else							   */
	  buf =
	    (GST_RPAD_GETREGIONFUNC (realpad)) ((GstPad *) realpad, realpad->regiontype,
						realpad->offset, realpad->len);
	  realpad->regiontype = GST_REGION_VOID;
	}
	else {
	  g_return_val_if_fail (GST_RPAD_GETFUNC (realpad) != NULL, 0);
/*          if (GST_RPAD_GETFUNC(realpad) == NULL)			*/
/*            fprintf(stderr,"error, no getfunc in \"%s\"\n", name);	*/
/*          else							*/
	  buf = GST_RPAD_GETFUNC (realpad) ((GstPad *) realpad);
	}

	GST_DEBUG (GST_CAT_DATAFLOW, "calling gst_pad_push on pad %s:%s\n",
		   GST_DEBUG_PAD_NAME (realpad));
	gst_pad_push ((GstPad *) realpad, buf);
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
  GST_FLAG_UNSET (element, GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE ("");
  return 0;
}

static void
gst_basic_scheduler_chainhandler_proxy (GstPad * pad, GstBuffer * buf)
{
  GstRealPad *peer = GST_RPAD_PEER (pad);

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));
  GST_DEBUG (GST_CAT_DATAFLOW, "putting buffer %p in peer \"%s:%s\"'s pen\n", buf,
	     GST_DEBUG_PAD_NAME (peer));

  /* FIXME this should be bounded
   * loop until the bufferpen is empty so we can fill it up again
   */
  while (GST_RPAD_BUFPEN (pad) != NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to empty bufpen\n",
	       GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
    cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

    /* we may no longer be the same pad, check. */
    if (GST_RPAD_PEER (peer) != (GstRealPad *) pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *) GST_RPAD_PEER (peer);
    }
  }

  g_assert (GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) == NULL);
  /* now fill the bufferpen and switch so it can be consumed */
  GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) = buf;
  GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p\n",
	     GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
  cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

  GST_DEBUG (GST_CAT_DATAFLOW, "done switching\n");
}

static void
gst_basic_scheduler_select_proxy (GstPad * pad, GstBuffer * buf)
{
  g_print ("select proxy (%s:%s)\n", GST_DEBUG_PAD_NAME (pad));

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  GST_DEBUG (GST_CAT_DATAFLOW, "putting buffer %p in peer's pen\n", buf);

  g_assert (GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) == NULL);
  /* now fill the bufferpen and switch so it can be consumed */
  GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) = buf;
  GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p\n",
	     GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
  g_print ("%p %s\n", GST_ELEMENT (GST_PAD_PARENT (pad)),
	   gst_element_get_name (GST_ELEMENT (GST_PAD_PARENT (pad))));
  GST_ELEMENT (GST_PAD_PARENT (pad))->select_pad = pad;
  GST_FLAG_UNSET (GST_PAD_PARENT (pad), GST_ELEMENT_COTHREAD_STOPPING);
  cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

  g_print ("done switching\n");
  GST_DEBUG (GST_CAT_DATAFLOW, "done switching\n");
}


static GstBuffer *
gst_basic_scheduler_gethandler_proxy (GstPad * pad)
{
  GstBuffer *buf;
  GstRealPad *peer = GST_RPAD_PEER (pad);

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  /* FIXME this should be bounded */
  /* we will loop switching to the peer until it's filled up the bufferpen */
  while (GST_RPAD_BUFPEN (pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to \"%s\": %p to fill bufpen\n",
	       GST_ELEMENT_NAME (GST_ELEMENT (GST_PAD_PARENT (pad))),
	       GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
    cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

    /* we may no longer be the same pad, check. */
    if (GST_RPAD_PEER (peer) != (GstRealPad *) pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *) GST_RPAD_PEER (peer);
    }
  }
  GST_DEBUG (GST_CAT_DATAFLOW, "done switching\n");

  /* now grab the buffer from the pen, clear the pen, and return the buffer */
  buf = GST_RPAD_BUFPEN (pad);
  GST_RPAD_BUFPEN (pad) = NULL;

  return buf;
}

static GstBuffer *
gst_basic_scheduler_pullregionfunc_proxy (GstPad * pad, GstRegionType type, guint64 offset, guint64 len)
{
  GstBuffer *buf;
  GstRealPad *peer = GST_RPAD_PEER (pad);

  GST_DEBUG_ENTER ("%s:%s,%d,%lld,%lld", GST_DEBUG_PAD_NAME (pad), type, offset, len);

  /* put the region info into the pad */
  GST_RPAD_REGIONTYPE (pad) = type;
  GST_RPAD_OFFSET (pad) = offset;
  GST_RPAD_LEN (pad) = len;

  /* FIXME this should be bounded */
  /* we will loop switching to the peer until it's filled up the bufferpen */
  while (GST_RPAD_BUFPEN (pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to fill bufpen\n",
	       GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
    cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

    /* we may no longer be the same pad, check. */
    if (GST_RPAD_PEER (peer) != (GstRealPad *) pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *) GST_RPAD_PEER (peer);
    }
  }
  GST_DEBUG (GST_CAT_DATAFLOW, "done switching\n");

  /* now grab the buffer from the pen, clear the pen, and return the buffer */
  buf = GST_RPAD_BUFPEN (pad);
  GST_RPAD_BUFPEN (pad) = NULL;
  return buf;
}


static void
gst_basic_scheduler_cothreaded_chain (GstBin * bin, GstSchedulerChain * chain)
{
  GList *elements;
  GstElement *element;
  cothread_func wrapper_function;
  GList *pads;
  GstPad *pad;

  GST_DEBUG (GST_CAT_SCHEDULING, "chain is using COTHREADS\n");

  g_assert (bin->threadcontext != NULL);


  /* walk through all the chain's elements */
  elements = chain->elements;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);

    /* start out without a wrapper function, we select it later */
    wrapper_function = NULL;

    /* if the element has a loopfunc... */
    if (element->loopfunc != NULL) {
      wrapper_function = GST_DEBUG_FUNCPTR (gst_basic_scheduler_loopfunc_wrapper);
      GST_DEBUG (GST_CAT_SCHEDULING, "element '%s' is a loop-based\n", GST_ELEMENT_NAME (element));
    }
    else {
      /* otherwise we need to decide what kind of cothread */
      /* if it's not DECOUPLED, we decide based on whether it's a source or not */
      if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
	/* if it doesn't have any sinks, it must be a source (duh) */
	if (element->numsinkpads == 0) {
	  wrapper_function = GST_DEBUG_FUNCPTR (gst_basic_scheduler_src_wrapper);
	  GST_DEBUG (GST_CAT_SCHEDULING, "element '%s' is a source, using _src_wrapper\n",
		     GST_ELEMENT_NAME (element));
	}
	else {
	  wrapper_function = GST_DEBUG_FUNCPTR (gst_basic_scheduler_chain_wrapper);
	  GST_DEBUG (GST_CAT_SCHEDULING, "element '%s' is a filter, using _chain_wrapper\n",
		     GST_ELEMENT_NAME (element));
	}
      }
    }

    /* now we have to walk through the pads to set up their state */
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD (pad))
	continue;

      /* if the element is DECOUPLED or outside the manager, we have to chain */
      if ((wrapper_function == NULL) ||
	  (GST_RPAD_PEER (pad) &&
	   (GST_ELEMENT (GST_PAD_PARENT (GST_PAD (GST_RPAD_PEER (pad))))->sched != chain->sched))
	) {
	/* set the chain proxies */
	if (GST_RPAD_DIRECTION (pad) == GST_PAD_SINK) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "copying chain function into push proxy for %s:%s\n",
		     GST_DEBUG_PAD_NAME (pad));
	  GST_RPAD_CHAINHANDLER (pad) = GST_RPAD_CHAINFUNC (pad);
	}
	else {
	  GST_DEBUG (GST_CAT_SCHEDULING, "copying get function into pull proxy for %s:%s\n",
		     GST_DEBUG_PAD_NAME (pad));
	  GST_RPAD_GETHANDLER (pad) = GST_RPAD_GETFUNC (pad);
	  GST_RPAD_PULLREGIONFUNC (pad) = GST_RPAD_GETREGIONFUNC (pad);
	}

	/* otherwise we really are a cothread */
      }
      else {
	if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "setting cothreaded push proxy for sinkpad %s:%s\n",
		     GST_DEBUG_PAD_NAME (pad));
	  GST_RPAD_CHAINHANDLER (pad) = GST_DEBUG_FUNCPTR (gst_basic_scheduler_chainhandler_proxy);
	}
	else {
	  GST_DEBUG (GST_CAT_SCHEDULING, "setting cothreaded pull proxy for srcpad %s:%s\n",
		     GST_DEBUG_PAD_NAME (pad));
	  GST_RPAD_GETHANDLER (pad) = GST_DEBUG_FUNCPTR (gst_basic_scheduler_gethandler_proxy);
	  GST_RPAD_PULLREGIONFUNC (pad) = GST_DEBUG_FUNCPTR (gst_basic_scheduler_pullregionfunc_proxy);
	}
      }
    }

    /* need to set up the cothread now */
    if (wrapper_function != NULL) {
      if (element->threadstate == NULL) {
	/* FIXME handle cothread_create returning NULL */
	element->threadstate = cothread_create (bin->threadcontext);
	GST_DEBUG (GST_CAT_SCHEDULING, "created cothread %p for '%s'\n", element->threadstate,
		   GST_ELEMENT_NAME (element));
      }
      cothread_setfunc (element->threadstate, wrapper_function, 0, (char **) element);
      GST_DEBUG (GST_CAT_SCHEDULING, "set wrapper function for '%s' to &%s\n",
		 GST_ELEMENT_NAME (element), GST_DEBUG_FUNCPTR_NAME (wrapper_function));
    }
  }
}

/*
G_GNUC_UNUSED static void
gst_basic_scheduler_chained_chain (GstBin *bin, _GstBinChain *chain) {
  GList *elements;
  GstElement *element;
  GList *pads;
  GstPad *pad;

  GST_DEBUG (GST_CAT_SCHEDULING,"chain entered\n");
  // walk through all the elements
  elements = chain->elements;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);

    // walk through all the pads
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD(pad)) continue;

      if (GST_RPAD_DIRECTION(pad) == GST_PAD_SINK) {
        GST_DEBUG (GST_CAT_SCHEDULING,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        GST_RPAD_CHAINHANDLER(pad) = GST_RPAD_CHAINFUNC(pad);
      } else {
        GST_DEBUG (GST_CAT_SCHEDULING,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        GST_RPAD_GETHANDLER(pad) = GST_RPAD_GETFUNC(pad);
        GST_RPAD_PULLREGIONFUNC(pad) = GST_RPAD_GETREGIONFUNC(pad);
      }
    }
  }
}
*/


static GstSchedulerChain *
gst_basic_scheduler_chain_new (GstScheduler * sched)
{
  GstSchedulerChain *chain = g_new (GstSchedulerChain, 1);

  /* initialize the chain with sane values */
  chain->sched = sched;
  chain->disabled = NULL;
  chain->elements = NULL;
  chain->num_elements = 0;
  chain->entry = NULL;
  chain->cothreaded_elements = 0;
  chain->schedule = FALSE;

  /* add the chain to the schedulers' list of chains */
  sched->chains = g_list_prepend (sched->chains, chain);
  sched->num_chains++;

  GST_INFO (GST_CAT_SCHEDULING, "created new chain %p, now are %d chains in sched %p",
	    chain, sched->num_chains, sched);

  return chain;
}

static void
gst_basic_scheduler_chain_destroy (GstSchedulerChain * chain)
{
  GstScheduler *sched = chain->sched;

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

static void
gst_basic_scheduler_chain_add_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* set the sched pointer for the element */
  element->sched = chain->sched;

  /* add the element to the list of 'disabled' elements */
  chain->disabled = g_list_prepend (chain->disabled, element);
  chain->num_elements++;
}

static void
gst_basic_scheduler_chain_enable_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "enabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* remove from disabled list */
  chain->disabled = g_list_remove (chain->disabled, element);

  /* add to elements list */
  chain->elements = g_list_prepend (chain->elements, element);

  /* reschedule the chain */
  gst_basic_scheduler_cothreaded_chain (GST_BIN (chain->sched->parent), chain);
}

static void
gst_basic_scheduler_chain_disable_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "disabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* remove from elements list */
  chain->elements = g_list_remove (chain->elements, element);

  /* add to disabled list */
  chain->disabled = g_list_prepend (chain->disabled, element);

  /* reschedule the chain */
/* FIXME this should be done only if manager state != NULL */
/*  gst_basic_scheduler_cothreaded_chain(GST_BIN(chain->sched->parent),chain); */
}

static void
gst_basic_scheduler_chain_remove_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* if it's active, deactivate it */
  if (g_list_find (chain->elements, element)) {
    gst_basic_scheduler_chain_disable_element (chain, element);
  }

  /* remove the element from the list of elements */
  chain->disabled = g_list_remove (chain->disabled, element);
  chain->num_elements--;

  /* if there are no more elements in the chain, destroy the chain */
  if (chain->num_elements == 0)
    gst_basic_scheduler_chain_destroy (chain);

  /* unset the sched pointer for the element */
  element->sched = NULL;
}

static void
gst_basic_scheduler_chain_elements (GstScheduler * sched, GstElement * element1, GstElement * element2)
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
    chain = gst_basic_scheduler_chain_new (sched);
    gst_basic_scheduler_chain_add_element (chain, element1);
    gst_basic_scheduler_chain_add_element (chain, element2);
    /* FIXME chain changed here */
/*    gst_basic_scheduler_cothreaded_chain(chain->sched->parent,chain); */

    /* otherwise if both have chains already, join them */
  }
  else if ((chain1 != NULL) && (chain2 != NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "merging chain %p into chain %p", chain2, chain1);
    /* take the contents of chain2 and merge them into chain1 */
    chain1->disabled = g_list_concat (chain1->disabled, g_list_copy (chain2->disabled));
    chain1->elements = g_list_concat (chain1->elements, g_list_copy (chain2->elements));
    chain1->num_elements += chain2->num_elements;
    /* FIXME chain changed here */
/*    gst_basic_scheduler_cothreaded_chain(chain->sched->parent,chain); */

    gst_basic_scheduler_chain_destroy (chain2);

    /* otherwise one has a chain already, the other doesn't */
  }
  else {
    /* pick out which one has the chain, and which doesn't */
    if (chain1 != NULL)
      chain = chain1, element = element2;
    else
      chain = chain2, element = element1;

    GST_INFO (GST_CAT_SCHEDULING, "adding element to existing chain");
    gst_basic_scheduler_chain_add_element (chain, element);
    /* FIXME chain changed here */
/*    gst_basic_scheduler_cothreaded_chain(chain->sched->parent,chain); */
  }
}


/* find the chain within the scheduler that holds the element, if any */
static GstSchedulerChain *
gst_basic_scheduler_find_chain (GstScheduler * sched, GstElement * element)
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
gst_basic_scheduler_chain_recursive_add (GstSchedulerChain * chain, GstElement * element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;

  /* add the element to the chain */
  gst_basic_scheduler_chain_add_element (chain, element);

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
	if (gst_basic_scheduler_find_chain (chain->sched, peerelement) == NULL) {
	  gst_basic_scheduler_chain_recursive_add (chain, peerelement);
	}
      }
    }
  }
}

/*
 * Entry points for this scheduler.
 */
static void
gst_basic_scheduler_setup (GstScheduler *sched)
{
  GstBin *bin = GST_BIN (sched->parent);

  /* first create thread context */
  if (bin->threadcontext == NULL) {
    GST_DEBUG (GST_CAT_SCHEDULING, "initializing cothread context\n");
    bin->threadcontext = cothread_init ();
  }
}

static void
gst_basic_scheduler_reset (GstScheduler *sched)
{
  cothread_context *ctx;
  GstBin *bin = GST_BIN (GST_SCHED_PARENT (sched));
  GList *elements = sched->elements;

  while (elements) {
    GST_ELEMENT (elements->data)->threadstate = NULL;
    elements = g_list_next (elements);
  }
  
  ctx = GST_BIN (GST_SCHED_PARENT (sched))->threadcontext;

  cothread_free (ctx);
  
  GST_BIN (GST_SCHED_PARENT (sched))->threadcontext = NULL;
}

static void
gst_basic_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;
  GstSchedulerChain *chain;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  /* if it's already in this scheduler, don't bother doing anything */
  if (GST_ELEMENT_SCHED (element) == sched)
    return;

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to scheduler", GST_ELEMENT_NAME (element));

  /* if the element already has a different scheduler, remove the element from it */
  if (GST_ELEMENT_SCHED (element)) {
    gst_basic_scheduler_remove_element (GST_ELEMENT_SCHED (element), element);
  }

  /* set the sched pointer in the element itself */
  GST_ELEMENT_SCHED (element) = sched;

  /* only deal with elements after this point, not bins */
  /* exception is made for Bin's that are schedulable, like the autoplugger */
  if (GST_IS_BIN (element) && !GST_FLAG_IS_SET (element, GST_BIN_SELF_SCHEDULABLE))
    return;

  /* first add it to the list of elements that are to be scheduled */
  sched->elements = g_list_prepend (sched->elements, element);
  sched->num_elements++;

  /* create a chain to hold it, and add */
  chain = gst_basic_scheduler_chain_new (sched);
  gst_basic_scheduler_chain_add_element (chain, element);

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
	gst_basic_scheduler_chain_elements (sched, element, peerelement);
      }
    }
  }
}

static void
gst_basic_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerChain *chain;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (g_list_find (sched->elements, element)) {
    GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from scheduler",
	      GST_ELEMENT_NAME (element));

    /* find what chain the element is in */
    chain = gst_basic_scheduler_find_chain (sched, element);

    /* remove it from its chain */
    gst_basic_scheduler_chain_remove_element (chain, element);

    /* remove it from the list of elements */
    sched->elements = g_list_remove (sched->elements, element);
    sched->num_elements--;

    /* unset the scheduler pointer in the element */
    GST_ELEMENT_SCHED (element) = NULL;
  }
}

static void
gst_basic_scheduler_enable_element (GstScheduler *sched, GstElement *element)
{
  GstSchedulerChain *chain;

  /* find the chain the element's in */
  chain = gst_basic_scheduler_find_chain (sched, element);

  if (chain) {
    gst_basic_scheduler_chain_enable_element (chain, element);
  }
  else {
    GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" not found in any chain, not enabling", GST_ELEMENT_NAME (element));
  }
}

static void
gst_basic_scheduler_disable_element (GstScheduler *sched, GstElement *element)
{
  GstSchedulerChain *chain;

  /* find the chain the element is in */
  chain = gst_basic_scheduler_find_chain (sched, element);

  /* remove it from the chain */
  if (chain) {
    gst_basic_scheduler_chain_disable_element (chain, element);
  }
  else {
    GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" not found in any chain, not disabling", GST_ELEMENT_NAME (element));
  }
}

static void
gst_basic_scheduler_lock_element (GstScheduler * sched, GstElement * element)
{
  if (element->threadstate)
    cothread_lock (element->threadstate);
}

static void
gst_basic_scheduler_unlock_element (GstScheduler * sched, GstElement * element)
{
  if (element->threadstate)
    cothread_unlock (element->threadstate);
}

static void
gst_basic_scheduler_pad_connect (GstScheduler * sched, GstPad * srcpad, GstPad * sinkpad)
{
  GstElement *srcelement, *sinkelement;

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
    gst_basic_scheduler_chain_elements (sched, srcelement, sinkelement);
  }
}

static void
gst_basic_scheduler_pad_disconnect (GstScheduler * sched, GstPad * srcpad, GstPad * sinkpad)
{
  GstSchedulerChain *chain;
  GstElement *element1, *element2;
  GstSchedulerChain *chain1, *chain2;

  GST_INFO (GST_CAT_SCHEDULING, "disconnecting pads %s:%s and %s:%s",
	    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* we need to have the parent elements of each pad */
  element1 = GST_ELEMENT (GST_PAD_PARENT (srcpad));
  element2 = GST_ELEMENT (GST_PAD_PARENT (sinkpad));

  /* first task is to remove the old chain they belonged to.
   * this can be accomplished by taking either of the elements,
   * since they are guaranteed to be in the same chain
   * FIXME is it potentially better to make an attempt at splitting cleaner??
   */
  chain1 = gst_basic_scheduler_find_chain (sched, element1);
  chain2 = gst_basic_scheduler_find_chain (sched, element2);

  if (chain1 != chain2) {
    GST_INFO (GST_CAT_SCHEDULING, "elements not in the same chain");
    return;
  }

  if (chain1) {
    GST_INFO (GST_CAT_SCHEDULING, "destroying chain");
    gst_basic_scheduler_chain_destroy (chain1);

    /* now create a new chain to hold element1 and build it from scratch */
    chain1 = gst_basic_scheduler_chain_new (sched);
    gst_basic_scheduler_chain_recursive_add (chain1, element1);
  }

  /* check the other element to see if it landed in the newly created chain */
  if (chain2) {
    GST_INFO (GST_CAT_SCHEDULING, "destroying chain");
    gst_basic_scheduler_chain_destroy (chain2);

    /* if not in chain, create chain and build from scratch */
    chain2 = gst_basic_scheduler_chain_new (sched);
    gst_basic_scheduler_chain_recursive_add (chain2, element2);
  }
}

static GstPad *
gst_basic_scheduler_pad_select (GstScheduler * sched, GList * padlist)
{
  GstPad *pad = NULL;
  GList *padlist2 = padlist;

  GST_INFO (GST_CAT_SCHEDULING, "performing select");

  while (padlist2) {
    pad = GST_PAD (padlist2->data);

    if (gst_pad_peek (pad)) {
      g_print ("found something in pad %s:%s\n", GST_DEBUG_PAD_NAME (pad));
      return pad;
    }

    padlist2 = g_list_next (padlist2);
  }

  /* else there is nothing ready to consume, set up the select functions */
  while (padlist) {
    pad = GST_PAD (padlist->data);

    GST_RPAD_CHAINHANDLER (pad) = GST_DEBUG_FUNCPTR (gst_basic_scheduler_select_proxy);

    padlist = g_list_next (padlist);
  }
  if (pad != NULL) {
    GstRealPad *peer = GST_RPAD_PEER (pad);

    cothread_switch (GST_ELEMENT (GST_PAD_PARENT (peer))->threadstate);

    g_print ("%p %s\n", GST_ELEMENT (GST_PAD_PARENT (pad)),
	     gst_element_get_name (GST_ELEMENT (GST_PAD_PARENT (pad))));
    pad = GST_ELEMENT (GST_PAD_PARENT (pad))->select_pad;

    g_assert (pad != NULL);
    g_print ("back from select (%s:%s)\n", GST_DEBUG_PAD_NAME (pad));
  }
  return pad;
}

static gboolean
gst_basic_scheduler_iterate (GstScheduler * sched)
{
  GstBin *bin = GST_BIN (sched->parent);
  GList *chains;
  GstSchedulerChain *chain;
  GstElement *entry;
  gboolean eos = FALSE;
  GList *elements;

  GST_DEBUG_ENTER ("(\"%s\")", GST_ELEMENT_NAME (bin));

  g_return_val_if_fail (bin != NULL, TRUE);
  g_return_val_if_fail (GST_IS_BIN (bin), TRUE);

  /* step through all the chains */
  chains = sched->chains;

  g_return_val_if_fail (chains != NULL, FALSE);

  while (chains) {
    chain = (GstSchedulerChain *) (chains->data);
    chains = g_list_next (chains);

    /* all we really have to do is switch to the first child		*/
    /* FIXME this should be lots more intelligent about where to start  */
    GST_DEBUG (GST_CAT_DATAFLOW, "starting iteration via cothreads\n");

    if (chain->elements) {
      entry = NULL;		/*MattH ADDED?*/
      GST_DEBUG (GST_CAT_SCHEDULING, "there are %d elements in this chain\n", chain->num_elements);
      elements = chain->elements;
      while (elements) {
	entry = GST_ELEMENT (elements->data);
	elements = g_list_next (elements);
	if (GST_FLAG_IS_SET (entry, GST_ELEMENT_DECOUPLED)) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "entry \"%s\" is DECOUPLED, skipping\n",
		     GST_ELEMENT_NAME (entry));
	  entry = NULL;
	}
	else if (GST_FLAG_IS_SET (entry, GST_ELEMENT_NO_ENTRY)) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "entry \"%s\" is not valid, skipping\n",
		     GST_ELEMENT_NAME (entry));
	  entry = NULL;
	}
	else
	  break;
      }
      if (entry) {
	GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);
	GST_DEBUG (GST_CAT_DATAFLOW, "set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
		   GST_ELEMENT_NAME (entry), entry);
	cothread_switch (entry->threadstate);

	/* following is a check to see if the chain was interrupted due to a
	 * top-half state_change().  (i.e., if there's a pending state.)
	 *
	 * if it was, return to gstthread.c::gst_thread_main_loop() to
	 * execute the state change.
	 */
	GST_DEBUG (GST_CAT_DATAFLOW, "cothread switch ended or interrupted\n");
	if (GST_STATE_PENDING (GST_SCHEDULER (sched)->parent) != GST_STATE_VOID_PENDING) {
	  GST_DEBUG (GST_CAT_DATAFLOW, "handle pending state %d\n",
		     GST_STATE_PENDING (GST_SCHEDULER (sched)->parent));
	  return FALSE;
	}

      }
      else {
	GST_INFO (GST_CAT_DATAFLOW, "NO ENTRY INTO CHAIN!");
        eos = TRUE;
      }
    }
    else {
      GST_INFO (GST_CAT_DATAFLOW, "NO ENABLED ELEMENTS IN CHAIN!!");
      eos = TRUE;
    }
  }

  GST_DEBUG (GST_CAT_DATAFLOW, "leaving (%s)\n", GST_ELEMENT_NAME (bin));
  return !eos;
}


static void
gst_basic_scheduler_show (GstScheduler * sched)
{
  GList *chains, *elements;
  GstElement *element;
  GstSchedulerChain *chain;

  if (sched == NULL) {
    g_print ("scheduler doesn't exist for this element\n");
    return;
  }

  g_return_if_fail (GST_IS_SCHEDULER (sched));

  g_print ("SCHEDULER DUMP FOR MANAGING BIN \"%s\"\n", GST_ELEMENT_NAME (sched->parent));

  g_print ("scheduler has %d elements in it: ", sched->num_elements);
  elements = sched->elements;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);

    g_print ("%s, ", GST_ELEMENT_NAME (element));
  }
  g_print ("\n");

  g_print ("scheduler has %d chains in it\n", sched->num_chains);
  chains = sched->chains;
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
