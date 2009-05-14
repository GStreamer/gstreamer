/* GStreamer
 *
 * Copyright (C) <2005> Stefan Kost <ensonic at users dot sf dot net>
 * Copyright (C) 2007,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "gstinterpolationcontrolsource.h"
#include "gstinterpolationcontrolsourceprivate.h"

#define GST_CAT_DEFAULT controller_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_DEFAULT);

#define EMPTY(x) (x)

/* common helper */

static gint
gst_control_point_find (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstControlPoint *) p1)->timestamp;
  GstClockTime ct2 = *(GstClockTime *) p2;

  return ((ct1 < ct2) ? -1 : ((ct1 == ct2) ? 0 : 1));
}

/*
 * gst_interpolation_control_source_find_control_point_iter:
 * @self: the interpolation control source to search in
 * @timestamp: the search key
 *
 * Find last value before given timestamp in control point list.
 *
 * Returns: the found #GSequenceIter or %NULL
 */
static GSequenceIter *gst_interpolation_control_source_find_control_point_iter
    (GstInterpolationControlSource * self, GstClockTime timestamp)
{
  GSequenceIter *iter;
  GstControlPoint *cp;

  if (!self->priv->values)
    return NULL;

  iter =
      g_sequence_search (self->priv->values, &timestamp,
      (GCompareDataFunc) gst_control_point_find, NULL);

  /* g_sequence_search() returns the iter where timestamp
   * would be inserted, i.e. the iter > timestamp, so
   * we need to get the previous one */
  iter = g_sequence_iter_prev (iter);

  /* g_sequence_iter_prev () on the begin iter returns
   * the begin iter. Check if the prev iter is still
   * after our timestamp, in that case return NULL
   */
  cp = g_sequence_get (iter);
  if (cp->timestamp > timestamp)
    return NULL;

  /* If the iter is the end iter return NULL as no
   * data is linked to the end iter */
  return G_UNLIKELY (g_sequence_iter_is_end (iter)) ? NULL : iter;
}

/*  steps-like (no-)interpolation, default */
/*  just returns the value for the most recent key-frame */

