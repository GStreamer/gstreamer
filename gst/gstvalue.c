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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

GType gst_type_fourcc;
GType gst_type_int_range;
GType gst_type_double_range;
GType gst_type_list;

static GArray *gst_value_table;
static GArray *gst_value_union_funcs;
static GArray *gst_value_intersect_funcs;

/*************************************/
/* list */

static void
gst_value_init_list (GValue * value)
{
  value->data[0].v_pointer = g_array_new (FALSE, TRUE, sizeof (GValue));
}

static GArray *
gst_value_list_array_copy (const GArray * src)
{
  GArray *dest;
  gint i;

  dest = g_array_sized_new (FALSE, TRUE, sizeof (GValue), src->len);
  g_array_set_size (dest, src->len);
  for (i = 0; i < src->len; i++) {
    gst_value_init_and_copy (&g_array_index (dest, GValue, i),
        &g_array_index (src, GValue, i));
  }

  return dest;
}

static void
gst_value_copy_list (const GValue * src_value, GValue * dest_value)
{
  dest_value->data[0].v_pointer =
      gst_value_list_array_copy ((GArray *) src_value->data[0].v_pointer);
}

static void
gst_value_free_list (GValue * value)
{
  gint i;
  GArray *src = (GArray *) value->data[0].v_pointer;

  if ((value->data[1].v_uint & G_VALUE_NOCOPY_CONTENTS) == 0) {
    for (i = 0; i < src->len; i++) {
      g_value_unset (&g_array_index (src, GValue, i));
    }
    g_array_free (src, TRUE);
  }
}

static gpointer
gst_value_list_peek_pointer (const GValue * value)
{
  return value->data[0].v_pointer;
}

static gchar *
gst_value_collect_list (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  if (collect_flags & G_VALUE_NOCOPY_CONTENTS) {
    value->data[0].v_pointer = collect_values[0].v_pointer;
    value->data[1].v_uint = G_VALUE_NOCOPY_CONTENTS;
  } else {
    value->data[0].v_pointer =
        gst_value_list_array_copy ((GArray *) collect_values[0].v_pointer);
  }
  return NULL;
}

static gchar *
gst_value_lcopy_list (const GValue * value, guint n_collect_values,
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
    *dest = gst_value_list_array_copy ((GArray *) value->data[0].v_pointer);
  }
  return NULL;
}

/**
 * gst_value_list_prepend_value:
 * @value: a GstValueList to prepend a value to
 * @prepend_value: the value to prepend
 *
 * Prepends @prepend_value to the GstValueList in @value.
 *
 */
void
gst_value_list_prepend_value (GValue * value, const GValue * prepend_value)
{
  g_return_if_fail (GST_VALUE_HOLDS_LIST (value));

  g_array_prepend_vals ((GArray *) value->data[0].v_pointer, prepend_value, 1);
}

/**
 * gst_value_list_append_value:
 * @value: a GstValueList to append a value to
 * @append_value: the value to append
 *
 * Appends @append_value to the GstValueList in @value.
 */
void
gst_value_list_append_value (GValue * value, const GValue * append_value)
{
  g_return_if_fail (GST_VALUE_HOLDS_LIST (value));

  g_array_append_vals ((GArray *) value->data[0].v_pointer, append_value, 1);
}

