/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

#define CLASS(obj)	GST_SCHEDULER_CLASS (G_OBJECT_GET_CLASS (obj))

#include "gst_private.h"

#include "gstsystemclock.h"
#include "gstscheduler.h"

static void 	gst_scheduler_class_init 	(GstSchedulerClass *klass);
static void 	gst_scheduler_init 		(GstScheduler *sched);

static GstObjectClass *parent_class = NULL;

static gchar *_default_name = NULL;

GType
gst_scheduler_get_type (void)
{
  static GType _gst_scheduler_type = 0;

  if (!_gst_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstScheduler),
      0,
      (GInstanceInitFunc) gst_scheduler_init,
      NULL
    };

    _gst_scheduler_type = g_type_register_static (GST_TYPE_OBJECT, "GstScheduler", 
		    &scheduler_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_scheduler_type;
}

static void
gst_scheduler_class_init (GstSchedulerClass *klass)
{
  parent_class = g_type_class_ref (GST_TYPE_OBJECT);
}

static void
gst_scheduler_init (GstScheduler *sched)
{
  sched->clock_providers = NULL;
  sched->clock_receivers = NULL;
  sched->schedulers = NULL;
  sched->state = GST_SCHEDULER_STATE_NONE;
  sched->parent = NULL;
  sched->parent_sched = NULL;
  sched->clock = NULL;
}

/**
 * gst_scheduler_setup:
 * @sched: the scheduler
 *
 * Prepare the scheduler.
 */
void
gst_scheduler_setup (GstScheduler *sched)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  if (CLASS (sched)->setup)
    CLASS (sched)->setup (sched);
}

/**
 * gst_scheduler_reset:
 * @sched: the scheduler
 *
 * Reset the scheduler
 */
void
gst_scheduler_reset (GstScheduler *sched)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  if (CLASS (sched)->reset)
    CLASS (sched)->reset (sched);
}

/**
 * gst_scheduler_pad_connect:
 * @sched: the scheduler
 * @srcpad: the srcpad to connect
 * @sinkpad: the sinkpad to connect to
 *
 * Connect the srcpad to the given sinkpad.
 */
void
gst_scheduler_pad_connect (GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  if (CLASS (sched)->pad_connect)
    CLASS (sched)->pad_connect (sched, srcpad, sinkpad);
}

/**
 * gst_scheduler_pad_disconnect:
 * @sched: the scheduler
 * @srcpad: the srcpad to disconnect
 * @sinkpad: the sinkpad to disconnect from
 *
 * Disconnect the srcpad to the given sinkpad.
 */
void
gst_scheduler_pad_disconnect (GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  if (CLASS (sched)->pad_disconnect)
    CLASS (sched)->pad_disconnect (sched, srcpad, sinkpad);
}

/**
 * gst_scheduler_pad_select:
 * @sched: the scheduler
 * @padlist: the padlist to select on
 *
 * register the given padlist for a select operation. 
 *
 * Returns: the pad which received a buffer.
 */
GstPad *
gst_scheduler_pad_select (GstScheduler *sched, GList *padlist)
{
  g_return_val_if_fail (GST_IS_SCHEDULER (sched), NULL);
  g_return_val_if_fail (padlist != NULL, NULL);

  if (CLASS (sched)->pad_select)
    CLASS (sched)->pad_select (sched, padlist);

  return NULL;
}

/**
 * gst_scheduler_add_element:
 * @sched: the scheduler
 * @element: the element to add to the schedulerr
 *
 * Add an element to the schedulerr.
 */
void
gst_scheduler_add_element (GstScheduler *sched, GstElement *element)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (element->getclockfunc) {
    sched->clock_providers = g_list_prepend (sched->clock_providers, element);
  }
  if (element->setclockfunc) {
    sched->clock_receivers = g_list_prepend (sched->clock_receivers, element);
  }

  if (CLASS (sched)->add_element)
    CLASS (sched)->add_element (sched, element);
}

/**
 * gst_scheduler_remove_element:
 * @sched: the schedulerr
 * @element: the element to remove
 *
 * Remove an element from the schedulerr.
 */