#define DEFINE_NONE_GET(type) \
static inline GValue * \
_interpolate_none_get_##type (GstInterpolationControlSource *self, GstClockTime timestamp) \
{ \
  GValue *ret; \
  GSequenceIter *iter; \
  \
  if ((iter = \
          gst_interpolation_control_source_find_control_point_iter (self, timestamp))) { \
    GstControlPoint *cp = g_sequence_get (iter); \
    g##type ret_val = g_value_get_##type (&cp->value); \
    \
    if (g_value_get_##type (&self->priv->minimum_value) > ret_val) \
      ret = &self->priv->minimum_value; \
    else if (g_value_get_##type (&self->priv->maximum_value) < ret_val) \
      ret = &self->priv->maximum_value; \
    else \
      ret = &cp->value; \
  } else { \
    ret = &self->priv->default_value; \
  } \
  return ret; \
} \
\
static gboolean \
interpolate_none_get_##type (GstInterpolationControlSource *self, GstClockTime timestamp, GValue *value) \
{ \
  GValue *ret; \
  g_mutex_lock (self->lock); \
  \
  ret = _interpolate_none_get_##type (self, timestamp); \
  if (!ret) { \
    g_mutex_unlock (self->lock); \
    return FALSE; \
  } \
  g_value_copy (ret, value); \
  g_mutex_unlock (self->lock); \
  return TRUE; \
} \
\
static gboolean \
interpolate_none_get_##type##_value_array (GstInterpolationControlSource *self, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##type *values = (g##type *) value_array->values; \
  GValue *ret; \
  \
  g_mutex_lock (self->lock); \
  for(i = 0; i < value_array->nbsamples; i++) { \
    ret = _interpolate_none_get_##type (self, ts); \
    if (!ret) { \
      g_mutex_unlock (self->lock); \
      return FALSE; \
    } \
    *values = g_value_get_##type (ret); \
    ts += value_array->sample_interval; \
    values++; \
  } \
  g_mutex_unlock (self->lock); \
  return TRUE; \
}


DEFINE_NONE_GET (int);
DEFINE_NONE_GET (uint);
DEFINE_NONE_GET (long);

DEFINE_NONE_GET (ulong);
DEFINE_NONE_GET (int64);
DEFINE_NONE_GET (uint64);
DEFINE_NONE_GET (float);
DEFINE_NONE_GET (double);

static inline GValue *
_interpolate_none_get (GstInterpolationControlSource * self,
    GstClockTime timestamp)
{
  GSequenceIter *iter;
  GValue *ret;

  if ((iter =
          gst_interpolation_control_source_find_control_point_iter (self,
              timestamp))) {
    GstControlPoint *cp = g_sequence_get (iter);

    ret = &cp->value;
  } else {
    ret = &self->priv->default_value;
  }
  return ret;
}

static gboolean
interpolate_none_get (GstInterpolationControlSource * self,
    GstClockTime timestamp, GValue * value)
{
  GValue *ret;

  g_mutex_lock (self->lock);
  ret = _interpolate_none_get (self, timestamp);

  if (!ret) {
    g_mutex_unlock (self->lock);
    return FALSE;
  }

  g_value_copy (ret, value);
  g_mutex_unlock (self->lock);
  return TRUE;
}

static gboolean
interpolate_none_get_boolean_value_array (GstInterpolationControlSource * self,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gboolean *values = (gboolean *) value_array->values;
  GValue *ret;

  g_mutex_lock (self->lock);
  for (i = 0; i < value_array->nbsamples; i++) {
    ret = _interpolate_none_get (self, ts);
    if (!ret) {
      g_mutex_unlock (self->lock);
      return FALSE;
    }
    *values = g_value_get_boolean (ret);
    ts += value_array->sample_interval;
    values++;
  }
  g_mutex_unlock (self->lock);
  return TRUE;
}

static gboolean
interpolate_none_get_enum_value_array (GstInterpolationControlSource * self,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gint *values = (gint *) value_array->values;
  GValue *ret;

  g_mutex_lock (self->lock);
  for (i = 0; i < value_array->nbsamples; i++) {
    ret = _interpolate_none_get (self, ts);
    if (!ret) {
      g_mutex_unlock (self->lock);
      return FALSE;
    }
    *values = g_value_get_enum (ret);
    ts += value_array->sample_interval;
    values++;
  }
  g_mutex_unlock (self->lock);
  return TRUE;
}

static gboolean
interpolate_none_get_string_value_array (GstInterpolationControlSource * self,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gchar **values = (gchar **) value_array->values;
  GValue *ret;

  g_mutex_lock (self->lock);
  for (i = 0; i < value_array->nbsamples; i++) {
    ret = _interpolate_none_get (self, ts);
    if (!ret) {
      g_mutex_unlock (self->lock);
      return FALSE;
    }
    *values = (gchar *) g_value_get_string (ret);
    ts += value_array->sample_interval;
    values++;
  }
  g_mutex_unlock (self->lock);
  return TRUE;
}

static GstInterpolateMethod interpolate_none = {
  (GstControlSourceGetValue) interpolate_none_get_int,
  (GstControlSourceGetValueArray) interpolate_none_get_int_value_array,
  (GstControlSourceGetValue) interpolate_none_get_uint,
  (GstControlSourceGetValueArray) interpolate_none_get_uint_value_array,
  (GstControlSourceGetValue) interpolate_none_get_long,
  (GstControlSourceGetValueArray) interpolate_none_get_long_value_array,
  (GstControlSourceGetValue) interpolate_none_get_ulong,
  (GstControlSourceGetValueArray) interpolate_none_get_ulong_value_array,
  (GstControlSourceGetValue) interpolate_none_get_int64,
  (GstControlSourceGetValueArray) interpolate_none_get_int64_value_array,
  (GstControlSourceGetValue) interpolate_none_get_uint64,
  (GstControlSourceGetValueArray) interpolate_none_get_uint64_value_array,
  (GstControlSourceGetValue) interpolate_none_get_float,
  (GstControlSourceGetValueArray) interpolate_none_get_float_value_array,
  (GstControlSourceGetValue) interpolate_none_get_double,
  (GstControlSourceGetValueArray) interpolate_none_get_double_value_array,
  (GstControlSourceGetValue) interpolate_none_get,
  (GstControlSourceGetValueArray) interpolate_none_get_boolean_value_array,
  (GstControlSourceGetValue) interpolate_none_get,
  (GstControlSourceGetValueArray) interpolate_none_get_enum_value_array,
  (GstControlSourceGetValue) interpolate_none_get,
  (GstControlSourceGetValueArray) interpolate_none_get_string_value_array
};

/*  returns the default value of the property, except for times with specific values */
/*  needed for one-shot events, such as notes and triggers */

#define DEFINE_TRIGGER_GET(type) \
static inline GValue * \
_interpolate_trigger_get_##type (GstInterpolationControlSource *self, GstClockTime timestamp) \
{ \
  GSequenceIter *iter; \
  GstControlPoint *cp; \
  \
  /* check if there is a value at the registered timestamp */ \
  if ((iter = \
          gst_interpolation_control_source_find_control_point_iter (self, timestamp))) { \
    cp = g_sequence_get (iter); \
    if (timestamp == cp->timestamp) { \
      g##type ret = g_value_get_##type (&cp->value); \
      if (g_value_get_##type (&self->priv->minimum_value) > ret) \
        return &self->priv->minimum_value; \
      else if (g_value_get_##type (&self->priv->maximum_value) < ret) \
        return &self->priv->maximum_value; \
      else \
        return &cp->value; \
    } \
  } \
  \
  if (self->priv->nvalues > 0) \
    return &self->priv->default_value; \
  else \
    return NULL; \
} \
\
static gboolean \
interpolate_trigger_get_##type (GstInterpolationControlSource *self, GstClockTime timestamp, GValue *value) \
{ \
  GValue *ret; \
  g_mutex_lock (self->lock); \
  ret = _interpolate_trigger_get_##type (self, timestamp); \
  if (!ret) { \
    g_mutex_unlock (self->lock); \
    return FALSE; \
  } \
  g_value_copy (ret, value); \
  g_mutex_unlock (self->lock); \
  return TRUE; \
} \
\
static gboolean \
interpolate_trigger_get_##type##_value_array (GstInterpolationControlSource *self, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##type *values = (g##type *) value_array->values; \
  GValue *ret; \
  \
  g_mutex_lock (self->lock); \
  for(i = 0; i < value_array->nbsamples; i++) { \
    ret = _interpolate_trigger_get_##type (self, ts); \
    if (!ret) { \
      g_mutex_unlock (self->lock); \
      return FALSE; \
    } \
    *values = g_value_get_##type (ret); \
    ts += value_array->sample_interval; \
    values++; \
  } \
  g_mutex_unlock (self->lock); \
  return TRUE; \
}


DEFINE_TRIGGER_GET (int);

DEFINE_TRIGGER_GET (uint);
DEFINE_TRIGGER_GET (long);

DEFINE_TRIGGER_GET (ulong);
DEFINE_TRIGGER_GET (int64);
DEFINE_TRIGGER_GET (uint64);
DEFINE_TRIGGER_GET (float);
DEFINE_TRIGGER_GET (double);

static inline GValue *
_interpolate_trigger_get (GstInterpolationControlSource * self,
    GstClockTime timestamp)
{
  GSequenceIter *iter;
  GstControlPoint *cp;

  /* check if there is a value at the registered timestamp */
  if ((iter =
          gst_interpolation_control_source_find_control_point_iter (self,
              timestamp))) {
    cp = g_sequence_get (iter);
    if (timestamp == cp->timestamp) {
      return &cp->value;
    }
  }
  if (self->priv->nvalues > 0)
    return &self->priv->default_value;
  else
    return NULL;
}

static gboolean
interpolate_trigger_get (GstInterpolationControlSource * self,
    GstClockTime timestamp, GValue * value)
{
  GValue *ret;

  g_mutex_lock (self->lock);
  ret = _interpolate_trigger_get (self, timestamp);
  if (!ret) {
    g_mutex_unlock (self->lock);
    return FALSE;
  }
  g_value_copy (ret, value);
  g_mutex_unlock (self->lock);
  return TRUE;
}

static gboolean
interpolate_trigger_get_boolean_value_array (GstInterpolationControlSource *
    self, GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gint *values = (gint *) value_array->values;
  GValue *ret;

  g_mutex_lock (self->lock);
  for (i = 0; i < value_array->nbsamples; i++) {
    ret = _interpolate_trigger_get (self, ts);
    if (!ret) {
      g_mutex_unlock (self->lock);
      return FALSE;
    }
    *values = g_value_get_boolean (ret);
    ts += value_array->sample_interval;
    values++;
  }
  g_mutex_unlock (self->lock);
  return TRUE;
}

static gboolean
interpolate_trigger_get_enum_value_array (GstInterpolationControlSource * self,
    GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gint *values = (gint *) value_array->values;
  GValue *ret;

  g_mutex_lock (self->lock);
  for (i = 0; i < value_array->nbsamples; i++) {
    ret = _interpolate_trigger_get (self, ts);
    if (!ret) {
      g_mutex_unlock (self->lock);
      return FALSE;
    }
    *values = g_value_get_enum (ret);
    ts += value_array->sample_interval;
    values++;
  }
  g_mutex_unlock (self->lock);
  return TRUE;
}

static gboolean
interpolate_trigger_get_string_value_array (GstInterpolationControlSource *
    self, GstClockTime timestamp, GstValueArray * value_array)
{
  gint i;
  GstClockTime ts = timestamp;
  gchar **values = (gchar **) value_array->values;
  GValue *ret;

  g_mutex_lock (self->lock);
  for (i = 0; i < value_array->nbsamples; i++) {
    ret = _interpolate_trigger_get (self, ts);
    if (!ret) {
      g_mutex_unlock (self->lock);
      return FALSE;
    }
    *values = (gchar *) g_value_get_string (ret);
    ts += value_array->sample_interval;
    values++;
  }
  g_mutex_unlock (self->lock);
  return TRUE;
}

static GstInterpolateMethod interpolate_trigger = {
  (GstControlSourceGetValue) interpolate_trigger_get_int,
  (GstControlSourceGetValueArray) interpolate_trigger_get_int_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_uint,
  (GstControlSourceGetValueArray) interpolate_trigger_get_uint_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_long,
  (GstControlSourceGetValueArray) interpolate_trigger_get_long_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_ulong,
  (GstControlSourceGetValueArray) interpolate_trigger_get_ulong_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_int64,
  (GstControlSourceGetValueArray) interpolate_trigger_get_int64_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_uint64,
  (GstControlSourceGetValueArray) interpolate_trigger_get_uint64_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_float,
  (GstControlSourceGetValueArray) interpolate_trigger_get_float_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get_double,
  (GstControlSourceGetValueArray) interpolate_trigger_get_double_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get,
  (GstControlSourceGetValueArray) interpolate_trigger_get_boolean_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get,
  (GstControlSourceGetValueArray) interpolate_trigger_get_enum_value_array,
  (GstControlSourceGetValue) interpolate_trigger_get,
  (GstControlSourceGetValueArray) interpolate_trigger_get_string_value_array
};

/*  linear interpolation */
/*  smoothes inbetween values */

#define DEFINE_LINEAR_GET(vtype,round,convert) \
static inline gboolean \
_interpolate_linear_get_##vtype (GstInterpolationControlSource *self, GstClockTime timestamp, g##vtype *ret) \
{ \
  GSequenceIter *iter; \
  GstControlPoint *cp1 = NULL, *cp2, cp={0,}; \
  \
  iter = gst_interpolation_control_source_find_control_point_iter (self, timestamp); \
  if (iter) { \
    cp1 = g_sequence_get (iter); \
    iter = g_sequence_iter_next (iter); \
    iter = g_sequence_iter_is_end (iter) ? NULL : iter; \
  } else { \
    cp.timestamp = G_GUINT64_CONSTANT(0); \
    g_value_init (&cp.value, self->priv->type); \
    g_value_copy (&self->priv->default_value, &cp.value); \
    cp1 = &cp; \
    iter = g_sequence_get_begin_iter (self->priv->values); \
  } \
  if (iter) { \
    gdouble slope; \
    g##vtype value1,value2; \
    \
    cp2 = g_sequence_get (iter); \
    \
    value1 = g_value_get_##vtype (&cp1->value); \
    value2 = g_value_get_##vtype (&cp2->value); \
    slope = ((gdouble) convert (value2) - (gdouble) convert (value1)) / gst_guint64_to_gdouble (cp2->timestamp - cp1->timestamp); \
    \
    if (round) \
      *ret = (g##vtype) (convert (value1) + gst_guint64_to_gdouble (timestamp - cp1->timestamp) * slope + 0.5); \
    else \
      *ret = (g##vtype) (convert (value1) + gst_guint64_to_gdouble (timestamp - cp1->timestamp) * slope); \
  } \
  else { \
    *ret = g_value_get_##vtype (&cp1->value); \
  } \
  *ret = CLAMP (*ret, g_value_get_##vtype (&self->priv->minimum_value), g_value_get_##vtype (&self->priv->maximum_value)); \
  return TRUE; \
} \
\
static gboolean \
interpolate_linear_get_##vtype (GstInterpolationControlSource *self, GstClockTime timestamp, GValue *value) \
{ \
  g##vtype ret; \
  g_mutex_lock (self->lock); \
  if (_interpolate_linear_get_##vtype (self, timestamp, &ret)) { \
    g_value_set_##vtype (value, ret); \
    g_mutex_unlock (self->lock); \
    return TRUE; \
  } \
  g_mutex_unlock (self->lock); \
  return FALSE; \
} \
\
static gboolean \
interpolate_linear_get_##vtype##_value_array (GstInterpolationControlSource *self, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##vtype *values = (g##vtype *) value_array->values; \
  \
  g_mutex_lock (self->lock); \
  for(i = 0; i < value_array->nbsamples; i++) { \
    if (! _interpolate_linear_get_##vtype (self, ts, values)) { \
      g_mutex_unlock (self->lock); \
      return FALSE; \
    } \
    ts += value_array->sample_interval; \
    values++; \
  } \
  g_mutex_unlock (self->lock); \
  return TRUE; \
}

DEFINE_LINEAR_GET (int, TRUE, EMPTY);

DEFINE_LINEAR_GET (uint, TRUE, EMPTY);
DEFINE_LINEAR_GET (long, TRUE, EMPTY);

DEFINE_LINEAR_GET (ulong, TRUE, EMPTY);
DEFINE_LINEAR_GET (int64, TRUE, EMPTY);
DEFINE_LINEAR_GET (uint64, TRUE, gst_guint64_to_gdouble);
DEFINE_LINEAR_GET (float, FALSE, EMPTY);
DEFINE_LINEAR_GET (double, FALSE, EMPTY);

static GstInterpolateMethod interpolate_linear = {
  (GstControlSourceGetValue) interpolate_linear_get_int,
  (GstControlSourceGetValueArray) interpolate_linear_get_int_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_uint,
  (GstControlSourceGetValueArray) interpolate_linear_get_uint_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_long,
  (GstControlSourceGetValueArray) interpolate_linear_get_long_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_ulong,
  (GstControlSourceGetValueArray) interpolate_linear_get_ulong_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_int64,
  (GstControlSourceGetValueArray) interpolate_linear_get_int64_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_uint64,
  (GstControlSourceGetValueArray) interpolate_linear_get_uint64_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_float,
  (GstControlSourceGetValueArray) interpolate_linear_get_float_value_array,
  (GstControlSourceGetValue) interpolate_linear_get_double,
  (GstControlSourceGetValueArray) interpolate_linear_get_double_value_array,
  (GstControlSourceGetValue) NULL,
  (GstControlSourceGetValueArray) NULL,
  (GstControlSourceGetValue) NULL,
  (GstControlSourceGetValueArray) NULL,
  (GstControlSourceGetValue) NULL,
  (GstControlSourceGetValueArray) NULL
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

#define DEFINE_CUBIC_GET(vtype,round, convert) \
static void \
_interpolate_cubic_update_cache_##vtype (GstInterpolationControlSource *self) \
{ \
  gint i, n = self->priv->nvalues; \
  gdouble *o = g_new0 (gdouble, n); \
  gdouble *p = g_new0 (gdouble, n); \
  gdouble *q = g_new0 (gdouble, n); \
  \
  gdouble *h = g_new0 (gdouble, n); \
  gdouble *b = g_new0 (gdouble, n); \
  gdouble *z = g_new0 (gdouble, n); \
  \
  GSequenceIter *iter; \
  GstControlPoint *cp; \
  GstClockTime x_prev, x, x_next; \
  g##vtype y_prev, y, y_next; \
  \
  /* Fill linear system of equations */ \
  iter = g_sequence_get_begin_iter (self->priv->values); \
  cp = g_sequence_get (iter); \
  x = cp->timestamp; \
  y = g_value_get_##vtype (&cp->value); \
  \
  p[0] = 1.0; \
  \
  iter = g_sequence_iter_next (iter); \
  cp = g_sequence_get (iter); \
  x_next = cp->timestamp; \
  y_next = g_value_get_##vtype (&cp->value); \
  h[0] = gst_guint64_to_gdouble (x_next - x); \
  \
  for (i = 1; i < n-1; i++) { \
    /* Shuffle x and y values */ \
    x_prev = x; \
    y_prev = y; \
    x = x_next; \
    y = y_next; \
    iter = g_sequence_iter_next (iter); \
    cp = g_sequence_get (iter); \
    x_next = cp->timestamp; \
    y_next = g_value_get_##vtype (&cp->value); \
    \
    h[i] = gst_guint64_to_gdouble (x_next - x); \
    o[i] = h[i-1]; \
    p[i] = 2.0 * (h[i-1] + h[i]); \
    q[i] = h[i]; \
    b[i] = convert (y_next - y) / h[i] - convert (y - y_prev) / h[i-1]; \
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
  iter = g_sequence_get_begin_iter (self->priv->values); \
  for (i = 0; i < n; i++) { \
    cp = g_sequence_get (iter); \
    cp->cache.cubic.h = h[i]; \
    cp->cache.cubic.z = z[i]; \
    iter = g_sequence_iter_next (iter); \
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
static inline gboolean \
_interpolate_cubic_get_##vtype (GstInterpolationControlSource *self, GstClockTime timestamp, g##vtype *ret) \
{ \
  GSequenceIter *iter; \
  GstControlPoint *cp1 = NULL, *cp2, cp={0,}; \
  \
  if (self->priv->nvalues <= 2) \
    return _interpolate_linear_get_##vtype (self, timestamp, ret); \
  \
  if (!self->priv->valid_cache) { \
    _interpolate_cubic_update_cache_##vtype (self); \
    self->priv->valid_cache = TRUE; \
  } \
  \
  iter = gst_interpolation_control_source_find_control_point_iter (self, timestamp); \
  if (iter) { \
    cp1 = g_sequence_get (iter); \
    iter = g_sequence_iter_next (iter); \
    iter = g_sequence_iter_is_end (iter) ? NULL : iter; \
  } else { \
    cp.timestamp = G_GUINT64_CONSTANT(0); \
    g_value_init (&cp.value, self->priv->type); \
    g_value_copy (&self->priv->default_value, &cp.value); \
    cp1 = &cp; \
    iter = g_sequence_get_begin_iter (self->priv->values); \
  } \
  if (iter) { \
    gdouble diff1, diff2; \
    g##vtype value1,value2; \
    gdouble out; \
    \
    cp2 = g_sequence_get (iter); \
    \
    value1 = g_value_get_##vtype (&cp1->value); \
    value2 = g_value_get_##vtype (&cp2->value); \
    \
    diff1 = gst_guint64_to_gdouble (timestamp - cp1->timestamp); \
    diff2 = gst_guint64_to_gdouble (cp2->timestamp - timestamp); \
    \
    out = (cp2->cache.cubic.z * diff1 * diff1 * diff1 + cp1->cache.cubic.z * diff2 * diff2 * diff2) / cp1->cache.cubic.h; \
    out += (convert (value2) / cp1->cache.cubic.h - cp1->cache.cubic.h * cp2->cache.cubic.z) * diff1; \
    out += (convert (value1) / cp1->cache.cubic.h - cp1->cache.cubic.h * cp1->cache.cubic.z) * diff2; \
    \
    if (round) \
      *ret = (g##vtype) (out + 0.5); \
    else \
      *ret = (g##vtype) out; \
  } \
  else { \
    *ret = g_value_get_##vtype (&cp1->value); \
  } \
  *ret = CLAMP (*ret, g_value_get_##vtype (&self->priv->minimum_value), g_value_get_##vtype (&self->priv->maximum_value)); \
  return TRUE; \
} \
\
static gboolean \
interpolate_cubic_get_##vtype (GstInterpolationControlSource *self, GstClockTime timestamp, GValue *value) \
{ \
  g##vtype ret; \
  g_mutex_lock (self->lock); \
  if (_interpolate_cubic_get_##vtype (self, timestamp, &ret)) { \
    g_value_set_##vtype (value, ret); \
    g_mutex_unlock (self->lock); \
    return TRUE; \
  } \
  g_mutex_unlock (self->lock); \
  return FALSE; \
} \
\
static gboolean \
interpolate_cubic_get_##vtype##_value_array (GstInterpolationControlSource *self, \
    GstClockTime timestamp, GstValueArray * value_array) \
{ \
  gint i; \
  GstClockTime ts = timestamp; \
  g##vtype *values = (g##vtype *) value_array->values; \
  \
  g_mutex_lock (self->lock); \
  for(i = 0; i < value_array->nbsamples; i++) { \
    if (! _interpolate_cubic_get_##vtype (self, ts, values)) { \
      g_mutex_unlock (self->lock); \
      return FALSE; \
    } \
    ts += value_array->sample_interval; \
    values++; \
  } \
  g_mutex_unlock (self->lock); \
  return TRUE; \
}

DEFINE_CUBIC_GET (int, TRUE, EMPTY);

DEFINE_CUBIC_GET (uint, TRUE, EMPTY);
DEFINE_CUBIC_GET (long, TRUE, EMPTY);

DEFINE_CUBIC_GET (ulong, TRUE, EMPTY);
DEFINE_CUBIC_GET (int64, TRUE, EMPTY);
DEFINE_CUBIC_GET (uint64, TRUE, gst_guint64_to_gdouble);
DEFINE_CUBIC_GET (float, FALSE, EMPTY);
DEFINE_CUBIC_GET (double, FALSE, EMPTY);

static GstInterpolateMethod interpolate_cubic = {
  (GstControlSourceGetValue) interpolate_cubic_get_int,
  (GstControlSourceGetValueArray) interpolate_cubic_get_int_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_uint,
  (GstControlSourceGetValueArray) interpolate_cubic_get_uint_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_long,
  (GstControlSourceGetValueArray) interpolate_cubic_get_long_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_ulong,
  (GstControlSourceGetValueArray) interpolate_cubic_get_ulong_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_int64,
  (GstControlSourceGetValueArray) interpolate_cubic_get_int64_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_uint64,
  (GstControlSourceGetValueArray) interpolate_cubic_get_uint64_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_float,
  (GstControlSourceGetValueArray) interpolate_cubic_get_float_value_array,
  (GstControlSourceGetValue) interpolate_cubic_get_double,
  (GstControlSourceGetValueArray) interpolate_cubic_get_double_value_array,
  (GstControlSourceGetValue) NULL,
  (GstControlSourceGetValueArray) NULL,
  (GstControlSourceGetValue) NULL,
  (GstControlSourceGetValueArray) NULL,
  (GstControlSourceGetValue) NULL,
  (GstControlSourceGetValueArray) NULL
};

/*  register all interpolation methods */
GstInterpolateMethod *priv_gst_interpolation_methods[] = {
  &interpolate_none,
  &interpolate_trigger,
  &interpolate_linear,
  &interpolate_cubic,
  &interpolate_cubic
};

guint priv_gst_num_interpolation_methods =
G_N_ELEMENTS (priv_gst_interpolation_methods);
