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
#include <string.h>

#include <gst/gst.h>
#include <gobject/gvaluecollector.h>

typedef struct _GstValueCompareInfo GstValueCompareInfo;
struct _GstValueCompareInfo {
  GType type;
  GstValueCompareFunc func;
};

typedef struct _GstValueUnionInfo GstValueUnionInfo;
struct _GstValueUnionInfo {
  GType type1;
  GType type2;
  GstValueUnionFunc func;
};

typedef struct _GstValueIntersectInfo GstValueIntersectInfo;
struct _GstValueIntersectInfo {
  GType type1;
  GType type2;
  GstValueIntersectFunc func;
};

GType gst_type_fourcc;
GType gst_type_int_range;
GType gst_type_double_range;
GType gst_type_list;

GArray *gst_value_compare_funcs;
GArray *gst_value_union_funcs;
GArray *gst_value_intersect_funcs;

/* list */

static void
gst_value_init_list (GValue *value)
{
  value->data[0].v_pointer = g_array_new (FALSE, TRUE, sizeof(GValue));
}

static GArray *
gst_value_list_array_copy (const GArray *src)
{
  GArray *dest;
  gint i;
  
  dest = g_array_sized_new (FALSE, TRUE, sizeof(GValue), src->len);
  g_array_set_size (dest, src->len);
  for (i = 0; i < src->len; i++) {
    g_value_init (&g_array_index(dest, GValue, i), G_VALUE_TYPE (&g_array_index(src, GValue, i)));
    g_value_copy (&g_array_index(src, GValue, i), &g_array_index(dest, GValue, i));
  }

  return dest;
}

static void
gst_value_copy_list (const GValue *src_value, GValue *dest_value)
{
  dest_value->data[0].v_pointer = gst_value_list_array_copy ((GArray *) src_value->data[0].v_pointer);
}

static void
gst_value_free_list (GValue *value)
{
  gint i;
  GArray *src = (GArray *) value->data[0].v_pointer;
  
  if ((value->data[1].v_uint & G_VALUE_NOCOPY_CONTENTS) == 0) {
    for (i = 0; i < src->len; i++) {
      g_value_unset (&g_array_index(src, GValue, i));
    }
    g_array_free (src, TRUE);
  }
}

static gpointer
gst_value_list_peek_pointer (const GValue *value)
{
  return value->data[0].v_pointer;
}

static gchar *
gst_value_collect_list (GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  if (collect_flags & G_VALUE_NOCOPY_CONTENTS) {
    value->data[0].v_pointer = collect_values[0].v_pointer;
    value->data[1].v_uint = G_VALUE_NOCOPY_CONTENTS;
  } else {
    value->data[0].v_pointer = gst_value_list_array_copy ((GArray *) collect_values[0].v_pointer);
  }
  return NULL;
}

static gchar *
gst_value_lcopy_list (const GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
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

void 
gst_value_list_prepend_value (GValue *value, const GValue *prepend_value)
{
  g_return_if_fail (GST_VALUE_HOLDS_LIST (value));

  g_array_prepend_vals ((GArray *) value->data[0].v_pointer, prepend_value, 1);
}

void 
gst_value_list_append_value (GValue *value, const GValue *append_value)
{
  g_return_if_fail (GST_VALUE_HOLDS_LIST (value));

  g_array_append_vals ((GArray *) value->data[0].v_pointer, append_value, 1);
}

guint 
gst_value_list_get_size (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_LIST (value), 0);

  return ((GArray *) value->data[0].v_pointer)->len;
}

const GValue *
gst_value_list_get_value (const GValue *value, guint index)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_LIST (value), NULL);
  g_return_val_if_fail (index < gst_value_list_get_size (value), NULL);

  return (const GValue *) &g_array_index ((GArray *) value->data[0].v_pointer, GValue, index);
}

/**
 * gst_value_list_concat:
 * @dest: an uninitialized #GValue to take the result
 * @value1: first value to put into the union
 * @value2: second value to put into the union
 *
 * Concatenates copies of value1 and value2 into a list. dest will be 
 * initialized to the type GST_TYPE_LIST.
 */
