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
#include <cothreads/cothreads.h>

typedef struct _GstSchedulerChain GstSchedulerChain;

#define GST_PAD_THREADSTATE(pad)	 (cothread*) (GST_PAD_CAST (pad)->sched_private)
#define GST_ELEMENT_THREADSTATE(elem)	 (cothread*) (GST_ELEMENT_CAST (elem)->sched_private)
#define GST_ELEMENT_STANDARD_SCHED(elem) (GstStandardScheduler*) (GST_ELEMENT_CAST (elem)->sched)
#define GST_PAD_STANDARD_SCHED(pad)      (GstStandardScheduler*) (GST_PAD_PARENT (pad)->sched)

#define GST_ELEMENT_COTHREAD_STOPPING			GST_ELEMENT_SCHEDULER_PRIVATE1
#define GST_ELEMENT_IS_COTHREAD_STOPPING(element)	GST_FLAG_IS_SET((element), GST_ELEMENT_COTHREAD_STOPPING)

typedef struct _GstStandardScheduler GstStandardScheduler;
typedef struct _GstStandardSchedulerClass GstStandardSchedulerClass;

struct _GstSchedulerChain {
  GstStandardScheduler *sched;

  GList *disabled;

  GList *elements;
  gint num_elements;

  GstElement *entry;

  gint cothreaded_elements;
  gboolean schedule;
};

#define GST_TYPE_STANDARD_SCHEDULER \
  (gst_standard_scheduler_get_type())
#define GST_STANDARD_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STANDARD_SCHEDULER,GstStandardScheduler))
#define GST_STANDARD_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STANDARD_SCHEDULER,GstStandardSchedulerClass))
#define GST_IS_STANDARD_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STANDARD_SCHEDULER))
#define GST_IS_STANDARD_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STANDARD_SCHEDULER))

#define GST_STANDARD_SCHEDULER_CAST(sched)	((GstStandardScheduler *)(sched))

typedef enum {
  GST_STANDARD_SCHEDULER_STATE_NONE,
  GST_STANDARD_SCHEDULER_STATE_STOPPED,
  GST_STANDARD_SCHEDULER_STATE_ERROR,
  GST_STANDARD_SCHEDULER_STATE_RUNNING,
} GstStandardSchedulerState;

typedef enum {
  /* something important has changed inside the scheduler */
  GST_STANDARD_SCHEDULER_CHANGE	= GST_OBJECT_FLAG_LAST,
} GstStandardSchedulerFlags;

struct _GstStandardScheduler {
  GstScheduler parent;

  GList *elements;
  gint num_elements;

  GList *chains;
  gint num_chains;

  GstStandardSchedulerState state;

  cothread *main;
  cothread *current;
};

struct _GstStandardSchedulerClass {
  GstSchedulerClass parent_class;
};

static GType _gst_standard_scheduler_type = 0;

static void 		gst_standard_scheduler_class_init 		(GstStandardSchedulerClass * klass);
static void 		gst_standard_scheduler_init 		(GstStandardScheduler * scheduler);

static void 		gst_standard_scheduler_dispose 		(GObject *object);

static void 		gst_standard_scheduler_setup 		(GstScheduler *sched);
static void 		gst_standard_scheduler_reset 		(GstScheduler *sched);
static void		gst_standard_scheduler_add_element		(GstScheduler *sched, GstElement *element);
static void     	gst_standard_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
static GstElementStateReturn  
			gst_standard_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
static void 		gst_standard_scheduler_lock_element 	(GstScheduler *sched, GstElement *element);
static void 		gst_standard_scheduler_unlock_element 	(GstScheduler *sched, GstElement *element);
static void 		gst_standard_scheduler_yield 		(GstScheduler *sched, GstElement *element);
static gboolean		gst_standard_scheduler_interrupt 		(GstScheduler *sched, GstElement *element);
static void 		gst_standard_scheduler_error	 	(GstScheduler *sched, GstElement *element);
static void     	gst_standard_scheduler_pad_connect		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static void     	gst_standard_scheduler_pad_disconnect 	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static GstSchedulerState
			gst_standard_scheduler_iterate    		(GstScheduler *sched);

