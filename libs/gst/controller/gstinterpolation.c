/* GStreamer
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dot net>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
#include "gstcontrollerprivate.h"
#include "gstcontroller.h"

#define GST_CAT_DEFAULT gst_controller_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_DEFAULT);

/* common helper */

/*
 * gst_controlled_property_find_control_point_node:
 * @prop: the controlled property to search in
 * @timestamp: the search key
 *
 * Find last value before given timestamp in control point list.
 *
 * Returns: the found #GList node or %NULL
 */
GList *
gst_controlled_property_find_control_point_node (GstControlledProperty * prop,
    GstClockTime timestamp)
{
  GList *prev_node = g_list_last (prop->values);
  GList *node;
  GstControlPoint *cp;

  node = prop->values;
  if (prop->last_requested_value) {
    GstControlPoint *last_cp = prop->last_requested_value->data;

    if (timestamp > last_cp->timestamp)
      node = prop->last_requested_value;
  }

  /* iterate over timed value list */
  for (; node; node = g_list_next (node)) {
    cp = node->data;
    /* this timestamp is newer that the one we look for */
    if (timestamp < cp->timestamp) {
      /* get previous one again */
      prev_node = g_list_previous (node);
      break;
    }
  }

  if (prev_node)
    prop->last_requested_value = prev_node;

  return (prev_node);
}

/*  steps-like (no-)interpolation, default */
/*  just returns the value for the most recent key-frame */

