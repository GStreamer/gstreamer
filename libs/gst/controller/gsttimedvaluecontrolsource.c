/* GStreamer
 *
 * Copyright (C) 2007,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *               2011 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttimedvaluecontrolsource.c: Base class for timeed value based control
 *                               sources
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
 * SECTION:gsttimedvaluecontrolsource
 * @short_description: timed value control source base class
 *
 * Base class for #GstContrlSources that use time-stamped values.
 *
 * When overriding bind, chain up first to give this bind implementation a
 * chance to setup things.
 *
 * All functions are MT-safe.
 *
 */

#include <glib-object.h>
#include <gst/gst.h>

#include "gstinterpolationcontrolsource.h"
#include "gstinterpolationcontrolsourceprivate.h"
#include "gst/glib-compat-private.h"

#define GST_CAT_DEFAULT controller_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "timed value control source", 0, \
    "timed value control source base class")

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstTimedValueControlSource,
    gst_timed_value_control_source, GST_TYPE_CONTROL_SOURCE, _do_init);

/*
 * gst_control_point_free:
 * @prop: the object to free
 *
 * Private method which frees all data allocated by a #GstControlPoint
 * instance.
 */
static void
gst_control_point_free (GstControlPoint * cp)
{
  g_return_if_fail (cp);

  g_value_unset (&cp->value);
  g_slice_free (GstControlPoint, cp);
}

static void
gst_timed_value_control_source_reset (GstTimedValueControlSource * self)
{
  GstControlSource *csource = (GstControlSource *) self;

  csource->get_value = NULL;
  csource->get_value_array = NULL;

  self->type = self->base = G_TYPE_INVALID;

  if (G_IS_VALUE (&self->default_value))
    g_value_unset (&self->default_value);
  if (G_IS_VALUE (&self->minimum_value))
    g_value_unset (&self->minimum_value);
  if (G_IS_VALUE (&self->maximum_value))
    g_value_unset (&self->maximum_value);

  if (self->values) {
    g_sequence_free (self->values);
    self->values = NULL;
  }

  self->nvalues = 0;
  self->valid_cache = FALSE;
}

