/* GStreamer
 *
 * Copyright (C) 2011 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstcontrolbinding.c: Attachment for control sources
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
 * SECTION:gstcontrolbinding
 * @short_description: attachment for control source sources
 *
 * A value mapping object that attaches control sources to gobject properties.
 */

#include "gst_private.h"

#include <glib-object.h>
#include <gst/gst.h>

#include "gstcontrolbinding.h"

#include <math.h>

#define GST_CAT_DEFAULT control_binding_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void gst_control_binding_dispose (GObject * object);
static void gst_control_binding_finalize (GObject * object);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gstcontrolbinding", 0, \
      "dynamic parameter control source attachment");

G_DEFINE_TYPE_WITH_CODE (GstControlBinding, gst_control_binding, G_TYPE_OBJECT,
    _do_init);

static void
gst_control_binding_class_init (GstControlBindingClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_control_binding_dispose;
  gobject_class->finalize = gst_control_binding_finalize;
}

static void
gst_control_binding_init (GstControlBinding * self)
{
}

static void
gst_control_binding_dispose (GObject * object)
{
  GstControlBinding *self = GST_CONTROL_BINDING (object);

  if (self->csource) {
    g_object_unref (self->csource);
    self->csource = NULL;
  }
}

static void
gst_control_binding_finalize (GObject * object)
{
  GstControlBinding *self = GST_CONTROL_BINDING (object);

  g_value_unset (&self->cur_value);
}

/* mapping functions */
#define DEFINE_CONVERT(type,Type,TYPE) \
static void \
convert_to_##type (GstControlBinding *self, gdouble s, GValue *d) \
{ \
  GParamSpec##Type *pspec = G_PARAM_SPEC_##TYPE (self->pspec); \
  g##type v; \
  \
  s = CLAMP (s, 0.0, 1.0); \
  v = pspec->minimum + (g##type) ((pspec->maximum - pspec->minimum) * s); \
  g_value_set_##type (d, v); \
}

DEFINE_CONVERT (int, Int, INT);
DEFINE_CONVERT (uint, UInt, UINT);
DEFINE_CONVERT (long, Long, LONG);
DEFINE_CONVERT (ulong, ULong, ULONG);
DEFINE_CONVERT (int64, Int64, INT64);
DEFINE_CONVERT (uint64, UInt64, UINT64);
DEFINE_CONVERT (float, Float, FLOAT);
DEFINE_CONVERT (double, Double, DOUBLE);

static void
convert_to_boolean (GstControlBinding * self, gdouble s, GValue * d)
{
  s = CLAMP (s, 0.0, 1.0);
  g_value_set_boolean (d, (gboolean) (s + 0.5));
}

static void
convert_to_enum (GstControlBinding * self, gdouble s, GValue * d)
{
  GParamSpecEnum *pspec = G_PARAM_SPEC_ENUM (self->pspec);
  GEnumClass *e = pspec->enum_class;
  gint v;

  s = CLAMP (s, 0.0, 1.0);
  v = s * (e->n_values - 1);
  g_value_set_enum (d, e->values[v].value);
}

/**
 * gst_control_binding_new:
 * @object: the object of the property
 * @property_name: the property-name to attach the control source
 * @csource: the control source
 *
 * Create a new control-binding that attaches the #GstControlSource to the
 * #GObject property.
 *
 * Returns: the new #GstControlBinding
 */
GstControlBinding *
gst_control_binding_new (GstObject * object, const gchar * property_name,
    GstControlSource * csource)
{
  GstControlBinding *self = NULL;
  GParamSpec *pspec;

  GST_INFO_OBJECT (object, "trying to put property '%s' under control",
      property_name);

  /* check if the object has a property of that name */
  if ((pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object),
              property_name))) {
    GST_DEBUG_OBJECT (object, "  psec->flags : 0x%08x", pspec->flags);

    /* check if this param is witable && controlable && !construct-only */
    g_return_val_if_fail ((pspec->flags & (G_PARAM_WRITABLE |
                GST_PARAM_CONTROLLABLE | G_PARAM_CONSTRUCT_ONLY)) ==
        (G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE), NULL);

    if ((self = (GstControlBinding *) g_object_newv (GST_TYPE_CONTROL_BINDING,
                0, NULL))) {
      GType type = G_PARAM_SPEC_VALUE_TYPE (pspec), base;

      // add pspec as a construction parameter and move below to construct()
      self->pspec = pspec;
      self->name = pspec->name;
      self->csource = g_object_ref (csource);
      self->disabled = FALSE;

      g_value_init (&self->cur_value, type);

      base = type = G_PARAM_SPEC_VALUE_TYPE (pspec);
      while ((type = g_type_parent (type)))
        base = type;

      GST_DEBUG_OBJECT (object, "  using type %s", g_type_name (base));

      // select mapping function
      // FIXME: only select mapping if super class hasn't set any?
      switch (base) {
        case G_TYPE_INT:
          self->convert = convert_to_int;
          break;
        case G_TYPE_UINT:
          self->convert = convert_to_uint;
          break;
        case G_TYPE_LONG:
          self->convert = convert_to_long;
          break;
        case G_TYPE_ULONG:
          self->convert = convert_to_ulong;
          break;
        case G_TYPE_INT64:
          self->convert = convert_to_int64;
          break;
        case G_TYPE_UINT64:
          self->convert = convert_to_uint64;
          break;
        case G_TYPE_FLOAT:
          self->convert = convert_to_float;
          break;
        case G_TYPE_DOUBLE:
          self->convert = convert_to_double;
          break;
        case G_TYPE_BOOLEAN:
          self->convert = convert_to_boolean;
          break;
        case G_TYPE_ENUM:
          self->convert = convert_to_enum;
          break;
        default:
          // FIXME: return NULL?
          GST_WARNING ("incomplete implementation for paramspec type '%s'",
              G_PARAM_SPEC_TYPE_NAME (pspec));
          break;
      }
    }
  } else {
    GST_WARNING_OBJECT (object, "class '%s' has no property '%s'",
        G_OBJECT_TYPE_NAME (object), property_name);
  }
  return self;
}

