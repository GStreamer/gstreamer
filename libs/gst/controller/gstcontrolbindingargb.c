/* GStreamer
 *
 * Copyright (C) 2011 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstcontrolbindingargb.c: Attachment for multiple control sources to gargb
 *                            properties
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
 * SECTION:gstcontrolbindingargb
 * @short_description: attachment for control source sources to argb properties
 *
 * A value mapping object that attaches multiple control sources to a guint
 * gobject properties representing a color.
 */

#include <glib-object.h>
#include <gst/gst.h>

#include "gstcontrolbindingargb.h"

#include <math.h>

#define GST_CAT_DEFAULT control_binding_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void gst_control_binding_argb_dispose (GObject * object);
static void gst_control_binding_argb_finalize (GObject * object);

static gboolean gst_control_binding_argb_sync_values (GstControlBinding * _self,
    GstObject * object, GstClockTime timestamp, GstClockTime last_sync);
static GValue *gst_control_binding_argb_get_value (GstControlBinding * _self,
    GstClockTime timestamp);
static gboolean gst_control_binding_argb_get_value_array (GstControlBinding *
    _self, GstClockTime timestamp, GstClockTime interval, guint n_values,
    GValue * values);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gstcontrolbindingargb", 0, \
      "dynamic parameter control source attachment");

G_DEFINE_TYPE_WITH_CODE (GstControlBindingARGB, gst_control_binding_argb,
    GST_TYPE_CONTROL_BINDING, _do_init);

static void
gst_control_binding_argb_class_init (GstControlBindingARGBClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstControlBindingClass *control_binding_class =
      GST_CONTROL_BINDING_CLASS (klass);

  gobject_class->dispose = gst_control_binding_argb_dispose;
  gobject_class->finalize = gst_control_binding_argb_finalize;

  control_binding_class->sync_values = gst_control_binding_argb_sync_values;
  control_binding_class->get_value = gst_control_binding_argb_get_value;
  control_binding_class->get_value_array =
      gst_control_binding_argb_get_value_array;
}

static void
gst_control_binding_argb_init (GstControlBindingARGB * self)
{
}

static void
gst_control_binding_argb_dispose (GObject * object)
{
  GstControlBindingARGB *self = GST_CONTROL_BINDING_ARGB (object);

  if (self->cs_a)
    gst_object_replace ((GstObject **) & self->cs_a, NULL);
  if (self->cs_r)
    gst_object_replace ((GstObject **) & self->cs_r, NULL);
  if (self->cs_g)
    gst_object_replace ((GstObject **) & self->cs_g, NULL);
  if (self->cs_b)
    gst_object_replace ((GstObject **) & self->cs_b, NULL);
}

static void
gst_control_binding_argb_finalize (GObject * object)
{
  GstControlBindingARGB *self = GST_CONTROL_BINDING_ARGB (object);

  g_value_unset (&self->cur_value);
}

static gboolean
gst_control_binding_argb_sync_values (GstControlBinding * _self,
    GstObject * object, GstClockTime timestamp, GstClockTime last_sync)
{
  GstControlBindingARGB *self = GST_CONTROL_BINDING_ARGB (_self);
  gdouble src_val_a = 1.0, src_val_r = 0.0, src_val_g = 0.0, src_val_b = 0.0;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING_ARGB (self), FALSE);

  GST_LOG_OBJECT (object, "property '%s' at ts=%" GST_TIME_FORMAT,
      _self->name, GST_TIME_ARGS (timestamp));

  if (self->cs_a)
    ret &= gst_control_source_get_value (self->cs_a, timestamp, &src_val_a);
  if (self->cs_r)
    ret &= gst_control_source_get_value (self->cs_r, timestamp, &src_val_r);
  if (self->cs_g)
    ret &= gst_control_source_get_value (self->cs_g, timestamp, &src_val_g);
  if (self->cs_b)
    ret &= gst_control_source_get_value (self->cs_b, timestamp, &src_val_b);
  if (G_LIKELY (ret)) {
    guint src_val = (((guint) (CLAMP (src_val_a, 0.0, 1.0) * 255)) << 24) |
        (((guint) (CLAMP (src_val_r, 0.0, 1.0) * 255)) << 16) |
        (((guint) (CLAMP (src_val_g, 0.0, 1.0) * 255)) << 8) |
        ((guint) (CLAMP (src_val_b, 0.0, 1.0) * 255));
    GST_LOG_OBJECT (object, "  new value 0x%08x", src_val);
    /* always set the value for first time, but then only if it changed
     * this should limit g_object_notify invocations.
     * FIXME: can we detect negative playback rates?
     */
    if ((timestamp < last_sync) || (src_val != self->last_value)) {
      GValue *dst_val = &self->cur_value;

      g_value_set_uint (dst_val, src_val);
      /* we can make this faster
       * http://bugzilla.gnome.org/show_bug.cgi?id=536939
       */
      g_object_set_property ((GObject *) object, _self->name, dst_val);
      self->last_value = src_val;
    }
  } else {
    GST_DEBUG_OBJECT (object, "no control value for param %s", _self->name);
  }
  return (ret);
}