static void     	gst_standard_scheduler_show  		(GstScheduler *sched);

static GstSchedulerClass *parent_class = NULL;

static GType
gst_standard_scheduler_get_type (void)
{
  if (!_gst_standard_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstStandardSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_standard_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstStandardScheduler),
      0,
      (GInstanceInitFunc) gst_standard_scheduler_init,
      NULL
    };

    _gst_standard_scheduler_type = g_type_register_static (GST_TYPE_SCHEDULER, "GstStandardScheduler", &scheduler_info, 0);
  }
  return _gst_standard_scheduler_type;
}

static void
gst_standard_scheduler_class_init (GstStandardSchedulerClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstSchedulerClass *gstscheduler_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstscheduler_class = (GstSchedulerClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_SCHEDULER);

  gobject_class->dispose	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_dispose);

  gstscheduler_class->setup 		= GST_DEBUG_FUNCPTR (gst_standard_scheduler_setup);
  gstscheduler_class->reset	 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_reset);
  gstscheduler_class->add_element 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_add_element);
  gstscheduler_class->remove_element 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_remove_element);
  gstscheduler_class->state_transition 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_state_transition);
  gstscheduler_class->lock_element 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_lock_element);
  gstscheduler_class->unlock_element 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_unlock_element);
  gstscheduler_class->yield	 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_yield);
  gstscheduler_class->interrupt 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_interrupt);
  gstscheduler_class->error	 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_error);
  gstscheduler_class->pad_connect 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_pad_connect);
  gstscheduler_class->pad_disconnect 	= GST_DEBUG_FUNCPTR (gst_standard_scheduler_pad_disconnect);
  gstscheduler_class->pad_select	= NULL; /* FIXME */
  gstscheduler_class->iterate 		= GST_DEBUG_FUNCPTR (gst_standard_scheduler_iterate);

  gstscheduler_class->show 		= GST_DEBUG_FUNCPTR (gst_standard_scheduler_show);
  
  cothreads_init(NULL);
}

static void
gst_standard_scheduler_init (GstStandardScheduler *scheduler)
{
  scheduler->elements = NULL;
  scheduler->num_elements = 0;
  scheduler->chains = NULL;
  scheduler->num_chains = 0;
  scheduler->current = scheduler->main = cothread_create (NULL, 0, NULL);
}

static void
gst_standard_scheduler_dispose (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstSchedulerFactory *factory;

  gst_plugin_set_longname (plugin, "A standard scheduler");

  factory = gst_schedulerfactory_new ("standard",
	                              "A standard scheduler, it uses cothreads",
		                      gst_standard_scheduler_get_type());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register scheduler: standard");
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gststandardscheduler",
  plugin_init
};

static inline void sched_switch (GstStandardScheduler *sched, cothread *to)
{
  cothread *from = sched->current;
  GST_INFO (GST_CAT_COTHREAD_SWITCH, "switching from cothread %p to cothread %p",
	    sched->current, to);
  sched->current = to;
  cothread_switch (from, to);
}

static int
gst_standard_scheduler_loopfunc_wrapper (int argc, char *argv[])
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

static int
gst_standard_scheduler_chain_wrapper (int argc, char *argv[])
{
  GstElement *element = GST_ELEMENT_CAST (argv);
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

      realpad = GST_REAL_PAD_CAST (pad);

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
	    GST_DEBUG (GST_CAT_DATAFLOW, "calling chain function of %s:%s done\n", name,
		       GST_PAD_NAME (pad));
	  }
	}
	else {
          gst_element_error (element, "NULL buffer detected. Is \"%s:%s\" connected?",
			  name, GST_PAD_NAME (pad), NULL);
	}
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
  GST_FLAG_UNSET (element, GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE ("(%d,'%s')", argc, name);
  return 0;
}