/* functions */

/**
 * gst_control_binding_sync_values:
 * @self: the control binding
 * @object: the object that has controlled properties
 * @timestamp: the time that should be processed
 * @last_sync: the last time this was called
 *
 * Sets the property of the @object, according to the #GstControlSources that
 * handle them and for the given timestamp.
 *
 * If this function fails, it is most likely the application developers fault.
 * Most probably the control sources are not setup correctly.
 *
 * Returns: %TRUE if the controller value could be applied to the object
 * property, %FALSE otherwise
 */
gboolean
gst_control_binding_sync_values (GstControlBinding * self, GstObject * object,
    GstClockTime timestamp, GstClockTime last_sync)
{
  GValue *dst_val;
  gdouble src_val;
  gboolean ret;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), FALSE);

  if (self->disabled)
    return TRUE;

  GST_LOG_OBJECT (object, "property '%s' at ts=%" GST_TIME_FORMAT,
      self->name, GST_TIME_ARGS (timestamp));

  dst_val = &self->cur_value;
  ret = gst_control_source_get_value (self->csource, timestamp, &src_val);
  if (G_LIKELY (ret)) {
    GST_LOG_OBJECT (object, "  new value %lf", src_val);
    /* always set the value for first time, but then only if it changed
     * this should limit g_object_notify invocations.
     * FIXME: can we detect negative playback rates?
     */
    if ((timestamp < last_sync) || (src_val != self->last_value)) {
      GST_LOG_OBJECT (object, "  mapping %s to value of type %s", self->name,
          G_VALUE_TYPE_NAME (dst_val));
      /* run mapping function to convert gdouble to GValue */
      self->convert (self, src_val, dst_val);
      /* we can make this faster
       * http://bugzilla.gnome.org/show_bug.cgi?id=536939
       */
      g_object_set_property ((GObject *) object, self->name, dst_val);
      self->last_value = src_val;
    }
  } else {
    GST_DEBUG_OBJECT (object, "no control value for param %s", self->name);
  }
  return (ret);
}

