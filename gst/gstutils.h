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
#include <gst/gstelement.h>

G_BEGIN_DECLS

void		gst_util_set_value_from_string	(GValue *value, const gchar *value_str);
void 		gst_util_set_object_arg 	(GObject *object, const gchar *name, const gchar *value);
	
void 		gst_util_dump_mem		(const guchar *mem, guint size);

void 		gst_print_pad_caps 		(GString *buf, gint indent, GstPad *pad);
void 		gst_print_element_args 		(GString *buf, gint indent, GstElement *element);


/* Macros for defining classes.  Ideas taken from Bonobo, which took theirs 
   from Nautilus and GOB. */

/* Define the boilerplate type stuff to reduce typos and code size.  Defines
   the get_type method and the parent_class static variable.
   void additional_initializations (GType type) is for initializing interfaces
   and stuff like that */

#define GST_BOILERPLATE_FULL(type, type_as_function, parent_type, parent_type_macro, additional_initializations)     				\
										\
static void type_as_function ## _base_init     (gpointer      g_class);		\
static void type_as_function ## _class_init    (type ## Class *g_class);	\
static void type_as_function ## _init	       (type          *object);		\
static parent_type ## Class *parent_class = NULL;				\
static void									\
type_as_function ## _class_init_trampoline (gpointer g_class,			\
					    gpointer data)			\
{										\
  parent_class = (parent_type ## Class *) g_type_class_peek_parent (g_class);	\
  type_as_function ## _class_init ((type ## Class *)g_class);			\
}										\
										\
GType										\
type_as_function ## _get_type (void)						\
{										\
  static GType object_type = 0;							\
  if (object_type == 0) {							\
    static const GTypeInfo object_info = {					\
      sizeof (type ## Class),							\
      type_as_function ## _base_init,						\
      NULL,		  /* base_finalize */					\
      type_as_function ## _class_init_trampoline,				\
      NULL,		  /* class_finalize */					\
      NULL,               /* class_data */					\
      sizeof (type),								\
      0,                  /* n_preallocs */					\
      (GInstanceInitFunc) type_as_function ## _init				\
    };										\
    object_type = g_type_register_static (parent_type_macro, #type,		\
	&object_info, (GTypeFlags) 0);							\
    additional_initializations (object_type);					\
  }										\
  return object_type;								\
}

#define __GST_DO_NOTHING(type)	/* NOP */
#define GST_BOILERPLATE(type,type_as_function,parent_type,parent_type_macro)	\
  GST_BOILERPLATE_FULL (type, type_as_function, parent_type, parent_type_macro,	\
      __GST_DO_NOTHING)

/* Just call the parent handler.  This assumes that there is a variable
 * named parent_class that points to the (duh!) parent class.  Note that
 * this macro is not to be used with things that return something, use
 * the _WITH_DEFAULT version for that */
#define GST_CALL_PARENT(parent_class_cast, name, args)				\
	((parent_class_cast(parent_class)->name != NULL) ?			\
	 parent_class_cast(parent_class)->name args : (void) 0)

/* Same as above, but in case there is no implementation, it evaluates
 * to def_return */
#define GST_CALL_PARENT_WITH_DEFAULT(parent_class_cast, name, args, def_return)	\
	((parent_class_cast(parent_class)->name != NULL) ?			\
	 parent_class_cast(parent_class)->name args : def_return)


G_END_DECLS

#endif /* __GST_UTILS_H__ */