void
gst_scheduler_remove_element (GstScheduler *sched, GstElement *element)
{
  GList *pads;
  
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sched->clock_providers = g_list_remove (sched->clock_providers, element);
  sched->clock_receivers = g_list_remove (sched->clock_receivers, element);

  if (CLASS (sched)->remove_element)
    CLASS (sched)->remove_element (sched, element);
  
  for (pads = element->pads; pads; pads = pads->next) {
    GstPad *pad = GST_PAD (pads->data);
    
    if (GST_IS_REAL_PAD (pad)) {
      gst_pad_unset_sched (GST_PAD (pads->data));
    }
  }
}

/**
 * gst_scheduler_state_transition:
 * @sched: the scheduler
 * @element: the element with the state transition
 * @transition: the state transition
 *
 * Tell the scheduler that an element changed its state.
 *
 * Returns: a GstElementStateReturn indicating success or failure
 * of the state transition.
 */
GstElementStateReturn
gst_scheduler_state_transition (GstScheduler *sched, GstElement *element, gint transition)
{
  g_return_val_if_fail (GST_IS_SCHEDULER (sched), GST_STATE_FAILURE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);

  if (element == sched->parent && sched->parent_sched == NULL) {

    switch (transition) {
      case GST_STATE_READY_TO_PAUSED:
      {
        GstClock *clock = gst_scheduler_get_clock (sched);

        if (clock)
          gst_clock_reset (clock);

	sched->current_clock = clock;
        break;
      }
      case GST_STATE_PAUSED_TO_PLAYING:
      {
	gst_scheduler_set_clock (sched, sched->current_clock);
        if (sched->current_clock)
          gst_clock_activate (sched->current_clock, TRUE);
        break;
      }
      case GST_STATE_PLAYING_TO_PAUSED:
        if (sched->current_clock)
          gst_clock_activate (sched->current_clock, FALSE);
        break;
    }
  }

  if (CLASS (sched)->state_transition)
    return CLASS (sched)->state_transition (sched, element, transition);

  return GST_STATE_SUCCESS;
}

/**
 * gst_scheduler_add_scheduler:
 * @sched: the schedulerr
 * @sched2: the scheduler to add
 *
 a Notifies the scheduler that it has to monitor this scheduler.
 */
void
gst_scheduler_add_scheduler (GstScheduler *sched, GstScheduler *sched2)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_SCHEDULER (sched2));

  sched->schedulers = g_list_prepend (sched->schedulers, sched2);
  sched2->parent_sched = sched;

  if (CLASS (sched)->add_scheduler)
    CLASS (sched)->add_scheduler (sched, sched2);
}

/**
 * gst_scheduler_remove_scheduler:
 * @sched: the schedulerr
 * @sched2: the scheduler to remove
 *
 a Notifies the scheduler that it can stop monitoring this scheduler.
 */
void
gst_scheduler_remove_scheduler (GstScheduler *sched, GstScheduler *sched2)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_SCHEDULER (sched2));

  sched->schedulers = g_list_remove (sched->schedulers, sched2);
  sched2->parent_sched = NULL;

  if (CLASS (sched)->remove_scheduler)
    CLASS (sched)->remove_scheduler (sched, sched2);
}

/**
 * gst_scheduler_lock_element:
 * @sched: the scheduler
 * @element: the element to lock
 *
 * Acquire a lock on the given element in the given scheduler.
 */
void
gst_scheduler_lock_element (GstScheduler *sched, GstElement *element)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (CLASS (sched)->lock_element)
    CLASS (sched)->lock_element (sched, element);
}

/**
 * gst_scheduler_unlock_element:
 * @sched: the scheduler
 * @element: the element to unlock
 *
 * Release the lock on the given element in the given scheduler.
 */
void
gst_scheduler_unlock_element (GstScheduler *sched, GstElement *element)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (CLASS (sched)->unlock_element)
    CLASS (sched)->unlock_element (sched, element);
}

/**
 * gst_scheduler_error:
 * @sched: the scheduler
 * @element: the element with the error
 *
 * Tell the scheduler an element was in error
 */
void
gst_scheduler_error (GstScheduler *sched, GstElement *element)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (CLASS (sched)->error)
    CLASS (sched)->error (sched, element);
}

/**
 * gst_scheduler_yield:
 * @sched: the scheduler
 * @element: the element requesting a yield
 *
 * Tell the scheduler to schedule another element.
 */