/**
 * gst_control_binding_get_value:
 * @self: the control binding
 * @timestamp: the time the control-change should be read from
 *
 * Gets the value for the given controlled property at the requested time.
 *
 * Returns: the GValue of the property at the given time, or %NULL if the
 * property isn't controlled.
 */
GValue *
gst_control_binding_get_value (GstControlBinding * self, GstClockTime timestamp)
{
  GValue *dst_val = NULL;
  gdouble src_val;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), NULL);

  /* get current value via control source */
  if (gst_control_source_get_value (self->csource, timestamp, &src_val)) {
    dst_val = g_new0 (GValue, 1);
    g_value_init (dst_val, G_PARAM_SPEC_VALUE_TYPE (self->pspec));
    self->convert (self, src_val, dst_val);
  } else {
    GST_LOG ("no control value for property %s at ts %" GST_TIME_FORMAT,
        self->name, GST_TIME_ARGS (timestamp));
  }

  return dst_val;
}

/**
 * gst_control_binding_get_value_array:
 * @self: the control binding
 * @timestamp: the time that should be processed
 * @interval: the time spacing between subsequent values
 * @n_values: the number of values
 * @values: array to put control-values in
 *
 * Gets a number of values for the given controllered property starting at the
 * requested time. The array @values need to hold enough space for @n_values of
 * the same type as the objects property's type.
 *
 * This function is useful if one wants to e.g. draw a graph of the control
 * curve or apply a control curve sample by sample.
 *
 * Returns: %TRUE if the given array could be filled, %FALSE otherwise
 */
gboolean
gst_control_binding_get_value_array (GstControlBinding * self,
    GstClockTime timestamp, GstClockTime interval, guint n_values,
    GValue * values)
{
  gint i;
  gdouble *src_val;
  gboolean res = FALSE;
  GType type;
  GstControlBindingConvert convert;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (interval), FALSE);
  g_return_val_if_fail (values, FALSE);

  convert = self->convert;
  type = G_PARAM_SPEC_VALUE_TYPE (self->pspec);

  src_val = g_new0 (gdouble, n_values);
  if ((res = gst_control_source_get_value_array (self->csource, timestamp,
              interval, n_values, src_val))) {
    for (i = 0; i < n_values; i++) {
      if (!isnan (src_val[i])) {
        g_value_init (&values[i], type);
        convert (self, src_val[i], &values[i]);
      } else {
        GST_LOG ("no control value for property %s at index %d", self->name, i);
      }
    }
  } else {
    GST_LOG ("failed to get control value for property %s at ts %"
        GST_TIME_FORMAT, self->name, GST_TIME_ARGS (timestamp));
  }
  g_free (src_val);
  return res;
}



/**
 * gst_control_binding_get_control_source:
 * @self: the control binding
 *
 * Get the control source.
 *
 * Returns: the control source. Unref when done with it.
 */
GstControlSource *
gst_control_binding_get_control_source (GstControlBinding * self)
{
  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), NULL);
  return g_object_ref (self->csource);
}

/**
 * gst_control_binding_set_disabled:
 * @self: the control binding
 * @disabled: boolean that specifies whether to disable the controller
 * or not.
 *
 * This function is used to disable a control binding for some time, i.e.
 * gst_object_sync_values() will do nothing.
 */
void
gst_control_binding_set_disabled (GstControlBinding * self, gboolean disabled)
{
  g_return_if_fail (GST_IS_CONTROL_BINDING (self));
  self->disabled = disabled;
}

/**
 * gst_control_binding_is_disabled:
 * @self: the control binding
 *
 * Check if the control binding is disabled.
 *
 * Returns: %TRUE if the binding is inactive
 */
gboolean
gst_control_binding_is_disabled (GstControlBinding * self)
{
  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), TRUE);
  return (self->disabled == TRUE);
}
