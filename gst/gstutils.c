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
gst_util_get_int_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_INT);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_int (&value);
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
gst_util_get_bool_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_BOOLEAN);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_boolean (&value);
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
gst_util_get_long_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_LONG);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_long (&value);
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
gint64
gst_util_get_int64_arg (GObject *object, const gchar *argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_INT64);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_int64 (&value);
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
gst_util_get_float_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_FLOAT);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_float (&value);
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
gst_util_get_double_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_DOUBLE);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_double (&value);
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
const gchar *
gst_util_get_string_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_STRING);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_string (&value);	/* memleak? */
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
gst_util_get_pointer_arg (GObject * object, const gchar * argname)
{
  GValue value;

  ZERO (value);
  g_value_init (&value, G_TYPE_POINTER);
  g_object_get_property (G_OBJECT (object), argname, &value);

  return g_value_get_pointer (&value);
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
gst_util_dump_mem (guchar * mem, guint size)
{
  guint i, j;

  i = j = 0;
  while (i < size) {
    if (j == 0) {
      g_print ("\n%08x : ", i);
      j = 15;
    }
    else {
      j--;
    }
    g_print ("%02x ", mem[i]);
    i++;
  }
  g_print ("\n");
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
gst_util_set_object_arg (GObject * object, const gchar * name, const gchar * value)
{
  if (name && value) {
    GParamSpec *paramspec;

    paramspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);

    if (!paramspec) {
      return;
    }

    GST_DEBUG (0, "paramspec->flags is %d, paramspec->value_type is %d\n",
	       paramspec->flags, paramspec->value_type);

    if (paramspec->flags & G_PARAM_WRITABLE) {
      switch (paramspec->value_type) {
	case G_TYPE_STRING:
	  g_object_set (G_OBJECT (object), name, value, NULL);
	  break;
	case G_TYPE_ENUM:
	case G_TYPE_INT:{
	  gint i;

	  sscanf (value, "%d", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_UINT:{
	  guint i;

	  sscanf (value, "%u", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_LONG:{
	  glong i;

	  sscanf (value, "%ld", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_ULONG:{
	  gulong i;

	  sscanf (value, "%lu", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_BOOLEAN:{
	  gboolean i = FALSE;

	  if (!strncmp ("true", value, 4))
	    i = TRUE;
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_CHAR:{
	  gchar i;

	  sscanf (value, "%c", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_UCHAR:{
	  guchar i;

	  sscanf (value, "%c", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_FLOAT:{
	  gfloat i;

	  sscanf (value, "%f", &i);
	  g_object_set (G_OBJECT (object), name, i, NULL);
	  break;
	}
	case G_TYPE_DOUBLE:{
	  gfloat i;

	  sscanf (value, "%g", &i);
	  g_object_set (G_OBJECT (object), name, (gdouble) i, NULL);
	  break;
	}
	default:
	  if (G_IS_PARAM_SPEC_ENUM (paramspec)) {
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

/* -----------------------------------------------------
 *
 *  The following code will be moved out of the main
 * gstreamer library someday.
 */

#include "gstpad.h"
#include "gsttype.h"
#include "gstprops.h"
#include "gstpropsprivate.h"

static void
string_append_indent (GString * str, gint count)
{
  gint xx;

  for (xx = 0; xx < count; xx++)
    g_string_append_c (str, ' ');
}

static void
gst_print_props (GString * buf, gint indent, GList * props, gboolean showname)
{
  GList *elem;
  guint width = 0;

  if (showname)
    for (elem = props; elem; elem = g_list_next (elem)) {
      GstPropsEntry *prop = elem->data;
      const gchar *name = g_quark_to_string (prop->propid);

      if (width < strlen (name))
	width = strlen (name);
    }

  for (elem = props; elem; elem = g_list_next (elem)) {
    GstPropsEntry *prop = elem->data;

    string_append_indent (buf, indent);
    if (showname) {
      const gchar *name = g_quark_to_string (prop->propid);

      g_string_append (buf, name);
      string_append_indent (buf, 2 + width - strlen (name));
    }

    switch (prop->propstype) {
      case GST_PROPS_INT_ID:
	g_string_append_printf (buf, "%d (int)\n", prop->data.int_data);
	break;
      case GST_PROPS_INT_RANGE_ID:
	g_string_append_printf (buf, "%d - %d (int)\n",
				prop->data.int_range_data.min, prop->data.int_range_data.max);
	break;
      case GST_PROPS_FLOAT_ID:
	g_string_append_printf (buf, "%f (float)\n", prop->data.float_data);
	break;
      case GST_PROPS_FLOAT_RANGE_ID:
	g_string_append_printf (buf, "%f - %f (float)\n",
				prop->data.float_range_data.min, prop->data.float_range_data.max);
	break;
      case GST_PROPS_BOOL_ID:
	g_string_append_printf (buf, "%s\n", prop->data.bool_data ? "TRUE" : "FALSE");
	break;
      case GST_PROPS_STRING_ID:
	g_string_append_printf (buf, "\"%s\"\n", prop->data.string_data.string);
	break;
      case GST_PROPS_FOURCC_ID:
	g_string_append_printf (buf, "'%c%c%c%c' (fourcc)\n",
				prop->data.fourcc_data & 0xff,
				prop->data.fourcc_data >> 8 & 0xff,
				prop->data.fourcc_data >> 16 & 0xff,
				prop->data.fourcc_data >> 24 & 0xff);
	break;
      case GST_PROPS_LIST_ID:
	gst_print_props (buf, indent + 2, prop->data.list_data.entries, FALSE);
	break;
      default:
	g_string_append_printf (buf, "unknown proptype %d\n", prop->propstype);
	break;
    }
  }
}

/**
 * gst_print_pad_caps:
 * @buf: the buffer to print the caps in
 * @indent: initial indentation
 * @pad: the pad to print the caps from
 *
 * Write the pad capabilities in a human readable format into
 * the given GString.
 */
void
gst_print_pad_caps (GString * buf, gint indent, GstPad * pad)
{
  GstRealPad *realpad;
  GstCaps *caps;

  realpad = GST_PAD_REALIZE (pad);
  caps = realpad->caps;

  if (!caps) {
    string_append_indent (buf, indent);
    g_string_printf (buf, "%s:%s has no capabilities", GST_DEBUG_PAD_NAME (pad));
  }
  else {
    gint capx = 0;

    while (caps) {
      GstType *type;

      string_append_indent (buf, indent);
      g_string_append_printf (buf, "Cap[%d]: %s\n", capx++, caps->name);

      type = gst_type_find_by_id (caps->id);
      string_append_indent (buf, indent + 2);
      g_string_append_printf (buf, "MIME type: %s\n", type->mime ? type->mime : "unknown/unknown");

      if (caps->properties)
	gst_print_props (buf, indent + 4, caps->properties->properties, TRUE);

      caps = caps->next;
    }
  }
}

/**
 * gst_print_element_args:
 * @buf: the buffer to print the args in
 * @indent: initial indentation
 * @element: the element to print the args of
 *
 * Print the element argument in a human readable format in the given
 * GString.
 */
void
gst_print_element_args (GString * buf, gint indent, GstElement * element)
{
  gint num_properties;
  gint px;
  guint width;

  GParamSpec **property_specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (element),
								&num_properties);

  width = 0;
  for (px = 0; px < num_properties; px++) {
    GParamSpec *param = property_specs[px];

    if (width < strlen (param->name))
      width = strlen (param->name);
  }

  for (px = 0; px < num_properties; px++) {
    GParamSpec *param = property_specs[px];
    GValue value;

    ZERO (value);

    g_value_init (&value, param->value_type);
    g_object_get_property (G_OBJECT (element), param->name, &value);

    string_append_indent (buf, indent);
    g_string_append (buf, param->name);
    string_append_indent (buf, 2 + width - strlen (param->name));

    if (G_IS_PARAM_SPEC_ENUM (param)) {
      GEnumValue *values;

#ifdef USE_GLIB2
      values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
#else
      values = gtk_type_enum_get_values (param->value_type);
#endif

      g_string_append_printf (buf, "%s (%s)",
			      values[g_value_get_enum (&value)].value_nick,
			      g_type_name (G_VALUE_TYPE (&value)));
    }
    else {
      switch (G_VALUE_TYPE (&value)) {
	case G_TYPE_STRING:
	  g_string_append_printf (buf, "\"%s\"", g_value_get_string (&value));
	  break;
	case G_TYPE_BOOLEAN:
	  g_string_append (buf, g_value_get_boolean (&value) ? "TRUE" : "FALSE");
	  break;
	case G_TYPE_ULONG:{
	  gulong val = g_value_get_ulong (&value);

	  g_string_append_printf (buf, "%lu (0x%lx)", val, val);
	  break;
	}
	case G_TYPE_LONG:{
	  glong val = g_value_get_long (&value);

	  g_string_append_printf (buf, "%ld (0x%lx)", val, val);
	  break;
	}
	case G_TYPE_UINT:{
	  guint val = g_value_get_uint (&value);

	  g_string_append_printf (buf, "%u (0x%x)", val, val);
	  break;
	}
	case G_TYPE_INT:{
	  gint val = g_value_get_int (&value);

	  g_string_append_printf (buf, "%d (0x%x)", val, val);
	  break;
	}
	case G_TYPE_FLOAT:
	  g_string_append_printf (buf, "%f", g_value_get_float (&value));
	  break;
	case G_TYPE_DOUBLE:
	  g_string_append_printf (buf, "%f", g_value_get_double (&value));
	  break;
	default:
	  g_string_append_printf (buf, "unknown value_type %d", G_VALUE_TYPE (&value));
	  break;
      }
    }

    g_string_append_c (buf, '\n');

    if (G_VALUE_TYPE (&value)) {
      g_value_unset (&value);
    }
  }
  g_free (property_specs);
}

/**
 * gst_util_has_arg:
 * @object: an object
 * @argname: a property it might have
 * @arg_type: the type of the argument it should have
 * 
 * Determines whether this @object has a property of name
 * @argname and of type @arg_type
 * 
 * Return value: TRUE if it has the prop, else FALSE
 **/
gboolean
gst_util_has_arg (GObject *object, const gchar *argname, GType arg_type)
{
  GParamSpec *pspec;

  pspec = g_object_class_find_property (
	  G_OBJECT_GET_CLASS (object), argname);

  if (!pspec)
    return FALSE;

  if (pspec->value_type != arg_type)
    return FALSE;

  return TRUE;
}