void
gst_value_list_concat (GValue *dest, const GValue *value1, const GValue *value2)
{
  guint i, value1_length, value2_length;
  GArray *array;

  g_return_if_fail (dest != NULL);
  g_return_if_fail (G_VALUE_TYPE (dest) == 0);
  g_return_if_fail (G_IS_VALUE (value1));
  g_return_if_fail (G_IS_VALUE (value2));
  
  value1_length = (GST_VALUE_HOLDS_LIST (value1) ? gst_value_list_get_size (value1) : 1);
  value2_length = (GST_VALUE_HOLDS_LIST (value2) ? gst_value_list_get_size (value2) : 1);
  g_value_init (dest, GST_TYPE_LIST);
  array = (GArray *) dest->data[0].v_pointer;
  g_array_set_size (array, value1_length + value2_length);
  
  if (GST_VALUE_HOLDS_LIST (value1)) {
    for (i = 0; i < value1_length; i++) {
      g_value_init (&g_array_index(array, GValue, i), G_VALUE_TYPE (gst_value_list_get_value (value1, i)));
      g_value_copy (gst_value_list_get_value (value1, i), &g_array_index(array, GValue, i));
    }
  } else {
    g_value_init (&g_array_index(array, GValue, 0), G_VALUE_TYPE (value1));
    g_value_copy (value1, &g_array_index(array, GValue, 0));
  }
  
  if (GST_VALUE_HOLDS_LIST (value2)) {
    for (i = 0; i < value2_length; i++) {
      g_value_init (&g_array_index(array, GValue, i + value1_length), G_VALUE_TYPE (gst_value_list_get_value (value2, i)));
      g_value_copy (gst_value_list_get_value (value2, i), &g_array_index(array, GValue, i + value1_length));
    }
  } else {
    g_value_init (&g_array_index(array, GValue, value1_length), G_VALUE_TYPE (value2));
    g_value_copy (value2, &g_array_index(array, GValue, value1_length));
  }
}

/* fourcc */

static void 
gst_value_init_fourcc (GValue *value)
{
  value->data[0].v_int = 0;
}

static void
gst_value_copy_fourcc (const GValue *src_value, GValue *dest_value)
{
  dest_value->data[0].v_int = src_value->data[0].v_int;
}

static gchar *
gst_value_collect_fourcc (GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  value->data[0].v_int = collect_values[0].v_int;

  return NULL;
}

static gchar *
gst_value_lcopy_fourcc (const GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  guint32 *fourcc_p = collect_values[0].v_pointer;

  if (!fourcc_p)
    return g_strdup_printf ("value location for `%s' passed as NULL",
	G_VALUE_TYPE_NAME (value));

  *fourcc_p = value->data[0].v_int;

  return NULL;
}

void
gst_value_set_fourcc (GValue *value, guint32 fourcc)
{
  g_return_if_fail (GST_VALUE_HOLDS_FOURCC (value));

  value->data[0].v_int = fourcc;
}

guint32
gst_value_get_fourcc (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FOURCC (value), 0);

  return value->data[0].v_int;
}

/* int range */

static void 
gst_value_init_int_range (GValue *value)
{
  value->data[0].v_int = 0;
  value->data[1].v_int = 0;
}

static void
gst_value_copy_int_range (const GValue *src_value, GValue *dest_value)
{
  dest_value->data[0].v_int = src_value->data[0].v_int;
  dest_value->data[1].v_int = src_value->data[1].v_int;
}

static gchar *
gst_value_collect_int_range (GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  /* FIXME */
  value->data[0].v_int = collect_values[0].v_int;
  value->data[1].v_int = collect_values[1].v_int;

  return NULL;
}

static gchar *
gst_value_lcopy_int_range (const GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
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

void
gst_value_set_int_range (GValue *value, int start, int end)
{
  g_return_if_fail (GST_VALUE_HOLDS_INT_RANGE (value));

  value->data[0].v_int = start;
  value->data[1].v_int = end;
}

int
gst_value_get_int_range_min (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_INT_RANGE (value), 0);

  return value->data[0].v_int;
}

int
gst_value_get_int_range_max (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_INT_RANGE (value), 0);

  return value->data[1].v_int;
}

/* double range */

static void 
gst_value_init_double_range (GValue *value)
{
  value->data[0].v_double = 0;
  value->data[1].v_double = 0;
}

static void
gst_value_copy_double_range (const GValue *src_value, GValue *dest_value)
{
  dest_value->data[0].v_double = src_value->data[0].v_double;
  dest_value->data[1].v_double = src_value->data[1].v_double;
}

static gchar *
gst_value_collect_double_range (GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  value->data[0].v_double = collect_values[0].v_double;
  value->data[1].v_double = collect_values[1].v_double;

  return NULL;
}

