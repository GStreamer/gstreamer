/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstutils.c: Utility functions: gtk_get_arg stuff, etc.
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

#include "gstextratypes.h"

#include "gstutils.h"

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
gst_util_get_int_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_INT(arg);
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
gst_util_get_bool_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_BOOL(arg);
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
gst_util_get_long_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_LONG(arg);
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
gst_util_get_float_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_FLOAT(arg);
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
gst_util_get_double_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_DOUBLE(arg);
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
guchar*
gst_util_get_string_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_STRING(arg);
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
gst_util_get_pointer_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  
  return GTK_VALUE_POINTER(arg);
}

/**
 * gst_util_get_widget_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * Retrieves a property of an object as a widget.
 *
 * Returns: the property of the object
 */
GtkWidget*
gst_util_get_widget_arg (GtkObject *object,guchar *argname) 
{
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  
  return GTK_WIDGET(GTK_VALUE_OBJECT(arg));
}

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
gst_util_set_object_arg (GtkObject *object, guchar *name, gchar *value) 
{
  if (name && value) {
    GtkType type = GTK_OBJECT_TYPE (object);
    GtkArgInfo *info;
    gchar *result;

    result = gtk_object_arg_get_info (type, name, &info);

    if (result) {
      g_print("gstutil: %s\n", result);
    }
    else if (info->arg_flags & GTK_ARG_WRITABLE) {
      switch (info->type) {
        case GTK_TYPE_STRING:
          gtk_object_set (GTK_OBJECT (object), name, value, NULL);
          break;
        case GTK_TYPE_ENUM: 
        case GTK_TYPE_INT: {
          gint i;
          sscanf (value, "%d", &i);
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_LONG: {
	  glong i;
	  sscanf (value, "%ld", &i);
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_ULONG: {
	  gulong i;
	  sscanf (value, "%lu", &i);
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_BOOL: {
	  gboolean i = FALSE;
	  if (!strncmp ("true", value, 4)) i = TRUE;
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_CHAR: {
	  gchar i;
	  sscanf (value, "%c", &i);
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_UCHAR: {
	  guchar i;
	  sscanf (value, "%c", &i);
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_FLOAT: {
	  gfloat i;
	  sscanf (value, "%f", &i);
          gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  break;
	}
        case GTK_TYPE_DOUBLE: {
	  gfloat i;
	  sscanf (value, "%g", &i);
          gtk_object_set (GTK_OBJECT (object), name, (gdouble)i, NULL);
	  break;
	}
        default:
	  if (GTK_FUNDAMENTAL_TYPE(info->type) == GTK_TYPE_ENUM) {
            gint i;
            sscanf (value, "%d", &i);
            gtk_object_set (GTK_OBJECT (object), name, i, NULL);
	  }
	  else if (info->type == GST_TYPE_FILENAME) {
            gtk_object_set (GTK_OBJECT (object), name, value, NULL);
	  }
	  break;
      }
    }
  }
}
