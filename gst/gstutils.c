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


#include <gtk/gtk.h>

gint gst_util_get_int_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_INT(arg);
}

glong gst_util_get_long_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_LONG(arg);
}

gfloat gst_util_get_float_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_FLOAT(arg);
}

gdouble gst_util_get_double_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_DOUBLE(arg);
}

guchar *gst_util_get_string_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_STRING(arg);
}

gpointer gst_util_get_pointer_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_VALUE_POINTER(arg);
}

GtkWidget *gst_util_get_widget_arg(GtkObject *object,guchar *argname) {
  GtkArg arg;

  arg.name = argname;
  gtk_object_getv(GTK_OBJECT(object),1,&arg);
  return GTK_WIDGET(GTK_VALUE_OBJECT(arg));
}

void gst_util_dump_mem(guchar *mem, guint size) {
  guint i, j;

  i = j =0;
  while (i<size) {
    g_print("%02x ", mem[i]);
    if (j == 16) {
      g_print("\n");
      j = 0;
    }
    else {
      j++;
    }
    i++;
  }
  g_print("\n");
}