/**
 * gst_value_list_get_size:
 * @value: a GstValueList
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
 * @value: a GstValueList
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
 * gst_value_list_concat:
 * @dest: an uninitialized #GValue to take the result
 * @value1: first value to put into the union
 * @value2: second value to put into the union
 *
 * Concatenates copies of value1 and value2 into a list.  The value
 * @dest is initialized to the type GST_TYPE_LIST.
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

static void
gst_value_transform_list_string (const GValue * src_value, GValue * dest_value)
{
  GValue *list_value;
  GArray *array;
  GString *s;
  int i;
  char *list_s;

  array = src_value->data[0].v_pointer;

  s = g_string_new ("{ ");
  for (i = 0; i < array->len; i++) {
    list_value = &g_array_index (array, GValue, i);

    if (i != 0) {
      g_string_append (s, ", ");
    }
    list_s = g_strdup_value_contents (list_value);
    g_string_append (s, list_s);
    g_free (list_s);
  }
  g_string_append (s, " }");

  dest_value->data[0].v_pointer = g_string_free (s, FALSE);
}

static int
gst_value_compare_list (const GValue * value1, const GValue * value2)
{
  int i, j;
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

static char *
gst_value_serialize_list (const GValue * value)
{
  int i;
  GArray *array = value->data[0].v_pointer;
  GString *s;
  GValue *v;
  gchar *s_val;

  s = g_string_new ("{ ");
  for (i = 0; i < array->len; i++) {
    v = &g_array_index (array, GValue, i);
    s_val = gst_value_serialize (v);
    g_string_append (s, s_val);
    g_free (s_val);
    if (i < array->len - 1) {
      g_string_append (s, ", ");
    }
  }
  g_string_append (s, " }");
  return g_string_free (s, FALSE);
}

static gboolean
gst_value_deserialize_list (GValue * dest, const char *s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/*************************************/
/* fourcc */

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
 * @value: a GValue initialized to GST_TYPE_FOURCC
 * @fourcc: the fourcc to set
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
 * @value: a GValue initialized to GST_TYPE_FOURCC
 *
 * Gets the fourcc contained in @value.
 *
 * Returns: the fourcc contained in @value.
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
        g_strdup_printf (GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
  } else {
    dest_value->data[0].v_pointer = g_strdup_printf ("0x%08x", fourcc);
  }
}