static int
gst_standard_scheduler_src_wrapper (int argc, char *argv[])
{
  GstElement *element = GST_ELEMENT_CAST (argv);
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

      realpad = GST_REAL_PAD_CAST (pads->data);

      pads = g_list_next (pads);
      if (GST_RPAD_DIRECTION (realpad) == GST_PAD_SRC) {
	GST_DEBUG (GST_CAT_DATAFLOW, "calling _getfunc for %s:%s\n", GST_DEBUG_PAD_NAME (realpad));
	if (realpad->regiontype != GST_REGION_VOID) {
	  g_return_val_if_fail (GST_RPAD_GETREGIONFUNC (realpad) != NULL, 0);
/*          if (GST_RPAD_GETREGIONFUNC(realpad) == NULL)		   */	
/*            fprintf(stderr,"error, no getregionfunc in \"%s\"\n", name); */
/*          else							   */
	  buf =
	    (GST_RPAD_GETREGIONFUNC (realpad)) (GST_PAD_CAST (realpad), realpad->regiontype,
						realpad->offset, realpad->len);
	  realpad->regiontype = GST_REGION_VOID;
	}
	else {
	  g_return_val_if_fail (GST_RPAD_GETFUNC (realpad) != NULL, 0);
/*          if (GST_RPAD_GETFUNC(realpad) == NULL)			*/
/*            fprintf(stderr,"error, no getfunc in \"%s\"\n", name);	*/
/*          else							*/
	  buf = GST_RPAD_GETFUNC (realpad) (GST_PAD_CAST (realpad));
	}

	GST_DEBUG (GST_CAT_DATAFLOW, "calling gst_pad_push on pad %s:%s\n",
		   GST_DEBUG_PAD_NAME (realpad));
	gst_pad_push (GST_PAD_CAST (realpad), buf);
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));

  GST_FLAG_UNSET (element, GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE ("");
  return 0;
}

static void
gst_standard_scheduler_chainhandler_proxy (GstPad * pad, GstBuffer * buf)
{
  GstRealPad *peer = GST_RPAD_PEER (pad);
  gint loop_count = 100;

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));
  GST_DEBUG (GST_CAT_DATAFLOW, "putting buffer %p in peer \"%s:%s\"'s pen\n", buf,
	     GST_DEBUG_PAD_NAME (peer));

  /* 
   * loop until the bufferpen is empty so we can fill it up again
   */
  while (GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) != NULL && --loop_count) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to empty bufpen %d\n",
	       GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)), loop_count);
    sched_switch (GST_PAD_STANDARD_SCHED (pad),
                  GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));

    /* we may no longer be the same pad, check. */
    if (GST_RPAD_PEER (peer) != (GstRealPad *) pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *) GST_RPAD_PEER (peer);
    }
  }

  if (loop_count == 0) {
    gst_element_error (GST_PAD_PARENT (pad), 
		    "(internal error) maximum number of switches exceeded");
    return;
  }
  g_assert (GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) == NULL);
  /* now fill the bufferpen and switch so it can be consumed */
  GST_RPAD_BUFPEN (GST_RPAD_PEER (pad)) = buf;
  GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p\n",
	     GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));
  sched_switch (GST_PAD_STANDARD_SCHED (pad),
                GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));

  GST_DEBUG (GST_CAT_DATAFLOW, "done switching\n");
}

static GstBuffer *
gst_standard_scheduler_gethandler_proxy (GstPad * pad)
{
  GstBuffer *buf;
  GstRealPad *peer = GST_RPAD_PEER (pad);

  GST_DEBUG_ENTER ("(%s:%s)", GST_DEBUG_PAD_NAME (pad));

  /* FIXME this should be bounded */
  /* we will loop switching to the peer until it's filled up the bufferpen */
  while (GST_RPAD_BUFPEN (pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to \"%s\": %p to fill bufpen\n",
	       GST_ELEMENT_NAME (GST_ELEMENT (GST_PAD_PARENT (pad))),
	       GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));
    sched_switch (GST_PAD_STANDARD_SCHED (pad),
                  GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));

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
gst_standard_scheduler_pullregionfunc_proxy (GstPad * pad, GstRegionType type, guint64 offset, guint64 len)
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
	       GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));
    sched_switch (GST_PAD_STANDARD_SCHED (pad),
                  GST_ELEMENT_THREADSTATE (GST_PAD_PARENT (pad)));

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


