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
#include "gsturitype.h"
#include "gstinfo.h"

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
  GString *string = g_string_sized_new (50);
  GString *chars = g_string_sized_new (18);

  i = j = 0;
  while (i < size) {
    if (g_ascii_isprint (mem[i]))
      g_string_append_printf (chars, "%c", mem[i]);
    else
      g_string_append_printf (chars, ".");

    g_string_append_printf (string, "%02x ", mem[i]);

    j++;
    i++;

    if (j == 16 || i == size) {
      g_print ("%08x (%p): %-48.48s %-16.16s\n", i - j, mem + i - j,
          string->str, chars->str);
      g_string_set_size (string, 0);
      g_string_set_size (chars, 0);
      j = 0;
    }
  }
  g_string_free (string, TRUE);
  g_string_free (chars, TRUE);
}


/**
 * gst_util_set_value_from_string:
 * @value: the value to set
 * @value_str: the string to get the value from
 *
 * Converts the string to the type of the value and
 * sets the value with it.
 */
void
gst_util_set_value_from_string (GValue * value, const gchar * value_str)
{

  g_return_if_fail (value != NULL);
  g_return_if_fail (value_str != NULL);

  GST_CAT_DEBUG (GST_CAT_PARAMS, "parsing '%s' to type %s", value_str,
      g_type_name (G_VALUE_TYPE (value)));

  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      g_value_set_string (value, g_strdup (value_str));
      break;
    case G_TYPE_ENUM:
    case G_TYPE_INT:{
      gint i;

      sscanf (value_str, "%d", &i);
      g_value_set_int (value, i);
      break;
    }
    case G_TYPE_UINT:{
      guint i;

      sscanf (value_str, "%u", &i);
      g_value_set_uint (value, i);
      break;
    }
    case G_TYPE_LONG:{
      glong i;

      sscanf (value_str, "%ld", &i);
      g_value_set_long (value, i);
      break;
    }
    case G_TYPE_ULONG:{
      gulong i;

      sscanf (value_str, "%lu", &i);
      g_value_set_ulong (value, i);
      break;
    }
    case G_TYPE_BOOLEAN:{
      gboolean i = FALSE;

      if (!strncmp ("true", value_str, 4))
        i = TRUE;
      g_value_set_boolean (value, i);
      break;
    }
    case G_TYPE_CHAR:{
      gchar i;

      sscanf (value_str, "%c", &i);
      g_value_set_char (value, i);
      break;
    }
    case G_TYPE_UCHAR:{
      guchar i;

      sscanf (value_str, "%c", &i);
      g_value_set_uchar (value, i);
      break;
    }
    case G_TYPE_FLOAT:{
      gfloat i;

      sscanf (value_str, "%f", &i);
      g_value_set_float (value, i);
      break;
    }
    case G_TYPE_DOUBLE:{
      gfloat i;

      sscanf (value_str, "%g", &i);
      g_value_set_double (value, (gdouble) i);
      break;
    }
    default:
      break;
  }
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
gst_util_set_object_arg (GObject * object, const gchar * name,
    const gchar * value)
{
  if (name && value) {
    GParamSpec *paramspec;

    paramspec =
        g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);

    if (!paramspec) {
      return;
    }

    GST_DEBUG ("paramspec->flags is %d, paramspec->value_type is %d",
        paramspec->flags, (gint) paramspec->value_type);

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

          if (!g_ascii_strncasecmp ("true", value, 4))
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
          } else if (paramspec->value_type == GST_TYPE_URI) {
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

static void
string_append_indent (GString * str, gint count)
{
  gint xx;

  for (xx = 0; xx < count; xx++)
    g_string_append_c (str, ' ');
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
    g_string_printf (buf, "%s:%s has no capabilities",
        GST_DEBUG_PAD_NAME (pad));
  } else {
    char *s;

    s = gst_caps_to_string (caps);
    g_string_append (buf, s);
    g_free (s);
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
  guint width;
  GValue value = { 0, };        /* the important thing is that value.type = 0 */
  gchar *str = 0;
  GParamSpec *spec, **specs, **walk;

  specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (element), NULL);

  width = 0;
  for (walk = specs; *walk; walk++) {
    spec = *walk;
    if (width < strlen (spec->name))
      width = strlen (spec->name);
  }

  for (walk = specs; *walk; walk++) {
    spec = *walk;

    if (spec->flags & G_PARAM_READABLE) {
      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (spec));
      g_object_get_property (G_OBJECT (element), spec->name, &value);
      str = g_strdup_value_contents (&value);
      g_value_unset (&value);
    } else {
      str = g_strdup ("Parameter not readable.");
    }

    string_append_indent (buf, indent);
    g_string_append (buf, spec->name);
    string_append_indent (buf, 2 + width - strlen (spec->name));
    g_string_append (buf, str);
    g_string_append_c (buf, '\n');

    g_free (str);
  }

  g_free (specs);
}