static int
gst_value_compare_fourcc (const GValue * value1, const GValue * value2)
{
  if (value2->data[0].v_int == value1->data[0].v_int)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static char *
gst_value_serialize_fourcc (const GValue * value)
{
  guint32 fourcc = value->data[0].v_int;

  if (g_ascii_isalnum ((fourcc >> 0) & 0xff) &&
      g_ascii_isalnum ((fourcc >> 8) & 0xff) &&
      g_ascii_isalnum ((fourcc >> 16) & 0xff) &&
      g_ascii_isalnum ((fourcc >> 24) & 0xff)) {
    return g_strdup_printf (GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
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

/*************************************/
/* int range */

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
  /* FIXME */
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
gst_value_set_int_range (GValue * value, int start, int end)
{
  g_return_if_fail (GST_VALUE_HOLDS_INT_RANGE (value));

  value->data[0].v_int = start;
  value->data[1].v_int = end;
}

/**
 * gst_value_get_int_range_min:
 * @value: a GValue initialized to GST_TYPE_INT_RANGE
 *
 * Gets the minimum of the range specified by @value.
 *
 * Returns: the minumum of the range
 */
int
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
int
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

static int
gst_value_compare_int_range (const GValue * value1, const GValue * value2)
{
  if (value2->data[0].v_int == value1->data[0].v_int &&
      value2->data[0].v_int == value1->data[0].v_int)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static char *
gst_value_serialize_int_range (const GValue * value)
{
  return g_strdup_printf ("[ %d, %d ]", value->data[0].v_int,
      value->data[1].v_int);
}

static gboolean
gst_value_deserialize_int_range (GValue * dest, const char *s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/*************************************/
/* double range */

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
gst_value_set_double_range (GValue * value, double start, double end)
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
double
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
double
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

static int
gst_value_compare_double_range (const GValue * value1, const GValue * value2)
{
  if (value2->data[0].v_double == value1->data[0].v_double &&
      value2->data[0].v_double == value1->data[0].v_double)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static char *
gst_value_serialize_double_range (const GValue * value)
{
  char d1[G_ASCII_DTOSTR_BUF_SIZE];
  char d2[G_ASCII_DTOSTR_BUF_SIZE];

  g_ascii_dtostr (d1, G_ASCII_DTOSTR_BUF_SIZE, value->data[0].v_double);
  g_ascii_dtostr (d2, G_ASCII_DTOSTR_BUF_SIZE, value->data[1].v_double);
  return g_strdup_printf ("[ %s, %s ]", d1, d2);
}

static gboolean
gst_value_deserialize_double_range (GValue * dest, const char *s)
{
  g_warning ("unimplemented");
  return FALSE;
}

/*************************************/
/* GstCaps */

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

/*************************************/
/* GstBuffer */

static char *
gst_value_serialize_buffer (const GValue * value)
{
  guint8 *data;
  int i;
  int size;
  char *string;
  GstBuffer *buffer = g_value_get_boxed (value);

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  string = malloc (size * 2 + 1);
  for (i = 0; i < size; i++) {
    sprintf (string + i * 2, "%02x", data[i]);
  }
  string[size * 2] = 0;

  return string;
}

static gboolean
gst_value_deserialize_buffer (GValue * dest, const char *s)
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
    if (!isxdigit (s[i * 2]) || !isxdigit (s[i * 2 + 1])) {
      ret = FALSE;
      break;
    }
    ts[0] = s[i * 2 + 0];
    ts[1] = s[i * 2 + 1];
    ts[2] = 0;

    data[i] = strtoul (ts, NULL, 16);
  }

  if (ret) {
    g_value_set_boxed (dest, buffer);
    return TRUE;
  } else {
    gst_buffer_unref (buffer);
    return FALSE;
  }
}


/*************************************/
/* boolean */

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
gst_value_deserialize_boolean (GValue * dest, const char *s)
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

/*************************************/
/* int */

static int
gst_value_compare_int (const GValue * value1, const GValue * value2)
{
  if (value1->data[0].v_int > value2->data[0].v_int)
    return GST_VALUE_GREATER_THAN;
  if (value1->data[0].v_int < value2->data[0].v_int)
    return GST_VALUE_LESS_THAN;
  return GST_VALUE_EQUAL;
}

static char *
gst_value_serialize_int (const GValue * value)
{
  return g_strdup_printf ("%d", value->data[0].v_int);
}

static int
gst_strtoi (const char *s, char **end, int base)
{
  int i;

  if (s[0] == '-') {
    i = -(int) strtoul (s + 1, end, base);
  } else {
    i = strtoul (s, end, base);
  }

  return i;
}

static gboolean
gst_value_deserialize_int (GValue * dest, const char *s)
{
  int x;
  char *end;
  gboolean ret = FALSE;

  x = gst_strtoi (s, &end, 0);
  if (*end == 0) {
    ret = TRUE;
  } else {
    if (g_ascii_strcasecmp (s, "little_endian") == 0) {
      x = G_LITTLE_ENDIAN;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "big_endian") == 0) {
      x = G_BIG_ENDIAN;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "byte_order") == 0) {
      x = G_BYTE_ORDER;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "min") == 0) {
      x = G_MININT;
      ret = TRUE;
    } else if (g_ascii_strcasecmp (s, "max") == 0) {
      x = G_MAXINT;
      ret = TRUE;
    }
  }
  if (ret) {
    g_value_set_int (dest, x);
  }
  return ret;
}

/*************************************/
/* double */

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
gst_value_deserialize_double (GValue * dest, const char *s)
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

/*************************************/
/* string */

static int
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
gst_string_wrap (const char *s)
{
  const gchar *t;
  int len;
  gchar *d, *e;
  gboolean wrap = FALSE;

  len = 0;
  t = s;
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
    return strdup (s);

  e = d = g_malloc (len + 3);

  *e++ = '\"';
  t = s;
  while (*t) {
    if (GST_ASCII_IS_STRING (*t)) {
      *e++ = *t++;
    } else if (*t < 0x20 || *t >= 0x7f) {
      *e++ = '\\';
      *e++ = '0' + ((*t) >> 6);
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

static char *
gst_value_serialize_string (const GValue * value)
{
  return gst_string_wrap (value->data[0].v_pointer);
}

static gboolean
gst_value_deserialize_string (GValue * dest, const char *s)
{
  g_value_set_string (dest, s);

  return TRUE;
}

/*************************************/
/* unions */

static gboolean
gst_value_union_int_int_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  g_return_val_if_fail (G_VALUE_TYPE (src1) == G_TYPE_INT, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (src2) == GST_TYPE_INT_RANGE, FALSE);

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
  int min;
  int max;

  g_return_val_if_fail (G_VALUE_TYPE (src1) == GST_TYPE_INT_RANGE, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (src2) == GST_TYPE_INT_RANGE, FALSE);

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

/*************************************/
/* intersection */

static gboolean
gst_value_intersect_int_int_range (GValue * dest, const GValue * src1,
    const GValue * src2)
{
  g_return_val_if_fail (G_VALUE_TYPE (src1) == G_TYPE_INT, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (src2) == GST_TYPE_INT_RANGE, FALSE);

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
  int min;
  int max;

  g_return_val_if_fail (G_VALUE_TYPE (src1) == GST_TYPE_INT_RANGE, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (src2) == GST_TYPE_INT_RANGE, FALSE);

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
  g_return_val_if_fail (G_VALUE_TYPE (src1) == G_TYPE_DOUBLE, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (src2) == GST_TYPE_DOUBLE_RANGE, FALSE);

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
  double min;
  double max;

  g_return_val_if_fail (G_VALUE_TYPE (src1) == GST_TYPE_DOUBLE_RANGE, FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (src2) == GST_TYPE_DOUBLE_RANGE, FALSE);

  min = MAX (src1->data[0].v_double, src2->data[0].v_double);
  max = MIN (src1->data[1].v_double, src2->data[1].v_double);

  if (min < max) {
    g_value_init (dest, GST_TYPE_DOUBLE_RANGE);
    gst_value_set_double_range (dest, min, max);
    return TRUE;
  }
  if (min == max) {
    g_value_init (dest, G_TYPE_DOUBLE);
    g_value_set_int (dest, min);
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

  g_return_val_if_fail (GST_VALUE_HOLDS_LIST (value1), FALSE);

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
      }
      g_value_unset (&intersection);
    }
  }

  return ret;
}


/*************************************/

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
  int i;

  if (G_VALUE_TYPE (value1) != G_VALUE_TYPE (value2))
    return FALSE;
  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->type == G_VALUE_TYPE (value1) && table->compare)
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
  GstValueTable *table;
  int i;

  if (G_VALUE_TYPE (value1) != G_VALUE_TYPE (value2))
    return GST_VALUE_UNORDERED;

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->type != G_VALUE_TYPE (value1) || table->compare == NULL)
      continue;

    return table->compare (value1, value2);
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
  int i;

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
 * Returns: TRUE if the values could be unioned
 */
gboolean
gst_value_union (GValue * dest, const GValue * value1, const GValue * value2)
{
  GstValueUnionInfo *union_info;
  int i;

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

/*
 * gst_value_can_intersect:
 * @value1:
 * @value2:
 *
 * Returns:
 */
gboolean
gst_value_can_intersect (const GValue * value1, const GValue * value2)
{
  GstValueIntersectInfo *intersect_info;
  int i;

  /* special cases */
  if (GST_VALUE_HOLDS_LIST (value1) || GST_VALUE_HOLDS_LIST (value2))
    return TRUE;

  for (i = 0; i < gst_value_intersect_funcs->len; i++) {
    intersect_info = &g_array_index (gst_value_intersect_funcs,
        GstValueIntersectInfo, i);
    if (intersect_info->type1 == G_VALUE_TYPE (value1) &&
        intersect_info->type2 == G_VALUE_TYPE (value2))
      return TRUE;
  }

  return gst_value_can_compare (value1, value2);
}

/**
 * gst_value_intersect:
 * @dest: the destination value for intersection
 * @value1: a value to intersect
 * @value2: another value to intersect
 *
 * Calculates the intersection of the two values.
 *
 * Returns: TRUE if the intersection is non-empty
 */
gboolean
gst_value_intersect (GValue * dest, const GValue * value1,
    const GValue * value2)
{
  GstValueIntersectInfo *intersect_info;
  int i;
  int ret = FALSE;

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
 * gst_value_register_intersection_func:
 * @type1:
 * @type2:
 * @func:
 *
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

/*
 * gst_value_register:
 * @table:
 *
 */
void
gst_value_register (const GstValueTable * table)
{
  g_array_append_val (gst_value_table, *table);
}

/*
 * gst_value_init_and_copy:
 * @dest:
 * @src:
 *
 */
void
gst_value_init_and_copy (GValue * dest, const GValue * src)
{
  g_value_init (dest, G_VALUE_TYPE (src));
  g_value_copy (src, dest);
}

/*
 * gst_value_serialize:
 * @value:
 *
 * Returns:
 */
gchar *
gst_value_serialize (const GValue * value)
{
  int i;
  GValue s_val = { 0 };
  GstValueTable *table;
  char *s;

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->type != G_VALUE_TYPE (value) || table->serialize == NULL)
      continue;

    return table->serialize (value);
  }

  g_value_init (&s_val, G_TYPE_STRING);
  g_value_transform (value, &s_val);
  s = gst_string_wrap (g_value_get_string (&s_val));
  g_value_unset (&s_val);

  return s;
}

/*
 * gst_value_deserialize:
 * @dest:
 * @src:
 *
 * Returns:
 */
gboolean
gst_value_deserialize (GValue * dest, const gchar * src)
{
  GstValueTable *table;
  int i;

  for (i = 0; i < gst_value_table->len; i++) {
    table = &g_array_index (gst_value_table, GstValueTable, i);
    if (table->type != G_VALUE_TYPE (dest) || table->deserialize == NULL)
      continue;

    return table->deserialize (dest, src);
  }

  return FALSE;
}

/*
 * gst_type_is_fixed:
 * @type:
 *
 * Returns:
 */
gboolean
gst_type_is_fixed (GType type)
{
  if (type == GST_TYPE_INT_RANGE || type == GST_TYPE_DOUBLE_RANGE ||
      type == GST_TYPE_LIST) {
    return FALSE;
  }
  if (G_TYPE_IS_FUNDAMENTAL (type) &&
      type < G_TYPE_MAKE_FUNDAMENTAL (G_TYPE_RESERVED_GLIB_LAST)) {
    return TRUE;
  }
  if (type == GST_TYPE_BUFFER || type == GST_TYPE_FOURCC) {
    return TRUE;
  }
  return FALSE;
}

void
_gst_value_initialize (void)
{
  GTypeInfo info = {
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
  GTypeFundamentalInfo finfo = {
    0
  };

  //const GTypeFundamentalInfo finfo = { G_TYPE_FLAG_DERIVABLE, };

  gst_value_table = g_array_new (FALSE, FALSE, sizeof (GstValueTable));
  gst_value_union_funcs = g_array_new (FALSE, FALSE,
      sizeof (GstValueUnionInfo));
  gst_value_intersect_funcs = g_array_new (FALSE, FALSE,
      sizeof (GstValueIntersectInfo));

  {
    static const GTypeValueTable value_table = {
      gst_value_init_fourcc,
      NULL,
      gst_value_copy_fourcc,
      NULL,
      "i",
      gst_value_collect_fourcc,
      "p",
      gst_value_lcopy_fourcc
    };
    static GstValueTable gst_value = {
      0,
      gst_value_compare_fourcc,
      gst_value_serialize_fourcc,
      gst_value_deserialize_fourcc,
    };

    info.value_table = &value_table;
    gst_type_fourcc = g_type_register_fundamental (g_type_fundamental_next (),
        "GstFourcc", &info, &finfo, 0);
    gst_value.type = gst_type_fourcc;
    gst_value_register (&gst_value);
  }

  {
    static const GTypeValueTable value_table = {
      gst_value_init_int_range,
      NULL,
      gst_value_copy_int_range,
      NULL,
      "ii",
      gst_value_collect_int_range,
      "pp",
      gst_value_lcopy_int_range
    };
    static GstValueTable gst_value = {
      0,
      gst_value_compare_int_range,
      gst_value_serialize_int_range,
      gst_value_deserialize_int_range,
    };

    info.value_table = &value_table;
    gst_type_int_range =
        g_type_register_fundamental (g_type_fundamental_next (), "GstIntRange",
        &info, &finfo, 0);
    gst_value.type = gst_type_int_range;
    gst_value_register (&gst_value);
  }

  {
    static const GTypeValueTable value_table = {
      gst_value_init_double_range,
      NULL,
      gst_value_copy_double_range,
      NULL,
      "dd",
      gst_value_collect_double_range,
      "pp",
      gst_value_lcopy_double_range
    };
    static GstValueTable gst_value = {
      0,
      gst_value_compare_double_range,
      gst_value_serialize_double_range,
      gst_value_deserialize_double_range,
    };

    info.value_table = &value_table;
    gst_type_double_range =
        g_type_register_fundamental (g_type_fundamental_next (),
        "GstDoubleRange", &info, &finfo, 0);
    gst_value.type = gst_type_double_range;
    gst_value_register (&gst_value);
  }

  {
    static const GTypeValueTable value_table = {
      gst_value_init_list,
      gst_value_free_list,
      gst_value_copy_list,
      gst_value_list_peek_pointer,
      "p",
      gst_value_collect_list,
      "p",
      gst_value_lcopy_list
    };
    static GstValueTable gst_value = {
      0,
      gst_value_compare_list,
      gst_value_serialize_list,
      gst_value_deserialize_list,
    };

    info.value_table = &value_table;
    gst_type_list = g_type_register_fundamental (g_type_fundamental_next (),
        "GstValueList", &info, &finfo, 0);
    gst_value.type = gst_type_list;
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
      NULL,                     /*gst_value_compare_buffer, */
      gst_value_serialize_buffer,
      gst_value_deserialize_buffer,
    };

#if 0
    info.value_table = &value_table;
    gst_type_fourcc =
        g_type_register_static (G_TYPE_BOXED, "GstFourcc", &info, 0);
#endif
    gst_value.type = GST_TYPE_BUFFER;
    gst_value_register (&gst_value);
  }

  {
    static const GstValueTable gst_value = {
      G_TYPE_INT,
      gst_value_compare_int,
      gst_value_serialize_int,
      gst_value_deserialize_int,
    };

    gst_value_register (&gst_value);
  }

  {
    static const GstValueTable gst_value = {
      G_TYPE_DOUBLE,
      gst_value_compare_double,
      gst_value_serialize_double,
      gst_value_deserialize_double,
    };

    gst_value_register (&gst_value);
  }

  {
    static const GstValueTable gst_value = {
      G_TYPE_STRING,
      gst_value_compare_string,
      gst_value_serialize_string,
      gst_value_deserialize_string,
    };

    gst_value_register (&gst_value);
  }

  {
    static const GstValueTable gst_value = {
      G_TYPE_BOOLEAN,
      gst_value_compare_boolean,
      gst_value_serialize_boolean,
      gst_value_deserialize_boolean,
    };

    gst_value_register (&gst_value);
  }

  g_value_register_transform_func (GST_TYPE_FOURCC, G_TYPE_STRING,
      gst_value_transform_fourcc_string);
  g_value_register_transform_func (GST_TYPE_INT_RANGE, G_TYPE_STRING,
      gst_value_transform_int_range_string);
  g_value_register_transform_func (GST_TYPE_DOUBLE_RANGE, G_TYPE_STRING,
      gst_value_transform_double_range_string);
  g_value_register_transform_func (GST_TYPE_LIST, G_TYPE_STRING,
      gst_value_transform_list_string);

  gst_value_register_intersect_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_int_range);
  gst_value_register_intersect_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_range_int_range);
  gst_value_register_intersect_func (G_TYPE_DOUBLE, GST_TYPE_DOUBLE_RANGE,
      gst_value_intersect_double_double_range);
  gst_value_register_intersect_func (GST_TYPE_DOUBLE_RANGE,
      GST_TYPE_DOUBLE_RANGE, gst_value_intersect_double_range_double_range);

  gst_value_register_union_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_union_int_int_range);
  gst_value_register_union_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_union_int_range_int_range);
}
