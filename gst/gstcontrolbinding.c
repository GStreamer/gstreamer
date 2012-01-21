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

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gstcontrolbinding", 0, \
      "dynamic parameter control source attachment");

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstControlBinding, gst_control_binding,
    GST_TYPE_OBJECT, _do_init);

static void
gst_control_binding_class_init (GstControlBindingClass * klass)
{
}

static void
gst_control_binding_init (GstControlBinding * self)
{
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
  GstControlBindingClass *klass;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), FALSE);

  if (self->disabled)
    return TRUE;

  klass = GST_CONTROL_BINDING_GET_CLASS (self);

  if (G_LIKELY (klass->sync_values != NULL)) {
    ret = klass->sync_values (self, object, timestamp, last_sync);
  } else {
    GST_WARNING_OBJECT (self, "missing sync_values implementation");
  }
  return ret;
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
  GstControlBindingClass *klass;
  GValue *ret = NULL;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), NULL);

  klass = GST_CONTROL_BINDING_GET_CLASS (self);

  if (G_LIKELY (klass->get_value != NULL)) {
    ret = klass->get_value (self, timestamp);
  } else {
    GST_WARNING_OBJECT (self, "missing get_value implementation");
  }
  return ret;
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
  GstControlBindingClass *klass;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (interval), FALSE);
  g_return_val_if_fail (values, FALSE);

  klass = GST_CONTROL_BINDING_GET_CLASS (self);

  if (G_LIKELY (klass->get_value_array != NULL)) {
    ret = klass->get_value_array (self, timestamp, interval, n_values, values);
  } else {
    GST_WARNING_OBJECT (self, "missing get_value_array implementation");
  }
  return ret;
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
