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

#include <gst/gst.h>
#include <gobject/gvaluecollector.h>


GType gst_type_fourcc;
GType gst_type_int_range;
GType gst_type_double_range;

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
gst_value_get_int_range_start (const GValue *value)
{
  g_return_val_if_fail (GST_VALUE_HOLDS_FOURCC (value), 0);

  return value->data[0].v_long;
}

int
gst_value_get_int_range_end (const GValue *value)
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
}