static gchar *
gst_value_lcopy_double_range (const GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
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

void
gst_value_set_double_range (GValue *value, double start, double end)
{
  g_return_if_fail (GST_VALUE_HOLDS_DOUBLE_RANGE (value));

  value->data[0].v_double = start;
  value->data[1].v_double = end;
}

double
gst_value_get_double_range_min (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_DOUBLE_RANGE (value), 0);

  return value->data[0].v_double;
}

double
gst_value_get_double_range_max (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_DOUBLE_RANGE (value), 0);

  return value->data[1].v_double;
}

/* GstCaps */

void
gst_value_set_caps (GValue *value, const GstCaps *caps)
{
  g_return_if_fail (GST_VALUE_HOLDS_CAPS (value));

  value->data[0].v_pointer = gst_caps_copy (caps);
}

const GstCaps *
gst_value_get_caps (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_CAPS (value), 0);

  return value->data[0].v_pointer;
}

/* fourcc */

static void
gst_value_transform_fourcc_string (const GValue *src_value,
    GValue *dest_value)
{
  guint32 fourcc = src_value->data[0].v_int;

  if (g_ascii_isprint ((fourcc>>0) & 0xff) &&
      g_ascii_isprint ((fourcc>>8) & 0xff) &&
      g_ascii_isprint ((fourcc>>16) & 0xff) &&
      g_ascii_isprint ((fourcc>>24) & 0xff)){
    dest_value->data[0].v_pointer = g_strdup_printf(
	GST_FOURCC_FORMAT, GST_FOURCC_ARGS(fourcc));
  } else {
    dest_value->data[0].v_pointer = g_strdup_printf("0x%08x", fourcc);
  }
}

static void
gst_value_transform_int_range_string (const GValue *src_value,
    GValue *dest_value)
{
  dest_value->data[0].v_pointer = g_strdup_printf("[%d,%d]",
      (int)src_value->data[0].v_int, (int)src_value->data[1].v_int);
}

static void
gst_value_transform_double_range_string (const GValue *src_value,
    GValue *dest_value)
{
  dest_value->data[0].v_pointer = g_strdup_printf("[%g,%g]",
      src_value->data[0].v_double, src_value->data[1].v_double);
}

