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
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (padlist != NULL);

  if (CLASS (sched)->pad_select)
    CLASS (sched)->pad_select (sched, padlist);
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

  if (CLASS (sched)->add_element)
    CLASS (sched)->add_element (sched, element);
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

  if (CLASS (sched)->state_transition)
    return CLASS (sched)->state_transition (sched, element, transition);

  return GST_STATE_SUCCESS;
}

/**
 * gst_scheduler_remove_element:
 * @sched: the schedulerr
 * @element: the element to remov
 *
 * Remove an element from the schedulerr.
 */
void
gst_scheduler_remove_element (GstScheduler *sched, GstElement *element)
{
  GList *l;
  
  g_return_if_fail (GST_IS_SCHEDULER (sched));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (CLASS (sched)->remove_element)
    CLASS (sched)->remove_element (sched, element);
  
  for (l=element->pads; l; l=l->next)
    gst_pad_unset_sched ((GstPad*) l->data);
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
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  if (CLASS (sched)->iterate)
    CLASS (sched)->iterate (sched);
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

static void 		gst_schedulerfactory_class_init		(GstSchedulerFactoryClass *klass);
static void 		gst_schedulerfactory_init 		(GstSchedulerFactory *factory);

#ifndef GST_DISABLE_REGISTRY
static xmlNodePtr 	gst_schedulerfactory_save_thyself 	(GstObject *object, xmlNodePtr parent);
static void 		gst_schedulerfactory_restore_thyself 	(GstObject *object, xmlNodePtr parent);
#endif

static GstPluginFeatureClass *factory_parent_class = NULL;
/* static guint gst_schedulerfactory_signals[LAST_SIGNAL] = { 0 }; */

GType 
gst_schedulerfactory_get_type (void) 
{
  static GType schedulerfactory_type = 0;

  if (!schedulerfactory_type) {
    static const GTypeInfo schedulerfactory_info = {
      sizeof (GstSchedulerFactoryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_schedulerfactory_class_init,
      NULL,
      NULL,
      sizeof(GstSchedulerFactory),
      0,
      (GInstanceInitFunc) gst_schedulerfactory_init,
      NULL
    };
    schedulerfactory_type = g_type_register_static (GST_TYPE_PLUGIN_FEATURE, 
		    				  "GstSchedulerFactory", &schedulerfactory_info, 0);
  }
  return schedulerfactory_type;
}

static void
gst_schedulerfactory_class_init (GstSchedulerFactoryClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstPluginFeatureClass *gstpluginfeature_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstpluginfeature_class = (GstPluginFeatureClass*) klass;

  factory_parent_class = g_type_class_ref (GST_TYPE_PLUGIN_FEATURE);

#ifndef GST_DISABLE_REGISTRY
  gstobject_class->save_thyself = 	GST_DEBUG_FUNCPTR (gst_schedulerfactory_save_thyself);
  gstobject_class->restore_thyself = 	GST_DEBUG_FUNCPTR (gst_schedulerfactory_restore_thyself);
#endif

  _gst_schedulerfactories = NULL;
  if (!_default_name)
    _default_name = g_strdup ("basic");
}

static void
gst_schedulerfactory_init (GstSchedulerFactory *factory)
{
  _gst_schedulerfactories = g_list_prepend (_gst_schedulerfactories, factory);
}
	

/**
 * gst_schedulerfactory_new:
 * @name: name of schedulerfactory to create
 * @longdesc: long description of schedulerfactory to create
 * @type: the gtk type of the GstScheduler element of this factory
 *
 * Create a new schedulerfactory with the given parameters
 *
 * Returns: a new #GstSchedulerFactory.
 */
GstSchedulerFactory*
gst_schedulerfactory_new (const gchar *name, const gchar *longdesc, GType type)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);
  factory = gst_schedulerfactory_find (name);
  if (!factory) {
    factory = GST_SCHEDULERFACTORY (g_object_new (GST_TYPE_SCHEDULERFACTORY, NULL));
  }

  gst_object_set_name (GST_OBJECT (factory), name);
  if (factory->longdesc)
    g_free (factory->longdesc);
  factory->longdesc = g_strdup (longdesc);
  factory->type = type;

  return factory;
}