static GValue *
gst_control_binding_argb_get_value (GstControlBinding * _self,
    GstClockTime timestamp)
{
  GstControlBindingARGB *self = GST_CONTROL_BINDING_ARGB (_self);
  GValue *dst_val = NULL;
  gdouble src_val_a = 1.0, src_val_r = 0.0, src_val_g = 0.0, src_val_b = 0.0;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING_ARGB (self), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), NULL);

  /* get current value via control source */
  if (self->cs_a)
    ret &= gst_control_source_get_value (self->cs_a, timestamp, &src_val_a);
  if (self->cs_r)
    ret &= gst_control_source_get_value (self->cs_r, timestamp, &src_val_r);
  if (self->cs_g)
    ret &= gst_control_source_get_value (self->cs_g, timestamp, &src_val_g);
  if (self->cs_b)
    ret &= gst_control_source_get_value (self->cs_b, timestamp, &src_val_b);
  if (G_LIKELY (ret)) {
    guint src_val = (((guint) (CLAMP (src_val_a, 0.0, 1.0) * 255)) << 24) |
        (((guint) (CLAMP (src_val_r, 0.0, 1.0) * 255)) << 16) |
        (((guint) (CLAMP (src_val_g, 0.0, 1.0) * 255)) << 8) |
        ((guint) (CLAMP (src_val_b, 0.0, 1.0) * 255));
    dst_val = g_new0 (GValue, 1);
    g_value_init (dst_val, G_TYPE_UINT);
    g_value_set_uint (dst_val, src_val);
  } else {
    GST_LOG ("no control value for property %s at ts %" GST_TIME_FORMAT,
        _self->name, GST_TIME_ARGS (timestamp));
  }

  return dst_val;
}

static gboolean
gst_control_binding_argb_get_value_array (GstControlBinding * _self,
    GstClockTime timestamp, GstClockTime interval, guint n_values,
    GValue * values)
{
  GstControlBindingARGB *self = GST_CONTROL_BINDING_ARGB (_self);
  gint i;
  gdouble *src_val_a = NULL, *src_val_r = NULL, *src_val_g = NULL, *src_val_b =
      NULL;
  guint src_val;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_CONTROL_BINDING_ARGB (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (timestamp), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (interval), FALSE);
  g_return_val_if_fail (values, FALSE);

  if (self->cs_a) {
    src_val_a = g_new0 (gdouble, n_values);
    ret &= gst_control_source_get_value_array (self->cs_a, timestamp,
        interval, n_values, src_val_a);
  }
  if (self->cs_r) {
    src_val_r = g_new0 (gdouble, n_values);
    ret &= gst_control_source_get_value_array (self->cs_r, timestamp,
        interval, n_values, src_val_r);
  }
  if (self->cs_g) {
    src_val_g = g_new0 (gdouble, n_values);
    ret &= gst_control_source_get_value_array (self->cs_g, timestamp,
        interval, n_values, src_val_g);
  }
  if (self->cs_b) {
    src_val_b = g_new0 (gdouble, n_values);
    ret &= gst_control_source_get_value_array (self->cs_b, timestamp,
        interval, n_values, src_val_b);
  }
  if (G_LIKELY (ret)) {
    for (i = 0; i < n_values; i++) {
      gdouble a = 1.0, r = 0.0, g = 0.0, b = 0.0;
      if (src_val_a && !isnan (src_val_a[i]))
        a = src_val_a[i];
      if (src_val_r && !isnan (src_val_r[i]))
        r = src_val_r[i];
      if (src_val_g && !isnan (src_val_g[i]))
        g = src_val_g[i];
      if (src_val_b && !isnan (src_val_b[i]))
        b = src_val_b[i];
      src_val = (((guint) (CLAMP (a, 0.0, 1.0) * 255)) << 24) |
          (((guint) (CLAMP (r, 0.0, 1.0) * 255)) << 16) |
          (((guint) (CLAMP (g, 0.0, 1.0) * 255)) << 8) |
          ((guint) (CLAMP (b, 0.0, 1.0) * 255));
      g_value_init (&values[i], G_TYPE_UINT);
      g_value_set_uint (&values[i], src_val);
    }
  } else {
    GST_LOG ("failed to get control value for property %s at ts %"
        GST_TIME_FORMAT, _self->name, GST_TIME_ARGS (timestamp));
  }
  g_free (src_val_a);
  g_free (src_val_r);
  g_free (src_val_g);
  g_free (src_val_b);
  return ret;
}

/* mapping function */

/**
 * gst_control_binding_argb_new:
 * @object: the object of the property
 * @property_name: the property-name to attach the control source
 * @cs_a: the control source for the alpha channel
 * @cs_r: the control source for the red channel
 * @cs_g: the control source for the green channel
 * @cs_b: the control source for the blue channel
 *
 * Create a new control-binding that attaches the given #GstControlSource to the
 * #GObject property.
 *
 * Returns: the new #GstControlBindingARGB
 */
GstControlBinding *
gst_control_binding_argb_new (GstObject * object, const gchar * property_name,
    GstControlSource * cs_a, GstControlSource * cs_r, GstControlSource * cs_g,
    GstControlSource * cs_b)
{
  GstControlBindingARGB *self = NULL;
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

    g_return_val_if_fail (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_UINT, NULL);

    if ((self = (GstControlBindingARGB *)
            g_object_newv (GST_TYPE_CONTROL_BINDING_ARGB, 0, NULL))) {
      // move below to construct()
      ((GstControlBinding *) self)->pspec = pspec;
      ((GstControlBinding *) self)->name = pspec->name;
      if (cs_a)
        self->cs_a = gst_object_ref (cs_a);
      if (cs_r)
        self->cs_r = gst_object_ref (cs_r);
      if (cs_g)
        self->cs_g = gst_object_ref (cs_g);
      if (cs_b)
        self->cs_b = gst_object_ref (cs_b);

      g_value_init (&self->cur_value, G_TYPE_UINT);
    }
  } else {
    GST_WARNING_OBJECT (object, "class '%s' has no property '%s'",
        G_OBJECT_TYPE_NAME (object), property_name);
  }
  return (GstControlBinding *) self;
}

/* functions */
