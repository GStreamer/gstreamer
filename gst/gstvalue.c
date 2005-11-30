/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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

/**
 * SECTION:gstvalue
 * @short_description: GValue implementations specific to GStreamer
 *
 * GValue implementations specific to GStreamer.
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gst_private.h"
#include "glib-compat-private.h"
#include <gst/gst.h>
#include <gobject/gvaluecollector.h>

typedef struct _GstValueUnionInfo GstValueUnionInfo;
struct _GstValueUnionInfo
{
  GType type1;
  GType type2;
  GstValueUnionFunc func;
};

typedef struct _GstValueIntersectInfo GstValueIntersectInfo;
struct _GstValueIntersectInfo
{
  GType type1;
  GType type2;
  GstValueIntersectFunc func;
};

typedef struct _GstValueSubtractInfo GstValueSubtractInfo;
struct _GstValueSubtractInfo
{
  GType minuend;
  GType subtrahend;
  GstValueSubtractFunc func;
};

GType gst_type_double_range;
GType gst_type_fraction_range;
GType gst_type_list;
GType gst_type_array;
GType gst_type_fraction;
GType gst_type_date;

static GArray *gst_value_table;
static GArray *gst_value_union_funcs;
static GArray *gst_value_intersect_funcs;
static GArray *gst_value_subtract_funcs;

/* Forward declarations */
static gint gst_greatest_common_divisor (gint a, gint b);
static gchar *gst_value_serialize_fraction (const GValue * value);

/********
 * list *
 ********/

/* two helper functions to serialize/stringify any type of list
 * regular lists are done with { }, arrays with < >
 */
static gchar *
gst_value_serialize_any_list (const GValue * value, const gchar * begin,
    const gchar * end)
{
  guint i;
  GArray *array = value->data[0].v_pointer;
  GString *s;
  GValue *v;
  gchar *s_val;

  s = g_string_new (begin);
  for (i = 0; i < array->len; i++) {
    v = &g_array_index (array, GValue, i);
    s_val = gst_value_serialize (v);
    g_string_append (s, s_val);
    g_free (s_val);
    if (i < array->len - 1) {
      g_string_append (s, ", ");
    }
  }
  g_string_append (s, end);
  return g_string_free (s, FALSE);
}

static void
gst_value_transform_any_list_string (const GValue * src_value,
    GValue * dest_value, const gchar * begin, const gchar * end)
{
  GValue *list_value;
  GArray *array;
  GString *s;
  guint i;
  gchar *list_s;

  array = src_value->data[0].v_pointer;

  s = g_string_new (begin);
  for (i = 0; i < array->len; i++) {
    list_value = &g_array_index (array, GValue, i);

    if (i != 0) {
      g_string_append (s, ", ");
    }
    list_s = g_strdup_value_contents (list_value);
    g_string_append (s, list_s);
    g_free (list_s);
  }
  g_string_append (s, end);

  dest_value->data[0].v_pointer = g_string_free (s, FALSE);
}

/*
 * helper function to see if a type is fixed. Is used internally here and
 * there. Do not export, since it doesn't work for types where the content
 * decides the fixedness (e.g. GST_TYPE_ARRAY).
 */

static gboolean
gst_type_is_fixed (GType type)
{
  if (type == GST_TYPE_INT_RANGE || type == GST_TYPE_DOUBLE_RANGE ||
      type == GST_TYPE_LIST) {
    return FALSE;
  }
  if (G_TYPE_FUNDAMENTAL (type) <=
      G_TYPE_MAKE_FUNDAMENTAL (G_TYPE_RESERVED_GLIB_LAST)) {
    return TRUE;
  }
  if (type == GST_TYPE_BUFFER || type == GST_TYPE_FOURCC
      || type == GST_TYPE_ARRAY || type == GST_TYPE_FRACTION) {
    return TRUE;
  }

  return FALSE;
}

/* GValue functions usable for both regular lists and arrays */
static void
gst_value_init_list_or_array (GValue * value)
{
  value->data[0].v_pointer = g_array_new (FALSE, TRUE, sizeof (GValue));
}

static GArray *
copy_garray_of_gstvalue (const GArray * src)
{
  GArray *dest;
  guint i;

  dest = g_array_sized_new (FALSE, TRUE, sizeof (GValue), src->len);
  g_array_set_size (dest, src->len);
  for (i = 0; i < src->len; i++) {
    gst_value_init_and_copy (&g_array_index (dest, GValue, i),
        &g_array_index (src, GValue, i));
  }

  return dest;
}

static void
gst_value_copy_list_or_array (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_pointer =
      copy_garray_of_gstvalue ((GArray *) src_value->data[0].v_pointer);
}

static void
gst_value_free_list_or_array (GValue * value)
{
  guint i;
  GArray *src = (GArray *) value->data[0].v_pointer;

  if ((value->data[1].v_uint & G_VALUE_NOCOPY_CONTENTS) == 0) {
    for (i = 0; i < src->len; i++) {
      g_value_unset (&g_array_index (src, GValue, i));
    }
    g_array_free (src, TRUE);
  }
}

static gpointer
gst_value_list_or_array_peek_pointer (const GValue * value)
{
  return value->data[0].v_pointer;
}

static gchar *
gst_value_collect_list_or_array (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  if (collect_flags & G_VALUE_NOCOPY_CONTENTS) {
    value->data[0].v_pointer = collect_values[0].v_pointer;
    value->data[1].v_uint = G_VALUE_NOCOPY_CONTENTS;
  } else {
    value->data[0].v_pointer =
        copy_garray_of_gstvalue ((GArray *) collect_values[0].v_pointer);
  }
  return NULL;
}

static gchar *
gst_value_lcopy_list_or_array (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  GArray **dest = collect_values[0].v_pointer;

  if (!dest)
    return g_strdup_printf ("value location for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));
  if (!value->data[0].v_pointer)
    return g_strdup_printf ("invalid value given for `%s'",
        G_VALUE_TYPE_NAME (value));
  if (collect_flags & G_VALUE_NOCOPY_CONTENTS) {
    *dest = (GArray *) value->data[0].v_pointer;
  } else {
    *dest = copy_garray_of_gstvalue ((GArray *) value->data[0].v_pointer);
  }
  return NULL;
}

/**
 * gst_value_list_append_value:
 * @value: a #GValue of type #GST_TYPE_LIST
 * @append_value: the value to append
 *
 * Appends @append_value to the GstValueList in @value.
 */
void
gst_value_list_append_value (GValue * value, const GValue * append_value)
{
  GValue val = { 0, };

  g_return_if_fail (GST_VALUE_HOLDS_LIST (value));

  gst_value_init_and_copy (&val, append_value);
  g_array_append_vals ((GArray *) value->data[0].v_pointer, &val, 1);
}

/**
 * gst_value_list_prepend_value:
 * @value: a #GValue of type #GST_TYPE_LIST
 * @prepend_value: the value to prepend
 *
 * Prepends @prepend_value to the GstValueList in @value.
 */
void
gst_value_list_prepend_value (GValue * value, const GValue * prepend_value)
{
  GValue val = { 0, };

  g_return_if_fail (GST_VALUE_HOLDS_LIST (value));

  gst_value_init_and_copy (&val, prepend_value);
  g_array_prepend_vals ((GArray *) value->data[0].v_pointer, &val, 1);
}

/**
 * gst_value_list_concat:
 * @dest: an uninitialized #GValue to take the result
 * @value1: a #GValue
 * @value2: a #GValue
 *
 * Concatenates copies of @value1 and @value2 into a list.  Values that are not
 * of type #GST_TYPE_LIST are treated as if they were lists of length 1.
 * @dest will be initialized to the type #GST_TYPE_LIST.
 */
void
gst_value_list_concat (GValue * dest, const GValue * value1,
    const GValue * value2)
{
  guint i, value1_length, value2_length;
  GArray *array;

  g_return_if_fail (dest != NULL);
  g_return_if_fail (G_VALUE_TYPE (dest) == 0);
  g_return_if_fail (G_IS_VALUE (value1));
  g_return_if_fail (G_IS_VALUE (value2));

  value1_length =
      (GST_VALUE_HOLDS_LIST (value1) ? gst_value_list_get_size (value1) : 1);
  value2_length =
      (GST_VALUE_HOLDS_LIST (value2) ? gst_value_list_get_size (value2) : 1);
  g_value_init (dest, GST_TYPE_LIST);
  array = (GArray *) dest->data[0].v_pointer;
  g_array_set_size (array, value1_length + value2_length);

  if (GST_VALUE_HOLDS_LIST (value1)) {
    for (i = 0; i < value1_length; i++) {
      gst_value_init_and_copy (&g_array_index (array, GValue, i),
          gst_value_list_get_value (value1, i));
    }
  } else {
    gst_value_init_and_copy (&g_array_index (array, GValue, 0), value1);
  }

  if (GST_VALUE_HOLDS_LIST (value2)) {
    for (i = 0; i < value2_length; i++) {
      gst_value_init_and_copy (&g_array_index (array, GValue,
              i + value1_length), gst_value_list_get_value (value2, i));
    }
  } else {
    gst_value_init_and_copy (&g_array_index (array, GValue, value1_length),
        value2);
  }
}

/**
 * gst_value_list_get_size:
 * @value: a #GValue of type #GST_TYPE_LIST
 *
 * Gets the number of values contained in @value.
 *
 * Returns: the number of values
 */
guint
gst_value_list_get_size (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_LIST (value), 0);

  return ((GArray *) value->data[0].v_pointer)->len;
}

/**
 * gst_value_list_get_value:
 * @value: a #GValue of type #GST_TYPE_LIST
 * @index: index of value to get from the list
 *
 * Gets the value that is a member of the list contained in @value and
 * has the index @index.
 *
 * Returns: the value at the given index
 */
const GValue *
gst_value_list_get_value (const GValue * value, guint index)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_LIST (value), NULL);
  g_return_val_if_fail (index < gst_value_list_get_size (value), NULL);

  return (const GValue *) &g_array_index ((GArray *) value->data[0].v_pointer,
      GValue, index);
}

/**
 * gst_value_array_append_value:
 * @value: a #GValue of type #GST_TYPE_ARRAY
 * @append_value: the value to append
 *
 * Appends @append_value to the GstValueArray in @value.
 */
void
gst_value_array_append_value (GValue * value, const GValue * append_value)
{
  GValue val = { 0, };

  g_return_if_fail (GST_VALUE_HOLDS_ARRAY (value));

  gst_value_init_and_copy (&val, append_value);
  g_array_append_vals ((GArray *) value->data[0].v_pointer, &val, 1);
}

/**
 * gst_value_array_prepend_value:
 * @value: a #GValue of type #GST_TYPE_ARRAY
 * @prepend_value: the value to prepend
 *
 * Prepends @prepend_value to the GstValueArray in @value.
 */
void
gst_value_array_prepend_value (GValue * value, const GValue * prepend_value)
{
  GValue val = { 0, };

  g_return_if_fail (GST_VALUE_HOLDS_ARRAY (value));

  gst_value_init_and_copy (&val, prepend_value);
  g_array_prepend_vals ((GArray *) value->data[0].v_pointer, &val, 1);
}

/**
 * gst_value_array_get_size:
 * @value: a #GValue of type #GST_TYPE_ARRAY
 *
 * Gets the number of values contained in @value.
 *
 * Returns: the number of values
 */
guint
gst_value_array_get_size (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (value), 0);

  return ((GArray *) value->data[0].v_pointer)->len;
}

/**
 * gst_value_array_get_value:
 * @value: a #GValue of type #GST_TYPE_ARRAY
 * @index: index of value to get from the array
 *
 * Gets the value that is a member of the array contained in @value and
 * has the index @index.
 *
 * Returns: the value at the given index
 */
const GValue *
gst_value_array_get_value (const GValue * value, guint index)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (value), NULL);
  g_return_val_if_fail (index < gst_value_array_get_size (value), NULL);

  return (const GValue *) &g_array_index ((GArray *) value->data[0].v_pointer,
      GValue, index);
}

static void
gst_value_transform_list_string (const GValue * src_value, GValue * dest_value)
{
  gst_value_transform_any_list_string (src_value, dest_value, "{ ", " }");
}

static void
gst_value_transform_array_string (const GValue * src_value, GValue * dest_value)
{
  gst_value_transform_any_list_string (src_value, dest_value, "< ", " >");
}

static int
gst_value_compare_list_or_array (const GValue * value1, const GValue * value2)
{
  guint i, j;
  GArray *array1 = value1->data[0].v_pointer;
  GArray *array2 = value2->data[0].v_pointer;
  GValue *v1;
  GValue *v2;

  if (array1->len != array2->len)
    return GST_VALUE_UNORDERED;

  for (i = 0; i < array1->len; i++) {
    v1 = &g_array_index (array1, GValue, i);
    for (j = 0; j < array1->len; j++) {
      v2 = &g_array_index (array2, GValue, j);
      if (gst_value_compare (v1, v2) == GST_VALUE_EQUAL)
        break;
    }
    if (j == array1->len) {
      return GST_VALUE_UNORDERED;
    }
  }

  return GST_VALUE_EQUAL;
}

static gchar *
gst_value_serialize_list (const GValue * value)
{
  return gst_value_serialize_any_list (value, "{ ", " }");
}

static gboolean
gst_value_deserialize_list (GValue * dest, const gchar * s)
{
  g_warning ("unimplemented");
  return FALSE;
}

static gchar *
gst_value_serialize_array (const GValue * value)
{
  return gst_value_serialize_any_list (value, "< ", " >");
}

