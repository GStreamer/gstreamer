/* GStreamer
 *
 * Copyright (C) 2007,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *               2011 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttriggercontrolsource.c: Control source that provides some values at time-
 *                            stamps
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
 * SECTION:gsttriggercontrolsource
 * @short_description: interpolation control source
 *
 * #GstTriggerControlSource is a #GstControlSource, that returns values from user-given
 * control points. It allows for a tolerance on the time-stamps.
 *
 * To use #GstTriggerControlSource get a new instance by calling
 * gst_trigger_control_source_new(), bind it to a #GParamSpec and set some
 * control points by calling gst_timed_value_control_source_set().
 *
 * All functions are MT-safe.
 */

#include <glib-object.h>
#include <gst/gst.h>

#include "gsttriggercontrolsource.h"
#include "gstinterpolationcontrolsourceprivate.h"
#include "gst/glib-compat-private.h"

#define GST_CAT_DEFAULT controller_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_TOLERANCE = 1,
};

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "trigger control source", 0, \
    "timeline value trigger control source")

G_DEFINE_TYPE_WITH_CODE (GstTriggerControlSource, gst_trigger_control_source,
    GST_TYPE_TIMED_VALUE_CONTROL_SOURCE, _do_init);

struct _GstTriggerControlSourcePrivate
{
  gint64 tolerance;
};

/* control point accessors */

/*  returns the default value of the property, except for times with specific values */
/*  needed for one-shot events, such as notes and triggers */
static inline const GValue *
_interpolate_trigger_get (GstTimedValueControlSource * self,
    GSequenceIter * iter, GstClockTime timestamp)
{
  GstControlPoint *cp;

  /* check if there is a value at the registered timestamp */
  if (iter) {
    gint64 tolerance = ((GstTriggerControlSource *) self)->priv->tolerance;
    cp = g_sequence_get (iter);
    if (GST_CLOCK_DIFF (cp->timestamp, timestamp) <= tolerance) {
      return &cp->value;
    } else {
      if ((iter = g_sequence_iter_next (iter))) {
        cp = g_sequence_get (iter);
        if (GST_CLOCK_DIFF (timestamp, cp->timestamp) <= tolerance) {
          return &cp->value;
        }
      }
    }
  }
  if (self->nvalues > 0)
    return &self->default_value;
  else
    return NULL;
}

