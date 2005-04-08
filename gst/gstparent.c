/* GStreamer
 * Copyright (C) 2005 Stefan Kost <ensonic@users.sf.net>
 *
 * gstparent.c: interface for multi child elements
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

#include "gstparent.h"

/* Parent signals */
/*
enum
{
  CHILD_ADDED,
  CHILD_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
*/

/**
 * gst_parent_get_child_by_name:
 * @parent: the parent object to get the child from
 * @name: the childs name
 *
 * Looks up a child element by the given name.
 *
 * Implementors can e.g. use #GstObject together with gst_object_set_name() or
 * GObject with g_object_set_data() to identify objects.
 *
 * Returns: the child object or %NULL if not found
 */
GObject *
gst_parent_get_child_by_name (GstParent * parent, const gchar * name)
{
  return (GST_PARENT_GET_CLASS (parent)->get_child_by_name (parent, name));
}

/**
 * gst_parent_get_child_by_index:
 * @parent: the parent object to get the child from
 * @index: the childs position in the child list
 *
 * Fetches a child by its number.
 *
 * Returns: the child object or %NULL if not found (index too high)
 */
GObject *
gst_parent_get_child_by_index (GstParent * parent, guint index)
{
  return (GST_PARENT_GET_CLASS (parent)->get_child_by_index (parent, index));
}

/**
 * gst_parent_get_children_count:
 * @parent: the parent object
 *
 * Gets the number of child objects this parent contains.
 *
 * Returns: the number of child objects
 */
guint
gst_parent_get_children_count (GstParent * parent)
{
  return (GST_PARENT_GET_CLASS (parent)->get_children_count (parent));
}

/**
 * gst_parent_get_valist:
 * @parent: the parent object
 * @first_property_name: name of the first property to get
 * @var_args: return location for the first property, followed optionally by more name/return location pairs, followed by NULL
 * Gets properties of the parents child objects.
 */
void
gst_parent_get_valist (GstParent * parent, const gchar * first_property_name,
    va_list var_args)
{
  GObject *child;
  const gchar *name;
  gchar *child_name, *prop_name;

  g_return_if_fail (G_IS_OBJECT (parent));

  g_object_ref (parent);

  name = first_property_name;

  // iterate of pairs
  while (name) {
    // split on '::' into child_name, prop_name
    prop_name = strstr (name, "::");
    if (prop_name) {
      child_name = g_strndup (name, ((gulong) prop_name - (gulong) name));
      prop_name += 2;
      // get the child by name
      child = gst_parent_get_child_by_name (parent, child_name);
      g_object_get (child, prop_name, var_args, NULL);
    } else {
      GST_WARNING ("property name '%s' has no '::' separator", name);
      break;
    }
    name = va_arg (var_args, gchar *);
  }

  g_object_unref (parent);
}

/**
 * gst_parent_get:
 * @parent: the parent object
 * @first_property_name: name of the first property to get
 * @...: return location for the first property, followed optionally by more name/return location pairs, followed by NULL
 * Gets properties of the parents child objects.
 */
void
gst_parent_get (GstParent * parent, const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (G_IS_OBJECT (parent));

  va_start (var_args, first_property_name);
  gst_parent_get_valist (parent, first_property_name, var_args);
  va_end (var_args);
}

/**
 * gst_parent_set_valist:
 * @parent: the parent object
 * @first_property_name: name of the first property to set
 * @var_args: value for the first property, followed optionally by more name/value pairs, followed by NULL
 * Sets properties of the parents child objects.
 */
void
gst_parent_set_valist (GstParent * parent, const gchar * first_property_name,
    va_list var_args)
{
  GObject *child;
  const gchar *name;
  gchar *child_name, *prop_name;

  g_return_if_fail (G_IS_OBJECT (parent));

  g_object_ref (parent);

  name = first_property_name;

  // iterate of pairs
  while (name) {
    // split on '::' into child_name, prop_name
    prop_name = strstr (name, "::");
    if (prop_name) {
      child_name = g_strndup (name, ((gulong) prop_name - (gulong) name));
      prop_name += 2;
      // get the child by name
      child = gst_parent_get_child_by_name (parent, child_name);
      g_object_set (child, prop_name, var_args, NULL);
    } else {
      GST_WARNING ("property name '%s' has no '::' separator", name);
      break;
    }
    name = va_arg (var_args, gchar *);
  }

  g_object_unref (parent);
}

/**
 * gst_parent_set:
 * @parent: the parent object
 * @first_property_name: name of the first property to set
 * @...: value for the first property, followed optionally by more name/value pairs, followed by NULL
 * Sets properties of the parents child objects.
 */
void
gst_parent_set (GstParent * parent, const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (G_IS_OBJECT (parent));

  va_start (var_args, first_property_name);
  gst_parent_set_valist (parent, first_property_name, var_args);
  va_end (var_args);
}

static void
gst_parent_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (!initialized) {
    /* create interface signals and properties here. */
    /*
       signals[CHILD_ADDED] =
       g_signal_new ("child-added", G_TYPE_FROM_CLASS (g_class),
       G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstParentClass, child_added), NULL,
       NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);

       signals[CHILD_REMOVED] =
       g_signal_new ("child-removed", G_TYPE_FROM_CLASS (g_class),
       G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstParentClass, child_removed), NULL,
       NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
     */
    initialized = TRUE;
  }
}

GType
gst_parent_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GstParentClass),
      gst_parent_base_init,     /* base_init */
      NULL,                     /* base_finalize */
      NULL,                     /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      0,
      0,                        /* n_preallocs */
      NULL                      /* instance_init */
    };
    type = g_type_register_static (G_TYPE_INTERFACE, "GstParent", &info, 0);
  }
  return type;
}