static gboolean
gst_timed_value_control_source_bind (GstControlSource * source,
    GParamSpec * pspec)
{
  GType type, base;
  GstTimedValueControlSource *self = (GstTimedValueControlSource *) source;
  gboolean ret = TRUE;

  if (self->type != G_TYPE_INVALID) {
    gst_timed_value_control_source_reset (self);
  }

  /* get the fundamental base type */
  self->type = base = type = G_PARAM_SPEC_VALUE_TYPE (pspec);
  while ((type = g_type_parent (type)))
    base = type;

  self->base = base;
  /* restore type */
  type = self->type;

  switch (base) {
    case G_TYPE_INT:{
      GParamSpecInt *tpspec = G_PARAM_SPEC_INT (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_int (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_int (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_int (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_UINT:{
      GParamSpecUInt *tpspec = G_PARAM_SPEC_UINT (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_uint (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_uint (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_uint (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_LONG:{
      GParamSpecLong *tpspec = G_PARAM_SPEC_LONG (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_long (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_long (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_long (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_ULONG:{
      GParamSpecULong *tpspec = G_PARAM_SPEC_ULONG (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_ulong (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_ulong (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_ulong (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_INT64:{
      GParamSpecInt64 *tpspec = G_PARAM_SPEC_INT64 (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_int64 (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_int64 (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_int64 (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_UINT64:{
      GParamSpecUInt64 *tpspec = G_PARAM_SPEC_UINT64 (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_uint64 (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_uint64 (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_uint64 (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_FLOAT:{
      GParamSpecFloat *tpspec = G_PARAM_SPEC_FLOAT (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_float (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_float (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_float (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_DOUBLE:{
      GParamSpecDouble *tpspec = G_PARAM_SPEC_DOUBLE (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_double (&self->default_value, tpspec->default_value);
      g_value_init (&self->minimum_value, type);
      g_value_set_double (&self->minimum_value, tpspec->minimum);
      g_value_init (&self->maximum_value, type);
      g_value_set_double (&self->maximum_value, tpspec->maximum);
      break;
    }
    case G_TYPE_BOOLEAN:{
      GParamSpecBoolean *tpspec = G_PARAM_SPEC_BOOLEAN (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_boolean (&self->default_value, tpspec->default_value);
      break;
    }
    case G_TYPE_ENUM:{
      GParamSpecEnum *tpspec = G_PARAM_SPEC_ENUM (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_enum (&self->default_value, tpspec->default_value);
      break;
    }
    case G_TYPE_STRING:{
      GParamSpecString *tpspec = G_PARAM_SPEC_STRING (pspec);

      g_value_init (&self->default_value, type);
      g_value_set_string (&self->default_value, tpspec->default_value);
      break;
    }
    default:
      GST_WARNING ("incomplete implementation for paramspec type '%s'",
          G_PARAM_SPEC_TYPE_NAME (pspec));
      ret = FALSE;
      break;
  }

  if (ret) {
    self->valid_cache = FALSE;
    self->nvalues = 0;
  } else {
    gst_timed_value_control_source_reset (self);
  }

  return ret;
}

/*
 * gst_control_point_compare:
 * @p1: a pointer to a #GstControlPoint
 * @p2: a pointer to a #GstControlPoint
 *
 * Compare function for g_list operations that operates on two #GstControlPoint
 * parameters.
 */
static gint
gst_control_point_compare (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstControlPoint *) p1)->timestamp;
  GstClockTime ct2 = ((GstControlPoint *) p2)->timestamp;

  return ((ct1 < ct2) ? -1 : ((ct1 == ct2) ? 0 : 1));
}

/*
 * gst_control_point_find:
 * @p1: a pointer to a #GstControlPoint
 * @p2: a pointer to a #GstClockTime
 *
 * Compare function for g_list operations that operates on a #GstControlPoint and
 * a #GstClockTime.
 */
static gint
gst_control_point_find (gconstpointer p1, gconstpointer p2)
{
  GstClockTime ct1 = ((GstControlPoint *) p1)->timestamp;
  GstClockTime ct2 = *(GstClockTime *) p2;

  return ((ct1 < ct2) ? -1 : ((ct1 == ct2) ? 0 : 1));
}

static GstControlPoint *
_make_new_cp (GstTimedValueControlSource * self, GstClockTime timestamp,
    const GValue * value)
{
  GstControlPoint *cp;

  /* create a new GstControlPoint */
  cp = g_slice_new0 (GstControlPoint);
  cp->timestamp = timestamp;
  g_value_init (&cp->value, self->type);
  g_value_copy (value, &cp->value);

  return cp;
}

static void
gst_timed_value_control_source_set_internal (GstTimedValueControlSource *
    self, GstClockTime timestamp, const GValue * value)
{
  GSequenceIter *iter;

  /* check if a control point for the timestamp already exists */

  /* iter contains the iter right *after* timestamp */
  if (G_LIKELY (self->values)) {
    iter =
        g_sequence_search (self->values, &timestamp,
        (GCompareDataFunc) gst_control_point_find, NULL);
    if (iter) {
      GSequenceIter *prev = g_sequence_iter_prev (iter);
      GstControlPoint *cp = g_sequence_get (prev);

      /* If the timestamp is the same just update the control point value */
      if (cp->timestamp == timestamp) {
        /* update control point */
        g_value_reset (&cp->value);
        g_value_copy (value, &cp->value);
        goto done;
      }
    }
  } else {
    self->values = g_sequence_new ((GDestroyNotify) gst_control_point_free);
  }

  /* sort new cp into the prop->values list */
  g_sequence_insert_sorted (self->values, _make_new_cp (self, timestamp,
          value), (GCompareDataFunc) gst_control_point_compare, NULL);
  self->nvalues++;

done:
  self->valid_cache = FALSE;
}

/**
 * gst_timed_value_control_source_find_control_point_iter:
 * @self: the control source to search in
 * @timestamp: the search key
 *
 * Find last value before given timestamp in control point list.
 * If all values in the control point list come after the given
 * timestamp or no values exist, %NULL is returned.
 *
 * For use in control source implementations.
 *
 * Returns: the found #GSequenceIter or %NULL
 */
GSequenceIter *gst_timed_value_control_source_find_control_point_iter
    (GstTimedValueControlSource * self, GstClockTime timestamp)
{
  GSequenceIter *iter;

  if (!self->values)
    return NULL;

  iter =
      g_sequence_search (self->values, &timestamp,
      (GCompareDataFunc) gst_control_point_find, NULL);

  /* g_sequence_search() returns the iter where timestamp
   * would be inserted, i.e. the iter > timestamp, so
   * we need to get the previous one. And of course, if
   * there is no previous one, we return NULL. */
  if (g_sequence_iter_is_begin (iter))
    return NULL;

  return g_sequence_iter_prev (iter);
}


/**
 * gst_timed_value_control_source_set:
 * @self: the #GstTimedValueControlSource object
 * @timestamp: the time the control-change is scheduled for
 * @value: the control-value
 *
 * Set the value of given controller-handled property at a certain time.
 *
 * Returns: FALSE if the values couldn't be set, TRUE otherwise.
 */
gboolean
gst_timed_value_control_source_set (GstTimedValueControlSource * self,
    GstClockTime timestamp, const GValue * value)
{
  g_return_val_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);
  g_return_val_if_fail (G_VALUE_TYPE (value) == self->type, FALSE);

  g_mutex_lock (self->lock);
  gst_timed_value_control_source_set_internal (self, timestamp, value);
  g_mutex_unlock (self->lock);

  return TRUE;
}

/**
 * gst_timed_value_control_source_set_from_list:
 * @self: the #GstTimedValueControlSource object
 * @timedvalues: (transfer none) (element-type GstController.TimedValue): a list
 * with #GstTimedValue items
 *
 * Sets multiple timed values at once.
 *
 * Returns: FALSE if the values couldn't be set, TRUE otherwise.
 */
gboolean
gst_timed_value_control_source_set_from_list (GstTimedValueControlSource *
    self, const GSList * timedvalues)
{
  const GSList *node;
  GstTimedValue *tv;
  gboolean res = FALSE;

  g_return_val_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self), FALSE);

  for (node = timedvalues; node; node = g_slist_next (node)) {
    tv = node->data;
    if (!GST_CLOCK_TIME_IS_VALID (tv->timestamp)) {
      GST_WARNING ("GstTimedValued with invalid timestamp passed to %s",
          GST_FUNCTION);
    } else if (!G_IS_VALUE (&tv->value)) {
      GST_WARNING ("GstTimedValued with invalid value passed to %s",
          GST_FUNCTION);
    } else if (G_VALUE_TYPE (&tv->value) != self->type) {
      GST_WARNING ("incompatible value type for property");
    } else {
      g_mutex_lock (self->lock);
      gst_timed_value_control_source_set_internal (self, tv->timestamp,
          &tv->value);
      g_mutex_unlock (self->lock);
      res = TRUE;
    }
  }
  return res;
}

/**
 * gst_timed_value_control_source_unset:
 * @self: the #GstTimedValueControlSource object
 * @timestamp: the time the control-change should be removed from
 *
 * Used to remove the value of given controller-handled property at a certain
 * time.
 *
 * Returns: FALSE if the value couldn't be unset (i.e. not found, TRUE otherwise.
 */
gboolean
gst_timed_value_control_source_unset (GstTimedValueControlSource * self,
    GstClockTime timestamp)
{
  GSequenceIter *iter;
  gboolean res = FALSE;

  g_return_val_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);

  g_mutex_lock (self->lock);
  /* check if a control point for the timestamp exists */
  if (G_LIKELY (self->values) && (iter =
          g_sequence_search (self->values, &timestamp,
              (GCompareDataFunc) gst_control_point_find, NULL))) {
    GstControlPoint *cp;

    /* Iter contains the iter right after timestamp, i.e.
     * we need to get the previous one and check the timestamp
     */
    iter = g_sequence_iter_prev (iter);
    cp = g_sequence_get (iter);
    if (cp->timestamp == timestamp) {
      g_sequence_remove (iter);
      self->nvalues--;
      self->valid_cache = FALSE;
      res = TRUE;
    }
  }
  g_mutex_unlock (self->lock);

  return res;
}

/**
 * gst_timed_value_control_source_unset_all:
 * @self: the #GstTimedValueControlSource object
 *
 * Used to remove all time-stamped values of given controller-handled property
 *
 */
void
gst_timed_value_control_source_unset_all (GstTimedValueControlSource * self)
{
  g_return_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self));

  g_mutex_lock (self->lock);
  /* free GstControlPoint structures */
  if (self->values) {
    g_sequence_free (self->values);
    self->values = NULL;
  }
  self->nvalues = 0;
  self->valid_cache = FALSE;

  g_mutex_unlock (self->lock);
}

static void
_append_control_point (GstControlPoint * cp, GQueue * res)
{
  g_queue_push_tail (res, cp);
}

/**
 * gst_timed_value_control_source_get_all:
 * @self: the #GstTimedValueControlSource to get the list from
 *
 * Returns a read-only copy of the list of #GstTimedValue for the given property.
 * Free the list after done with it.
 *
 * Returns: (transfer container) (element-type GstController.TimedValue): a copy
 * of the list, or %NULL if the property isn't handled by the controller
 */
GList *
gst_timed_value_control_source_get_all (GstTimedValueControlSource * self)
{
  GQueue res = G_QUEUE_INIT;

  g_return_val_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self), NULL);

  g_mutex_lock (self->lock);
  if (G_LIKELY (self->values))
    g_sequence_foreach (self->values, (GFunc) _append_control_point, &res);
  g_mutex_unlock (self->lock);

  return res.head;
}

/**
 * gst_timed_value_control_source_get_count:
 * @self: the #GstTimedValueControlSource to get the number of values from
 *
 * Get the number of control points that are set.
 *
 * Returns: the number of control points that are set.
 */
gint
gst_timed_value_control_source_get_count (GstTimedValueControlSource * self)
{
  g_return_val_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self), 0);
  return self->nvalues;
}

/**
 * gst_timed_value_control_source_get_base_value_type:
 * @self: the #GstTimedValueControlSource
 *
 * Get the base #GType of the property value.
 *
 * Returns: the #GType, %G_TYPE_INVALID if not yet known.
 */
GType
gst_timed_value_control_source_get_base_value_type (GstTimedValueControlSource *
    self)
{
  g_return_val_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self),
      G_TYPE_INVALID);
  return self->base;
}

/**
 * gst_timed_value_control_invalidate_cache:
 * @self: the #GstTimedValueControlSource
 *
 * Reset the controlled value cache.
 */
void
gst_timed_value_control_invalidate_cache (GstTimedValueControlSource * self)
{
  g_return_if_fail (GST_IS_TIMED_VALUE_CONTROL_SOURCE (self));
  self->valid_cache = FALSE;
}

static void
gst_timed_value_control_source_init (GstTimedValueControlSource * self)
{
  self->lock = g_mutex_new ();
}

static void
gst_timed_value_control_source_finalize (GObject * obj)
{
  GstTimedValueControlSource *self = GST_TIMED_VALUE_CONTROL_SOURCE (obj);

  g_mutex_lock (self->lock);
  gst_timed_value_control_source_reset (self);
  g_mutex_unlock (self->lock);
  g_mutex_free (self->lock);

  G_OBJECT_CLASS (gst_timed_value_control_source_parent_class)->finalize (obj);
}

static void
gst_timed_value_control_source_class_init (GstTimedValueControlSourceClass
    * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstControlSourceClass *csource_class = GST_CONTROL_SOURCE_CLASS (klass);

  gobject_class->finalize = gst_timed_value_control_source_finalize;
  csource_class->bind = gst_timed_value_control_source_bind;
}