static gboolean
gst_standard_scheduler_cothreaded_chain (GstBin * bin, GstSchedulerChain * chain)
{
  GList *elements;
  GstElement *element;
  cothread_func wrapper_function;
  GList *pads;
  GstPad *pad;

  GST_DEBUG (GST_CAT_SCHEDULING, "chain is using COTHREADS\n");

  /* walk through all the chain's elements */
  elements = chain->elements;
  while (elements) {
    gboolean decoupled;

    element = GST_ELEMENT_CAST (elements->data);
    elements = g_list_next (elements);

    decoupled = (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED) ? TRUE :  FALSE);

    /* start out without a wrapper function, we select it later */
    wrapper_function = NULL;

    /* if the element has a loopfunc... */
    if (element->loopfunc != NULL) {
      wrapper_function = GST_DEBUG_FUNCPTR (gst_standard_scheduler_loopfunc_wrapper);
      GST_DEBUG (GST_CAT_SCHEDULING, "element '%s' is a loop-based\n", GST_ELEMENT_NAME (element));
    }
    else {
      /* otherwise we need to decide what kind of cothread */
      /* if it's not DECOUPLED, we decide based on whether it's a source or not */
      if (!decoupled) {
	/* if it doesn't have any sinks, it must be a source (duh) */
	if (element->numsinkpads == 0) {
	  wrapper_function = GST_DEBUG_FUNCPTR (gst_standard_scheduler_src_wrapper);
	  GST_DEBUG (GST_CAT_SCHEDULING, "element '%s' is a source, using _src_wrapper\n",
		     GST_ELEMENT_NAME (element));
	}
	else {
	  wrapper_function = GST_DEBUG_FUNCPTR (gst_standard_scheduler_chain_wrapper);
	  GST_DEBUG (GST_CAT_SCHEDULING, "element '%s' is a filter, using _chain_wrapper\n",
		     GST_ELEMENT_NAME (element));
	}
      }
    }

    /* now we have to walk through the pads to set up their state */
    pads = gst_element_get_pad_list (element);
    while (pads) {
      GstPad *peerpad;

      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);

      if (!GST_IS_REAL_PAD (pad))
	continue;
      
      peerpad = GST_PAD (GST_RPAD_PEER (pad));

      /* if the element is DECOUPLED or outside the manager, we have to chain */
      if ((wrapper_function == NULL) ||
	  (peerpad && (GST_ELEMENT_CAST (GST_PAD_PARENT (peerpad))->sched != GST_SCHEDULER (chain->sched)))) {

	if (!decoupled && GST_RPAD_PEER (pad) && 
	    !GST_FLAG_IS_SET (GST_PAD_PARENT (peerpad), GST_ELEMENT_DECOUPLED)) {
          /* whoa non decoupled with different schedulers */
          gst_element_error (element, "element \"%s\" is not decoupled but has pads in different schedulers",
			  GST_ELEMENT_NAME (element), NULL);
	  return FALSE;
	}
	
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

      }
      /* otherwise we really are a cothread */
      else {
	if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "setting cothreaded push proxy for sinkpad %s:%s\n",
	     GST_DEBUG_PAD_NAME (pad));
	  GST_RPAD_CHAINHANDLER (pad) = GST_DEBUG_FUNCPTR (gst_standard_scheduler_chainhandler_proxy);
	}
	else {
	  GST_DEBUG (GST_CAT_SCHEDULING, "setting cothreaded pull proxy for srcpad %s:%s\n",
	     GST_DEBUG_PAD_NAME (pad));
	  GST_RPAD_GETHANDLER (pad) = GST_DEBUG_FUNCPTR (gst_standard_scheduler_gethandler_proxy);
	  GST_RPAD_PULLREGIONFUNC (pad) = GST_DEBUG_FUNCPTR (gst_standard_scheduler_pullregionfunc_proxy);
	}
      }
    }
    
    /* need to set up the cothread now */
    if (wrapper_function != NULL) {
      GST_ELEMENT_THREADSTATE (element) = cothread_create (wrapper_function, 1, (void **) element);
      if (GST_ELEMENT_THREADSTATE (element) == NULL) {
        gst_element_error (element, "could not create cothread for \"%s\"", 
                           GST_ELEMENT_NAME (element), NULL);
        return FALSE;
      }
      GST_DEBUG (GST_CAT_SCHEDULING, "created cothread %p for '%s'\n", 
                 GST_ELEMENT_THREADSTATE (element),
                 GST_ELEMENT_NAME (element));
      GST_DEBUG (GST_CAT_SCHEDULING, "set wrapper function for '%s' to &%s\n",
		 GST_ELEMENT_NAME (element), GST_DEBUG_FUNCPTR_NAME (wrapper_function));
    }
  }
  
  return TRUE;
}

