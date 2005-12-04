/* GStreamer
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dot net>
 *
 * gstinterpolation.c: Interpolation methods for dynamic properties
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
#  include "config.h"
#endif
#include "gstcontroller.h"

#define GST_CAT_DEFAULT gst_controller_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_DEFAULT);

/* common helper */

/*
 * gst_controlled_property_find_timed_value_node:
 * @prop: the controlled property to search in
 * @timestamp: the search key
 *
 * Find last value before given timestamp in timed value list.
 *
 * Returns: the found #GList node or %NULL
 */
GList *
gst_controlled_property_find_timed_value_node (GstControlledProperty * prop,
    GstClockTime timestamp)
{
  /* GList *prev_node = NULL; */
  GList *prev_node = g_list_last (prop->values);
  GList *node;
  GstTimedValue *tv;

  /*
     if((prop->last_value) &&
     (timestamp>((GstTimedValue *)(prop->last_value->data))->timestamp)) {
     node=prop->last_value;
     }
     else {
     node=prop->values;
     }
   */

  /* iterate over timed value list */
  for (node = prop->values; node; node = g_list_next (node)) {
    tv = node->data;
    /* this timestamp is newer that the one we look for */
    if (timestamp < tv->timestamp) {
      /* get previous one again */
      prev_node = g_list_previous (node);
      break;
    }
  }
  /*
     if(node) {
     prop->last_value=prev_node;
     }
   */
  return (prev_node);
}

/*  steps-like (no-)interpolation, default */
/*  just returns the value for the most recent key-frame */

static GValue *
interpolate_none_get (GstControlledProperty * prop, GstClockTime timestamp)
{
  GList *node;

  if ((node = gst_controlled_property_find_timed_value_node (prop, timestamp))) {
    GstTimedValue *tv = node->data;

    return (&tv->value);
  }
  return (&prop->default_value);
}

#define DEFINE_NONE_GET(type) \
static gboolean \
interpolate_none_get_##type##_value_array (GstControlledProperty * prop, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##type *values = (g##type *) value_array->values; \
  \
  for(i = 0; i < value_array->nbsamples; i++) { \
    *values = g_value_get_##type (interpolate_none_get (prop,ts)); \
    ts += value_array->sample_interval; \
    values++; \
  } \
  return (TRUE); \
}

DEFINE_NONE_GET (int);
DEFINE_NONE_GET (uint);
DEFINE_NONE_GET (long);

DEFINE_NONE_GET (ulong);
DEFINE_NONE_GET (float);
DEFINE_NONE_GET (double);

DEFINE_NONE_GET (boolean);

static gboolean
interpolate_none_get_enum_value_array (GstControlledProperty * prop,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gint *values = (gint *) value_array->values;

  for (i = 0; i < value_array->nbsamples; i++) {
    *values = g_value_get_enum (interpolate_none_get (prop, ts));
    ts += value_array->sample_interval;
    values++;
  }
  return (TRUE);
}


static GstInterpolateMethod interpolate_none = {
  interpolate_none_get,
  interpolate_none_get_int_value_array,
  interpolate_none_get,
  interpolate_none_get_uint_value_array,
  interpolate_none_get,
  interpolate_none_get_long_value_array,
  interpolate_none_get,
  interpolate_none_get_ulong_value_array,
  interpolate_none_get,
  interpolate_none_get_float_value_array,
  interpolate_none_get,
  interpolate_none_get_double_value_array,
  interpolate_none_get,
  interpolate_none_get_boolean_value_array,
  interpolate_none_get,
  interpolate_none_get_enum_value_array
};

/*  returns the default value of the property, except for times with specific values */
/*  needed for one-shot events, such as notes and triggers */

