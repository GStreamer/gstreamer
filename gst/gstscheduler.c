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

#include "gst_private.h"

#include "gstsystemclock.h"
#include "gstscheduler.h"
#include "gstinfo.h"
#include "gstregistrypool.h"

static void gst_scheduler_class_init (GstSchedulerClass * klass);
static void gst_scheduler_init (GstScheduler * sched);
static void gst_scheduler_dispose (GObject * object);

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

    _gst_scheduler_type =
	g_type_register_static (GST_TYPE_OBJECT, "GstScheduler",
	&scheduler_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_scheduler_type;
}

static void
gst_scheduler_class_init (GstSchedulerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_scheduler_dispose);
}

static void
gst_scheduler_init (GstScheduler * sched)
{
  sched->clock_providers = NULL;
  sched->clock_receivers = NULL;
  sched->schedulers = NULL;
  sched->state = GST_SCHEDULER_STATE_NONE;
  sched->parent = NULL;
  sched->parent_sched = NULL;
  sched->clock = NULL;
}

static void
gst_scheduler_dispose (GObject * object)
{
  GstScheduler *sched = GST_SCHEDULER (object);

  /* thse lists should all be NULL */
  GST_DEBUG ("scheduler %p dispose %p %p %p",
      object,
      sched->clock_providers, sched->clock_receivers, sched->schedulers);

  gst_object_replace ((GstObject **) & sched->current_clock, NULL);
  gst_object_replace ((GstObject **) & sched->clock, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * gst_scheduler_setup:
 * @sched: the scheduler
 *
 * Prepare the scheduler.
 */
void
gst_scheduler_setup (GstScheduler * sched)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->setup)
    sclass->setup (sched);
}

/**
 * gst_scheduler_reset:
 * @sched: a #GstScheduler to reset.
 *
 * Reset the schedulers.
 */
void
gst_scheduler_reset (GstScheduler * sched)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->reset)
    sclass->reset (sched);
}

/**
 * gst_scheduler_pad_link:
 * @sched: the scheduler
 * @srcpad: the srcpad to link
 * @sinkpad: the sinkpad to link to
 *
 * Links the srcpad to the given sinkpad.
 */
void
gst_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad, GstPad * sinkpad)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->pad_link)
    sclass->pad_link (sched, srcpad, sinkpad);
}

/**
 * gst_scheduler_pad_unlink:
 * @sched: the scheduler
 * @srcpad: the srcpad to unlink
 * @sinkpad: the sinkpad to unlink from
 *
 * Unlinks the srcpad from the given sinkpad.
 */
void
gst_scheduler_pad_unlink (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->pad_unlink)
    sclass->pad_unlink (sched, srcpad, sinkpad);
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
gst_scheduler_pad_select (GstScheduler * sched, GList * padlist)
{
  GstSchedulerClass *sclass;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), NULL);
  g_return_val_if_fail (padlist != NULL, NULL);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->pad_select)
    sclass->pad_select (sched, padlist);

  return NULL;
}

/**
 * gst_scheduler_add_element:
 * @sched: the scheduler
 * @element: the element to add to the scheduler
 *
 * Add an element to the scheduler.
 */
void
gst_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  /* if it's already in this scheduler, don't bother doing anything */
  if (GST_ELEMENT_SCHED (element) == sched) {
    GST_CAT_DEBUG (GST_CAT_SCHEDULING, "element %s already in scheduler %p",
	GST_ELEMENT_NAME (element), sched);
    return;
  }

  /* if it's not inside this scheduler, it has to be NULL */
  g_assert (GST_ELEMENT_SCHED (element) == NULL);

  if (gst_element_provides_clock (element)) {
    sched->clock_providers = g_list_prepend (sched->clock_providers, element);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "added clock provider %s",
	GST_ELEMENT_NAME (element));
  }
  if (gst_element_requires_clock (element)) {
    sched->clock_receivers = g_list_prepend (sched->clock_receivers, element);
    GST_CAT_DEBUG (GST_CAT_CLOCK, "added clock receiver %s",
	GST_ELEMENT_NAME (element));
  }

  gst_element_set_scheduler (element, sched);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->add_element)
    sclass->add_element (sched, element);
}

/**
 * gst_scheduler_remove_element:
 * @sched: the scheduler
 * @element: the element to remove
 *
 * Remove an element from the scheduler.
 */