/**
 * gst_schedulerfactory_destroy:
 * @factory: factory to destroy
 *
 * Removes the scheduler from the global list.
 */
void
gst_schedulerfactory_destroy (GstSchedulerFactory *factory)
{
  g_return_if_fail (factory != NULL);

  _gst_schedulerfactories = g_list_remove (_gst_schedulerfactories, factory);

  /* we don't free the struct bacause someone might  have a handle to it.. */
}

/**
 * gst_schedulerfactory_find:
 * @name: name of schedulerfactory to find
 *
 * Search for an schedulerfactory of the given name.
 *
 * Returns: #GstSchedulerFactory if found, NULL otherwise
 */
GstSchedulerFactory*
gst_schedulerfactory_find (const gchar *name)
{
  GList *walk;
  GstSchedulerFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  GST_DEBUG (0,"gstscheduler: find \"%s\"\n", name);

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
 * gst_schedulerfactory_get_list:
 *
 * Get the global list of schedulerfactories.
 *
 * Returns: GList of type #GstSchedulerFactory
 */
GList*
gst_schedulerfactory_get_list (void)
{
  return _gst_schedulerfactories;
}

/**
 * gst_schedulerfactory_create:
 * @factory: the factory used to create the instance
 * @parent: the parent element of this scheduler
 *
 * Create a new #GstScheduler instance from the 
 * given schedulerfactory with the given parent.
 *
 * Returns: A new #GstScheduler instance.
 */
GstScheduler*
gst_schedulerfactory_create (GstSchedulerFactory *factory, GstElement *parent)
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
 * gst_schedulerfactory_make:
 * @name: the name of the factory used to create the instance
 * @parent: the parent element of this scheduler
 *
 * Create a new #GstScheduler instance from the 
 * schedulerfactory with the given name and parent.
 *
 * Returns: A new #GstScheduler instance.
 */
GstScheduler*
gst_schedulerfactory_make (const gchar *name, GstElement *parent)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);

  factory = gst_schedulerfactory_find (name);

  if (factory == NULL)
    return NULL;

  return gst_schedulerfactory_create (factory, parent);
}

/**
 * gst_schedulerfactory_set_default_name:
 * @name: the name of the factory used as a default
 *
 * Set the default schedulerfactory name.
 */
void
gst_schedulerfactory_set_default_name (const gchar* name)
{
  if (_default_name)
    g_free (_default_name);

  _default_name = g_strdup (name);
}

/**
 * gst_schedulerfactory_get_default_name:
 *
 * Get the default schedulerfactory name.
 *
 * Returns: the name of the default scheduler.
 */
const gchar*
gst_schedulerfactory_get_default_name (void)
{
  return _default_name;
}

#ifndef GST_DISABLE_REGISTRY
static xmlNodePtr
gst_schedulerfactory_save_thyself (GstObject *object, xmlNodePtr parent)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail (GST_IS_SCHEDULERFACTORY (object), parent);

  factory = GST_SCHEDULERFACTORY (object);

  if (GST_OBJECT_CLASS (factory_parent_class)->save_thyself) {
    GST_OBJECT_CLASS (factory_parent_class)->save_thyself (object, parent);
  }

  xmlNewChild (parent, NULL, "longdesc", factory->longdesc);

  return parent;
}

/**
 * gst_schedulerfactory_load_thyself:
 * @parent: the parent XML node pointer
 *
 * Load an schedulerfactory from the given XML parent node.
 *
 * Returns: A new factory based on the XML node.
 */
static void
gst_schedulerfactory_restore_thyself (GstObject *object, xmlNodePtr parent)
{
  GstSchedulerFactory *factory = GST_SCHEDULERFACTORY (object);
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