/*
G_GNUC_UNUSED static void
gst_standard_scheduler_chained_chain (GstBin *bin, _GstBinChain *chain) {
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
gst_standard_scheduler_chain_new (GstStandardScheduler * sched)
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

  /* notify the scheduler that something changed */
  GST_FLAG_SET(sched, GST_STANDARD_SCHEDULER_CHANGE);

  GST_INFO (GST_CAT_SCHEDULING, "created new chain %p, now are %d chains in sched %p",
	    chain, sched->num_chains, sched);

  return chain;
}

static void
gst_standard_scheduler_chain_destroy (GstSchedulerChain * chain)
{
  GstStandardScheduler *sched = chain->sched;

  /* remove the chain from the schedulers' list of chains */
  sched->chains = g_list_remove (sched->chains, chain);
  sched->num_chains--;

  /* destroy the chain */
  g_list_free (chain->disabled);	/* should be empty... */
  g_list_free (chain->elements);	/* ditto 	      */

  GST_INFO (GST_CAT_SCHEDULING, "destroyed chain %p, now are %d chains in sched %p", chain,
	    sched->num_chains, sched);

  g_free (chain);

  /* notify the scheduler that something changed */
  GST_FLAG_SET(sched, GST_STANDARD_SCHEDULER_CHANGE);
}

static void
gst_standard_scheduler_chain_add_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* set the sched pointer for the element */
  element->sched = GST_SCHEDULER (chain->sched);

  /* add the element to the list of 'disabled' elements */
  chain->disabled = g_list_prepend (chain->disabled, element);
  chain->num_elements++;

  /* notify the scheduler that something changed */
  GST_FLAG_SET(chain->sched, GST_STANDARD_SCHEDULER_CHANGE);
}

static gboolean
gst_standard_scheduler_chain_enable_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "enabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* remove from disabled list */
  chain->disabled = g_list_remove (chain->disabled, element);

  /* add to elements list */
  chain->elements = g_list_prepend (chain->elements, element);

  /* notify the scheduler that something changed */
  GST_FLAG_SET(chain->sched, GST_STANDARD_SCHEDULER_CHANGE);

  /* reschedule the chain */
  return gst_standard_scheduler_cothreaded_chain (GST_BIN (GST_SCHEDULER (chain->sched)->parent), chain);
}

static void
gst_standard_scheduler_chain_disable_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "disabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* remove from elements list */
  chain->elements = g_list_remove (chain->elements, element);

  /* add to disabled list */
  chain->disabled = g_list_prepend (chain->disabled, element);

  /* notify the scheduler that something changed */
  GST_FLAG_SET(chain->sched, GST_STANDARD_SCHEDULER_CHANGE);

  /* reschedule the chain */
/* FIXME this should be done only if manager state != NULL */
/*  gst_standard_scheduler_cothreaded_chain(GST_BIN(chain->sched->parent),chain); */
}