void
gst_scheduler_yield (GstScheduler *sched, GstElement *element)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (CLASS (sched)->yield)
    CLASS (sched)->yield (sched, element);
}

/**
 * gst_scheduler_interrupt:
 * @sched: the scheduler
 * @element: the element requesting an interrupt
 *
 * Tell the scheduler to interrupt execution of this element.
 *
 * Returns: TRUE if the element should return NULL from the chain/get
 * function.
 */
gboolean
gst_scheduler_interrupt (GstScheduler *sched, GstElement *element)
{
  g_return_val_if_fail (GST_IS_SCHEDULER (sched), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  if (CLASS (sched)->interrupt)
    return CLASS (sched)->interrupt (sched, element);

  return FALSE;
}

/**
 * gst_scheduler_get_clock:
 * @sched: the scheduler
 *
 * Get the current clock used by the scheduler
 *
 * Returns: a GstClock
 */
GstClock*
gst_scheduler_get_clock (GstScheduler *sched)
{
  GstClock *clock = NULL;
  
  if (GST_FLAG_IS_SET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK)) {
    clock = sched->clock;  
  }
  else {
    if (sched->schedulers) {
      GList *schedulers = sched->schedulers;

      while (schedulers) {
        GstScheduler *scheduler = GST_SCHEDULER (schedulers->data);
      
        clock = gst_scheduler_get_clock (scheduler);
        if (clock)
	  break;

        schedulers = g_list_next (schedulers);
      }
    }
    if (!clock && sched->clock_providers) {
      clock = gst_element_get_clock (GST_ELEMENT (sched->clock_providers->data));
    }
    if (!clock && sched->parent_sched == NULL) {
      clock = gst_system_clock_obtain ();
    }
  }

  return clock;
}

/**
 * gst_scheduler_use_clock:
 * @sched: the scheduler
 * @clock: the clock to use
 *
 * Force the scheduler to use the given clock. The scheduler will
 * always use the given clock even if new clock providers are added
 * to this scheduler.
 */
void
gst_scheduler_use_clock (GstScheduler *sched, GstClock *clock)
{
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_FLAG_SET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK);
  sched->clock = clock;
}

/**
 * gst_scheduler_set_clock:
 * @sched: the scheduler
 * @clock: the clock to set
 *
 * Set the clock for the scheduler. The clock will be distributed 
 * to all the elements managed by the scheduler. 
 */
void
gst_scheduler_set_clock (GstScheduler *sched, GstClock *clock)
{
  GList *receivers;
  GList *schedulers;

  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  receivers = sched->clock_receivers;
  schedulers = sched->schedulers;

  sched->current_clock = clock;

  while (receivers) {
    GstElement *element = GST_ELEMENT (receivers->data);

    gst_element_set_clock (element, clock);
    receivers = g_list_next (receivers);
  }
  while (schedulers) {
    GstScheduler *scheduler = GST_SCHEDULER (schedulers->data);

    gst_scheduler_set_clock (scheduler, clock);
    schedulers = g_list_next (schedulers);
  }
}

/**
 * gst_scheduler_auto_clock:
 * @sched: the scheduler
 *
 * Let the scheduler select a clock automatically.
 */
void
gst_scheduler_auto_clock (GstScheduler *sched)
{
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_FLAG_UNSET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK);
  sched->clock = NULL;
}

/**
 * gst_scheduler_clock_wait:
 * @sched: the scheduler
 * @element: the element that wants to wait
 * @clock: the clock to use
 * @time: the time to wait for
 *
 * Wait till the clock reaches a specific time
 *
 * Returns: the status of the operation
 */
GstClockReturn
gst_scheduler_clock_wait (GstScheduler *sched, GstElement *element, GstClock *clock, GstClockTime time)
{
  g_return_val_if_fail (GST_IS_SCHEDULER (sched), GST_CLOCK_ERROR);

  if (CLASS (sched)->clock_wait)
    return CLASS (sched)->clock_wait (sched, element, clock, time);

  return GST_CLOCK_TIMEOUT;
}

/**
 * gst_scheduler_iterate:
 * @sched: the scheduler
 *
 * Perform one iteration on the schedulerr.
 *
 * Returns: a boolean indicating something usefull has happened.
 */