static GValue *
interpolate_none_get (GstControlledProperty * prop, GstClockTime timestamp)
{
  GList *node;

  if ((node =
          gst_controlled_property_find_control_point_node (prop, timestamp))) {
    GstControlPoint *cp = node->data;

    return (&cp->value);
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

static gboolean
interpolate_none_get_string_value_array (GstControlledProperty * prop,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gchar **values = (gchar **) value_array->values;

  for (i = 0; i < value_array->nbsamples; i++) {
    *values = (gchar *) g_value_get_string (interpolate_none_get (prop, ts));
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
  interpolate_none_get_enum_value_array,
  interpolate_none_get,
  interpolate_none_get_string_value_array
};

/*  returns the default value of the property, except for times with specific values */
/*  needed for one-shot events, such as notes and triggers */

static GValue *
interpolate_trigger_get (GstControlledProperty * prop, GstClockTime timestamp)
{
  GList *node;
  GstControlPoint *cp;

  /* check if there is a value at the registered timestamp */
  for (node = prop->values; node; node = g_list_next (node)) {
    cp = node->data;
    if (timestamp == cp->timestamp) {
      return (&cp->value);
    }
  }

  return (&prop->default_value);
}

#define DEFINE_TRIGGER_GET(type) \
static gboolean \
interpolate_trigger_get_##type##_value_array (GstControlledProperty * prop, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##type *values = (g##type *) value_array->values; \
  \
  for(i = 0; i < value_array->nbsamples; i++) { \
    *values = g_value_get_##type (interpolate_trigger_get (prop,ts)); \
    ts += value_array->sample_interval; \
    values++; \
  } \
  return (TRUE); \
}

DEFINE_TRIGGER_GET (int);
DEFINE_TRIGGER_GET (uint);
DEFINE_TRIGGER_GET (long);

DEFINE_TRIGGER_GET (ulong);
DEFINE_TRIGGER_GET (float);
DEFINE_TRIGGER_GET (double);

DEFINE_TRIGGER_GET (boolean);

static gboolean
interpolate_trigger_get_enum_value_array (GstControlledProperty * prop,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gint *values = (gint *) value_array->values;

  for (i = 0; i < value_array->nbsamples; i++) {
    *values = g_value_get_enum (interpolate_trigger_get (prop, ts));
    ts += value_array->sample_interval;
    values++;
  }
  return (TRUE);
}

static gboolean
interpolate_trigger_get_string_value_array (GstControlledProperty * prop,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gchar **values = (gchar **) value_array->values;

  for (i = 0; i < value_array->nbsamples; i++) {
    *values = (gchar *) g_value_get_string (interpolate_trigger_get (prop, ts));
    ts += value_array->sample_interval;
    values++;
  }
  return (TRUE);
}

static GstInterpolateMethod interpolate_trigger = {
  interpolate_trigger_get,
  interpolate_trigger_get_int_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_uint_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_long_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_ulong_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_float_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_double_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_boolean_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_enum_value_array,
  interpolate_trigger_get,
  interpolate_trigger_get_string_value_array
};

/*  linear interpolation */
/*  smoothes inbetween values */

#define DEFINE_LINEAR_GET(type) \
static g##type \
_interpolate_linear_get_##type (GstControlledProperty * prop, GstClockTime timestamp) \
{ \
  GList *node; \
  \
  if ((node = gst_controlled_property_find_control_point_node (prop, timestamp))) { \
    GstControlPoint *cp1, *cp2; \
    \
    cp1 = node->data; \
    if ((node = g_list_next (node))) { \
      gdouble slope; \
      g##type value1,value2; \
      \
      cp2 = node->data; \
      \
      value1 = g_value_get_##type (&cp1->value); \
      value2 = g_value_get_##type (&cp2->value); \
      slope = (gdouble) (value2 - value1) / gst_guint64_to_gdouble (cp2->timestamp - cp1->timestamp); \
      \
      return ((g##type) (value1 + gst_guint64_to_gdouble (timestamp - cp1->timestamp) * slope)); \
    } \
    else { \
      return (g_value_get_##type (&cp1->value)); \
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
  g##type *values = (g##type *) value_array->values; \
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
  NULL,
  NULL,
  NULL
};

/*  square interpolation */

/*  cubic interpolation */

/* The following functions implement a natural cubic spline interpolator.
 * For details look at http://en.wikipedia.org/wiki/Spline_interpolation
 *
 * Instead of using a real matrix with n^2 elements for the linear system
 * of equations we use three arrays o, p, q to hold the tridiagonal matrix
 * as following to save memory:
 *
 * p[0] q[0]    0    0    0
 * o[1] p[1] q[1]    0    0
 *    0 o[2] p[2] q[2]    .
 *    .    .    .    .    .
 */

#define DEFINE_CUBIC_GET(type) \
static void \
_interpolate_cubic_update_cache_##type (GstControlledProperty *prop) \
{ \
  gint i, n = prop->nvalues; \
  gdouble *o = g_new0 (gdouble, n); \
  gdouble *p = g_new0 (gdouble, n); \
  gdouble *q = g_new0 (gdouble, n); \
  \
  gdouble *h = g_new0 (gdouble, n); \
  gdouble *b = g_new0 (gdouble, n); \
  gdouble *z = g_new0 (gdouble, n); \
  \
  GList *node; \
  GstControlPoint *cp; \
  GstClockTime x_prev, x, x_next; \
  g##type y_prev, y, y_next; \
  \
  /* Fill linear system of equations */ \
  node = prop->values; \
  cp = node->data; \
  x = cp->timestamp; \
  y = g_value_get_##type (&cp->value); \
  \
  p[0] = 1.0; \
  \
  node = node->next; \
  cp = node->data; \
  x_next = cp->timestamp; \
  y_next = g_value_get_##type (&cp->value); \
  h[0] = gst_util_guint64_to_gdouble (x_next - x); \
  \
  for (i = 1; i < n-1; i++) { \
    /* Shuffle x and y values */ \
    x_prev = x; \
    y_prev = y; \
    x = x_next; \
    y = y_next; \
    node = node->next; \
    cp = node->data; \
    x_next = cp->timestamp; \
    y_next = g_value_get_##type (&cp->value); \
    \
    h[i] = gst_util_guint64_to_gdouble (x_next - x); \
    o[i] = h[i-1]; \
    p[i] = 2.0 * (h[i-1] + h[i]); \
    q[i] = h[i]; \
    b[i] = (y_next - y) / h[i] - (y - y_prev) / h[i-1]; \
  } \
  p[n-1] = 1.0; \
  \
  /* Use Gauss elimination to set everything below the \
   * diagonal to zero */ \
  for (i = 1; i < n-1; i++) { \
    gdouble a = o[i] / p[i-1]; \
    p[i] -= a * q[i-1]; \
    b[i] -= a * b[i-1]; \
  } \
  \
  /* Solve everything else from bottom to top */ \
  for (i = n-2; i > 0; i--) \
    z[i] = (b[i] - q[i] * z[i+1]) / p[i]; \
  \
  /* Save cache next in the GstControlPoint */ \
  \
  node = prop->values; \
  for (i = 0; i < n; i++) { \
    cp = node->data; \
    cp->cache.cubic.h = h[i]; \
    cp->cache.cubic.z = z[i]; \
    node = node->next; \
  } \
  \
  /* Free our temporary arrays */ \
  g_free (o); \
  g_free (p); \
  g_free (q); \
  g_free (h); \
  g_free (b); \
  g_free (z); \
} \
\
static g##type \
_interpolate_cubic_get_##type (GstControlledProperty * prop, GstClockTime timestamp) \
{ \
  GList *node; \
  \
  if (prop->nvalues <= 2) \
    return _interpolate_linear_get_##type (prop, timestamp); \
  \
  if (!prop->valid_cache) { \
    _interpolate_cubic_update_cache_##type (prop); \
    prop->valid_cache = TRUE; \
  } \
  \
  if ((node = gst_controlled_property_find_control_point_node (prop, timestamp))) { \
    GstControlPoint *cp1, *cp2; \
    \
    cp1 = node->data; \
    if ((node = g_list_next (node))) { \
      gdouble diff1, diff2; \
      g##type value1,value2; \
      gdouble ret; \
      \
      cp2 = node->data; \
      \
      value1 = g_value_get_##type (&cp1->value); \
      value2 = g_value_get_##type (&cp2->value); \
      \
      diff1 = gst_guint64_to_gdouble (timestamp - cp1->timestamp); \
      diff2 = gst_guint64_to_gdouble (cp2->timestamp - timestamp); \
      \
      ret = (cp2->cache.cubic.z * diff1 * diff1 * diff1 + cp1->cache.cubic.z * diff2 * diff2 * diff2) / cp1->cache.cubic.h; \
      ret += (value2 / cp1->cache.cubic.h - cp1->cache.cubic.h * cp2->cache.cubic.z) * diff1; \
      ret += (value1 / cp1->cache.cubic.h - cp1->cache.cubic.h * cp1->cache.cubic.z) * diff2; \
      \
      return (g##type) ret; \
    } \
    else { \
      return (g_value_get_##type (&cp1->value)); \
    } \
  } \
  return (g_value_get_##type (&prop->default_value)); \
} \
\
static GValue * \
interpolate_cubic_get_##type (GstControlledProperty * prop, GstClockTime timestamp) \
{ \
  g_value_set_##type (&prop->result_value,_interpolate_cubic_get_##type (prop,timestamp)); \
  return (&prop->result_value); \
} \
\
static gboolean \
interpolate_cubic_get_##type##_value_array (GstControlledProperty * prop, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##type *values = (g##type *) value_array->values; \
  \
  for(i = 0; i < value_array->nbsamples; i++) { \
    *values = _interpolate_cubic_get_##type (prop, ts); \
    ts += value_array->sample_interval; \
    values++; \
  } \
  return (TRUE); \
}

DEFINE_CUBIC_GET (int);

DEFINE_CUBIC_GET (uint);
DEFINE_CUBIC_GET (long);

DEFINE_CUBIC_GET (ulong);
DEFINE_CUBIC_GET (float);
DEFINE_CUBIC_GET (double);

static GstInterpolateMethod interpolate_cubic = {
  interpolate_cubic_get_int,
  interpolate_cubic_get_int_value_array,
  interpolate_cubic_get_uint,
  interpolate_cubic_get_uint_value_array,
  interpolate_cubic_get_long,
  interpolate_cubic_get_long_value_array,
  interpolate_cubic_get_ulong,
  interpolate_cubic_get_ulong_value_array,
  interpolate_cubic_get_float,
  interpolate_cubic_get_float_value_array,
  interpolate_cubic_get_double,
  interpolate_cubic_get_double_value_array,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};


/*  register all interpolation methods */
GstInterpolateMethod *interpolation_methods[] = {
  &interpolate_none,
  &interpolate_trigger,
  &interpolate_linear,
  &interpolate_cubic,
  &interpolate_cubic
};

guint num_interpolation_methods = G_N_ELEMENTS (interpolation_methods);