void
gst_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sched->clock_providers = g_list_remove (sched->clock_providers, element);
  sched->clock_receivers = g_list_remove (sched->clock_receivers, element);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->remove_element)
    sclass->remove_element (sched, element);

  gst_element_set_scheduler (element, NULL);
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
gst_scheduler_state_transition (GstScheduler * sched, GstElement * element,
    gint transition)
{
  GstSchedulerClass *sclass;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), GST_STATE_FAILURE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_STATE_FAILURE);

  if (element == sched->parent && sched->parent_sched == NULL) {
    switch (transition) {
      case GST_STATE_READY_TO_PAUSED:
      {
	GstClock *clock = gst_scheduler_get_clock (sched);

	GST_CAT_DEBUG (GST_CAT_CLOCK,
	    "scheduler READY to PAUSED clock is %p (%s)", clock,
	    (clock ? GST_OBJECT_NAME (clock) : "nil"));

	gst_scheduler_set_clock (sched, clock);
	break;
      }
    }
  }

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->state_transition)
    return sclass->state_transition (sched, element, transition);

  return GST_STATE_SUCCESS;
}

/**
 * gst_scheduler_scheduling_change:
 * @sched: the scheduler
 * @element: the element that changed its scheduling strategy
 *
 * Tell the scheduler that an element changed its scheduling strategy.
 * An element could, for example, change its loop function or changes
 * from a loop based element to a chain based element.
 */
void
gst_scheduler_scheduling_change (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->scheduling_change)
    sclass->scheduling_change (sched, element);
}

/**
 * gst_scheduler_add_scheduler:
 * @sched: a  #GstScheduler to add to
 * @sched2: the #GstScheduler to add
 *
 * Notifies the scheduler that it has to monitor this scheduler.
 */
void
gst_scheduler_add_scheduler (GstScheduler * sched, GstScheduler * sched2)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_SCHEDULER (sched2));
  g_return_if_fail (sched2->parent_sched == NULL);

  GST_DEBUG ("gstscheduler: %p add scheduler %p", sched, sched2);

  gst_object_ref (GST_OBJECT (sched2));
  gst_object_ref (GST_OBJECT (sched));

  sched->schedulers = g_list_prepend (sched->schedulers, sched2);
  sched2->parent_sched = sched;

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->add_scheduler)
    sclass->add_scheduler (sched, sched2);
}

/**
 * gst_scheduler_remove_scheduler:
 * @sched: the scheduler
 * @sched2: the scheduler to remove
 *
 a Notifies the scheduler that it can stop monitoring this scheduler.
 */
void
gst_scheduler_remove_scheduler (GstScheduler * sched, GstScheduler * sched2)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_SCHEDULER (sched2));
  g_return_if_fail (sched2->parent_sched == sched);

  GST_DEBUG ("gstscheduler: %p remove scheduler %p", sched, sched2);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->remove_scheduler)
    sclass->remove_scheduler (sched, sched2);

  sched->schedulers = g_list_remove (sched->schedulers, sched2);
  sched2->parent_sched = NULL;

  gst_object_unref (GST_OBJECT (sched2));
  gst_object_unref (GST_OBJECT (sched));
}

/**
 * gst_scheduler_lock_element:
 * @sched: the scheduler
 * @element: the element to lock
 *
 * Acquire a lock on the given element in the given scheduler.
 */
void
gst_scheduler_lock_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->lock_element)
    sclass->lock_element (sched, element);
}

/**
 * gst_scheduler_unlock_element:
 * @sched: the scheduler
 * @element: the element to unlock
 *
 * Release the lock on the given element in the given scheduler.
 */
void
gst_scheduler_unlock_element (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->unlock_element)
    sclass->unlock_element (sched, element);
}

/**
 * gst_scheduler_error:
 * @sched: the scheduler
 * @element: the element with the error
 *
 * Tell the scheduler an element was in error
 */
void
gst_scheduler_error (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->error)
    sclass->error (sched, element);
}

/**
 * gst_scheduler_yield:
 * @sched: the scheduler
 * @element: the element requesting a yield
 *
 * Tell the scheduler to schedule another element.
 *
 * Returns: TRUE if the element should save its state, FALSE
 * if the scheduler can perform this action itself.
 */
