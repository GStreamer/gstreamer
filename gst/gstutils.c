/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstutils.c: Utility functions: gtk_get_property stuff, etc.
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

#include <stdio.h>
#include <string.h>

#include "gst_private.h"
#include "gstutils.h"

#include "gstextratypes.h"

#define ZERO(mem) memset(&mem, 0, sizeof(mem))

/**
 * gst_util_get_int_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as an integer.
 *
 * Returns: the property of the object
 */
gint
gst_util_get_int_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_INT);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_int(&value);
}

/**
 * gst_util_get_bool_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a boolean.
 *
 * Returns: the property of the object
 */
gint
gst_util_get_bool_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_BOOLEAN);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_boolean(&value);
}

/**
 * gst_util_get_long_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a long.
 *
 * Returns: the property of the object
 */
glong
gst_util_get_long_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_LONG);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_long(&value);
}

/**
 * gst_util_get_float_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a float.
 *
 * Returns: the property of the object
 */
gfloat
gst_util_get_float_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_FLOAT);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_float(&value);
}

/**
 * gst_util_get_double_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a double.
 *
 * Returns: the property of the object
 */
gdouble 
gst_util_get_double_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_DOUBLE);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_double(&value);
}

/**
 * gst_util_get_string_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a string.
 *
 * Returns: the property of the object
 */
 const gchar*
gst_util_get_string_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_STRING);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_string(&value);  // memleak?
}

/**
 * gst_util_get_pointer_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a pointer.
 *
 * Returns: the property of the object
 */
gpointer
gst_util_get_pointer_arg (GObject *object, const gchar *argname) 
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_POINTER);
  g_object_get_property(G_OBJECT(object),argname,&value);

  return g_value_get_pointer(&value);
}

/**
 * gst_util_get_widget_property:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a widget.
 *
 * Returns: the property of the object
 */
/* COMMENTED OUT BECAUSE WE HAVE NO MORE gtk.h
GtkWidget*
gst_util_get_widget_property (GObject *object, const gchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(G_OBJECT(object),1,&arg);
  
  return GTK_WIDGET(G_VALUE_OBJECT(arg));
}
*/

/**
 * gst_util_dump_mem:
 * @mem: a pointer to the memory to dump
 * @size: the size of the memory block to dump
 *
 * Dumps the memory block into a hex representation. Useful for debugging.
 */
void 
gst_util_dump_mem (guchar *mem, guint size) 
{
  guint i, j;

  i = j =0;
  while (i<size) {
    if (j == 0) {
      g_print("\n%08x : ", i);
      j = 15;
    }
    else {
      j--;
    }
    g_print("%02x ", mem[i]);
    i++;
  }
  g_print("\n");
}

/**
 * gst_util_set_object_arg:
 * @object: the object to set the argument of
 * @name: the name of the argument to set
 * @value: the string value to set
 *
 * Convertes the string value to the type of the objects argument and
 * sets the argument with it.
 */
void
gst_util_set_object_arg (GObject *object,  const gchar *name,  const gchar *value) 
{
  if (name && value) {
    GParamSpec *paramspec;

    paramspec = g_object_class_find_property(G_OBJECT_GET_CLASS(object),name);

    if (!paramspec) {
      return;
    }

    GST_DEBUG(0,"paramspec->flags is %d, paramspec->value_type is %d\n",
              paramspec->flags,paramspec->value_type);

    if (paramspec->flags & G_PARAM_WRITABLE) {
      switch (paramspec->value_type) {
        case G_TYPE_STRING:
          g_object_set (G_OBJECT (object), name, value, NULL);
          break;
        case G_TYPE_ENUM: 
        case G_TYPE_INT: {
          gint i;
          sscanf (value, "%d", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_UINT: {
          guint i;
          sscanf (value, "%u", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_LONG: {
	  glong i;
	  sscanf (value, "%ld", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_ULONG: {
	  gulong i;
	  sscanf (value, "%lu", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_BOOLEAN: {
	  gboolean i = FALSE;
	  if (!strncmp ("true", value, 4)) i = TRUE;
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_CHAR: {
	  gchar i;
	  sscanf (value, "%c", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_UCHAR: {
	  guchar i;
	  sscanf (value, "%c", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_FLOAT: {
	  gfloat i;
	  sscanf (value, "%f", &i);
          g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
        case G_TYPE_DOUBLE: {
	  gfloat i;
	  sscanf (value, "%g", &i);
          g_object_set (G_OBJECT (object), name, (gdouble)i, NULL);
	  break;
	}
        default:
	  if (G_IS_PARAM_SPEC_ENUM(paramspec)) {
            gint i;
            sscanf (value, "%d", &i);
            g_object_set (G_OBJECT (object), name, i, NULL);
	  }
	  else if (paramspec->value_type == GST_TYPE_FILENAME) {
            g_object_set (G_OBJECT (object), name, value, NULL);
	  }
	  break;
      }
    }
  }
}
