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
#include "gstplugin.h"

GList* _gst_autoplugfactories;

enum {
  NEW_OBJECT,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void     gst_autoplug_class_init (GstAutoplugClass *klass);
static void     gst_autoplug_init       (GstAutoplug *autoplug);

static GstObjectClass *parent_class = NULL;
static guint gst_autoplug_signals[LAST_SIGNAL] = { 0 };

GtkType gst_autoplug_get_type(void)
{
  static GtkType autoplug_type = 0;

  if (!autoplug_type) {
    static const GtkTypeInfo autoplug_info = {
      "GstAutoplug",
      sizeof(GstAutoplug),
      sizeof(GstAutoplugClass),
      (GtkClassInitFunc)gst_autoplug_class_init,
      (GtkObjectInitFunc)gst_autoplug_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    autoplug_type = gtk_type_unique (GST_TYPE_OBJECT, &autoplug_info);
  }
  return autoplug_type;
}

static void
gst_autoplug_class_init(GstAutoplugClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstObjectClass *gstobject_class;

  gtkobject_class = (GtkObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  parent_class = gtk_type_class(GST_TYPE_OBJECT);

  gst_autoplug_signals[NEW_OBJECT] =
    gtk_signal_new ("new_object", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstAutoplugClass, new_object),
                    gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
                    GST_TYPE_OBJECT);

  gtk_object_class_add_signals (gtkobject_class, gst_autoplug_signals, LAST_SIGNAL);
}

static void gst_autoplug_init(GstAutoplug *autoplug)
{
}

void
_gst_autoplug_initialize (void)
{
  _gst_autoplugfactories = NULL;
}

void
gst_autoplug_signal_new_object (GstAutoplug *autoplug, GstObject *object)
{
  gtk_signal_emit (GTK_OBJECT (autoplug), gst_autoplug_signals[NEW_OBJECT], object);
}


GstElement*
gst_autoplug_to_caps (GstAutoplug *autoplug, GstCaps *srccaps, GstCaps *sinkcaps, ...)
{
  GstAutoplugClass *oclass;
  GstElement *element = NULL;
  va_list args;

  va_start (args, sinkcaps);

  oclass = GST_AUTOPLUG_CLASS (GTK_OBJECT (autoplug)->klass);
  if (oclass->autoplug_to_caps)
    element = (oclass->autoplug_to_caps) (autoplug, srccaps, sinkcaps, args);

  va_end (args);

  return element;
}

GstElement*
gst_autoplug_to_renderers (GstAutoplug *autoplug, GstCaps *srccaps, GstElement *target, ...)
{
  GstAutoplugClass *oclass;
  GstElement *element = NULL;
  va_list args;

  va_start (args, target);

  oclass = GST_AUTOPLUG_CLASS (GTK_OBJECT (autoplug)->klass);
  if (oclass->autoplug_to_renderers)
    element = (oclass->autoplug_to_renderers) (autoplug, srccaps, target, args);

  va_end (args);

  return element;
}


/**
 * gst_autoplugfactory_new:
 * @name: name of autoplugfactory to create
 * @longdesc: long description of autoplugfactory to create
 * @type: the gtk type of the GstAutoplug element of this factory
 *
 * Create a new autoplugfactory with the given parameters
 *
 * Returns: a new #GstAutoplugFactory.
 */
GstAutoplugFactory*
gst_autoplugfactory_new (const gchar *name, const gchar *longdesc, GtkType type)
{
  GstAutoplugFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  factory = g_new0(GstAutoplugFactory, 1);

  factory->name = g_strdup(name);
  factory->longdesc = g_strdup (longdesc);
  factory->type = type;

  _gst_autoplugfactories = g_list_prepend (_gst_autoplugfactories, factory);

  return factory;
}

/**
 * gst_autoplugfactory_destroy:
 * @autoplug: factory to destroy
 *
 * Removes the autoplug from the global list.
 */
void
gst_autoplugfactory_destroy (GstAutoplugFactory *autoplug)
{
  g_return_if_fail (autoplug != NULL);

  _gst_autoplugfactories = g_list_remove (_gst_autoplugfactories, autoplug);

  // we don't free the struct bacause someone might  have a handle to it..
}

/**
 * gst_autoplug_find:
 * @name: name of autoplugger to find
 *
 * Search for an autoplugger of the given name.
 *
 * Returns: #GstAutoplug if found, NULL otherwise
 */
GstAutoplugFactory*
gst_autoplugfactory_find (const gchar *name)
{
  GList *walk;
  GstAutoplugFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  GST_DEBUG (0,"gstautoplug: find \"%s\"\n", name);

  walk = _gst_autoplugfactories;
  while (walk) {
    factory = (GstAutoplugFactory *)(walk->data);
    if (!strcmp (name, factory->name))
      return factory;
    walk = g_list_next (walk);
  }

  return NULL;
}

/**
 * gst_autoplugfactory_get_list:
 *
 * Get the global list of elementfactories.
 *
 * Returns: GList of type #GstElementFactory
 */
GList*
gst_autoplugfactory_get_list (void)
{
  return _gst_autoplugfactories;
}

GstAutoplug*
gst_autoplugfactory_create (GstAutoplugFactory *factory)
{
  GstAutoplug *new = NULL;

  g_return_val_if_fail (factory != NULL, NULL);

  if (factory->type == 0){
    factory = gst_plugin_load_autoplugfactory (factory->name);
  }
  g_return_val_if_fail (factory != NULL, NULL);
  g_return_val_if_fail (factory->type != 0, NULL);

  new = GST_AUTOPLUG (gtk_type_new (factory->type));

  return new;
}

GstAutoplug*
gst_autoplugfactory_make (const gchar *name)
{
  GstAutoplugFactory *factory;

  g_return_val_if_fail (name != NULL, NULL);

  factory = gst_autoplugfactory_find (name);

  if (factory == NULL)
    return NULL;

  return gst_autoplugfactory_create (factory);;
}

xmlNodePtr
gst_autoplugfactory_save_thyself (GstAutoplugFactory *factory, xmlNodePtr parent)
{
  g_return_val_if_fail(factory != NULL, NULL);

  xmlNewChild(parent,NULL,"name",factory->name);
  xmlNewChild(parent,NULL,"longdesc", factory->longdesc);

  return parent;
}

GstAutoplugFactory*
gst_autoplugfactory_load_thyself (xmlNodePtr parent)
{
  GstAutoplugFactory *factory = g_new0(GstAutoplugFactory, 1);
  xmlNodePtr children = parent->xmlChildrenNode;

  while (children) {
    if (!strcmp(children->name, "name")) {
      factory->name = xmlNodeGetContent(children);
    }
    if (!strcmp(children->name, "longdesc")) {
      factory->longdesc = xmlNodeGetContent(children);
    }
    children = children->next;
  }

  _gst_autoplugfactories = g_list_prepend (_gst_autoplugfactories, factory);

  return factory;
}