gboolean
gst_scheduler_iterate (GstScheduler *sched)
{
  g_return_val_if_fail (GST_IS_SCHEDULER (sched), FALSE);

  if (CLASS (sched)->iterate)
    return CLASS (sched)->iterate (sched);

  return FALSE;
}


/**
 * gst_scheduler_show:
 * @sched: the scheduler
 *
 * Dump the state of the schedulerr
 */
void
gst_scheduler_show (GstScheduler *sched)
{
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  if (CLASS (sched)->show)
    CLASS (sched)->show (sched);
}

/*
 * Factory stuff starts here
 *
 */

static GList* _gst_schedulerfactories;

static void 		gst_scheduler_factory_class_init		(GstSchedulerFactoryClass *klass);
static void 		gst_scheduler_factory_init 		(GstSchedulerFactory *factory);

#ifndef GST_DISABLE_REGISTRY
static xmlNodePtr 	gst_scheduler_factory_save_thyself 	(GstObject *object, xmlNodePtr parent);
static void 		gst_scheduler_factory_restore_thyself 	(GstObject *object, xmlNodePtr parent);
#endif

static GstPluginFeatureClass *factory_parent_class = NULL;
/* static guint gst_scheduler_factory_signals[LAST_SIGNAL] = { 0 }; */

GType 
gst_scheduler_factory_get_type (void) 
{
  static GType schedulerfactory_type = 0;

  if (!schedulerfactory_type) {
    static const GTypeInfo schedulerfactory_info = {
      sizeof (GstSchedulerFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_scheduler_factory_class_init,
      NULL,
      NULL,
      sizeof(GstSchedulerFactory),
      0,
      (GInstanceInitFunc) gst_scheduler_factory_init,
      NULL
    };
    schedulerfactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
		    				  "GstSchedulerFactory", &schedulerfactory_info, 0);
  }
  return schedulerfactory_type;
}

static void
gst_scheduler_factory_class_init (GstSchedulerFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  factory_parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);

#ifndef GST_DISABLE_REGISTRY
  gstobject_class->save_thyself = 	GST_DEBUG_FUNCPTR (gst_scheduler_factory_save_thyself);
  gstobject_class->restore_thyself = 	GST_DEBUG_FUNCPTR (gst_scheduler_factory_restore_thyself);
#endif

  _gst_schedulerfactories = NULL;
  if (!_default_name)
    _default_name = g_strdup ("basic");
}

static void
gst_scheduler_factory_init (GstSchedulerFactory *factory)
{
  _gst_schedulerfactories = g_list_prepend (_gst_schedulerfactories, factory);
}
	

/**
 * gst_scheduler_factory_new:
 * @name: name of schedulerfactory to create
 * @longdesc: long description of schedulerfactory to create
 * @type: the gtk type of the GstScheduler element of this factory
 *
 * Create a new schedulerfactory with the given parameters
 *
 * Returns: a new #GstSchedulerFactory.
 */
GstSchedulerFactory*
gst_scheduler_factory_new (const gchar *name, const gchar *longdesc, GType type)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);
  factory = gst_scheduler_factory_find (name);
  if (!factory) {
    factory = GST_SCHEDULER_FACTORY (g_object_new (GST_TYPE_SCHEDULER_FACTORY, NULL));
  }

  gst_object_set_name (GST_OBJECT (factory), name);
  if (factory->longdesc)
    g_free (factory->longdesc);
  factory->longdesc = g_strdup (longdesc);
  factory->type = type;

  return factory;
}

/**
 * gst_scheduler_factory_destroy:
 * @factory: factory to destroy
 *
 * Removes the scheduler from the global list.
 */
void
gst_scheduler_factory_destroy (GstSchedulerFactory *factory)
{
  g_return_if_fail (factory != NULL);

  _gst_schedulerfactories = g_list_remove (_gst_schedulerfactories, factory);

  /* we don't free the struct bacause someone might  have a handle to it.. */
}

/**
 * gst_scheduler_factory_find:
 * @name: name of schedulerfactory to find
 *
 * Search for an schedulerfactory of the given name.
 *
 * Returns: #GstSchedulerFactory if found, NULL otherwise
 */