static void
gst_value_transform_list_string (const GValue *src_value,
    GValue *dest_value)
{
  GValue *list_value;
  GArray *array;
  GString *s;
  int i;
  char *list_s;

  array = src_value->data[0].v_pointer;

  s = g_string_new("{ ");
  for(i=0;i<array->len;i++){
    list_value = &g_array_index(array, GValue, i);

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

/* comparison functions */

static int
gst_value_compare_boolean (const GValue *value1, const GValue *value2)
{
  /* FIXME: ds, can we assume that those values are only TRUE and FALSE? */
  if (value1->data[0].v_int == value2->data[0].v_int)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static int
gst_value_compare_int (const GValue *value1, const GValue *value2)
{
  if (value1->data[0].v_int > value2->data[0].v_int)
    return GST_VALUE_GREATER_THAN;
  if (value1->data[0].v_int < value2->data[0].v_int)
    return GST_VALUE_LESS_THAN;
  return GST_VALUE_EQUAL;
}

static int
gst_value_compare_double (const GValue *value1, const GValue *value2)
{
  if (value1->data[0].v_double > value2->data[0].v_double)
    return GST_VALUE_GREATER_THAN;
  if (value1->data[0].v_double < value2->data[0].v_double)
    return GST_VALUE_LESS_THAN;
  if (value1->data[0].v_double == value2->data[0].v_double)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static int
gst_value_compare_float (const GValue *value1, const GValue *value2)
{
  if (value1->data[0].v_float > value2->data[0].v_float)
    return GST_VALUE_GREATER_THAN;
  if (value1->data[0].v_float < value2->data[0].v_float)
    return GST_VALUE_LESS_THAN;
  if (value1->data[0].v_float == value2->data[0].v_float)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static int
gst_value_compare_string (const GValue *value1, const GValue *value2)
{
  int x = strcmp(value1->data[0].v_pointer, value2->data[0].v_pointer);
  if(x<0) return GST_VALUE_LESS_THAN;
  if(x>0) return GST_VALUE_GREATER_THAN;
  return GST_VALUE_EQUAL;
}

static int
gst_value_compare_fourcc (const GValue *value1, const GValue *value2)
{
  if (value2->data[0].v_int == value1->data[0].v_int) return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static int
gst_value_compare_int_range (const GValue *value1, const GValue *value2)
{
  if (value2->data[0].v_int == value1->data[0].v_int &&
      value2->data[0].v_int == value1->data[0].v_int) return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static int
gst_value_compare_double_range (const GValue *value1, const GValue *value2)
{
  if (value2->data[0].v_double == value1->data[0].v_double &&
      value2->data[0].v_double == value1->data[0].v_double)
    return GST_VALUE_EQUAL;
  return GST_VALUE_UNORDERED;
}

static int
gst_value_compare_list (const GValue *value1, const GValue *value2)
{
  int i,j;
  GArray *array1 = value1->data[0].v_pointer;
  GArray *array2 = value2->data[0].v_pointer;
  GValue *v1;
  GValue *v2;

  if (array1->len != array2->len) return GST_VALUE_UNORDERED;

  for(i=0;i<array1->len;i++){
    v1 = &g_array_index (array1, GValue, i);
    for(j=0;j<array1->len;j++){
      v2 = &g_array_index (array2, GValue, j);
      if (gst_value_compare(v1, v2) == GST_VALUE_EQUAL) break;
    }
    if (j==array1->len) {
      return GST_VALUE_UNORDERED;
    }
  }

  return GST_VALUE_EQUAL;
}

gboolean
gst_value_can_compare (const GValue *value1, const GValue *value2)
{
  GstValueCompareInfo *compare_info;
  int i;

  if(G_VALUE_TYPE(value1) != G_VALUE_TYPE(value2))return FALSE;
  for(i=0;i<gst_value_compare_funcs->len;i++){
    compare_info = &g_array_index(gst_value_compare_funcs,
	GstValueCompareInfo, i);
    if(compare_info->type == G_VALUE_TYPE(value1)) return TRUE;
  }

  return FALSE;
}

int
gst_value_compare (const GValue *value1, const GValue *value2)
{
  GstValueCompareInfo *compare_info;
  int i;

  if (G_VALUE_TYPE(value1) != G_VALUE_TYPE(value2)) return GST_VALUE_UNORDERED;

  for(i=0;i<gst_value_compare_funcs->len;i++){
    compare_info = &g_array_index(gst_value_compare_funcs,
	GstValueCompareInfo, i);
    if(compare_info->type != G_VALUE_TYPE(value1)) continue;

    return compare_info->func(value1, value2);
  }

  g_critical("unable to compare values of type %s\n",
      g_type_name (G_VALUE_TYPE (value1)));
  return GST_VALUE_UNORDERED;
}

void
gst_value_register_compare_func (GType type, GstValueCompareFunc func)
{
  GstValueCompareInfo compare_info;

  compare_info.type = type;
  compare_info.func = func;

  g_array_append_val(gst_value_compare_funcs, compare_info);
}

/* union */

gboolean
gst_value_can_union (const GValue *value1, const GValue *value2)
{
  GstValueUnionInfo *union_info;
  int i;

  for(i=0;i<gst_value_union_funcs->len;i++){
    union_info = &g_array_index(gst_value_union_funcs, GstValueUnionInfo, i);
    if(union_info->type1 == G_VALUE_TYPE(value1) &&
	union_info->type2 == G_VALUE_TYPE(value2)) return TRUE;
  }

  return FALSE;
}

gboolean
gst_value_union (GValue *dest, const GValue *value1, const GValue *value2)
{
  GstValueUnionInfo *union_info;
  int i;

  for(i=0;i<gst_value_union_funcs->len;i++){
    union_info = &g_array_index(gst_value_union_funcs, GstValueUnionInfo, i);
    if(union_info->type1 == G_VALUE_TYPE(value1) &&
	union_info->type2 == G_VALUE_TYPE(value2)) {
      return union_info->func(dest, value1, value2);
    }
  }
  
  gst_value_list_concat (dest, value1, value2);
  return TRUE;
}

void
gst_value_register_union_func (GType type1, GType type2, GstValueUnionFunc func)
{
  GstValueUnionInfo union_info;

  union_info.type1 = type1;
  union_info.type2 = type2;
  union_info.func = func;

  g_array_append_val(gst_value_union_funcs, union_info);
}

/* intersection */

static gboolean
gst_value_intersect_int_int_range (GValue *dest, const GValue *src1,
    const GValue *src2)
{
  g_return_val_if_fail(G_VALUE_TYPE(src1) == G_TYPE_INT, FALSE);
  g_return_val_if_fail(G_VALUE_TYPE(src2) == GST_TYPE_INT_RANGE, FALSE);

  if (src2->data[0].v_int <= src1->data[0].v_int &&
      src2->data[1].v_int >= src1->data[0].v_int){
    g_value_init(dest, G_TYPE_INT);
    g_value_copy(src1, dest);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_int_range_int_range (GValue *dest, const GValue *src1,
    const GValue *src2)
{
  int min;
  int max;

  g_return_val_if_fail(G_VALUE_TYPE(src1) == GST_TYPE_INT_RANGE, FALSE);
  g_return_val_if_fail(G_VALUE_TYPE(src2) == GST_TYPE_INT_RANGE, FALSE);

  min = MAX(src1->data[0].v_int, src2->data[0].v_int);
  max = MIN(src1->data[1].v_int, src2->data[1].v_int);

  if(min < max){
    g_value_init(dest, GST_TYPE_INT_RANGE);
    gst_value_set_int_range(dest, min, max);
    return TRUE;
  }
  if(min == max){
    g_value_init(dest, G_TYPE_INT);
    g_value_set_int(dest, min);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_double_double_range (GValue *dest, const GValue *src1,
    const GValue *src2)
{
  g_return_val_if_fail(G_VALUE_TYPE(src1) == G_TYPE_DOUBLE, FALSE);
  g_return_val_if_fail(G_VALUE_TYPE(src2) == GST_TYPE_DOUBLE_RANGE, FALSE);

  if (src2->data[0].v_double <= src1->data[0].v_double &&
      src2->data[1].v_double >= src1->data[0].v_double){
    g_value_init(dest, G_TYPE_DOUBLE);
    g_value_copy(src1, dest);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_double_range_double_range (GValue *dest, const GValue *src1,
    const GValue *src2)
{
  double min;
  double max;

  g_return_val_if_fail(G_VALUE_TYPE(src1) == GST_TYPE_DOUBLE_RANGE, FALSE);
  g_return_val_if_fail(G_VALUE_TYPE(src2) == GST_TYPE_DOUBLE_RANGE, FALSE);

  min = MAX(src1->data[0].v_double, src2->data[0].v_double);
  max = MIN(src1->data[1].v_double, src2->data[1].v_double);

  if(min < max){
    g_value_init(dest, GST_TYPE_DOUBLE_RANGE);
    gst_value_set_double_range(dest, min, max);
    return TRUE;
  }
  if(min == max){
    g_value_init(dest, G_TYPE_DOUBLE);
    g_value_set_int(dest, min);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_value_intersect_list (GValue *dest, const GValue *value1, const GValue *value2)
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
	g_value_init (dest, G_VALUE_TYPE(&intersection));
	g_value_copy (dest, &intersection);
	ret = TRUE;
      } else if (GST_VALUE_HOLDS_LIST (dest)) {
	gst_value_list_append_value (dest, &intersection);
      } else {
	GValue temp = {0, };

	g_value_init (&temp, G_VALUE_TYPE(dest));
	g_value_copy (dest, &temp);
	g_value_unset (dest);
	gst_value_list_concat (dest, &temp, &intersection);
      }
      g_value_unset (&intersection);
    }
  }

  return ret;
}

gboolean
gst_value_can_intersect (const GValue *value1, const GValue *value2)
{
  GstValueIntersectInfo *intersect_info;
  int i;

  /* special cases */
  if (GST_VALUE_HOLDS_LIST (value1) || 
      GST_VALUE_HOLDS_LIST (value2))
    return TRUE;

  for(i=0;i<gst_value_intersect_funcs->len;i++){
    intersect_info = &g_array_index(gst_value_intersect_funcs,
	GstValueIntersectInfo, i);
    if(intersect_info->type1 == G_VALUE_TYPE(value1) &&
	intersect_info->type2 == G_VALUE_TYPE(value2)) return TRUE;
  }

  return gst_value_can_compare (value1, value2);
}

gboolean
gst_value_intersect (GValue *dest, const GValue *value1, const GValue *value2)
{
  GstValueIntersectInfo *intersect_info;
  int i;
  int ret = FALSE;

  /* special cases first */
  if (GST_VALUE_HOLDS_LIST (value1))
    return gst_value_intersect_list (dest, value1, value2);
  if (GST_VALUE_HOLDS_LIST (value2))
    return gst_value_intersect_list (dest, value2, value1);
    
  for(i=0;i<gst_value_intersect_funcs->len;i++){
    intersect_info = &g_array_index(gst_value_intersect_funcs,
	GstValueIntersectInfo, i);
    if(intersect_info->type1 == G_VALUE_TYPE(value1) &&
	intersect_info->type2 == G_VALUE_TYPE(value2)) {
      ret = intersect_info->func(dest, value1, value2);
      return ret;
    }
    if(intersect_info->type1 == G_VALUE_TYPE(value2) &&
	intersect_info->type2 == G_VALUE_TYPE(value1)) {
      ret = intersect_info->func(dest, value2, value1);
      return ret;
    }
  }

  if(gst_value_compare(value1, value2) == GST_VALUE_EQUAL){
    g_value_init(dest, G_VALUE_TYPE(value1));
    g_value_copy(value1, dest);
    ret = TRUE;
  }

  return ret;
}

void
gst_value_register_intersect_func (GType type1, GType type2,
    GstValueIntersectFunc func)
{
  GstValueIntersectInfo intersect_info;

  intersect_info.type1 = type1;
  intersect_info.type2 = type2;
  intersect_info.func = func;

  g_array_append_val(gst_value_intersect_funcs, intersect_info);
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
  //const GTypeFundamentalInfo finfo = { G_TYPE_FLAG_DERIVABLE, };

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
    info.value_table = &value_table;
    gst_type_fourcc = g_type_register_static (G_TYPE_BOXED, "GstFourcc", &info, 0);
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
    info.value_table = &value_table;
    gst_type_int_range = g_type_register_static (G_TYPE_BOXED, "GstIntRange", &info, 0);
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
    info.value_table = &value_table;
    gst_type_double_range = g_type_register_static (G_TYPE_BOXED, "GstDoubleRange", &info, 0);
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
    info.value_table = &value_table;
    gst_type_list = g_type_register_static (G_TYPE_BOXED, "GstValueList", &info, 0);
  }

  g_value_register_transform_func (GST_TYPE_FOURCC, G_TYPE_STRING,
      gst_value_transform_fourcc_string);
  g_value_register_transform_func (GST_TYPE_INT_RANGE, G_TYPE_STRING,
      gst_value_transform_int_range_string);
  g_value_register_transform_func (GST_TYPE_DOUBLE_RANGE, G_TYPE_STRING,
      gst_value_transform_double_range_string);
  g_value_register_transform_func (GST_TYPE_LIST, G_TYPE_STRING,
      gst_value_transform_list_string);

  gst_value_compare_funcs = g_array_new(FALSE, FALSE,
      sizeof(GstValueCompareInfo));

  gst_value_register_compare_func (G_TYPE_BOOLEAN, gst_value_compare_boolean);
  gst_value_register_compare_func (G_TYPE_INT, gst_value_compare_int);
  gst_value_register_compare_func (G_TYPE_FLOAT, gst_value_compare_float);
  gst_value_register_compare_func (G_TYPE_DOUBLE, gst_value_compare_double);
  gst_value_register_compare_func (G_TYPE_STRING, gst_value_compare_string);
  gst_value_register_compare_func (GST_TYPE_FOURCC, gst_value_compare_fourcc);
  gst_value_register_compare_func (GST_TYPE_INT_RANGE, gst_value_compare_int_range);
  gst_value_register_compare_func (GST_TYPE_DOUBLE_RANGE, gst_value_compare_double_range);
  gst_value_register_compare_func (GST_TYPE_LIST, gst_value_compare_list);

  gst_value_union_funcs = g_array_new(FALSE, FALSE,
      sizeof(GstValueUnionInfo));

  gst_value_intersect_funcs = g_array_new(FALSE, FALSE,
      sizeof(GstValueIntersectInfo));

  gst_value_register_intersect_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_int_range);
  gst_value_register_intersect_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_range_int_range);
  gst_value_register_intersect_func (G_TYPE_DOUBLE, GST_TYPE_DOUBLE_RANGE,
      gst_value_intersect_double_double_range);
  gst_value_register_intersect_func (GST_TYPE_DOUBLE_RANGE, GST_TYPE_DOUBLE_RANGE,
      gst_value_intersect_double_range_double_range);
}