static void
gst_standard_scheduler_chain_remove_element (GstSchedulerChain * chain, GstElement * element)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from chain %p", GST_ELEMENT_NAME (element),
	    chain);

  /* if it's active, deactivate it */
  if (g_list_find (chain->elements, element)) {
    gst_standard_scheduler_chain_disable_element (chain, element);
  }
  /* we have to check for a threadstate here because a queue doesn't have one */
  if (GST_ELEMENT_THREADSTATE (element)) {
    cothread_destroy (GST_ELEMENT_THREADSTATE (element));
    GST_ELEMENT_THREADSTATE (element) = NULL;
  }

  /* remove the element from the list of elements */
  chain->disabled = g_list_remove (chain->disabled, element);
  chain->num_elements--;

  /* notify the scheduler that something changed */
  GST_FLAG_SET(chain->sched, GST_STANDARD_SCHEDULER_CHANGE);

  /* if there are no more elements in the chain, destroy the chain */
  if (chain->num_elements == 0)
    gst_standard_scheduler_chain_destroy (chain);

}

static void
gst_standard_scheduler_chain_elements (GstStandardScheduler * sched, GstElement * element1, GstElement * element2)
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
    chain = gst_standard_scheduler_chain_new (sched);
    gst_standard_scheduler_chain_add_element (chain, element1);
    gst_standard_scheduler_chain_add_element (chain, element2);
    /* FIXME chain changed here */
/*    gst_standard_scheduler_cothreaded_chain(chain->sched->parent,chain); */

    /* otherwise if both have chains already, join them */
  }
  else if ((chain1 != NULL) && (chain2 != NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "merging chain %p into chain %p", chain2, chain1);
    /* take the contents of chain2 and merge them into chain1 */
    chain1->disabled = g_list_concat (chain1->disabled, g_list_copy (chain2->disabled));
    chain1->elements = g_list_concat (chain1->elements, g_list_copy (chain2->elements));
    chain1->num_elements += chain2->num_elements;
    /* FIXME chain changed here */
/*    gst_standard_scheduler_cothreaded_chain(chain->sched->parent,chain); */

    gst_standard_scheduler_chain_destroy (chain2);

    /* otherwise one has a chain already, the other doesn't */
  }
  else {
    /* pick out which one has the chain, and which doesn't */
    if (chain1 != NULL)
      chain = chain1, element = element2;
    else
      chain = chain2, element = element1;

    GST_INFO (GST_CAT_SCHEDULING, "adding element to existing chain");
    gst_standard_scheduler_chain_add_element (chain, element);
    /* FIXME chain changed here */
/*    gst_standard_scheduler_cothreaded_chain(chain->sched->parent,chain); */
  }

}


/* find the chain within the scheduler that holds the element, if any */
static GstSchedulerChain *
gst_standard_scheduler_find_chain (GstStandardScheduler * sched, GstElement * element)
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
gst_standard_scheduler_chain_recursive_add (GstSchedulerChain * chain, GstElement * element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;

  /* add the element to the chain */
  gst_standard_scheduler_chain_add_element (chain, element);

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
	if (gst_standard_scheduler_find_chain (chain->sched, peerelement) == NULL) {
	  gst_standard_scheduler_chain_recursive_add (chain, peerelement);
	}
      }
    }
  }
}

/*
 * Entry points for this scheduler.
 */
static void
gst_standard_scheduler_setup (GstScheduler *sched)
{
  /* bling blau */
}

static void
gst_standard_scheduler_reset (GstScheduler *sched)
{
  GList *elements = GST_STANDARD_SCHEDULER_CAST (sched)->elements;

  while (elements) {
    GST_ELEMENT_THREADSTATE (elements->data) = NULL;
    elements = g_list_next (elements);
  }
}

static void
gst_standard_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;
  GstSchedulerChain *chain;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

  /* if it's already in this scheduler, don't bother doing anything */
  if (GST_ELEMENT_SCHED (element) == sched)
    return;

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to scheduler", GST_ELEMENT_NAME (element));

  /* if the element already has a scheduler something went wrong */
  if (GST_ELEMENT_SCHED (element)) {
    GST_ERROR (element, "grave error");
    return;
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
  chain = gst_standard_scheduler_chain_new (bsched);
  gst_standard_scheduler_chain_add_element (chain, element);

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
	gst_standard_scheduler_chain_elements (bsched, element, peerelement);
      }
    }
  }
}

