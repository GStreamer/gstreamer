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

GArray *gst_value_compare_funcs;
GArray *gst_value_union_funcs;
GArray *gst_value_intersect_funcs;


static void 
gst_value_init_fourcc (GValue *value)
{
  value->data[0].v_long = 0;
}

static void
gst_value_copy_fourcc (const GValue *src_value, GValue *dest_value)
{
  dest_value->data[0].v_long = src_value->data[0].v_long;
}

static gchar *
gst_value_collect_fourcc (GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  value->data[0].v_long = collect_values[0].v_long;

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

  *fourcc_p = value->data[0].v_long;

  return NULL;
}

void
gst_value_set_fourcc (GValue *value, guint32 fourcc)
{
  g_return_if_fail (GST_VALUE_HOLDS_FOURCC (value));

  value->data[0].v_long = fourcc;
}

guint32
gst_value_get_fourcc (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FOURCC (value), 0);

  return value->data[0].v_long;
}

/* int range */

static void 
gst_value_init_int_range (GValue *value)
{
  value->data[0].v_long = 0;
}

static void
gst_value_copy_int_range (const GValue *src_value, GValue *dest_value)
{
  dest_value->data[0].v_long = src_value->data[0].v_long;
}

static gchar *
gst_value_collect_int_range (GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  value->data[0].v_long = collect_values[0].v_long;

  return NULL;
}

static gchar *
gst_value_lcopy_int_range (const GValue *value, guint n_collect_values,
    GTypeCValue *collect_values, guint collect_flags)
{
  guint32 *int_range_p = collect_values[0].v_pointer;

  if (!int_range_p)
    return g_strdup_printf ("value location for `%s' passed as NULL",
	G_VALUE_TYPE_NAME (value));

  *int_range_p = value->data[0].v_long;

  return NULL;
}

void
gst_value_set_int_range (GValue *value, int start, int end)
{
  g_return_if_fail (GST_VALUE_HOLDS_FOURCC (value));

  value->data[0].v_long = start;
  value->data[1].v_long = end;
}

int
gst_value_get_int_range_min (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FOURCC (value), 0);

  return value->data[0].v_long;
}

int
gst_value_get_int_range_max (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FOURCC (value), 0);

  return value->data[1].v_long;
}

static void
gst_value_transform_fourcc_string (const GValue *src_value,
    GValue *dest_value)
{
  dest_value->data[0].v_pointer = g_strdup_printf(GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS(src_value->data[0].v_long));
}

static void
gst_value_transform_int_range_string (const GValue *src_value,
    GValue *dest_value)
{
  dest_value->data[0].v_pointer = g_strdup_printf("[%d,%d]",
      (int)src_value->data[0].v_long, (int)src_value->data[1].v_long);
}

/* comparison functions */

static int
gst_value_compare_int (const GValue *value1, const GValue *value2)
{
  return value2->data[0].v_int - value1->data[0].v_int;
}

static int
gst_value_compare_double (const GValue *value1, const GValue *value2)
{
  return (value2->data[0].v_double > value1->data[0].v_double) -
    (value2->data[0].v_double < value1->data[0].v_double);
}

static int
gst_value_compare_string (const GValue *value1, const GValue *value2)
{
  return strcmp(value1->data[0].v_pointer, value2->data[0].v_pointer);
}

static int
gst_value_compare_fourcc (const GValue *value1, const GValue *value2)
{
  return value2->data[0].v_int - value1->data[0].v_int;
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

  g_return_val_if_fail(G_VALUE_TYPE(value1) == G_VALUE_TYPE(value2), 0);
  for(i=0;i<gst_value_compare_funcs->len;i++){
    compare_info = &g_array_index(gst_value_compare_funcs,
	GstValueCompareInfo, i);
    if(compare_info->type != G_VALUE_TYPE(value1)) continue;

    return compare_info->func(value1, value2);
  }

  g_return_val_if_fail(0 /* type not found */, 0);
  return 0;
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

void
gst_value_union (GValue *dest, const GValue *value1, const GValue *value2)
{
  GstValueUnionInfo *union_info;
  int i;

  for(i=0;i<gst_value_union_funcs->len;i++){
    union_info = &g_array_index(gst_value_union_funcs, GstValueUnionInfo, i);
    if(union_info->type1 == G_VALUE_TYPE(value1) &&
	union_info->type2 == G_VALUE_TYPE(value2)) {
      union_info->func(dest, value1, value2);
      return;
    }
  }
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

  if (src2->data[0].v_long <= src1->data[0].v_int &&
      src2->data[1].v_long >= src1->data[0].v_int){
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

  min = MAX(src1->data[0].v_long, src2->data[0].v_long);
  max = MIN(src1->data[1].v_long, src2->data[1].v_long);

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

gboolean
gst_value_can_intersect (const GValue *value1, const GValue *value2)
{
  GstValueIntersectInfo *intersect_info;
  int i;

  for(i=0;i<gst_value_intersect_funcs->len;i++){
    intersect_info = &g_array_index(gst_value_intersect_funcs,
	GstValueIntersectInfo, i);
    if(intersect_info->type1 == G_VALUE_TYPE(value1) &&
	intersect_info->type2 == G_VALUE_TYPE(value2)) return TRUE;
  }

  return FALSE;
}

void
gst_value_intersect (GValue *dest, const GValue *value1, const GValue *value2)
{
  GstValueIntersectInfo *intersect_info;
  int i;

  for(i=0;i<gst_value_intersect_funcs->len;i++){
    intersect_info = &g_array_index(gst_value_intersect_funcs,
	GstValueIntersectInfo, i);
    if(intersect_info->type1 == G_VALUE_TYPE(value1) &&
	intersect_info->type2 == G_VALUE_TYPE(value2)) {
      intersect_info->func(dest, value1, value2);
      return;
    }
  }
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
      "i",
      gst_value_collect_int_range,
      "p",
      gst_value_lcopy_int_range
    };
    info.value_table = &value_table;
    gst_type_int_range = g_type_register_static (G_TYPE_BOXED, "GstIntRange", &info, 0);
  }

  g_value_register_transform_func (GST_TYPE_FOURCC, G_TYPE_STRING,
      gst_value_transform_fourcc_string);
  g_value_register_transform_func (GST_TYPE_INT_RANGE, G_TYPE_STRING,
      gst_value_transform_int_range_string);

  gst_value_compare_funcs = g_array_new(FALSE, FALSE,
      sizeof(GstValueCompareInfo));

  gst_value_register_compare_func (G_TYPE_INT, gst_value_compare_int);
  gst_value_register_compare_func (G_TYPE_DOUBLE, gst_value_compare_double);
  gst_value_register_compare_func (G_TYPE_STRING, gst_value_compare_string);
  gst_value_register_compare_func (GST_TYPE_FOURCC, gst_value_compare_fourcc);

  gst_value_union_funcs = g_array_new(FALSE, FALSE,
      sizeof(GstValueUnionInfo));

  gst_value_intersect_funcs = g_array_new(FALSE, FALSE,
      sizeof(GstValueIntersectInfo));

  gst_value_register_intersect_func (G_TYPE_INT, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_int_range);
  gst_value_register_intersect_func (GST_TYPE_INT_RANGE, GST_TYPE_INT_RANGE,
      gst_value_intersect_int_range_int_range);
}