GstSchedulerFactory*
gst_scheduler_factory_find (const gchar *name)
{
  GList *walk;
  GstSchedulerFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  GST_DEBUG (0,"gstscheduler: find \"%s\"", name);

  walk = _gst_schedulerfactories;
  while (walk) {
    factory = (GstSchedulerFactory *)(walk->data);
    if (!strcmp (name, GST_OBJECT_NAME (factory)))
      return factory;
    walk = g_list_next (walk);
  }

  return NULL;
}

/**
 * gst_scheduler_factory_get_list:
 *
 * Get the global list of schedulerfactories.
 *
 * Returns: GList of type #GstSchedulerFactory
 */
GList*
gst_scheduler_factory_get_list (void)
{
  return _gst_schedulerfactories;
}

/**
 * gst_scheduler_factory_create:
 * @factory: the factory used to create the instance
 * @parent: the parent element of this scheduler
 *
 * Create a new #GstScheduler instance from the 
 * given schedulerfactory with the given parent.
 *
 * Returns: A new #GstScheduler instance.
 */
GstScheduler*
gst_scheduler_factory_create (GstSchedulerFactory *factory, GstElement *parent)
{
  GstScheduler *new = NULL;

  g_return_val_if_fail (factory != NULL, NULL);

  if (gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    g_return_val_if_fail (factory->type != 0, NULL);

    new = GST_SCHEDULER (g_object_new (factory->type, NULL));
    new->parent = parent;
  }

  return new;
}

/**
 * gst_scheduler_factory_make:
 * @name: the name of the factory used to create the instance
 * @parent: the parent element of this scheduler
 *
 * Create a new #GstScheduler instance from the 
 * schedulerfactory with the given name and parent.
 *
 * Returns: A new #GstScheduler instance.
 */
GstScheduler*
gst_scheduler_factory_make (const gchar *name, GstElement *parent)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);

  factory = gst_scheduler_factory_find (name);

  if (factory == NULL)
    return NULL;

  return gst_scheduler_factory_create (factory, parent);
}

/**
 * gst_scheduler_factory_set_default_name:
 * @name: the name of the factory used as a default
 *
 * Set the default schedulerfactory name.
 */
void
gst_scheduler_factory_set_default_name (const gchar* name)
{
  if (_default_name)
    g_free (_default_name);

  _default_name = g_strdup (name);
}

/**
 * gst_scheduler_factory_get_default_name:
 *
 * Get the default schedulerfactory name.
 *
 * Returns: the name of the default scheduler.
 */
const gchar*
gst_scheduler_factory_get_default_name (void)
{
  return _default_name;
}

#ifndef GST_DISABLE_REGISTRY
static xmlNodePtr
gst_scheduler_factory_save_thyself (GstObject *object, xmlNodePtr parent)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail (GST_IS_SCHEDULER_FACTORY (object), parent);

  factory = GST_SCHEDULER_FACTORY (object);

  if (GST_OBJECT_CLASS (factory_parent_class)->save_thyself) {
    GST_OBJECT_CLASS (factory_parent_class)->save_thyself (object, parent);
  }

  xmlNewChild (parent, NULL, "longdesc", factory->longdesc);

  return parent;
}

/**
 * gst_scheduler_factory_load_thyself:
 * @parent: the parent XML node pointer
 *
 * Load an schedulerfactory from the given XML parent node.
 *
 * Returns: A new factory based on the XML node.
 */
static void
gst_scheduler_factory_restore_thyself (GstObject *object, xmlNodePtr parent)
{
  GstSchedulerFactory *factory = GST_SCHEDULER_FACTORY (object);
  xmlNodePtr children = parent->xmlChildrenNode;

  if (GST_OBJECT_CLASS (factory_parent_class)->restore_thyself) {
    GST_OBJECT_CLASS (factory_parent_class)->restore_thyself (object, parent);
  }

  while (children) {
    if (!strcmp(children->name, "name")) {
      gst_object_set_name (GST_OBJECT (factory), xmlNodeGetContent (children));
    }
    if (!strcmp(children->name, "longdesc")) {
      factory->longdesc = xmlNodeGetContent (children);
    }
    children = children->next;
  }
}
#endif /* GST_DISABLE_REGISTRY */