#define DEFINE_TRIGGER_GET_FUNC_COMPARABLE(type) \
static inline const GValue * \
_interpolate_trigger_get_##type (GstTimedValueControlSource *self, GSequenceIter *iter, GstClockTime timestamp) \
{ \
  GstControlPoint *cp; \
  \
  /* check if there is a value at the registered timestamp */ \
  if (iter) { \
    gint64 tolerance = ((GstTriggerControlSource *)self)->priv->tolerance; \
    gboolean found = FALSE; \
    cp = g_sequence_get (iter); \
    if (GST_CLOCK_DIFF (cp->timestamp,timestamp) <= tolerance ) { \
      found = TRUE; \
    } else { \
      if ((iter = g_sequence_iter_next (iter))) { \
        cp = g_sequence_get (iter); \
        if (GST_CLOCK_DIFF (timestamp, cp->timestamp) <= tolerance) { \
          found = TRUE; \
        } \
      } \
    } \
    if (found) { \
      g##type ret = g_value_get_##type (&cp->value); \
      if (g_value_get_##type (&self->minimum_value) > ret) \
        return &self->minimum_value; \
      else if (g_value_get_##type (&self->maximum_value) < ret) \
        return &self->maximum_value; \
      else \
        return &cp->value; \
    } \
  } \
  \
  if (self->nvalues > 0) \
    return &self->default_value; \
  else \
    return NULL; \
}

#define DEFINE_TRIGGER_GET(type, ctype, get_func) \
static gboolean \
interpolate_trigger_get_##type (GstTimedValueControlSource *self, GstClockTime timestamp, GValue *value) \
{ \
  const GValue *ret; \
  GSequenceIter *iter; \
  \
  g_mutex_lock (self->lock); \
  \
  iter = gst_timed_value_control_source_find_control_point_iter (self, timestamp); \
  ret = get_func (self, iter, timestamp); \
  if (!ret) { \
    g_mutex_unlock (self->lock); \
    return FALSE; \
  } \
  \
  g_value_copy (ret, value); \
  g_mutex_unlock (self->lock); \
  return TRUE; \
} \
\
static gboolean \
interpolate_trigger_get_##type##_value_array (GstTimedValueControlSource *self, \
    GstClockTime timestamp, GstClockTime interval, guint n_values, gpointer _values) \
{ \
  guint i; \
  GstClockTime ts = timestamp; \
  GstClockTime next_ts = 0; \
  ctype *values = (ctype *) _values; \
  const GValue *ret_val = NULL; \
  ctype ret = 0; \
  GSequenceIter *iter1 = NULL, *iter2 = NULL; \
  gboolean triggered = FALSE; \
  \
  g_mutex_lock (self->lock); \
  for(i = 0; i < n_values; i++) { \
    if (!ret_val || ts >= next_ts) { \
      iter1 = gst_timed_value_control_source_find_control_point_iter (self, ts); \
      if (!iter1) { \
        if (G_LIKELY (self->values)) \
          iter2 = g_sequence_get_begin_iter (self->values); \
	else \
	  iter2 = NULL; \
      } else { \
        iter2 = g_sequence_iter_next (iter1); \
      } \
      \
      if (iter2 && !g_sequence_iter_is_end (iter2)) { \
        GstControlPoint *cp; \
        \
        cp = g_sequence_get (iter2); \
        next_ts = cp->timestamp; \
      } else { \
        next_ts = GST_CLOCK_TIME_NONE; \
      } \
      \
      ret_val = get_func (self, iter1, ts); \
      if (!ret_val) { \
        g_mutex_unlock (self->lock); \
        return FALSE; \
      } \
      ret = g_value_get_##type (ret_val); \
      triggered = TRUE; \
    } else if (triggered) { \
      ret_val = get_func (self, iter1, ts); \
      if (!ret_val) { \
        g_mutex_unlock (self->lock); \
        return FALSE; \
      } \
      ret = g_value_get_##type (ret_val); \
      triggered = FALSE; \
    } \
    *values = ret; \
    ts += interval; \
    values++; \
  } \
  g_mutex_unlock (self->lock); \
  return TRUE; \
}

DEFINE_TRIGGER_GET_FUNC_COMPARABLE (int);
DEFINE_TRIGGER_GET (int, gint, _interpolate_trigger_get_int);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (uint);
DEFINE_TRIGGER_GET (uint, guint, _interpolate_trigger_get_uint);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (long);
DEFINE_TRIGGER_GET (long, glong, _interpolate_trigger_get_long);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (ulong);
DEFINE_TRIGGER_GET (ulong, gulong, _interpolate_trigger_get_ulong);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (int64);
DEFINE_TRIGGER_GET (int64, gint64, _interpolate_trigger_get_int64);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (uint64);
DEFINE_TRIGGER_GET (uint64, guint64, _interpolate_trigger_get_uint64);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (float);
DEFINE_TRIGGER_GET (float, gfloat, _interpolate_trigger_get_float);
DEFINE_TRIGGER_GET_FUNC_COMPARABLE (double);
DEFINE_TRIGGER_GET (double, gdouble, _interpolate_trigger_get_double);

DEFINE_TRIGGER_GET (boolean, gboolean, _interpolate_trigger_get);
DEFINE_TRIGGER_GET (enum, gint, _interpolate_trigger_get);
DEFINE_TRIGGER_GET (string, const gchar *, _interpolate_trigger_get);

/**
 * gst_trigger_control_source_new:
 *
 * This returns a new, unbound #GstTriggerControlSource.
 *
 * Returns: a new, unbound #GstTriggerControlSource.
 */
GstTriggerControlSource *
gst_trigger_control_source_new (void)
{
  return g_object_newv (GST_TYPE_TRIGGER_CONTROL_SOURCE, 0, NULL);
}

static gboolean
gst_trigger_control_source_bind (GstControlSource * csource, GParamSpec * pspec)
{
  if (GST_CONTROL_SOURCE_CLASS
      (gst_trigger_control_source_parent_class)->bind (csource, pspec)) {
    gboolean ret = TRUE;

    GST_TIMED_VALUE_CONTROL_SOURCE_LOCK (csource);
    switch (gst_timed_value_control_source_get_base_value_type (
            (GstTimedValueControlSource *) csource)) {
      case G_TYPE_INT:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_int;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_int_value_array;
        break;
      case G_TYPE_UINT:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_uint;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_uint_value_array;
        break;
      case G_TYPE_LONG:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_long;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_long_value_array;
        break;
      case G_TYPE_ULONG:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_ulong;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_ulong_value_array;
        break;
      case G_TYPE_INT64:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_int64;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_int64_value_array;
        break;
      case G_TYPE_UINT64:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_uint64;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_uint64_value_array;
        break;
      case G_TYPE_FLOAT:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_float;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_float_value_array;
        break;
      case G_TYPE_DOUBLE:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_double;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_double_value_array;
        break;
      case G_TYPE_BOOLEAN:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_boolean;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_boolean_value_array;
        break;
      case G_TYPE_ENUM:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_enum;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_enum_value_array;
        break;
      case G_TYPE_STRING:
        csource->get_value =
            (GstControlSourceGetValue) interpolate_trigger_get_string;
        csource->get_value_array = (GstControlSourceGetValueArray)
            interpolate_trigger_get_string_value_array;
        break;
      default:
        ret = FALSE;
        break;
    }

    /* Incomplete implementation */
    if (!csource->get_value || !csource->get_value_array) {
      ret = FALSE;
    }
    gst_timed_value_control_invalidate_cache ((GstTimedValueControlSource *)
        csource);

    GST_TIMED_VALUE_CONTROL_SOURCE_UNLOCK (csource);

    return ret;
  }
  return FALSE;
}

static void
gst_trigger_control_source_init (GstTriggerControlSource * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_TRIGGER_CONTROL_SOURCE,
      GstTriggerControlSourcePrivate);
}

static void
gst_trigger_control_source_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTriggerControlSource *self = GST_TRIGGER_CONTROL_SOURCE (object);

  switch (prop_id) {
    case PROP_TOLERANCE:
      GST_TIMED_VALUE_CONTROL_SOURCE_LOCK (self);
      self->priv->tolerance = g_value_get_int64 (value);
      GST_TIMED_VALUE_CONTROL_SOURCE_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_trigger_control_source_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTriggerControlSource *self = GST_TRIGGER_CONTROL_SOURCE (object);

  switch (prop_id) {
    case PROP_TOLERANCE:
      g_value_set_int64 (value, self->priv->tolerance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_trigger_control_source_class_init (GstTriggerControlSourceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstControlSourceClass *csource_class = GST_CONTROL_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstTriggerControlSourcePrivate));

  gobject_class->set_property = gst_trigger_control_source_set_property;
  gobject_class->get_property = gst_trigger_control_source_get_property;

  csource_class->bind = gst_trigger_control_source_bind;

  g_object_class_install_property (gobject_class, PROP_TOLERANCE,
      g_param_spec_int64 ("tolerance", "Tolerance",
          "Amount of ns a control time can be off to still trigger",
          0, G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}