gboolean
gst_scheduler_yield (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), TRUE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), TRUE);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->yield)
    return sclass->yield (sched, element);

  return TRUE;
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
gst_scheduler_interrupt (GstScheduler * sched, GstElement * element)
{
  GstSchedulerClass *sclass;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->interrupt)
    return sclass->interrupt (sched, element);

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
GstClock *
gst_scheduler_get_clock (GstScheduler * sched)
{
  GstClock *clock = NULL;

  /* if we have a fixed clock, use that one */
  if (GST_FLAG_IS_SET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK)) {
    clock = sched->clock;

    GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler using fixed clock %p (%s)", clock,
	(clock ? GST_OBJECT_NAME (clock) : "nil"));
  } else {
    GList *schedulers = sched->schedulers;
    GList *providers = sched->clock_providers;

    /* try to get a clock from one of the schedulers we manage first */
    while (schedulers) {
      GstScheduler *scheduler = GST_SCHEDULER (schedulers->data);

      clock = gst_scheduler_get_clock (scheduler);
      if (clock)
	break;

      schedulers = g_list_next (schedulers);
    }
    /* still no clock, try to find one in the providers */
    while (!clock && providers) {
      clock = gst_element_get_clock (GST_ELEMENT (providers->data));

      providers = g_list_next (providers);
    }
    /* still no clock, use a system clock */
    if (!clock && sched->parent_sched == NULL) {
      clock = gst_system_clock_obtain ();
    }
  }
  GST_CAT_LOG_OBJECT (GST_CAT_CLOCK, sched, "scheduler selected clock %p (%s)",
      clock, clock ? GST_STR_NULL (GST_OBJECT_NAME (clock)) : "-");

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
gst_scheduler_use_clock (GstScheduler * sched, GstClock * clock)
{
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_FLAG_SET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & sched->clock, (GstObject *) clock);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler using fixed clock %p (%s)", clock,
      (clock ? GST_OBJECT_NAME (clock) : "nil"));
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
gst_scheduler_set_clock (GstScheduler * sched, GstClock * clock)
{
  GList *receivers;
  GList *schedulers;

  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  receivers = sched->clock_receivers;
  schedulers = sched->schedulers;

  gst_object_replace ((GstObject **) & sched->current_clock,
      (GstObject *) clock);

  while (receivers) {
    GstElement *element = GST_ELEMENT (receivers->data);

    GST_CAT_DEBUG (GST_CAT_CLOCK,
	"scheduler setting clock %p (%s) on element %s", clock,
	(clock ? GST_OBJECT_NAME (clock) : "nil"), GST_ELEMENT_NAME (element));

    gst_element_set_clock (element, clock);
    receivers = g_list_next (receivers);
  }
  while (schedulers) {
    GstScheduler *scheduler = GST_SCHEDULER (schedulers->data);

    GST_CAT_DEBUG (GST_CAT_CLOCK,
	"scheduler setting clock %p (%s) on scheduler %p", clock,
	(clock ? GST_OBJECT_NAME (clock) : "nil"), scheduler);
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
gst_scheduler_auto_clock (GstScheduler * sched)
{
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_FLAG_UNSET (sched, GST_SCHEDULER_FLAG_FIXED_CLOCK);

  gst_object_replace ((GstObject **) & sched->clock, NULL);

  GST_CAT_DEBUG (GST_CAT_CLOCK, "scheduler using automatic clock");
}

GstClockReturn gst_clock_id_wait (GstClockID id, GstClockTimeDiff * jitter);

/**
 * gst_scheduler_clock_wait:
 * @sched: the scheduler
 * @element: the element that wants to wait
 * @id: the clockid to use
 * @jitter: the time difference between requested time and actual time
 *
 * Wait till the clock reaches a specific time. The ClockID can
 * be obtained from #gst_clock_new_single_shot_id. 
 *
 * Returns: the status of the operation
 */
GstClockReturn
gst_scheduler_clock_wait (GstScheduler * sched, GstElement * element,
    GstClockID id, GstClockTimeDiff * jitter)
{
  GstSchedulerClass *sclass;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), GST_CLOCK_ERROR);
  g_return_val_if_fail (id != NULL, GST_CLOCK_ERROR);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->clock_wait)
    return sclass->clock_wait (sched, element, id, jitter);
  else
    return gst_clock_id_wait (id, jitter);
}

/**
 * gst_scheduler_iterate:
 * @sched: the scheduler
 *
 * Perform one iteration on the scheduler.
 *
 * Returns: a boolean indicating something usefull has happened.
 */
gboolean
gst_scheduler_iterate (GstScheduler * sched)
{
  GstSchedulerClass *sclass;
  gboolean res = FALSE;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), FALSE);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->iterate) {
    res = sclass->iterate (sched);
  }

  return res;
}