static gboolean
gst_value_deserialize_array (GValue * dest, const gchar * s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/**********
 * fourcc *
 **********/

static void
gst_value_init_fourcc (GValue * value)
{
  value->data[0].v_int = 0;
}

static void
gst_value_copy_fourcc (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_int = src_value->data[0].v_int;
}

static gchar *
gst_value_collect_fourcc (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  value->data[0].v_int = collect_values[0].v_int;

  return NULL;
}

static gchar *
gst_value_lcopy_fourcc (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  guint32 *fourcc_p = collect_values[0].v_pointer;

  if (!fourcc_p)
    return g_strdup_printf ("value location for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));

  *fourcc_p = value->data[0].v_int;

  return NULL;
}

/**
 * gst_value_set_fourcc:
 * @value: a GValue initialized to #GST_TYPE_FOURCC
 * @fourcc: the #guint32 fourcc to set
 *
 * Sets @value to @fourcc.
 */
void
gst_value_set_fourcc (GValue * value, guint32 fourcc)
{
  g_return_if_fail (GST_VALUE_HOLDS_FOURCC (value));

  value->data[0].v_int = fourcc;
}

/**
 * gst_value_get_fourcc:
 * @value: a GValue initialized to #GST_TYPE_FOURCC
 *
 * Gets the #guint32 fourcc contained in @value.
 *
 * Returns: the #guint32 fourcc contained in @value.
 */
guint32
gst_value_get_fourcc (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FOURCC (value), 0);

  return value->data[0].v_int;
}

static void
gst_value_transform_fourcc_string (const GValue * src_value,
    GValue * dest_value)
{
  guint32 fourcc = src_value->data[0].v_int;

  if (g_ascii_isprint ((fourcc >> 0) & 0xff) &&
      g_ascii_isprint ((fourcc >> 8) & 0xff) &&
      g_ascii_isprint ((fourcc >> 16) & 0xff) &&
      g_ascii_isprint ((fourcc >> 24) & 0xff)) {
    dest_value->data[0].v_pointer =
        g_strdup_printf ("%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
  } else {
    dest_value->data[0].v_pointer = g_strdup_printf ("0x%08x", fourcc);
  }
}

static gint
gst_value_compare_fourcc (const GValue * value1, const GValue * value2)
{
  if (value2->data[0].v_int == value1->data[0].v_int)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static gchar *
gst_value_serialize_fourcc (const GValue * value)
{
  guint32 fourcc = value->data[0].v_int;

  if (g_ascii_isalnum ((fourcc >> 0) & 0xff) &&
      g_ascii_isalnum ((fourcc >> 8) & 0xff) &&
      g_ascii_isalnum ((fourcc >> 16) & 0xff) &&
      g_ascii_isalnum ((fourcc >> 24) & 0xff)) {
    return g_strdup_printf ("%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
  } else {
    return g_strdup_printf ("0x%08x", fourcc);
  }
}

static gboolean
gst_value_deserialize_fourcc (GValue * dest, const char *s)
{
  gboolean ret = FALSE;
  guint32 fourcc = 0;
  char *end;

  if (strlen (s) == 4) {
    fourcc = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
    ret = TRUE;
  } else if (g_ascii_isdigit (*s)) {
    fourcc = strtoul (s, &end, 0);
    if (*end == 0) {
      ret = TRUE;
    }
  }
  gst_value_set_fourcc (dest, fourcc);

  return ret;
}

/*************
 * int range *
 *************/

static void
gst_value_init_int_range (GValue * value)
{
  value->data[0].v_int = 0;
  value->data[1].v_int = 0;
}

static void
gst_value_copy_int_range (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_int = src_value->data[0].v_int;
  dest_value->data[1].v_int = src_value->data[1].v_int;
}

static gchar *
gst_value_collect_int_range (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  value->data[0].v_int = collect_values[0].v_int;
  value->data[1].v_int = collect_values[1].v_int;

  return NULL;
}

static gchar *
gst_value_lcopy_int_range (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  guint32 *int_range_start = collect_values[0].v_pointer;
  guint32 *int_range_end = collect_values[1].v_pointer;

  if (!int_range_start)
    return g_strdup_printf ("start value location for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));
  if (!int_range_end)
    return g_strdup_printf ("end value location for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));

  *int_range_start = value->data[0].v_int;
  *int_range_end = value->data[1].v_int;

  return NULL;
}

/**
 * gst_value_set_int_range:
 * @value: a GValue initialized to GST_TYPE_INT_RANGE
 * @start: the start of the range
 * @end: the end of the range
 *
 * Sets @value to the range specified by @start and @end.
 */
void
gst_value_set_int_range (GValue * value, gint start, gint end)
{
  g_return_if_fail (GST_VALUE_HOLDS_INT_RANGE (value));
  g_return_if_fail (start < end);

  value->data[0].v_int = start;
  value->data[1].v_int = end;
}

/**
 * gst_value_get_int_range_min:
 * @value: a GValue initialized to GST_TYPE_INT_RANGE
 *
 * Gets the minimum of the range specified by @value.
 *
 * Returns: the minimum of the range
 */
gint
gst_value_get_int_range_min (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_INT_RANGE (value), 0);

  return value->data[0].v_int;
}

/**
 * gst_value_get_int_range_max:
 * @value: a GValue initialized to GST_TYPE_INT_RANGE
 *
 * Gets the maximum of the range specified by @value.
 *
 * Returns: the maxumum of the range
 */
gint
gst_value_get_int_range_max (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_INT_RANGE (value), 0);

  return value->data[1].v_int;
}

static void
gst_value_transform_int_range_string (const GValue * src_value,
    GValue * dest_value)
{
  dest_value->data[0].v_pointer = g_strdup_printf ("[%d,%d]",
      (int) src_value->data[0].v_int, (int) src_value->data[1].v_int);
}

static gint
gst_value_compare_int_range (const GValue * value1, const GValue * value2)
{
  if (value2->data[0].v_int == value1->data[0].v_int &&
      value2->data[1].v_int == value1->data[1].v_int)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static gchar *
gst_value_serialize_int_range (const GValue * value)
{
  return g_strdup_printf ("[ %d, %d ]", value->data[0].v_int,
      value->data[1].v_int);
}

static gboolean
gst_value_deserialize_int_range (GValue * dest, const gchar * s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/****************
 * double range *
 ****************/

static void
gst_value_init_double_range (GValue * value)
{
  value->data[0].v_double = 0;
  value->data[1].v_double = 0;
}

static void
gst_value_copy_double_range (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_double = src_value->data[0].v_double;
  dest_value->data[1].v_double = src_value->data[1].v_double;
}

static gchar *
gst_value_collect_double_range (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  value->data[0].v_double = collect_values[0].v_double;
  value->data[1].v_double = collect_values[1].v_double;

  return NULL;
}

static gchar *
gst_value_lcopy_double_range (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  gdouble *double_range_start = collect_values[0].v_pointer;
  gdouble *double_range_end = collect_values[1].v_pointer;

  if (!double_range_start)
    return g_strdup_printf ("start value location for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));
  if (!double_range_end)
    return g_strdup_printf ("end value location for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));

  *double_range_start = value->data[0].v_double;
  *double_range_end = value->data[1].v_double;

  return NULL;
}

/**
 * gst_value_set_double_range:
 * @value: a GValue initialized to GST_TYPE_DOUBLE_RANGE
 * @start: the start of the range
 * @end: the end of the range
 *
 * Sets @value to the range specified by @start and @end.
 */
void
gst_value_set_double_range (GValue * value, gdouble start, gdouble end)
{
  g_return_if_fail (GST_VALUE_HOLDS_DOUBLE_RANGE (value));

  value->data[0].v_double = start;
  value->data[1].v_double = end;
}

/**
 * gst_value_get_double_range_min:
 * @value: a GValue initialized to GST_TYPE_DOUBLE_RANGE
 *
 * Gets the minimum of the range specified by @value.
 *
 * Returns: the minumum of the range
 */
gdouble
gst_value_get_double_range_min (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_DOUBLE_RANGE (value), 0);

  return value->data[0].v_double;
}

/**
 * gst_value_get_double_range_max:
 * @value: a GValue initialized to GST_TYPE_DOUBLE_RANGE
 *
 * Gets the maximum of the range specified by @value.
 *
 * Returns: the maxumum of the range
 */
gdouble
gst_value_get_double_range_max (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_DOUBLE_RANGE (value), 0);

  return value->data[1].v_double;
}

static void
gst_value_transform_double_range_string (const GValue * src_value,
    GValue * dest_value)
{
  char s1[G_ASCII_DTOSTR_BUF_SIZE], s2[G_ASCII_DTOSTR_BUF_SIZE];

  dest_value->data[0].v_pointer = g_strdup_printf ("[%s,%s]",
      g_ascii_dtostr (s1, G_ASCII_DTOSTR_BUF_SIZE,
          src_value->data[0].v_double),
      g_ascii_dtostr (s2, G_ASCII_DTOSTR_BUF_SIZE,
          src_value->data[1].v_double));
}

static gint
gst_value_compare_double_range (const GValue * value1, const GValue * value2)
{
  if (value2->data[0].v_double == value1->data[0].v_double &&
      value2->data[0].v_double == value1->data[0].v_double)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static gchar *
gst_value_serialize_double_range (const GValue * value)
{
  char d1[G_ASCII_DTOSTR_BUF_SIZE];
  char d2[G_ASCII_DTOSTR_BUF_SIZE];

  g_ascii_dtostr (d1, G_ASCII_DTOSTR_BUF_SIZE, value->data[0].v_double);
  g_ascii_dtostr (d2, G_ASCII_DTOSTR_BUF_SIZE, value->data[1].v_double);
  return g_strdup_printf ("[ %s, %s ]", d1, d2);
}

static gboolean
gst_value_deserialize_double_range (GValue * dest, const gchar * s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/****************
 * fraction range *
 ****************/

static void
gst_value_init_fraction_range (GValue * value)
{
  GValue *vals;

  value->data[0].v_pointer = vals = g_new0 (GValue, 2);
  g_value_init (&vals[0], GST_TYPE_FRACTION);
  g_value_init (&vals[1], GST_TYPE_FRACTION);
}

static void
gst_value_free_fraction_range (GValue * value)
{
  GValue *vals = (GValue *) value->data[0].v_pointer;

  if (vals != NULL) {
    g_value_unset (&vals[0]);
    g_value_unset (&vals[1]);
    g_free (vals);
    value->data[0].v_pointer = NULL;
  }
}

static void
gst_value_copy_fraction_range (const GValue * src_value, GValue * dest_value)
{
  GValue *vals = (GValue *) dest_value->data[0].v_pointer;
  GValue *src_vals = (GValue *) src_value->data[0].v_pointer;

  if (vals == NULL) {
    dest_value->data[0].v_pointer = vals = g_new0 (GValue, 2);
    g_return_if_fail (vals != NULL);
    g_value_init (&vals[0], GST_TYPE_FRACTION);
    g_value_init (&vals[1], GST_TYPE_FRACTION);
  }

  if (src_vals != NULL) {
    g_value_copy (&src_vals[0], &vals[0]);
    g_value_copy (&src_vals[1], &vals[1]);
  }
}

static gchar *
gst_value_collect_fraction_range (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  GValue *vals = (GValue *) value->data[0].v_pointer;

  if (n_collect_values != 4)
    return g_strdup_printf ("not enough value locations for `%s' passed",
        G_VALUE_TYPE_NAME (value));
  if (vals == NULL) {
    value->data[0].v_pointer = vals = g_new0 (GValue, 2);
    if (vals == NULL)
      return g_strdup_printf ("Could not initialise`%s' during collect",
          G_VALUE_TYPE_NAME (value));
    g_value_init (&vals[0], GST_TYPE_FRACTION);
    g_value_init (&vals[1], GST_TYPE_FRACTION);
  }

  gst_value_set_fraction (&vals[0], collect_values[0].v_int,
      collect_values[1].v_int);
  gst_value_set_fraction (&vals[1], collect_values[2].v_int,
      collect_values[3].v_int);

  return NULL;
}

static gchar *
gst_value_lcopy_fraction_range (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  int i;
  int *dest_values[4];
  GValue *vals = (GValue *) value->data[0].v_pointer;

  if (n_collect_values != 4)
    return g_strdup_printf ("not enough value locations for `%s' passed",
        G_VALUE_TYPE_NAME (value));

  for (i = 0; i < 4; i++) {
    if (collect_values[i].v_pointer == NULL) {
      return g_strdup_printf ("value location for `%s' passed as NULL",
          G_VALUE_TYPE_NAME (value));
    }
    dest_values[i] = collect_values[i].v_pointer;
  }

  if (vals == NULL) {
    return g_strdup_printf ("Uninitialised `%s' passed",
        G_VALUE_TYPE_NAME (value));
  }

  dest_values[0][0] = gst_value_get_fraction_numerator (&vals[0]);
  dest_values[1][0] = gst_value_get_fraction_denominator (&vals[0]);
  dest_values[2][0] = gst_value_get_fraction_denominator (&vals[1]);
  dest_values[3][0] = gst_value_get_fraction_denominator (&vals[1]);
  return NULL;
}

/**
 * gst_value_set_fraction_range:
 * @value: a GValue initialized to GST_TYPE_FRACTION_RANGE
 * @start: the start of the range (a GST_TYPE_FRACTION GValue)
 * @end: the end of the range (a GST_TYPE_FRACTION GValue)
 *
 * Sets @value to the range specified by @start and @end.
 */
void
gst_value_set_fraction_range (GValue * value, const GValue * start,
    const GValue * end)
{
  GValue *vals;

  g_return_if_fail (GST_VALUE_HOLDS_FRACTION_RANGE (value));

  vals = (GValue *) value->data[0].v_pointer;
  if (vals == NULL) {
    value->data[0].v_pointer = vals = g_new0 (GValue, 2);
    g_value_init (&vals[0], GST_TYPE_FRACTION);
    g_value_init (&vals[1], GST_TYPE_FRACTION);
  }

  g_value_copy (start, &vals[0]);
  g_value_copy (end, &vals[1]);
}

/**
 * gst_value_set_fraction_range_full:
 * @value: a GValue initialized to GST_TYPE_FRACTION_RANGE
 * @numerator_start: the numerator start of the range
 * @denominator_start: the denominator start of the range
 * @numerator_end: the numerator end of the range
 * @denominator_end: the denominator end of the range
 *
 * Sets @value to the range specified by @numerator_start/@denominator_start
 * and @numerator_end/@denominator_end.
 */
void
gst_value_set_fraction_range_full (GValue * value,
    gint numerator_start, gint denominator_start,
    gint numerator_end, gint denominator_end)
{
  GValue start = { 0 };
  GValue end = { 0 };

  g_value_init (&start, GST_TYPE_FRACTION);
  g_value_init (&end, GST_TYPE_FRACTION);

  gst_value_set_fraction (&start, numerator_start, denominator_start);
  gst_value_set_fraction (&end, numerator_end, denominator_end);
  gst_value_set_fraction_range (value, &start, &end);

  g_value_unset (&start);
  g_value_unset (&end);
}

/**
 * gst_value_get_fraction_range_min:
 * @value: a GValue initialized to GST_TYPE_FRACTION_RANGE
 *
 * Gets the minimum of the range specified by @value.
 *
 * Returns: the minumum of the range
 */
const GValue *
gst_value_get_fraction_range_min (const GValue * value)
{
  GValue *vals;

  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION_RANGE (value), FALSE);

  vals = (GValue *) value->data[0].v_pointer;
  if (vals != NULL) {
    return &vals[0];
  }

  return NULL;
}

/**
 * gst_value_get_fraction_range_max:
 * @value: a GValue initialized to GST_TYPE_FRACTION_RANGE
 *
 * Gets the maximum of the range specified by @value.
 *
 * Returns: the maximum of the range
 */
const GValue *
gst_value_get_fraction_range_max (const GValue * value)
{
  GValue *vals;

  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION_RANGE (value), FALSE);

  vals = (GValue *) value->data[0].v_pointer;
  if (vals != NULL) {
    return &vals[1];
  }

  return NULL;
}

static char *
gst_value_serialize_fraction_range (const GValue * value)
{
  GValue *vals = (GValue *) value->data[0].v_pointer;
  gchar *retval;

  if (vals == NULL) {
    retval = g_strdup ("[ 0/1, 0/1 ]");
  } else {
    gchar *start, *end;

    start = gst_value_serialize_fraction (&vals[0]);
    end = gst_value_serialize_fraction (&vals[1]);

    retval = g_strdup_printf ("[ %s, %s ]", start, end);
    g_free (start);
    g_free (end);
  }

  return retval;
}

static void
gst_value_transform_fraction_range_string (const GValue * src_value,
    GValue * dest_value)
{
  dest_value->data[0].v_pointer =
      gst_value_serialize_fraction_range (src_value);
}

static gint
gst_value_compare_fraction_range (const GValue * value1, const GValue * value2)
{
  GValue *vals1, *vals2;

  if (value2->data[0].v_pointer == value1->data[0].v_pointer)
    return GST_VALUE_EQUAL;     /* Only possible if both are NULL */

  if (value2->data[0].v_pointer == NULL || value1->data[0].v_pointer == NULL)
    return GST_VALUE_UNORDERED;

  vals1 = (GValue *) value1->data[0].v_pointer;
  vals2 = (GValue *) value2->data[0].v_pointer;
  if (gst_value_compare (&vals1[0], &vals2[0]) == GST_VALUE_EQUAL &&
      gst_value_compare (&vals1[1], &vals2[1]) == GST_VALUE_EQUAL)
    return GST_VALUE_EQUAL;

  return GST_VALUE_UNORDERED;
}

static gboolean
gst_value_deserialize_fraction_range (GValue * dest, const gchar * s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/***********
 * GstCaps *
 ***********/

/**
 * gst_value_set_caps:
 * @value: a GValue initialized to GST_TYPE_CAPS
 * @caps: the caps to set the value to
 *
 * Sets the contents of @value to coorespond to @caps.  The actual
 * #GstCaps structure is copied before it is used.
 */
void
gst_value_set_caps (GValue * value, const GstCaps * caps)
{
  g_return_if_fail (G_VALUE_TYPE (value) == GST_TYPE_CAPS);

  g_value_set_boxed (value, caps);
}

/**
 * gst_value_get_caps:
 * @value: a GValue initialized to GST_TYPE_CAPS
 *
 * Gets the contents of @value.
 *
 * Returns: the contents of @value
 */
const GstCaps *
gst_value_get_caps (const GValue * value)
{
  g_return_val_if_fail (G_VALUE_TYPE (value) == GST_TYPE_CAPS, NULL);

  return (GstCaps *) g_value_get_boxed (value);
}

static char *
gst_value_serialize_caps (const GValue * value)
{
  GstCaps *caps = g_value_get_boxed (value);

  return gst_caps_to_string (caps);
}

static gboolean
gst_value_deserialize_caps (GValue * dest, const gchar * s)
{
  GstCaps *caps;

  caps = gst_caps_from_string (s);

  if (caps) {
    g_value_set_boxed (dest, caps);
    return TRUE;
  }
  return FALSE;
}


/*************
 * GstBuffer *
 *************/

static int
gst_value_compare_buffer (const GValue * value1, const GValue * value2)
{
  GstBuffer *buf1 = GST_BUFFER (gst_value_get_mini_object (value1));
  GstBuffer *buf2 = GST_BUFFER (gst_value_get_mini_object (value2));

  if (GST_BUFFER_SIZE (buf1) != GST_BUFFER_SIZE (buf2))
    return GST_VALUE_UNORDERED;
  if (GST_BUFFER_SIZE (buf1) == 0)
    return GST_VALUE_EQUAL;
  g_assert (GST_BUFFER_DATA (buf1));
  g_assert (GST_BUFFER_DATA (buf2));
  if (memcmp (GST_BUFFER_DATA (buf1), GST_BUFFER_DATA (buf2),
          GST_BUFFER_SIZE (buf1)) == 0)
    return GST_VALUE_EQUAL;

  return GST_VALUE_UNORDERED;
}

static char *
gst_value_serialize_buffer (const GValue * value)
{
  guint8 *data;
  int i;
  int size;
  char *string;
  GstBuffer *buffer = GST_BUFFER (gst_value_get_mini_object (value));

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  string = g_malloc (size * 2 + 1);
  for (i = 0; i < size; i++) {
    sprintf (string + i * 2, "%02x", data[i]);
  }
  string[size * 2] = 0;

  return string;
}

static gboolean
gst_value_deserialize_buffer (GValue * dest, const gchar * s)
{
  GstBuffer *buffer;
  gboolean ret = TRUE;
  int len;
  char ts[3];
  guint8 *data;
  int i;

  len = strlen (s);
  if (len & 1)
    return FALSE;
  buffer = gst_buffer_new_and_alloc (len / 2);
  data = GST_BUFFER_DATA (buffer);
  for (i = 0; i < len / 2; i++) {
    if (!isxdigit ((int) s[i * 2]) || !isxdigit ((int) s[i * 2 + 1])) {
      ret = FALSE;
      break;
    }
    ts[0] = s[i * 2 + 0];
    ts[1] = s[i * 2 + 1];
    ts[2] = 0;

    data[i] = (guint8) strtoul (ts, NULL, 16);
  }

  if (ret) {
    gst_value_take_mini_object (dest, GST_MINI_OBJECT (buffer));
    return TRUE;
  } else {
    gst_buffer_unref (buffer);
    return FALSE;
  }
}


/***********
 * boolean *
 ***********/

static int
gst_value_compare_boolean (const GValue * value1, const GValue * value2)
{
  if ((value1->data[0].v_int != 0) == (value2->data[0].v_int != 0))
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static char *
gst_value_serialize_boolean (const GValue * value)
{
  if (value->data[0].v_int) {
    return g_strdup ("true");
  }
  return g_strdup ("false");
}

static gboolean
gst_value_deserialize_boolean (GValue * dest, const gchar * s)
{
  gboolean ret = FALSE;

  if (g_ascii_strcasecmp (s, "true") == 0 ||
      g_ascii_strcasecmp (s, "yes") == 0 ||
      g_ascii_strcasecmp (s, "t") == 0 || strcmp (s, "1") == 0) {
    g_value_set_boolean (dest, TRUE);
    ret = TRUE;
  } else if (g_ascii_strcasecmp (s, "false") == 0 ||
      g_ascii_strcasecmp (s, "no") == 0 ||
      g_ascii_strcasecmp (s, "f") == 0 || strcmp (s, "0") == 0) {
    g_value_set_boolean (dest, FALSE);
    ret = TRUE;
  }

  return ret;
}

#define CREATE_SERIALIZATION_START(_type,_macro)			\
static gint								\
gst_value_compare_ ## _type						\
(const GValue * value1, const GValue * value2)				\
{									\
  g ## _type val1 = g_value_get_ ## _type (value1);			\
  g ## _type val2 = g_value_get_ ## _type (value2);			\
  if (val1 > val2)							\
    return GST_VALUE_GREATER_THAN;					\
  if (val1 < val2)							\
    return GST_VALUE_LESS_THAN;						\
  return GST_VALUE_EQUAL;						\
}									\
									\
static char *								\
gst_value_serialize_ ## _type (const GValue * value)			\
{									\
  GValue val = { 0, };							\
  g_value_init (&val, G_TYPE_STRING);					\
  if (!g_value_transform (value, &val))					\
    g_assert_not_reached ();						\
  /* NO_COPY_MADNESS!!! */						\
  return (char *) g_value_get_string (&val);				\
}

/* deserialize the given s into to as a gint64.
 * check if the result is actually storeable in the given size number of
 * bytes.
 */
static gboolean
gst_value_deserialize_int_helper (gint64 * to, const gchar * s,
    gint64 min, gint64 max, gint size)
{
  gboolean ret = FALSE;
  char *end;
  gint64 mask = -1;

  errno = 0;
  *to = g_ascii_strtoull (s, &end, 0);
  /* a range error is a definitive no-no */
  if (errno == ERANGE) {
    return FALSE;
  }

  if (*end == 0) {
    ret = TRUE;
  } else {
    if (g_ascii_strcasecmp (s, "little_endian") == 0) {
      *to = G_LITTLE_ENDIAN;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "big_endian") == 0) {
      *to = G_BIG_ENDIAN;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "byte_order") == 0) {
      *to = G_BYTE_ORDER;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "min") == 0) {
      *to = min;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "max") == 0) {
      *to = max;
      ret = TRUE;
    }
  }
  if (ret) {
    /* by definition, a gint64 fits into a gint64; so ignore those */
    if (size != sizeof (mask)) {
      if (*to >= 0) {
        /* for positive numbers, we create a mask of 1's outside of the range
         * and 0's inside the range.  An and will thus keep only 1 bits
         * outside of the range */
        mask <<= (size * 8);
        if ((mask & *to) != 0) {
          ret = FALSE;
        }
      } else {
        /* for negative numbers, we do a 2's complement version */
        mask <<= ((size * 8) - 1);
        if ((mask & *to) != mask) {
          ret = FALSE;
        }
      }
    }
  }
  return ret;
}

#define CREATE_SERIALIZATION(_type,_macro)				\
CREATE_SERIALIZATION_START(_type,_macro)				\
									\
static gboolean								\
gst_value_deserialize_ ## _type (GValue * dest, const gchar *s)		\
{									\
  gint64 x;								\
									\
  if (gst_value_deserialize_int_helper (&x, s, G_MIN ## _macro,		\
      G_MAX ## _macro, sizeof (g ## _type))) {				\
    g_value_set_ ## _type (dest, /*(g ## _type)*/ x);			\
    return TRUE;							\
  } else {								\
    return FALSE;							\
  }									\
}

#define CREATE_USERIALIZATION(_type,_macro)				\
CREATE_SERIALIZATION_START(_type,_macro)				\
									\
static gboolean								\
gst_value_deserialize_ ## _type (GValue * dest, const gchar *s)		\
{									\
  gint64 x;								\
  char *end;								\
  gboolean ret = FALSE;							\
									\
  errno = 0;								\
  x = g_ascii_strtoull (s, &end, 0);					\
  /* a range error is a definitive no-no */				\
  if (errno == ERANGE) {						\
    return FALSE;							\
  }									\
  /* the cast ensures the range check later on makes sense */		\
  x = (g ## _type) x;							\
  if (*end == 0) {							\
    ret = TRUE;								\
  } else {								\
    if (g_ascii_strcasecmp (s, "little_endian") == 0) {			\
      x = G_LITTLE_ENDIAN;						\
      ret = TRUE;							\
    } else if (g_ascii_strcasecmp (s, "big_endian") == 0) {		\
      x = G_BIG_ENDIAN;							\
      ret = TRUE;							\
    } else if (g_ascii_strcasecmp (s, "byte_order") == 0) {		\
      x = G_BYTE_ORDER;							\
      ret = TRUE;							\
    } else if (g_ascii_strcasecmp (s, "min") == 0) {			\
      x = 0;								\
      ret = TRUE;							\
    } else if (g_ascii_strcasecmp (s, "max") == 0) {			\
      x = G_MAX ## _macro;						\
      ret = TRUE;							\
    }									\
  }									\
  if (ret) {								\
    if (x > G_MAX ## _macro) {						\
      ret = FALSE;							\
    } else {								\
      g_value_set_ ## _type (dest, x);					\
    }									\
  }									\
  return ret;								\
}

#define REGISTER_SERIALIZATION(_gtype, _type)				\
G_STMT_START {								\
  static const GstValueTable gst_value = {				\
    _gtype,								\
    gst_value_compare_ ## _type,					\
    gst_value_serialize_ ## _type,					\
    gst_value_deserialize_ ## _type,					\
  };									\
									\
  gst_value_register (&gst_value);					\
} G_STMT_END

CREATE_SERIALIZATION (int, INT);
CREATE_SERIALIZATION (int64, INT64);
CREATE_SERIALIZATION (long, LONG);

CREATE_USERIALIZATION (uint, UINT);
CREATE_USERIALIZATION (uint64, UINT64);
CREATE_USERIALIZATION (ulong, ULONG);

/**********
 * double *
 **********/
static int
gst_value_compare_double (const GValue * value1, const GValue * value2)
{
  if (value1->data[0].v_double > value2->data[0].v_double)
    return GST_VALUE_GREATER_THAN;
  if (value1->data[0].v_double < value2->data[0].v_double)
    return GST_VALUE_LESS_THAN;
  if (value1->data[0].v_double == value2->data[0].v_double)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static char *
gst_value_serialize_double (const GValue * value)
{
  char d[G_ASCII_DTOSTR_BUF_SIZE];

  g_ascii_dtostr (d, G_ASCII_DTOSTR_BUF_SIZE, value->data[0].v_double);
  return g_strdup (d);
}

static gboolean
gst_value_deserialize_double (GValue * dest, const gchar * s)
{
  double x;
  gboolean ret = FALSE;
  char *end;

  x = g_ascii_strtod (s, &end);
  if (*end == 0) {
    ret = TRUE;
  } else {
    if (g_ascii_strcasecmp (s, "min") == 0) {
      x = -G_MAXDOUBLE;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "max") == 0) {
      x = G_MAXDOUBLE;
      ret = TRUE;
    }
  }
  if (ret) {
    g_value_set_double (dest, x);
  }
  return ret;
}

/*********
 * float *
 *********/

static gint
gst_value_compare_float (const GValue * value1, const GValue * value2)
{
  if (value1->data[0].v_float > value2->data[0].v_float)
    return GST_VALUE_GREATER_THAN;
  if (value1->data[0].v_float < value2->data[0].v_float)
    return GST_VALUE_LESS_THAN;
  if (value1->data[0].v_float == value2->data[0].v_float)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static gchar *
gst_value_serialize_float (const GValue * value)
{
  gchar d[G_ASCII_DTOSTR_BUF_SIZE];

  g_ascii_dtostr (d, G_ASCII_DTOSTR_BUF_SIZE, value->data[0].v_float);
  return g_strdup (d);
}

static gboolean
gst_value_deserialize_float (GValue * dest, const gchar * s)
{
  double x;
  gboolean ret = FALSE;
  char *end;

  x = g_ascii_strtod (s, &end);
  if (*end == 0) {
    ret = TRUE;
  } else {
    if (g_ascii_strcasecmp (s, "min") == 0) {
      x = -G_MAXFLOAT;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "max") == 0) {
      x = G_MAXFLOAT;
      ret = TRUE;
    }
  }
  if (x > G_MAXFLOAT || x < -G_MAXFLOAT)
    ret = FALSE;
  if (ret) {
    g_value_set_float (dest, (float) x);
  }
  return ret;
}

/**********
 * string *
 **********/

static gint
gst_value_compare_string (const GValue * value1, const GValue * value2)
{
  int x = strcmp (value1->data[0].v_pointer, value2->data[0].v_pointer);

  if (x < 0)
    return GST_VALUE_LESS_THAN;
  if (x > 0)
    return GST_VALUE_GREATER_THAN;
  return GST_VALUE_EQUAL;
}

#define GST_ASCII_IS_STRING(c) (g_ascii_isalnum((c)) || ((c) == '_') || \
    ((c) == '-') || ((c) == '+') || ((c) == '/') || ((c) == ':') || \
    ((c) == '.'))

static gchar *
gst_string_wrap (const gchar * s)
{
  const gchar *t;
  int len;
  gchar *d, *e;
  gboolean wrap = FALSE;

  len = 0;
  t = s;
  if (!s)
    return NULL;
  while (*t) {
    if (GST_ASCII_IS_STRING (*t)) {
      len++;
    } else if (*t < 0x20 || *t >= 0x7f) {
      wrap = TRUE;
      len += 4;
    } else {
      wrap = TRUE;
      len += 2;
    }
    t++;
  }

  if (!wrap)
    return g_strdup (s);

  e = d = g_malloc (len + 3);

  *e++ = '\"';
  t = s;
  while (*t) {
    if (GST_ASCII_IS_STRING (*t)) {
      *e++ = *t++;
    } else if (*t < 0x20 || *t >= 0x7f) {
      *e++ = '\\';
      *e++ = '0' + ((*(guchar *) t) >> 6);
      *e++ = '0' + (((*t) >> 3) & 0x7);
      *e++ = '0' + ((*t++) & 0x7);
    } else {
      *e++ = '\\';
      *e++ = *t++;
    }
  }
  *e++ = '\"';
  *e = 0;

  return d;
}

/*
 * This function takes a string delimited with double quotes (")
 * and unescapes any \xxx octal numbers.
 *
 * If sequences of \y are found where y is not in the range of
 * 0->3, y is copied unescaped.
 *
 * If \xyy is found where x is an octal number but y is not, an
 * error is encountered and NULL is returned.
 *
 * the input string must be \0 terminated.
 */
static gchar *
gst_string_unwrap (const gchar * s)
{
  gchar *ret;
  gchar *read, *write;

  /* NULL string returns NULL */
  if (s == NULL)
    return NULL;

  /* strings not starting with " are invalid */
  if (*s != '"')
    return NULL;

  /* make copy of original string to hold the result. This
   * string will always be smaller than the original */
  ret = g_strdup (s);
  read = ret;
  write = ret;

  /* need to move to the next position as we parsed the " */
  read++;

  while (*read) {
    if (GST_ASCII_IS_STRING (*read)) {
      /* normal chars are just copied */
      *write++ = *read++;
    } else if (*read == '"') {
      /* quote marks end of string */
      break;
    } else if (*read == '\\') {
      /* got an escape char, move to next position to read a tripplet
       * of octal numbers */
      read++;
      /* is the next char a possible first octal number? */
      if (*read >= '0' && *read <= '3') {
        /* parse other 2 numbers, if one of them is not in the range of
         * an octal number, we error. We also catch the case where a zero
         * byte is found here. */
        if (read[1] < '0' || read[1] > '7' || read[2] < '0' || read[2] > '7')
          goto beach;

        /* now convert the octal number to a byte again. */
        *write++ = ((read[0] - '0') << 6) +
            ((read[1] - '0') << 3) + (read[2] - '0');

        read += 3;
      } else {
        /* if we run into a \0 here, we definately won't get a quote later */
        if (*read == 0)
          goto beach;

        /* else copy \X sequence */
        *write++ = *read++;
      }
    } else {
      /* weird character, error */
      goto beach;
    }
  }
  /* if the string is not ending in " and zero terminated, we error */
  if (*read != '"' || read[1] != '\0')
    goto beach;

  /* null terminate result string and return */
  *write++ = '\0';
  return ret;

beach:
  g_free (ret);
  return NULL;
}

static gchar *
gst_value_serialize_string (const GValue * value)
{
  return gst_string_wrap (value->data[0].v_pointer);
}

static gboolean
gst_value_deserialize_string (GValue * dest, const gchar * s)
{
  if (*s != '"') {
    if (!g_utf8_validate (s, -1, NULL))
      return FALSE;
    g_value_set_string (dest, s);
    return TRUE;
  } else {
    gchar *str = gst_string_unwrap (s);

    if (!str)
      return FALSE;
    g_value_take_string (dest, str);
  }

  return TRUE;
}

/********
 * enum *
 ********/

static gint
gst_value_compare_enum (const GValue * value1, const GValue * value2)
{
  GEnumValue *en1, *en2;
  GEnumClass *klass1 = (GEnumClass *) g_type_class_ref (G_VALUE_TYPE (value1));
  GEnumClass *klass2 = (GEnumClass *) g_type_class_ref (G_VALUE_TYPE (value2));

  g_return_val_if_fail (klass1, GST_VALUE_UNORDERED);
  g_return_val_if_fail (klass2, GST_VALUE_UNORDERED);
  en1 = g_enum_get_value (klass1, g_value_get_enum (value1));
  en2 = g_enum_get_value (klass2, g_value_get_enum (value2));
  g_type_class_unref (klass1);
  g_type_class_unref (klass2);
  g_return_val_if_fail (en1, GST_VALUE_UNORDERED);
  g_return_val_if_fail (en2, GST_VALUE_UNORDERED);
  if (en1->value < en2->value)
    return GST_VALUE_LESS_THAN;
  if (en1->value > en2->value)
    return GST_VALUE_GREATER_THAN;

  return GST_VALUE_EQUAL;
}

static gchar *
gst_value_serialize_enum (const GValue * value)
{
  GEnumValue *en;
  GEnumClass *klass = (GEnumClass *) g_type_class_ref (G_VALUE_TYPE (value));

  g_return_val_if_fail (klass, NULL);
  en = g_enum_get_value (klass, g_value_get_enum (value));
  g_type_class_unref (klass);
  g_return_val_if_fail (en, NULL);
  return g_strdup (en->value_name);
}

static gboolean
gst_value_deserialize_enum (GValue * dest, const gchar * s)
{
  GEnumValue *en;
  gchar *endptr = NULL;
  GEnumClass *klass = (GEnumClass *) g_type_class_ref (G_VALUE_TYPE (dest));

  g_return_val_if_fail (klass, FALSE);
  if (!(en = g_enum_get_value_by_name (klass, s))) {
    if (!(en = g_enum_get_value_by_nick (klass, s))) {
      gint i = strtol (s, &endptr, 0);

      if (endptr && *endptr == '\0') {
        en = g_enum_get_value (klass, i);
      }
    }
  }
  g_type_class_unref (klass);
  g_return_val_if_fail (en, FALSE);
  g_value_set_enum (dest, en->value);
  return TRUE;
}

/********
 * flags *
 ********/

/* we just compare the value here */
static gint
gst_value_compare_flags (const GValue * value1, const GValue * value2)
{
  guint fl1, fl2;
  GFlagsClass *klass1 =
      (GFlagsClass *) g_type_class_ref (G_VALUE_TYPE (value1));
  GFlagsClass *klass2 =
      (GFlagsClass *) g_type_class_ref (G_VALUE_TYPE (value2));

  g_return_val_if_fail (klass1, GST_VALUE_UNORDERED);
  g_return_val_if_fail (klass2, GST_VALUE_UNORDERED);
  fl1 = g_value_get_flags (value1);
  fl2 = g_value_get_flags (value2);
  g_type_class_unref (klass1);
  g_type_class_unref (klass2);
  if (fl1 < fl2)
    return GST_VALUE_LESS_THAN;
  if (fl1 > fl2)
    return GST_VALUE_GREATER_THAN;

  return GST_VALUE_EQUAL;
}

/* the different flags are serialized separated with a + */
static gchar *
gst_value_serialize_flags (const GValue * value)
{
  guint flags;
  GFlagsValue *fl;
  GFlagsClass *klass = (GFlagsClass *) g_type_class_ref (G_VALUE_TYPE (value));
  gchar *result, *tmp;
  gboolean first = TRUE;

  g_return_val_if_fail (klass, NULL);

  result = g_strdup ("");
  flags = g_value_get_flags (value);
  while (flags) {
    fl = gst_flags_get_first_value (klass, flags);
    if (fl != NULL) {
      tmp = g_strconcat (result, (first ? "" : "+"), fl->value_name, NULL);
      g_free (result);
      result = tmp;
      first = FALSE;
    }
    /* clear flag */
    flags &= ~fl->value;
  }
  g_type_class_unref (klass);

  return result;
}

static gboolean
gst_value_deserialize_flags (GValue * dest, const gchar * s)
{
  GFlagsValue *fl;
  gchar *endptr = NULL;
  GFlagsClass *klass = (GFlagsClass *) g_type_class_ref (G_VALUE_TYPE (dest));
  gchar **split;
  guint flags;
  gint i;

  g_return_val_if_fail (klass, FALSE);

  /* split into parts delimited with + */
  split = g_strsplit (s, "+", 0);

  flags = 0;
  i = 0;
  /* loop over each part */
  while (split[i]) {
    if (!(fl = g_flags_get_value_by_name (klass, split[i]))) {
      if (!(fl = g_flags_get_value_by_nick (klass, split[i]))) {
        gint val = strtol (split[i], &endptr, 0);

        /* just or numeric value */
        if (endptr && *endptr == '\0') {
          flags |= val;
        }
      }
    }
    if (fl) {
      flags |= fl->value;
    }
    i++;
  }
  g_strfreev (split);
  g_type_class_unref (klass);
  g_value_set_flags (dest, flags);

  return TRUE;
}

/*********
 * union *
 *********/

static gboolean
gst_value_union_int_int_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  if (src2->data[0].v_int <= src1->data[0].v_int &&
      src2->data[1].v_int >= src1->data[0].v_int) {
    gst_value_init_and_copy (dest, src2);
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_value_union_int_range_int_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  gint min;
  gint max;

  min = MAX (src1->data[0].v_int, src2->data[0].v_int);
  max = MIN (src1->data[1].v_int, src2->data[1].v_int);

  if (min <= max) {
    g_value_init (dest, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (dest,
        MIN (src1->data[0].v_int, src2->data[0].v_int),
        MAX (src1->data[1].v_int, src2->data[1].v_int));
    return TRUE;
  }

  return FALSE;
}

/****************
 * intersection *
 ****************/

static gboolean
gst_value_intersect_int_int_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  if (src2->data[0].v_int <= src1->data[0].v_int &&
      src2->data[1].v_int >= src1->data[0].v_int) {
    gst_value_init_and_copy (dest, src1);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_int_range_int_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  gint min;
  gint max;

  min = MAX (src1->data[0].v_int, src2->data[0].v_int);
  max = MIN (src1->data[1].v_int, src2->data[1].v_int);

  if (min < max) {
    g_value_init (dest, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (dest, min, max);
    return TRUE;
  }
  if (min == max) {
    g_value_init (dest, G_TYPE_INT);
    g_value_set_int (dest, min);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_double_double_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  if (src2->data[0].v_double <= src1->data[0].v_double &&
      src2->data[1].v_double >= src1->data[0].v_double) {
    gst_value_init_and_copy (dest, src1);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_double_range_double_range (GValue * dest,
    const GValue * src1, const GValue * src2)
{
  gdouble min;
  gdouble max;

  min = MAX (src1->data[0].v_double, src2->data[0].v_double);
  max = MIN (src1->data[1].v_double, src2->data[1].v_double);

  if (min < max) {
    g_value_init (dest, GST_TYPE_DOUBLE_RANGE);
    gst_value_set_double_range (dest, min, max);
    return TRUE;
  }
  if (min == max) {
    g_value_init (dest, G_TYPE_DOUBLE);
    g_value_set_int (dest, (int) min);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_list (GValue * dest, const GValue * value1,
    const GValue * value2)
{
  guint i, size;
  GValue intersection = { 0, };
  gboolean ret = FALSE;

  size = gst_value_list_get_size (value1);
  for (i = 0; i < size; i++) {
    const GValue *cur = gst_value_list_get_value (value1, i);

    if (gst_value_intersect (&intersection, cur, value2)) {
      /* append value */
      if (!ret) {
        gst_value_init_and_copy (dest, &intersection);
        ret = TRUE;
      } else if (GST_VALUE_HOLDS_LIST (dest)) {
        gst_value_list_append_value (dest, &intersection);
      } else {
        GValue temp = { 0, };

        gst_value_init_and_copy (&temp, dest);
        g_value_unset (dest);
        gst_value_list_concat (dest, &temp, &intersection);
        g_value_unset (&temp);
      }
      g_value_unset (&intersection);
    }
  }

  return ret;
}

static gboolean
gst_value_intersect_array (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  guint size;
  guint n;
  GValue val = { 0 };

  /* only works on similar-sized arrays */
  size = gst_value_array_get_size (src1);
  if (size != gst_value_array_get_size (src2))
    return FALSE;
  g_value_init (dest, GST_TYPE_ARRAY);

  for (n = 0; n < size; n++) {
    if (!gst_value_intersect (&val, gst_value_array_get_value (src1, n),
            gst_value_array_get_value (src2, n))) {
      g_value_unset (dest);
      return FALSE;
    }
    gst_value_array_append_value (dest, &val);
    g_value_unset (&val);
  }

  return TRUE;
}

static gboolean
gst_value_intersect_fraction_fraction_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  int res1, res2;
  GValue *vals;

  vals = src2->data[0].v_pointer;

  if (vals == NULL)
    return FALSE;

  res1 = gst_value_compare (&vals[0], src1);
  res2 = gst_value_compare (&vals[1], src1);

  if ((res1 == GST_VALUE_EQUAL || res1 == GST_VALUE_LESS_THAN) &&
      (res2 == GST_VALUE_EQUAL || res2 == GST_VALUE_GREATER_THAN)) {
    gst_value_init_and_copy (dest, src1);
    return TRUE;
  }

  return FALSE;
}

static gboolean
    gst_value_intersect_fraction_range_fraction_range
    (GValue * dest, const GValue * src1, const GValue * src2)
{
  GValue *min;
  GValue *max;
  int res;
  GValue *vals1, *vals2;

  vals1 = src1->data[0].v_pointer;
  vals2 = src2->data[0].v_pointer;
  g_return_val_if_fail (vals1 != NULL && vals2 != NULL, FALSE);

  /* min = MAX (src1.start, src2.start) */
  res = gst_value_compare (&vals1[0], &vals2[0]);
  g_return_val_if_fail (res != GST_VALUE_UNORDERED, FALSE);
  if (res == GST_VALUE_LESS_THAN)
    min = &vals2[0];            /* Take the max of the 2 */
  else
    min = &vals1[0];

  /* max = MIN (src1.end, src2.end) */
  res = gst_value_compare (&vals1[1], &vals2[1]);
  g_return_val_if_fail (res != GST_VALUE_UNORDERED, FALSE);
  if (res == GST_VALUE_GREATER_THAN)
    max = &vals2[1];            /* Take the min of the 2 */
  else
    max = &vals1[1];

  res = gst_value_compare (min, max);
  g_return_val_if_fail (res != GST_VALUE_UNORDERED, FALSE);
  if (res == GST_VALUE_LESS_THAN) {
    g_value_init (dest, GST_TYPE_FRACTION_RANGE);
    vals1 = dest->data[0].v_pointer;
    g_value_copy (min, &vals1[0]);
    g_value_copy (max, &vals1[1]);
    return TRUE;
  }
  if (res == GST_VALUE_EQUAL) {
    gst_value_init_and_copy (dest, min);
    return TRUE;
  }

  return FALSE;
}

/***************
 * subtraction *
 ***************/

static gboolean
gst_value_subtract_int_int_range (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  int min = gst_value_get_int_range_min (subtrahend);
  int max = gst_value_get_int_range_max (subtrahend);
  int val = g_value_get_int (minuend);

  /* subtracting a range from an int only works if the int is not in the
   * range */
  if (val < min || val > max) {
    /* and the result is the int */
    gst_value_init_and_copy (dest, minuend);
    return TRUE;
  }
  return FALSE;
}

/* creates a new int range based on input values.
 */
static gboolean
gst_value_create_new_range (GValue * dest, gint min1, gint max1, gint min2,
    gint max2)
{
  GValue v1 = { 0, };
  GValue v2 = { 0, };
  GValue *pv1, *pv2;            /* yeah, hungarian! */

  if (min1 <= max1 && min2 <= max2) {
    pv1 = &v1;
    pv2 = &v2;
  } else if (min1 <= max1) {
    pv1 = dest;
    pv2 = NULL;
  } else if (min2 <= max2) {
    pv1 = NULL;
    pv2 = dest;
  } else {
    return FALSE;
  }

  if (min1 < max1) {
    g_value_init (pv1, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (pv1, min1, max1);
  } else if (min1 == max1) {
    g_value_init (pv1, G_TYPE_INT);
    g_value_set_int (pv1, min1);
  }
  if (min2 < max2) {
    g_value_init (pv2, GST_TYPE_INT_RANGE);
    gst_value_set_int_range (pv2, min2, max2);
  } else if (min2 == max2) {
    g_value_init (pv2, G_TYPE_INT);
    g_value_set_int (pv2, min2);
  }

  if (min1 <= max1 && min2 <= max2) {
    gst_value_list_concat (dest, pv1, pv2);
    g_value_unset (pv1);
    g_value_unset (pv2);
  }
  return TRUE;
}

static gboolean
gst_value_subtract_int_range_int (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  gint min = gst_value_get_int_range_min (minuend);
  gint max = gst_value_get_int_range_max (minuend);
  gint val = g_value_get_int (subtrahend);

  g_return_val_if_fail (min < max, FALSE);

  /* value is outside of the range, return range unchanged */
  if (val < min || val > max) {
    gst_value_init_and_copy (dest, minuend);
    return TRUE;
  } else {
    /* max must be MAXINT too as val <= max */
    if (val == G_MAXINT) {
      max--;
      val--;
    }
    /* min must be MININT too as val >= max */
    if (val == G_MININT) {
      min++;
      val++;
    }
    gst_value_create_new_range (dest, min, val - 1, val + 1, max);
  }
  return TRUE;
}

static gboolean
gst_value_subtract_int_range_int_range (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  gint min1 = gst_value_get_int_range_min (minuend);
  gint max1 = gst_value_get_int_range_max (minuend);
  gint min2 = gst_value_get_int_range_min (subtrahend);
  gint max2 = gst_value_get_int_range_max (subtrahend);

  if (max2 == G_MAXINT && min2 == G_MININT) {
    return FALSE;
  } else if (max2 == G_MAXINT) {
    return gst_value_create_new_range (dest, min1, MIN (min2 - 1, max1), 1, 0);
  } else if (min2 == G_MININT) {
    return gst_value_create_new_range (dest, MAX (max2 + 1, min1), max1, 1, 0);
  } else {
    return gst_value_create_new_range (dest, min1, MIN (min2 - 1, max1),
        MAX (max2 + 1, min1), max1);
  }
}

static gboolean
gst_value_subtract_double_double_range (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  gdouble min = gst_value_get_double_range_min (subtrahend);
  gdouble max = gst_value_get_double_range_max (subtrahend);
  gdouble val = g_value_get_double (minuend);

  if (val < min || val > max) {
    gst_value_init_and_copy (dest, minuend);
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_value_subtract_double_range_double (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  /* since we don't have open ranges, we cannot create a hole in
   * a double range. We return the original range */
  gst_value_init_and_copy (dest, minuend);
  return TRUE;
}

static gboolean
gst_value_subtract_double_range_double_range (GValue * dest,
    const GValue * minuend, const GValue * subtrahend)
{
  /* since we don't have open ranges, we have to approximate */
  /* done like with ints */
  gdouble min1 = gst_value_get_double_range_min (minuend);
  gdouble max2 = gst_value_get_double_range_max (minuend);
  gdouble max1 = MIN (gst_value_get_double_range_min (subtrahend), max2);
  gdouble min2 = MAX (gst_value_get_double_range_max (subtrahend), min1);
  GValue v1 = { 0, };
  GValue v2 = { 0, };
  GValue *pv1, *pv2;            /* yeah, hungarian! */

  if (min1 < max1 && min2 < max2) {
    pv1 = &v1;
    pv2 = &v2;
  } else if (min1 < max1) {
    pv1 = dest;
    pv2 = NULL;
  } else if (min2 < max2) {
    pv1 = NULL;
    pv2 = dest;
  } else {
    return FALSE;
  }

  if (min1 < max1) {
    g_value_init (pv1, GST_TYPE_DOUBLE_RANGE);
    gst_value_set_double_range (pv1, min1, max1);
  }
  if (min2 < max2) {
    g_value_init (pv2, GST_TYPE_DOUBLE_RANGE);
    gst_value_set_double_range (pv2, min2, max2);
  }

  if (min1 < max1 && min2 < max2) {
    gst_value_list_concat (dest, pv1, pv2);
    g_value_unset (pv1);
    g_value_unset (pv2);
  }
  return TRUE;
}

static gboolean
gst_value_subtract_from_list (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  guint i, size;
  GValue subtraction = { 0, };
  gboolean ret = FALSE;

  size = gst_value_list_get_size (minuend);
  for (i = 0; i < size; i++) {
    const GValue *cur = gst_value_list_get_value (minuend, i);

    if (gst_value_subtract (&subtraction, cur, subtrahend)) {
      if (!ret) {
        gst_value_init_and_copy (dest, &subtraction);
        ret = TRUE;
      } else if (GST_VALUE_HOLDS_LIST (dest)
          && GST_VALUE_HOLDS_LIST (&subtraction)) {
        /* unroll */
        GValue unroll = { 0, };

        gst_value_init_and_copy (&unroll, dest);
        g_value_unset (dest);
        gst_value_list_concat (dest, &unroll, &subtraction);
      } else if (GST_VALUE_HOLDS_LIST (dest)) {
        gst_value_list_append_value (dest, &subtraction);
      } else {
        GValue temp = { 0, };

        gst_value_init_and_copy (&temp, dest);
        g_value_unset (dest);
        gst_value_list_concat (dest, &temp, &subtraction);
        g_value_unset (&temp);
      }
      g_value_unset (&subtraction);
    }
  }
  return ret;
}

static gboolean
gst_value_subtract_list (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  guint i, size;
  GValue data[2] = { {0,}, {0,} };
  GValue *subtraction = &data[0], *result = &data[1];

  gst_value_init_and_copy (result, minuend);
  size = gst_value_list_get_size (subtrahend);
  for (i = 0; i < size; i++) {
    const GValue *cur = gst_value_list_get_value (subtrahend, i);

    if (gst_value_subtract (subtraction, result, cur)) {
      GValue *temp = result;

      result = subtraction;
      subtraction = temp;
      g_value_unset (subtraction);
    } else {
      g_value_unset (result);
      return FALSE;
    }
  }
  gst_value_init_and_copy (dest, result);
  g_value_unset (result);
  return TRUE;
}

static gboolean
gst_value_subtract_fraction_fraction_range (GValue * dest,
    const GValue * minuend, const GValue * subtrahend)
{
  const GValue *min = gst_value_get_fraction_range_min (subtrahend);
  const GValue *max = gst_value_get_fraction_range_max (subtrahend);

  /* subtracting a range from an fraction only works if the fraction
   * is not in the range */
  if (gst_value_compare (minuend, min) == GST_VALUE_LESS_THAN ||
      gst_value_compare (minuend, max) == GST_VALUE_GREATER_THAN) {
    /* and the result is the value */
    gst_value_init_and_copy (dest, minuend);
    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_value_subtract_fraction_range_fraction (GValue * dest,
    const GValue * minuend, const GValue * subtrahend)
{
  /* since we don't have open ranges, we cannot create a hole in
   * a range. We return the original range */
  gst_value_init_and_copy (dest, minuend);
  return TRUE;
}

static gboolean
gst_value_subtract_fraction_range_fraction_range (GValue * dest,
    const GValue * minuend, const GValue * subtrahend)
{
  /* since we don't have open ranges, we have to approximate */
  /* done like with ints and doubles. Creates a list of 2 fraction ranges */
  const GValue *min1 = gst_value_get_fraction_range_min (minuend);
  const GValue *max2 = gst_value_get_fraction_range_max (minuend);
  const GValue *max1 = gst_value_get_fraction_range_min (subtrahend);
  const GValue *min2 = gst_value_get_fraction_range_max (subtrahend);
  int cmp1, cmp2;
  GValue v1 = { 0, };
  GValue v2 = { 0, };
  GValue *pv1, *pv2;            /* yeah, hungarian! */

  g_return_val_if_fail (min1 != NULL && max1 != NULL, FALSE);
  g_return_val_if_fail (min2 != NULL && max2 != NULL, FALSE);

  cmp1 = gst_value_compare (max2, max1);
  g_return_val_if_fail (cmp1 != GST_VALUE_UNORDERED, FALSE);
  if (cmp1 == GST_VALUE_LESS_THAN)
    max1 = max2;
  cmp1 = gst_value_compare (min1, min2);
  g_return_val_if_fail (cmp1 != GST_VALUE_UNORDERED, FALSE);
  if (cmp1 == GST_VALUE_GREATER_THAN)
    min2 = min1;

  cmp1 = gst_value_compare (min1, max1);
  cmp2 = gst_value_compare (min2, max2);

  if (cmp1 == GST_VALUE_LESS_THAN && cmp2 == GST_VALUE_LESS_THAN) {
    pv1 = &v1;
    pv2 = &v2;
  } else if (cmp1 == GST_VALUE_LESS_THAN) {
    pv1 = dest;
    pv2 = NULL;
  } else if (cmp2 == GST_VALUE_LESS_THAN) {
    pv1 = NULL;
    pv2 = dest;
  } else {
    return FALSE;
  }

  if (cmp1 == GST_VALUE_LESS_THAN) {
    g_value_init (pv1, GST_TYPE_FRACTION_RANGE);
    gst_value_set_fraction_range (pv1, min1, max1);
  }
  if (cmp2 == GST_VALUE_LESS_THAN) {
    g_value_init (pv2, GST_TYPE_FRACTION_RANGE);
    gst_value_set_fraction_range (pv2, min2, max2);
  }

  if (cmp1 == GST_VALUE_LESS_THAN && cmp2 == GST_VALUE_LESS_THAN) {
    gst_value_list_concat (dest, pv1, pv2);
    g_value_unset (pv1);
    g_value_unset (pv2);
  }
  return TRUE;
}


/**************
 * comparison *
 **************/

/**
 * gst_value_can_compare:
 * @value1: a value to compare
 * @value2: another value to compare
 *
 * Determines if @value1 and @value2 can be compared.
 *
 * Returns: TRUE if the values can be compared
 */
gboolean
gst_value_can_compare (const GValue * value1, const GValue * value2)
{
  GstValueTable *table;
  guint i;

  if (G_VALUE_TYPE (value1) != G_VALUE_TYPE (value2))
    return FALSE;

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (g_type_is_a (G_VALUE_TYPE (value1), table->type) && table->compare)
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_value_compare:
 * @value1: a value to compare
 * @value2: another value to compare
 *
 * Compares @value1 and @value2.  If @value1 and @value2 cannot be
 * compared, the function returns GST_VALUE_UNORDERED.  Otherwise,
 * if @value1 is greater than @value2, GST_VALUE_GREATER is returned.
 * If @value1 is less than @value2, GST_VALUE_LESSER is returned.
 * If the values are equal, GST_VALUE_EQUAL is returned.
 *
 * Returns: A GstValueCompareType value
 */
int
gst_value_compare (const GValue * value1, const GValue * value2)
{
  GstValueTable *table, *best = NULL;
  guint i;

  if (G_VALUE_TYPE (value1) != G_VALUE_TYPE (value2))
    return GST_VALUE_UNORDERED;

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->type == G_VALUE_TYPE (value1) && table->compare != NULL) {
      best = table;
      break;
    }
    if (g_type_is_a (G_VALUE_TYPE (value1), table->type)) {
      if (!best || g_type_is_a (table->type, best->type))
        best = table;
    }
  }
  if (best) {
    return best->compare (value1, value2);
  }

  g_critical ("unable to compare values of type %s\n",
      g_type_name (G_VALUE_TYPE (value1)));
  return GST_VALUE_UNORDERED;
}

/* union */

/**
 * gst_value_can_union:
 * @value1: a value to union
 * @value2: another value to union
 *
 * Determines if @value1 and @value2 can be non-trivially unioned.
 * Any two values can be trivially unioned by adding both of them
 * to a GstValueList.  However, certain types have the possibility
 * to be unioned in a simpler way.  For example, an integer range
 * and an integer can be unioned if the integer is a subset of the
 * integer range.  If there is the possibility that two values can
 * be unioned, this function returns TRUE.
 *
 * Returns: TRUE if there is a function allowing the two values to
 * be unioned.
 */
gboolean
gst_value_can_union (const GValue * value1, const GValue * value2)
{
  GstValueUnionInfo *union_info;
  guint i;

  for (i = 0; i < gst_value_union_funcs->len; i++) {
    union_info = &g_array_index (gst_value_union_funcs, GstValueUnionInfo, i);
    if (union_info->type1 == G_VALUE_TYPE (value1) &&
        union_info->type2 == G_VALUE_TYPE (value2))
      return TRUE;
    if (union_info->type1 == G_VALUE_TYPE (value2) &&
        union_info->type2 == G_VALUE_TYPE (value1))
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_value_union:
 * @dest: the destination value
 * @value1: a value to union
 * @value2: another value to union
 *
 * Creates a GValue cooresponding to the union of @value1 and @value2.
 *
 * Returns: always returns %TRUE
 */
/* FIXME: change return type to 'void'? */
gboolean
gst_value_union (GValue * dest, const GValue * value1, const GValue * value2)
{
  GstValueUnionInfo *union_info;
  guint i;

  for (i = 0; i < gst_value_union_funcs->len; i++) {
    union_info = &g_array_index (gst_value_union_funcs, GstValueUnionInfo, i);
    if (union_info->type1 == G_VALUE_TYPE (value1) &&
        union_info->type2 == G_VALUE_TYPE (value2)) {
      if (union_info->func (dest, value1, value2)) {
        return TRUE;
      }
    }
    if (union_info->type1 == G_VALUE_TYPE (value2) &&
        union_info->type2 == G_VALUE_TYPE (value1)) {
      if (union_info->func (dest, value2, value1)) {
        return TRUE;
      }
    }
  }

  gst_value_list_concat (dest, value1, value2);
  return TRUE;
}

/**
 * gst_value_register_union_func:
 * @type1: a type to union
 * @type2: another type to union
 * @func: a function that implments creating a union between the two types
 *
 * Registers a union function that can create a union between GValues
 * of the type @type1 and @type2.
 *
 */
void
gst_value_register_union_func (GType type1, GType type2, GstValueUnionFunc func)
{
  GstValueUnionInfo union_info;

  union_info.type1 = type1;
  union_info.type2 = type2;
  union_info.func = func;

  g_array_append_val (gst_value_union_funcs, union_info);
}

/* intersection */

/**
 * gst_value_can_intersect:
 * @value1: a value to intersect
 * @value2: another value to intersect
 *
 * Determines if intersecting two values will produce a valid result.
 * Two values will produce a valid intersection if they have the same
 * type, or if there is a method (registered by
 * #gst_value_register_intersection_func) to calculate the intersection.
 *
 * Returns: TRUE if the values can intersect
 */
gboolean
gst_value_can_intersect (const GValue * value1, const GValue * value2)
{
  GstValueIntersectInfo *intersect_info;
  guint i;

  /* special cases */
  if (GST_VALUE_HOLDS_LIST (value1) || GST_VALUE_HOLDS_LIST (value2))
    return TRUE;

  for (i = 0; i < gst_value_intersect_funcs->len; i++) {
    intersect_info = &g_array_index (gst_value_intersect_funcs,
        GstValueIntersectInfo, i);
    if (intersect_info->type1 == G_VALUE_TYPE (value1) &&
        intersect_info->type2 == G_VALUE_TYPE (value2))
      if (intersect_info->type2 == G_VALUE_TYPE (value1) &&
          intersect_info->type1 == G_VALUE_TYPE (value2))
        return TRUE;
  }

  return gst_value_can_compare (value1, value2);
}

/**
 * gst_value_intersect:
 * @dest: a uninitialized #GValue that will hold the calculated
 * intersection value
 * @value1: a value to intersect
 * @value2: another value to intersect
 *
 * Calculates the intersection of two values.  If the values have
 * a non-empty intersection, the value representing the intersection
 * is placed in @dest.  If the intersection is non-empty, @dest is
 * not modified.
 *
 * Returns: TRUE if the intersection is non-empty
 */
gboolean
gst_value_intersect (GValue * dest, const GValue * value1,
    const GValue * value2)
{
  GstValueIntersectInfo *intersect_info;
  guint i;
  gboolean ret = FALSE;

  /* special cases first */
  if (GST_VALUE_HOLDS_LIST (value1))
    return gst_value_intersect_list (dest, value1, value2);
  if (GST_VALUE_HOLDS_LIST (value2))
    return gst_value_intersect_list (dest, value2, value1);

  for (i = 0; i < gst_value_intersect_funcs->len; i++) {
    intersect_info = &g_array_index (gst_value_intersect_funcs,
        GstValueIntersectInfo, i);
    if (intersect_info->type1 == G_VALUE_TYPE (value1) &&
        intersect_info->type2 == G_VALUE_TYPE (value2)) {
      ret = intersect_info->func (dest, value1, value2);
      return ret;
    }
    if (intersect_info->type1 == G_VALUE_TYPE (value2) &&
        intersect_info->type2 == G_VALUE_TYPE (value1)) {
      ret = intersect_info->func (dest, value2, value1);
      return ret;
    }
  }

  if (gst_value_compare (value1, value2) == GST_VALUE_EQUAL) {
    gst_value_init_and_copy (dest, value1);
    ret = TRUE;
  }

  return ret;
}

/**
 * gst_value_register_intersect_func:
 * @type1: the first type to intersect
 * @type2: the second type to intersect
 * @func: the intersection function
 *
 * Registers a function that is called to calculate the intersection
 * of the values having the types @type1 and @type2.
 */
void
gst_value_register_intersect_func (GType type1, GType type2,
    GstValueIntersectFunc func)
{
  GstValueIntersectInfo intersect_info;

  intersect_info.type1 = type1;
  intersect_info.type2 = type2;
  intersect_info.func = func;

  g_array_append_val (gst_value_intersect_funcs, intersect_info);
}


/* subtraction */

/**
 * gst_value_subtract:
 * @dest: the destination value for the result if the subtraction is not empty
 * @minuend: the value to subtract from
 * @subtrahend: the value to subtract
 *
 * Subtracts @subtrahend from @minuend and stores the result in @dest.
 * Note that this means subtraction as in sets, not as in mathematics.
 *
 * Returns: %TRUE if the subtraction is not empty
 */
gboolean
gst_value_subtract (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  GstValueSubtractInfo *info;
  guint i;

  /* special cases first */
  if (GST_VALUE_HOLDS_LIST (minuend))
    return gst_value_subtract_from_list (dest, minuend, subtrahend);
  if (GST_VALUE_HOLDS_LIST (subtrahend))
    return gst_value_subtract_list (dest, minuend, subtrahend);

  for (i = 0; i < gst_value_subtract_funcs->len; i++) {
    info = &g_array_index (gst_value_subtract_funcs, GstValueSubtractInfo, i);
    if (info->minuend == G_VALUE_TYPE (minuend) &&
        info->subtrahend == G_VALUE_TYPE (subtrahend)) {
      return info->func (dest, minuend, subtrahend);
    }
  }

  if (gst_value_compare (minuend, subtrahend) != GST_VALUE_EQUAL) {
    gst_value_init_and_copy (dest, minuend);
    return TRUE;
  }

  return FALSE;
}

#if 0
gboolean
gst_value_subtract (GValue * dest, const GValue * minuend,
    const GValue * subtrahend)
{
  gboolean ret = gst_value_subtract2 (dest, minuend, subtrahend);

  g_printerr ("\"%s\"  -  \"%s\"  =  \"%s\"\n", gst_value_serialize (minuend),
      gst_value_serialize (subtrahend),
      ret ? gst_value_serialize (dest) : "---");
  return ret;
}
#endif

/**
 * gst_value_can_subtract:
 * @minuend: the value to subtract from
 * @subtrahend: the value to subtract
 *
 * Checks if it's possible to subtract @subtrahend from @minuend.
 *
 * Returns: TRUE if a subtraction is possible
 */
gboolean
gst_value_can_subtract (const GValue * minuend, const GValue * subtrahend)
{
  GstValueSubtractInfo *info;
  guint i;

  /* special cases */
  if (GST_VALUE_HOLDS_LIST (minuend) || GST_VALUE_HOLDS_LIST (subtrahend))
    return TRUE;

  for (i = 0; i < gst_value_subtract_funcs->len; i++) {
    info = &g_array_index (gst_value_subtract_funcs, GstValueSubtractInfo, i);
    if (info->minuend == G_VALUE_TYPE (minuend) &&
        info->subtrahend == G_VALUE_TYPE (subtrahend))
      return TRUE;
  }

  return gst_value_can_compare (minuend, subtrahend);
}

/**
 * gst_value_register_subtract_func:
 * @minuend_type: type of the minuend
 * @subtrahend_type: type of the subtrahend
 * @func: function to use
 *
 * Registers @func as a function capable of subtracting the values of
 * @subtrahend_type from values of @minuend_type.
 */
void
gst_value_register_subtract_func (GType minuend_type, GType subtrahend_type,
    GstValueSubtractFunc func)
{
  GstValueSubtractInfo info;

  /* one type must be unfixed, other subtractions can be done as comparisons */
  g_return_if_fail (!gst_type_is_fixed (minuend_type)
      || !gst_type_is_fixed (subtrahend_type));

  info.minuend = minuend_type;
  info.subtrahend = subtrahend_type;
  info.func = func;

  g_array_append_val (gst_value_subtract_funcs, info);
}

/**
 * gst_value_register:
 * @table: structure containing functions to register
 *
 * Registers functions to perform calculations on #GValues of a given
 * type.
 */
/**
 * GstValueTable:
 * @type: GType that the functions operate on.
 * @compare: A function that compares two values of this type.
 * @serialize: A function that transforms a value of this type to a
 * string.  Strings created by this function must be unique and should
 * be human readable.
 * @deserialize: A function that transforms a string to a value of
 * this type.  This function must transform strings created by the
 * serialize function back to the original value.  This function may
 * optionally transform other strings into values.
 */
void
gst_value_register (const GstValueTable * table)
{
  g_array_append_val (gst_value_table, *table);
}

/**
 * gst_value_init_and_copy:
 * @dest: the target value
 * @src: the source value
 *
 * Initialises the target value to be of the same type as source and then copies
 * the contents from source to target.
 */
void
gst_value_init_and_copy (GValue * dest, const GValue * src)
{
  g_value_init (dest, G_VALUE_TYPE (src));
  g_value_copy (src, dest);
}

/**
 * gst_value_serialize:
 * @value: a #GValue to serialize
 *
 * tries to transform the given @value into a string representation that allows
 * getting back this string later on using gst_value_deserialize().
 *
 * Returns: the serialization for @value or NULL if none exists
 */
gchar *
gst_value_serialize (const GValue * value)
{
  guint i;
  GValue s_val = { 0 };
  GstValueTable *table, *best = NULL;
  char *s;

  g_return_val_if_fail (G_IS_VALUE (value), NULL);

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->serialize == NULL)
      continue;
    if (table->type == G_VALUE_TYPE (value)) {
      best = table;
      break;
    }
    if (g_type_is_a (G_VALUE_TYPE (value), table->type)) {
      if (!best || g_type_is_a (table->type, best->type))
        best = table;
    }
  }
  if (best)
    return best->serialize (value);

  g_value_init (&s_val, G_TYPE_STRING);
  if (g_value_transform (value, &s_val)) {
    s = gst_string_wrap (g_value_get_string (&s_val));
  } else {
    s = NULL;
  }
  g_value_unset (&s_val);

  return s;
}

/**
 * gst_value_deserialize:
 * @dest: #GValue to fill with contents of deserialization
 * @src: string to deserialize
 *
 * Tries to deserialize a string into the type specified by the given GValue.
 * If the operation succeeds, TRUE is returned, FALSE otherwise.
 *
 * Returns: TRUE on success
 */
gboolean
gst_value_deserialize (GValue * dest, const gchar * src)
{
  GstValueTable *table, *best = NULL;
  guint i;

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (G_IS_VALUE (dest), FALSE);

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->serialize == NULL)
      continue;

    if (table->type == G_VALUE_TYPE (dest)) {
      best = table;
      break;
    }

    if (g_type_is_a (G_VALUE_TYPE (dest), table->type)) {
      if (!best || g_type_is_a (table->type, best->type))
        best = table;
    }
  }
  if (best) {
    return best->deserialize (dest, src);
  }

  return FALSE;
}

/**
 * gst_value_is_fixed:
 * @value: the #GValue to check
 *
 * Tests if the given GValue, if available in a GstStructure (or any other
 * container) contains a "fixed" (which means: one value) or an "unfixed"
 * (which means: multiple possible values, such as data lists or data
 * ranges) value.
 *
 * Returns: true if the value is "fixed".
 */

gboolean
gst_value_is_fixed (const GValue * value)
{
  GType type = G_VALUE_TYPE (value);

  if (type == GST_TYPE_ARRAY) {
    gboolean fixed = TRUE;
    gint size, n;
    const GValue *kid;

    /* check recursively */
    size = gst_value_array_get_size (value);
    for (n = 0; n < size; n++) {
      kid = gst_value_array_get_value (value, n);
      fixed &= gst_value_is_fixed (kid);
    }

    return fixed;
  }

  return gst_type_is_fixed (type);
}

/************
 * fraction *
 ************/

/* helper functions */

/* Finds the greatest common divisor.
 * Returns 1 if none other found.
 * This is Euclid's algorithm. */
static gint
gst_greatest_common_divisor (gint a, gint b)
{
  while (b != 0) {
    int temp = a;

    a = b;
    b = temp % b;
  }

  return ABS (a);
}

static void
gst_value_init_fraction (GValue * value)
{
  value->data[0].v_int = 0;
  value->data[1].v_int = 1;
}

static void
gst_value_copy_fraction (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_int = src_value->data[0].v_int;
  dest_value->data[1].v_int = src_value->data[1].v_int;
}

static gchar *
gst_value_collect_fraction (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  gst_value_set_fraction (value,
      collect_values[0].v_int, collect_values[1].v_int);

  return NULL;
}

static gchar *
gst_value_lcopy_fraction (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  gint *numerator = collect_values[0].v_pointer;
  gint *denominator = collect_values[1].v_pointer;

  if (!numerator)
    return g_strdup_printf ("numerator for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));
  if (!denominator)
    return g_strdup_printf ("denominator for `%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));

  *numerator = value->data[0].v_int;
  *denominator = value->data[1].v_int;

  return NULL;
}

/**
 * gst_value_set_fraction:
 * @value: a GValue initialized to #GST_TYPE_FRACTION
 * @numerator: the numerator of the fraction
 * @denominator: the denominator of the fraction
 *
 * Sets @value to the fraction specified by @numerator over @denominator.
 * The fraction gets reduced to the smallest numerator and denominator,
 * and if necessary the sign is moved to the numerator.
 */
void
gst_value_set_fraction (GValue * value, gint numerator, gint denominator)
{
  gint gcd = 0;

  g_return_if_fail (GST_VALUE_HOLDS_FRACTION (value));
  g_return_if_fail (denominator != 0);
  g_return_if_fail (denominator >= -G_MAXINT);
  g_return_if_fail (numerator >= -G_MAXINT);

  /* normalize sign */
  if (denominator < 0) {
    numerator = -numerator;
    denominator = -denominator;
  }

  /* check for reduction */
  gcd = gst_greatest_common_divisor (numerator, denominator);
  if (gcd) {
    numerator /= gcd;
    denominator /= gcd;
  }

  g_assert (denominator > 0);

  value->data[0].v_int = numerator;
  value->data[1].v_int = denominator;
}

/**
 * gst_value_get_fraction_numerator:
 * @value: a GValue initialized to #GST_TYPE_FRACTION
 *
 * Gets the numerator of the fraction specified by @value.
 *
 * Returns: the numerator of the fraction.
 */
gint
gst_value_get_fraction_numerator (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (value), 0);

  return value->data[0].v_int;
}

/**
 * gst_value_get_fraction_denominator:
 * @value: a GValue initialized to #GST_TYPE_FRACTION
 *
 * Gets the denominator of the fraction specified by @value.
 *
 * Returns: the denominator of the fraction.
 */
gint
gst_value_get_fraction_denominator (const GValue * value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (value), 1);

  return value->data[1].v_int;
}

/**
 * gst_value_fraction_multiply:
 * @product: a GValue initialized to #GST_TYPE_FRACTION
 * @factor1: a GValue initialized to #GST_TYPE_FRACTION
 * @factor2: a GValue initialized to #GST_TYPE_FRACTION
 *
 * Multiplies the two GValues containing a GstFraction and sets @product
 * to the product of the two fractions.
 *
 * Returns: FALSE in case of an error (like integer overflow), TRUE otherwise.
 */
gboolean
gst_value_fraction_multiply (GValue * product, const GValue * factor1,
    const GValue * factor2)
{
  gint gcd, n1, n2, d1, d2;

  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (factor1), FALSE);
  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (factor2), FALSE);

  n1 = factor1->data[0].v_int;
  n2 = factor2->data[0].v_int;
  d1 = factor1->data[1].v_int;
  d2 = factor2->data[1].v_int;

  gcd = gst_greatest_common_divisor (n1, d2);
  n1 /= gcd;
  d2 /= gcd;
  gcd = gst_greatest_common_divisor (n2, d1);
  n2 /= gcd;
  d1 /= gcd;

  g_return_val_if_fail (n1 == 0 || G_MAXINT / ABS (n1) >= ABS (n2), FALSE);
  g_return_val_if_fail (G_MAXINT / ABS (d1) >= ABS (d2), FALSE);

  gst_value_set_fraction (product, n1 * n2, d1 * d2);

  return TRUE;
}

/**
 * gst_value_fraction_subtract:
 * @dest: a GValue initialized to #GST_TYPE_FRACTION
 * @minuend: a GValue initialized to #GST_TYPE_FRACTION
 * @subtrahend: a GValue initialized to #GST_TYPE_FRACTION
 *
 * Subtracts the @subtrahend from the @minuend and sets @dest to the result.
 *
 * Returns: FALSE in case of an error (like integer overflow), TRUE otherwise.
 */
gboolean
gst_value_fraction_subtract (GValue * dest,
    const GValue * minuend, const GValue * subtrahend)
{
  gint n1, n2, d1, d2;

  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (minuend), FALSE);
  g_return_val_if_fail (GST_VALUE_HOLDS_FRACTION (subtrahend), FALSE);

  n1 = minuend->data[0].v_int;
  n2 = subtrahend->data[0].v_int;
  d1 = minuend->data[1].v_int;
  d2 = subtrahend->data[1].v_int;

  if (n1 == 0) {
    gst_value_set_fraction (dest, -n2, d2);
    return TRUE;
  }
  if (n2 == 0) {
    gst_value_set_fraction (dest, n1, d1);
    return TRUE;
  }

  g_return_val_if_fail (n1 == 0 || G_MAXINT / ABS (n1) >= ABS (d2), FALSE);
  g_return_val_if_fail (G_MAXINT / ABS (d1) >= ABS (n2), FALSE);
  g_return_val_if_fail (G_MAXINT / ABS (d1) >= ABS (d2), FALSE);

  gst_value_set_fraction (dest, (n1 * d2) - (n2 * d1), d1 * d2);

  return TRUE;
}

static gchar *
gst_value_serialize_fraction (const GValue * value)
{
  gint32 numerator = value->data[0].v_int;
  gint32 denominator = value->data[1].v_int;
  gboolean positive = TRUE;

  /* get the sign and make components absolute */
  if (numerator < 0) {
    numerator = -numerator;
    positive = !positive;
  }
  if (denominator < 0) {
    denominator = -denominator;
    positive = !positive;
  }

  return g_strdup_printf ("%s%d/%d",
      positive ? "" : "-", numerator, denominator);
}

static gboolean
gst_value_deserialize_fraction (GValue * dest, const gchar * s)
{
  gint num, den;

  if (s && sscanf (s, "%d/%d", &num, &den) == 2) {
    gst_value_set_fraction (dest, num, den);
    return TRUE;
  }
  if (s && sscanf (s, "%d", &num) == 1) {
    gst_value_set_fraction (dest, num, 1);
    return TRUE;
  }
  if (g_ascii_strcasecmp (s, "min") == 0) {
    gst_value_set_fraction (dest, -G_MAXINT, 1);
    return TRUE;
  } else if (g_ascii_strcasecmp (s, "max") == 0) {
    gst_value_set_fraction (dest, G_MAXINT, 1);
    return TRUE;
  }

  return FALSE;
}

static void
gst_value_transform_fraction_string (const GValue * src_value,
    GValue * dest_value)
{
  dest_value->data[0].v_pointer = gst_value_serialize_fraction (src_value);
}

static void
gst_value_transform_string_fraction (const GValue * src_value,
    GValue * dest_value)
{
  if (!gst_value_deserialize_fraction (dest_value,
          src_value->data[0].v_pointer))
    /* If the deserialize fails, ensure we leave the fraction in a
     * valid, if incorrect, state */
    gst_value_set_fraction (dest_value, 0, 1);
}

#define MAX_TERMS       30
#define MIN_DIVISOR     1.0e-10
#define MAX_ERROR       1.0e-20

/* use continued fractions to transform a double into a fraction,
 * see http://mathforum.org/dr.math/faq/faq.fractions.html#decfrac.
 * This algorithm takes care of overflows.
 */
static void
gst_value_transform_double_fraction (const GValue * src_value,
    GValue * dest_value)
{
  gdouble V, F;                 /* double being converted */
  gint N, D;                    /* will contain the result */
  gint A;                       /* current term in continued fraction */
  gint64 N1, D1;                /* numerator, denominator of last approx */
  gint64 N2, D2;                /* numerator, denominator of previous approx */
  gint i;
  gboolean negative = FALSE;

  /* initialize fraction being converted */
  F = src_value->data[0].v_double;
  if (F < 0.0) {
    F = -F;
    negative = TRUE;
  }

  V = F;
  /* initialize fractions with 1/0, 0/1 */
  N1 = 1;
  D1 = 0;
  N2 = 0;
  D2 = 1;
  N = 1;
  D = 1;

  for (i = 0; i < MAX_TERMS; i++) {
    /* get next term */
    A = floor (F);
    /* get new divisor */
    F = F - A;

    /* calculate new fraction in temp */
    N2 = N1 * A + N2;
    D2 = D1 * A + D2;

    /* guard against overflow */
    if (N2 > G_MAXINT || D2 > G_MAXINT) {
      break;
    }

    N = N2;
    D = D2;

    /* save last two fractions */
    N2 = N1;
    D2 = D1;
    N1 = N;
    D1 = D;

    /* quit if dividing by zero or close enough to target */
    if (F < MIN_DIVISOR || fabs (V - ((gdouble) N) / D) < MAX_ERROR) {
      break;
    }

    /* Take reciprocal */
    F = 1 / F;
  }
  /* fix for overflow */
  if (D == 0) {
    N = G_MAXINT;
    D = 1;
  }
  /* fix for negative */
  if (negative)
    N = -N;

  /* will also simplify */
  gst_value_set_fraction (dest_value, N, D);
}

static void
gst_value_transform_fraction_double (const GValue * src_value,
    GValue * dest_value)
{
  dest_value->data[0].v_double = ((double) src_value->data[0].v_int) /
      ((double) src_value->data[1].v_int);
}

static gint
gst_value_compare_fraction (const GValue * value1, const GValue * value2)
{
  gint n1, n2;
  gint d1, d2;

  gint64 new_num_1;
  gint64 new_num_2;

  n1 = value1->data[0].v_int;
  n2 = value2->data[0].v_int;
  d1 = value1->data[1].v_int;
  d2 = value2->data[1].v_int;

  /* fractions are reduced when set, so we can quickly see if they're equal */
  if (n1 == n2 && d1 == d2)
    return GST_VALUE_EQUAL;

  /* extend to 64 bits */
  new_num_1 = ((gint64) n1) * d2;
  new_num_2 = ((gint64) n2) * d1;
  if (new_num_1 < new_num_2)
    return GST_VALUE_LESS_THAN;
  if (new_num_1 > new_num_2)
    return GST_VALUE_GREATER_THAN;

  /* new_num_1 == new_num_2 implies that both denominators must have 
   * been 0, beause otherwise simplification would have caught the
   * equivalence */
  g_assert_not_reached ();
  return GST_VALUE_UNORDERED;
}

/*********
 * GDate *
 *********/

/**
 * gst_value_set_date:
 * @value: a GValue initialized to GST_TYPE_DATE
 * @date: the date to set the value to
 *
 * Sets the contents of @value to coorespond to @date.  The actual
 * #GDate structure is copied before it is used.
 */
void
gst_value_set_date (GValue * value, const GDate * date)
{
  g_return_if_fail (G_VALUE_TYPE (value) == GST_TYPE_DATE);

  g_value_set_boxed (value, date);
}

/**
 * gst_value_get_date:
 * @value: a GValue initialized to GST_TYPE_DATE
 *
 * Gets the contents of @value.
 *
 * Returns: the contents of @value
 */
const GDate *
gst_value_get_date (const GValue * value)
{
  g_return_val_if_fail (G_VALUE_TYPE (value) == GST_TYPE_DATE, NULL);

  return (const GDate *) g_value_get_boxed (value);
}

static gpointer
gst_date_copy (gpointer boxed)
{
  const GDate *date = (const GDate *) boxed;

  return g_date_new_julian (g_date_get_julian (date));
}

static gint
gst_value_compare_date (const GValue * value1, const GValue * value2)
{
  const GDate *date1 = (const GDate *) g_value_get_boxed (value1);
  const GDate *date2 = (const GDate *) g_value_get_boxed (value2);
  guint32 j1, j2;

  if (date1 == date2)
    return GST_VALUE_EQUAL;

  if ((date1 == NULL || !g_date_valid (date1))
      && (date2 != NULL && g_date_valid (date2))) {
    return GST_VALUE_LESS_THAN;
  }

  if ((date2 == NULL || !g_date_valid (date2))
      && (date1 != NULL && g_date_valid (date1))) {
    return GST_VALUE_GREATER_THAN;
  }

  if (date1 == NULL || date2 == NULL || !g_date_valid (date1)
      || !g_date_valid (date2)) {
    return GST_VALUE_UNORDERED;
  }

  j1 = g_date_get_julian (date1);
  j2 = g_date_get_julian (date2);

  if (j1 == j2)
    return GST_VALUE_EQUAL;
  else if (j1 < j2)
    return GST_VALUE_LESS_THAN;
  else
    return GST_VALUE_GREATER_THAN;
}

static gchar *
gst_value_serialize_date (const GValue * val)
{
  const GDate *date = (const GDate *) g_value_get_boxed (val);

  if (date == NULL || !g_date_valid (date))
    return g_strdup ("9999-99-99");

  return g_strdup_printf ("%04u-%02u-%02u", g_date_get_year (date),
      g_date_get_month (date), g_date_get_day (date));
}

static gboolean
gst_value_deserialize_date (GValue * dest, const char *s)
{
  guint year, month, day;

  if (!s || sscanf (s, "%04u-%02u-%02u", &year, &month, &day) != 3)
    return FALSE;

  if (!g_date_valid_dmy (day, month, year))
    return FALSE;

  g_value_take_boxed (dest, g_date_new_dmy (day, month, year));
  return TRUE;
}

static void
gst_value_transform_date_string (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_pointer = gst_value_serialize_date (src_value);
}

static void
gst_value_transform_string_date (const GValue * src_value, GValue * dest_value)
{
  gst_value_deserialize_date (dest_value, src_value->data[0].v_pointer);
}

static GTypeInfo _info = {
  0,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  0,
  0,
  NULL,
  NULL,
};

static GTypeFundamentalInfo _finfo = {
  0
};

#define FUNC_VALUE_GET_TYPE(type, name)				\
GType gst_ ## type ## _get_type (void)				\
{								\
  static GType gst_ ## type ## _type = 0;			\
								\
  if (!gst_ ## type ## _type) {					\
    _info.value_table = & _gst_ ## type ## _value_table;	\
    gst_ ## type ## _type = g_type_register_fundamental (	\
        g_type_fundamental_next (),				\
        name, &_info, &_finfo, 0);				\
  }								\
								\
  return gst_ ## type ## _type;					\
}

static const GTypeValueTable _gst_fourcc_value_table = {
  gst_value_init_fourcc,
  NULL,
  gst_value_copy_fourcc,
  NULL,
  "i",
  gst_value_collect_fourcc,
  "p",
  gst_value_lcopy_fourcc
};

FUNC_VALUE_GET_TYPE (fourcc, "GstFourcc");

static const GTypeValueTable _gst_int_range_value_table = {
  gst_value_init_int_range,
  NULL,
  gst_value_copy_int_range,
  NULL,
  "ii",
  gst_value_collect_int_range,
  "pp",
  gst_value_lcopy_int_range
};

FUNC_VALUE_GET_TYPE (int_range, "GstIntRange");

static const GTypeValueTable _gst_double_range_value_table = {
  gst_value_init_double_range,
  NULL,
  gst_value_copy_double_range,
  NULL,
  "dd",
  gst_value_collect_double_range,
  "pp",
  gst_value_lcopy_double_range
};

FUNC_VALUE_GET_TYPE (double_range, "GstDoubleRange");

static const GTypeValueTable _gst_fraction_range_value_table = {
  gst_value_init_fraction_range,
  gst_value_free_fraction_range,
  gst_value_copy_fraction_range,
  NULL,
  "iiii",
  gst_value_collect_fraction_range,
  "pppp",
  gst_value_lcopy_fraction_range
};

FUNC_VALUE_GET_TYPE (fraction_range, "GstFractionRange");

static const GTypeValueTable _gst_value_list_value_table = {
  gst_value_init_list_or_array,
  gst_value_free_list_or_array,
  gst_value_copy_list_or_array,
  gst_value_list_or_array_peek_pointer,
  "p",
  gst_value_collect_list_or_array,
  "p",
  gst_value_lcopy_list_or_array
};

FUNC_VALUE_GET_TYPE (value_list, "GstValueList");

static const GTypeValueTable _gst_value_array_value_table = {
  gst_value_init_list_or_array,
  gst_value_free_list_or_array,
  gst_value_copy_list_or_array,
  gst_value_list_or_array_peek_pointer,
  "p",
  gst_value_collect_list_or_array,
  "p",
  gst_value_lcopy_list_or_array
};

FUNC_VALUE_GET_TYPE (value_array, "GstValueArray");

static const GTypeValueTable _gst_fraction_value_table = {
  gst_value_init_fraction,
  NULL,
  gst_value_copy_fraction,
  NULL,
  "ii",
  gst_value_collect_fraction,
  "pp",
  gst_value_lcopy_fraction
};

FUNC_VALUE_GET_TYPE (fraction, "GstFraction");


GType
gst_date_get_type (void)
{
  static GType gst_date_type = 0;

  if (!gst_date_type) {
    /* Not using G_TYPE_DATE here on purpose, even if we could
     * if GLIB_CHECK_VERSION(2,8,0) was true: we don't want the
     * serialised strings to have different type strings depending
     * on what version is used, so FIXME in 0.11 when we
     * require GLib-2.8 */
    gst_date_type = g_boxed_type_register_static ("GstDate",
        (GBoxedCopyFunc) gst_date_copy, (GBoxedFreeFunc) g_date_free);
  }

  return gst_date_type;
}

void
_gst_value_initialize (void)
{
  //const GTypeFundamentalInfo finfo = { G_TYPE_FLAG_DERIVABLE, };

  gst_value_table = g_array_new (FALSE, FALSE, sizeof (GstValueTable));
  gst_value_union_funcs = g_array_new (FALSE, FALSE,
      sizeof (GstValueUnionInfo));
  gst_value_intersect_funcs = g_array_new (FALSE, FALSE,
      sizeof (GstValueIntersectInfo));
  gst_value_subtract_funcs = g_array_new (FALSE, FALSE,
      sizeof (GstValueSubtractInfo));

  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_fourcc,
      gst_value_serialize_fourcc,
      gst_value_deserialize_fourcc,
    };

    gst_value.type = gst_fourcc_get_type ();
    gst_value_register (&gst_value);
  }

  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_int_range,
      gst_value_serialize_int_range,
      gst_value_deserialize_int_range,
    };

    gst_value.type = gst_int_range_get_type ();
    gst_value_register (&gst_value);
  }

  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_double_range,
      gst_value_serialize_double_range,
      gst_value_deserialize_double_range,
    };

    gst_value.type = gst_double_range_get_type ();
    gst_value_register (&gst_value);
  }

  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_fraction_range,
      gst_value_serialize_fraction_range,
      gst_value_deserialize_fraction_range,
    };

    gst_value.type = gst_fraction_range_get_type ();
    gst_value_register (&gst_value);
  }

  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_list_or_array,
      gst_value_serialize_list,
      gst_value_deserialize_list,
    };

    gst_value.type = gst_value_list_get_type ();
    gst_value_register (&gst_value);
  }

  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_list_or_array,
      gst_value_serialize_array,
      gst_value_deserialize_array,
    };

    gst_value.type = gst_value_array_get_type ();;
    gst_value_register (&gst_value);
  }

  {
#if 0
    static const GTypeValueTable value_table = {
      gst_value_init_buffer,
      NULL,
      gst_value_copy_buffer,
      NULL,
      "i",
      NULL,                     /*gst_value_collect_buffer, */
      "p",
      NULL                      /*gst_value_lcopy_buffer */
    };
#endif
    static GstValueTable gst_value = {
      0,
      gst_value_compare_buffer,
      gst_value_serialize_buffer,
      gst_value_deserialize_buffer,
    };

    gst_value.type = GST_TYPE_BUFFER;
    gst_value_register (&gst_value);
  }
  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_fraction,
      gst_value_serialize_fraction,
      gst_value_deserialize_fraction,
    };

    gst_value.type = gst_fraction_get_type ();
    gst_value_register (&gst_value);
  }
  {
    static GstValueTable gst_value = {
      0,
      NULL,
      gst_value_serialize_caps,
      gst_value_deserialize_caps,
    };

    gst_value.type = GST_TYPE_CAPS;
    gst_value_register (&gst_value);
  }
  {
    static GstValueTable gst_value = {
      0,
      gst_value_compare_date,
      gst_value_serialize_date,
      gst_value_deserialize_date,
    };

    gst_value.type = gst_date_get_type ();
    gst_value_register (&gst_value);
  }

  REGISTER_SERIALIZATION (G_TYPE_DOUBLE, double);
  REGISTER_SERIALIZATION (G_TYPE_FLOAT, float);

  REGISTER_SERIALIZATION (G_TYPE_STRING, string);
  REGISTER_SERIALIZATION (G_TYPE_BOOLEAN, boolean);
  REGISTER_SERIALIZATION (G_TYPE_ENUM, enum);

  REGISTER_SERIALIZATION (G_TYPE_FLAGS, flags);

  REGISTER_SERIALIZATION (G_TYPE_INT, int);

  REGISTER_SERIALIZATION (G_TYPE_INT64, int64);
  REGISTER_SERIALIZATION (G_TYPE_LONG, long);

  REGISTER_SERIALIZATION (G_TYPE_UINT, uint);
  REGISTER_SERIALIZATION (G_TYPE_UINT64, uint64);
  REGISTER_SERIALIZATION (G_TYPE_ULONG, ulong);

  g_value_register_transform_func (GST_TYPE_FOURCC, G_TYPE_STRING,
      gst_value_transform_fourcc_string);
  g_value_register_transform_func (GST_TYPE_INT_RANGE, G_TYPE_STRING,
      gst_value_transform_int_range_string);
  g_value_register_transform_func (GST_TYPE_DOUBLE_RANGE, G_TYPE_STRING,
      gst_value_transform_double_range_string);
  g_value_register_transform_func (GST_TYPE_FRACTION_RANGE, G_TYPE_STRING,
      gst_value_transform_fraction_range_string);
  g_value_register_transform_func (GST_TYPE_LIST, G_TYPE_STRING,
      gst_value_transform_list_string);
  g_value_register_transform_func (GST_TYPE_ARRAY, G_TYPE_STRING,
      gst_value_transform_array_string);
  g_value_register_transform_func (GST_TYPE_FRACTION, G_TYPE_STRING,
      gst_value_transform_fraction_string);
  g_value_register_transform_func (G_TYPE_STRING, GST_TYPE_FRACTION,
      gst_value_transform_string_fraction);
  g_value_register_transform_func (GST_TYPE_FRACTION, G_TYPE_DOUBLE,
      gst_value_transform_fraction_double);
  g_value_register_transform_func (G_TYPE_DOUBLE, GST_TYPE_FRACTION,
      gst_value_transform_double_fraction);
  g_value_register_transform_func (GST_TYPE_DATE, G_TYPE_STRING,
      gst_value_transform_date_string);
  g_value_register_transform_func (G_TYPE_STRING, GST_TYPE_DATE,
      gst_value_transform_string_date);

  gst_value_register_intersect_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_int_range);
  gst_value_register_intersect_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_range_int_range);
  gst_value_register_intersect_func (G_TYPE_DOUBLE, GST_TYPE_DOUBLE_RANGE,
      gst_value_intersect_double_double_range);
  gst_value_register_intersect_func (GST_TYPE_DOUBLE_RANGE,
      GST_TYPE_DOUBLE_RANGE, gst_value_intersect_double_range_double_range);
  gst_value_register_intersect_func (GST_TYPE_ARRAY,
      GST_TYPE_ARRAY, gst_value_intersect_array);
  gst_value_register_intersect_func (GST_TYPE_FRACTION, GST_TYPE_FRACTION_RANGE,
      gst_value_intersect_fraction_fraction_range);
  gst_value_register_intersect_func (GST_TYPE_FRACTION_RANGE,
      GST_TYPE_FRACTION_RANGE,
      gst_value_intersect_fraction_range_fraction_range);

  gst_value_register_subtract_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_subtract_int_int_range);
  gst_value_register_subtract_func (GST_TYPE_INT_RANGE, G_TYPE_INT,
      gst_value_subtract_int_range_int);
  gst_value_register_subtract_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_subtract_int_range_int_range);
  gst_value_register_subtract_func (G_TYPE_DOUBLE, GST_TYPE_DOUBLE_RANGE,
      gst_value_subtract_double_double_range);
  gst_value_register_subtract_func (GST_TYPE_DOUBLE_RANGE, G_TYPE_DOUBLE,
      gst_value_subtract_double_range_double);
  gst_value_register_subtract_func (GST_TYPE_DOUBLE_RANGE,
      GST_TYPE_DOUBLE_RANGE, gst_value_subtract_double_range_double_range);

  gst_value_register_subtract_func (GST_TYPE_FRACTION, GST_TYPE_FRACTION_RANGE,
      gst_value_subtract_fraction_fraction_range);
  gst_value_register_subtract_func (GST_TYPE_FRACTION_RANGE, GST_TYPE_FRACTION,
      gst_value_subtract_fraction_range_fraction);
  gst_value_register_subtract_func (GST_TYPE_FRACTION_RANGE,
      GST_TYPE_FRACTION_RANGE,
      gst_value_subtract_fraction_range_fraction_range);

#if GLIB_CHECK_VERSION(2,8,0)
  /* see bug #317246, #64994, #65041 */
  {
    volatile GType date_type = G_TYPE_DATE;

    GST_LOG ("Faking out the compiler: %d", date_type);
  }
#endif

  gst_value_register_union_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_union_int_int_range);
  gst_value_register_union_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_union_int_range_int_range);

#if 0
  /* Implement these if needed */
  gst_value_register_union_func (GST_TYPE_FRACTION, GST_TYPE_FRACTION_RANGE,
      gst_value_union_fraction_fraction_range);
  gst_value_register_union_func (GST_TYPE_FRACTION_RANGE,
      GST_TYPE_FRACTION_RANGE, gst_value_union_fraction_range_fraction_range);
#endif
}
