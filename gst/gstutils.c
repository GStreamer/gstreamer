/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include "gstutils.h"

/**
 * gst_util_get_int_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * retrieves a property of an object as an integer
 *
 * Returns: the property of the object
 */
gint gst_util_get_int_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_INT(arg);
}

/**
 * gst_util_get_long_arg:
 * @object: the object to query
 * @argname: the name of the argument
 *
 * retrieves a property of an object as a long
 *
 * Returns: the property of the object
 */
glong gst_util_get_long_arg(GtkObject *object,guchar *argname) {
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
 * retrieves a property of an object as a float
 *
 * Returns: the property of the object
 */
gfloat gst_util_get_float_arg(GtkObject *object,guchar *argname) {
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
 * retrieves a property of an object as a double
 *
 * Returns: the property of the object
 */
gdouble gst_util_get_double_arg(GtkObject *object,guchar *argname) {
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
 * retrieves a property of an object as a string
 *
 * Returns: the property of the object
 */
guchar *gst_util_get_string_arg(GtkObject *object,guchar *argname) {
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
 * retrieves a property of an object as a pointer
 *
 * Returns: the property of the object
 */
gpointer gst_util_get_pointer_arg(GtkObject *object,guchar *argname) {
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
 * retrieves a property of an object as a widget
 *
 * Returns: the property of the object
 */
GtkWidget *gst_util_get_widget_arg(GtkObject *object,guchar *argname) {
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
 * dumps the memory block into a hex representation. usefull
 * for debugging.
 */
void gst_util_dump_mem(guchar *mem, guint size) {
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