/**
 * gst_scheduler_show:
 * @sched: the scheduler
 *
 * Dump the state of the scheduler
 */
void
gst_scheduler_show (GstScheduler * sched)
{
  GstSchedulerClass *sclass;

  g_return_if_fail (GST_IS_SCHEDULER (sched));

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->show)
    sclass->show (sched);
}

/*
 * Factory stuff starts here
 *
 */
static void gst_scheduler_factory_class_init (GstSchedulerFactoryClass * klass);
static void gst_scheduler_factory_init (GstSchedulerFactory * factory);

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
      sizeof (GstSchedulerFactory),
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
gst_scheduler_factory_class_init (GstSchedulerFactoryClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstpluginfeature_class = (GstPluginFeatureClass *) klass;

  factory_parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);

  if (!_default_name) {
    if (g_getenv ("GST_SCHEDULER")) {
      _default_name = g_strdup (g_getenv ("GST_SCHEDULER"));
    } else {
      _default_name = g_strdup (GST_SCHEDULER_DEFAULT_NAME);
    }
  }
  g_assert (_default_name);
}

static void
gst_scheduler_factory_init (GstSchedulerFactory * factory)
{
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
GstSchedulerFactory *
gst_scheduler_factory_new (const gchar * name, const gchar * longdesc,
    GType type)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);

  factory = gst_scheduler_factory_find (name);

  if (!factory) {
    factory =
	GST_SCHEDULER_FACTORY (g_object_new (GST_TYPE_SCHEDULER_FACTORY, NULL));
    GST_PLUGIN_FEATURE_NAME (factory) = g_strdup (name);
  } else {
    g_free (factory->longdesc);
  }

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
gst_scheduler_factory_destroy (GstSchedulerFactory * factory)
{
  g_return_if_fail (factory != NULL);

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
GstSchedulerFactory *
gst_scheduler_factory_find (const gchar * name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  GST_DEBUG ("gstscheduler: find \"%s\"", name);

  feature = gst_registry_pool_find_feature (name, GST_TYPE_SCHEDULER_FACTORY);

  if (feature)
    return GST_SCHEDULER_FACTORY (feature);

  return NULL;
}

/**
 * gst_scheduler_factory_create:
 * @factory: the factory used to create the instance
 * @parent: the parent element of this scheduler
 *
 * Create a new #GstScheduler instance from the 
 * given schedulerfactory with the given parent. @parent will
 * have its scheduler set to the returned #GstScheduler instance.
 *
 * Returns: A new #GstScheduler instance with a reference count of %1.
 */
GstScheduler *
gst_scheduler_factory_create (GstSchedulerFactory * factory,
    GstElement * parent)
{
  GstScheduler *sched = NULL;

  g_return_val_if_fail (factory != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (parent), NULL);

  if (gst_plugin_feature_ensure_loaded (GST_PLUGIN_FEATURE (factory))) {
    g_return_val_if_fail (factory->type != 0, NULL);

    sched = GST_SCHEDULER (g_object_new (factory->type, NULL));
    sched->parent = parent;

    GST_ELEMENT_SCHED (parent) = sched;

    /* let's refcount the scheduler */
    gst_object_ref (GST_OBJECT (sched));
    gst_object_sink (GST_OBJECT (sched));
  }

  return sched;
}

/**
 * gst_scheduler_factory_make:
 * @name: the name of the factory used to create the instance
 * @parent: the parent element of this scheduler
 *
 * Create a new #GstScheduler instance from the 
 * schedulerfactory with the given name and parent. @parent will
 * have its scheduler set to the returned #GstScheduler instance.
 * If %NULL is passed as @name, the default scheduler name will
 * be used.
 *
 * Returns: A new #GstScheduler instance with a reference count of %1.
 */
GstScheduler *
gst_scheduler_factory_make (const gchar * name, GstElement * parent)
{
  GstSchedulerFactory *factory;
  const gchar *default_name = gst_scheduler_factory_get_default_name ();

  if (name)
    factory = gst_scheduler_factory_find (name);
  else {
    /* FIXME: do better error handling */
    if (default_name == NULL)
      g_error ("No default scheduler name - do you have a registry ?");
    factory = gst_scheduler_factory_find (default_name);
  }

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
gst_scheduler_factory_set_default_name (const gchar * name)
{
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
const gchar *
gst_scheduler_factory_get_default_name (void)
{
  return _default_name;
}