static GValue *
interpolate_trigger_get (GstControlledProperty * prop, GstClockTime timestamp)
{
  GList *node;
  GstTimedValue *tv;

  /* check if there is a value at the registered timestamp */
  for (node = prop->values; node; node = g_list_next (node)) {
    tv = node->data;
    if (timestamp == tv->timestamp) {
      return (&tv->value);
    }
  }

  return (&prop->default_value);
}

static gboolean
interpolate_trigger_get_value_array (GstControlledProperty * prop,
    GstClockTime timestamp, GstValueArray * value_array)
{
  return (FALSE);
}

static GstInterpolateMethod interpolate_trigger = {
  interpolate_trigger_get,
  interpolate_trigger_get_value_array,
  interpolate_trigger_get,
  NULL,
  interpolate_trigger_get,
  NULL,
  interpolate_trigger_get,
  NULL,
  interpolate_trigger_get,
  NULL,
  interpolate_trigger_get,
  NULL,
  interpolate_trigger_get,
  NULL,
  interpolate_trigger_get,
  NULL
};

/*  linear interpolation */
/*  smoothes inbetween values */

#define DEFINE_LINEAR_GET(type) \
static g##type \
_interpolate_linear_get_##type (GstControlledProperty * prop, GstClockTime timestamp) \
{ \
  GList *node; \
  \
  if ((node = gst_controlled_property_find_timed_value_node (prop, timestamp))) { \
    GstTimedValue *tv1, *tv2; \
    \
    tv1 = node->data; \
    if ((node = g_list_next (node))) { \
      gdouble timediff,valuediff; \
      g##type value1,value2; \
      \
      tv2 = node->data; \
      \
      timediff = gst_guint64_to_gdouble (tv2->timestamp - tv1->timestamp); \
      value1 = g_value_get_##type (&tv1->value); \
      value2 = g_value_get_##type (&tv2->value); \
      valuediff = (gdouble) (value2 - value1); \
      \
      return ((g##type) (value1 + valuediff * (gst_guint64_to_gdouble (timestamp - tv1->timestamp) / timediff))); \
    } \
    else { \
      return (g_value_get_##type (&tv1->value)); \
    } \
  } \
  return (g_value_get_##type (&prop->default_value)); \
} \
\
static GValue * \
interpolate_linear_get_##type (GstControlledProperty * prop, GstClockTime timestamp) \
{ \
  g_value_set_##type (&prop->result_value,_interpolate_linear_get_##type (prop,timestamp)); \
  return (&prop->result_value); \
} \
\
static gboolean \
interpolate_linear_get_##type##_value_array (GstControlledProperty * prop, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  gint *values = (gint *) value_array->values; \
  \
  for(i = 0; i < value_array->nbsamples; i++) { \
    *values = _interpolate_linear_get_##type (prop, ts); \
    ts += value_array->sample_interval; \
    values++; \
  } \
  return (TRUE); \
}

DEFINE_LINEAR_GET (int);

DEFINE_LINEAR_GET (uint);
DEFINE_LINEAR_GET (long);

DEFINE_LINEAR_GET (ulong);
DEFINE_LINEAR_GET (float);
DEFINE_LINEAR_GET (double);

static GstInterpolateMethod interpolate_linear = {
  interpolate_linear_get_int,
  interpolate_linear_get_int_value_array,
  interpolate_linear_get_uint,
  interpolate_linear_get_uint_value_array,
  interpolate_linear_get_long,
  interpolate_linear_get_long_value_array,
  interpolate_linear_get_ulong,
  interpolate_linear_get_ulong_value_array,
  interpolate_linear_get_float,
  interpolate_linear_get_float_value_array,
  interpolate_linear_get_double,
  interpolate_linear_get_double_value_array,
  NULL,
  NULL,
  NULL,
  NULL
};

/*  square interpolation */

/*  cubic interpolation */

/*  register all interpolation methods */
GstInterpolateMethod *interpolation_methods[] = {
  &interpolate_none,
  &interpolate_trigger,
  &interpolate_linear,
  NULL,
  NULL
};
