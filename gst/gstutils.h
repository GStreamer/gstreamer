/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstutils.h: Header for various utility functions
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

#include <glib.h>
#include <gst/gstobject.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

gint 		gst_util_get_int_arg		(GObject *object, gchar *argname);
gboolean	gst_util_get_bool_arg		(GObject *object, gchar *argname);
glong 		gst_util_get_long_arg		(GObject *object, gchar *argname);
gfloat 		gst_util_get_float_arg		(GObject *object, gchar *argname);
gdouble 	gst_util_get_double_arg		(GObject *object, gchar *argname);
gchar*		gst_util_get_string_arg		(GObject *object, gchar *argname);
gpointer 	gst_util_get_pointer_arg	(GObject *object, gchar *argname);
//GtkWidget*	gst_util_get_widget_property	(GObject *object, gchar *argname);

void 		gst_util_set_object_arg 	(GObject *object, gchar *name, gchar *value);
	
void 		gst_util_dump_mem		(guchar *mem, guint size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_UTILS_H__ */
