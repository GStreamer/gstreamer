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


#ifndef __GST_UTILS_H__
#define __GST_UTILS_H__

#include <gtk/gtk.h>

gint 		gst_util_get_int_arg		(GtkObject *object, guchar *argname);
gboolean	gst_util_get_bool_arg		(GtkObject *object, guchar *argname);
glong 		gst_util_get_long_arg		(GtkObject *object, guchar *argname);
gfloat 		gst_util_get_float_arg		(GtkObject *object, guchar *argname);
gdouble 	gst_util_get_double_arg		(GtkObject *object, guchar *argname);
guchar*		gst_util_get_string_arg		(GtkObject *object, guchar *argname);
gpointer 	gst_util_get_pointer_arg	(GtkObject *object, guchar *argname);
GtkWidget*	gst_util_get_widget_arg		(GtkObject *object, guchar *argname);

void 		gst_util_dump_mem		(guchar *mem, guint size);

#endif /* __GST_UTILS_H__ */