static void
gst_standard_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerChain *chain;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

  if (g_list_find (bsched->elements, element)) {
    GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from scheduler",
	      GST_ELEMENT_NAME (element));

    /* find what chain the element is in */
    chain = gst_standard_scheduler_find_chain (bsched, element);

    /* remove it from its chain */
    gst_standard_scheduler_chain_remove_element (chain, element);

    /* remove it from the list of elements */
    bsched->elements = g_list_remove (bsched->elements, element);
    bsched->num_elements--;

    /* unset the scheduler pointer in the element */
    GST_ELEMENT_SCHED (element) = NULL;
    
  }
}

static GstElementStateReturn
gst_standard_scheduler_state_transition (GstScheduler *sched, GstElement *element, gint transition)
{
  GstSchedulerChain *chain;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

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
    chain = gst_standard_scheduler_find_chain (bsched, element);

    /* remove it from the chain */
    if (chain) {
      if (transition == GST_STATE_PLAYING_TO_PAUSED) {
        gst_standard_scheduler_chain_disable_element (chain, element);
      }
      else if (transition == GST_STATE_PAUSED_TO_PLAYING) {
        if (!gst_standard_scheduler_chain_enable_element (chain, element)) {
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
gst_standard_scheduler_lock_element (GstScheduler * sched, GstElement * element)
{
//  if (GST_ELEMENT_THREADSTATE (element))
//    cothread_lock (GST_ELEMENT_THREADSTATE (element));
}

static void
gst_standard_scheduler_unlock_element (GstScheduler * sched, GstElement * element)
{
//  if (GST_ELEMENT_THREADSTATE (element))
//    cothread_unlock (GST_ELEMENT_THREADSTATE (element));
}

static void
gst_standard_scheduler_yield (GstScheduler *sched, GstElement *element)
{
  if (GST_ELEMENT_IS_COTHREAD_STOPPING (element)) {
    sched_switch ((GstStandardScheduler*) sched, ((GstStandardScheduler*) sched)->main);
  }
}

static gboolean
gst_standard_scheduler_interrupt (GstScheduler *sched, GstElement *element)
{
  GST_FLAG_SET (element, GST_ELEMENT_COTHREAD_STOPPING);
  sched_switch ((GstStandardScheduler*) sched, ((GstStandardScheduler*) sched)->main);

  return FALSE;
}

static void
gst_standard_scheduler_error (GstScheduler *sched, GstElement *element)
{
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

  if (GST_ELEMENT_THREADSTATE (element)) {
    GstSchedulerChain *chain;
    
    chain = gst_standard_scheduler_find_chain (bsched, element);
    if (chain)
      gst_standard_scheduler_chain_disable_element (chain, element);

    GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_ERROR;

    sched_switch ((GstStandardScheduler*) sched, ((GstStandardScheduler*) sched)->main);
  }
}

static void
gst_standard_scheduler_pad_connect (GstScheduler * sched, GstPad *srcpad, GstPad *sinkpad)
{
  GstElement *srcelement, *sinkelement;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

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
    gst_standard_scheduler_chain_elements (bsched, srcelement, sinkelement);
  }
}

static void
gst_standard_scheduler_pad_disconnect (GstScheduler * sched, GstPad * srcpad, GstPad * sinkpad)
{
  GstElement *element1, *element2;
  GstSchedulerChain *chain1, *chain2;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

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
  chain1 = gst_standard_scheduler_find_chain (bsched, element1);
  chain2 = gst_standard_scheduler_find_chain (bsched, element2);

  if (chain1 != chain2) {
    /* elements not in the same chain don't need to be separated */
    GST_INFO (GST_CAT_SCHEDULING, "elements not in the same chain");
    return;
  }

  if (chain1) {
    GST_INFO (GST_CAT_SCHEDULING, "destroying chain");
    gst_standard_scheduler_chain_destroy (chain1);

    /* now create a new chain to hold element1 and build it from scratch */
    chain1 = gst_standard_scheduler_chain_new (bsched);
    gst_standard_scheduler_chain_recursive_add (chain1, element1);
  }

  /* check the other element to see if it landed in the newly created chain */
  if (gst_standard_scheduler_find_chain (bsched, element2) == NULL) {
    /* if not in chain, create chain and build from scratch */
    chain2 = gst_standard_scheduler_chain_new (bsched);
    gst_standard_scheduler_chain_recursive_add (chain2, element2);
  }
}

static GstSchedulerState
gst_standard_scheduler_iterate (GstScheduler * sched)
{
  GstBin *bin = GST_BIN (sched->parent);
  GList *chains;
  GstSchedulerChain *chain;
  GstElement *entry;
  GList *elements;
  gint scheduled = 0;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

  GST_DEBUG_ENTER ("(\"%s\")", GST_ELEMENT_NAME (bin));

  /* clear the changes flag */
  GST_FLAG_UNSET(bsched, GST_STANDARD_SCHEDULER_CHANGE);
  
  /* step through all the chains */
  chains = bsched->chains;

  if (chains == NULL)
    return GST_SCHEDULER_STATE_STOPPED;

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
	entry = GST_ELEMENT_CAST (elements->data);
	elements = g_list_next (elements);
	if (GST_FLAG_IS_SET (entry, GST_ELEMENT_DECOUPLED)) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "entry \"%s\" is DECOUPLED, skipping\n",
		     GST_ELEMENT_NAME (entry));
	  entry = NULL;
	}
	else if (GST_FLAG_IS_SET (entry, GST_ELEMENT_INFINITE_LOOP)) {
	  GST_DEBUG (GST_CAT_SCHEDULING, "entry \"%s\" is not valid, skipping\n",
		     GST_ELEMENT_NAME (entry));
	  entry = NULL;
	}
	else
	  break;
      }
      if (entry) {
	GstSchedulerState state;
	      
	GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);

	GST_DEBUG (GST_CAT_DATAFLOW, "set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
		   GST_ELEMENT_NAME (entry), entry);
	if (GST_ELEMENT_THREADSTATE (entry)) {
	  sched_switch (GST_ELEMENT_STANDARD_SCHED (entry), GST_ELEMENT_THREADSTATE (entry));
	  /* if something changed, return - go on else */
	  if (GST_FLAG_IS_SET(bsched, GST_STANDARD_SCHEDULER_CHANGE))
	    return GST_SCHEDULER_STATE_RUNNING;
	}
	else {
	  GST_DEBUG (GST_CAT_DATAFLOW, "cothread switch not possible, element has no threadstate\n");
	  return GST_SCHEDULER_STATE_ERROR;
	}

	state = GST_SCHEDULER_STATE (sched);

	/* following is a check to see if the chain was interrupted due to a
	 * top-half state_change().  (i.e., if there's a pending state.)
	 *
	 * if it was, return to gstthread.c::gst_thread_main_loop() to
	 * execute the state change.
	 */
	GST_DEBUG (GST_CAT_DATAFLOW, "cothread switch ended or interrupted\n");

	if (state != GST_SCHEDULER_STATE_RUNNING) {
	  GST_INFO (GST_CAT_DATAFLOW, "scheduler is not running, in state %d", state);
	  return state;
	}

	scheduled++;
      }
      else {
        GST_INFO (GST_CAT_DATAFLOW, "no entry in this chain, trying the next one");
      }
    }
    else {
      GST_INFO (GST_CAT_DATAFLOW, "no enabled elements in this chain, trying the next one");
    }
  }

  GST_DEBUG (GST_CAT_DATAFLOW, "leaving (%s)\n", GST_ELEMENT_NAME (bin));
  if (scheduled == 0) {
    GST_INFO (GST_CAT_DATAFLOW, "nothing was scheduled, return STOPPED");
    return GST_SCHEDULER_STATE_STOPPED;
  }
  else {
    GST_INFO (GST_CAT_DATAFLOW, "scheduler still running, return RUNNING");
    return GST_SCHEDULER_STATE_RUNNING;
  }
}


static void
gst_standard_scheduler_show (GstScheduler * sched)
{
  GList *chains, *elements;
  GstElement *element;
  GstSchedulerChain *chain;
  GstStandardScheduler *bsched = GST_STANDARD_SCHEDULER (sched);

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
