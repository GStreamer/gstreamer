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
  sched->parent = NULL;
}

static void
gst_scheduler_dispose (GObject * object)
{
  GstScheduler *sched = GST_SCHEDULER (object);

  G_OBJECT_CLASS (parent_class)->dispose (G_OBJECT (sched));
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

GstTask *
gst_scheduler_create_task (GstScheduler * sched, GstTaskFunction func,
    gpointer data)
{
  GstSchedulerClass *sclass;
  GstTask *result = NULL;

  g_return_val_if_fail (GST_IS_SCHEDULER (sched), result);

  sclass = GST_SCHEDULER_GET_CLASS (sched);

  if (sclass->create_task)
    result = sclass->create_task (sched, func, data);

  return result;
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
 * gst_scheduler_register:
 * @plugin: a #GstPlugin
 * @name: name of the scheduler to register
 * @longdesc: description of the scheduler
 * @type: #GType of the scheduler to register
 *
 * Registers a scheduler with GStreamer.
 *
 * Returns: TRUE, if the registering succeeded, FALSE on error.
 *
 * Since: 0.8.5
 **/
gboolean
gst_scheduler_register (GstPlugin * plugin, const gchar * name,
    const gchar * longdesc, GType type)
{
  GstSchedulerFactory *factory;

  g_return_val_if_fail (plugin != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (longdesc != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (type, GST_TYPE_SCHEDULER), FALSE);

  factory = gst_scheduler_factory_find (name);
  if (factory) {
    g_return_val_if_fail (factory->type == 0, FALSE);
    g_free (factory->longdesc);
    factory->longdesc = g_strdup (longdesc);
    factory->type = type;
  } else {
    factory = gst_scheduler_factory_new (name, longdesc, type);
    g_return_val_if_fail (factory, FALSE);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }

  return TRUE;
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

    /* let's refcount the scheduler */
    gst_object_ref (sched);
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
