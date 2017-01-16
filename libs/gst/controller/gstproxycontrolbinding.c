/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:gstproxycontrolbinding
 * @title: GstProxyControlBinding
 * @short_description: attachment for forwarding control sources
 * @see_also: #GstControlBinding
 *
 * A #GstControlBinding that forwards requests to another #GstControlBinding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstproxycontrolbinding.h"

G_DEFINE_TYPE (GstProxyControlBinding,
    gst_proxy_control_binding, GST_TYPE_CONTROL_BINDING);

static void
gst_proxy_control_binding_init (GstProxyControlBinding * self)
{
  g_weak_ref_init (&self->ref_object, NULL);
}

static void
gst_proxy_control_binding_finalize (GObject * object)
{
  GstProxyControlBinding *self = (GstProxyControlBinding *) object;

  g_weak_ref_clear (&self->ref_object);
  g_free (self->property_name);

  G_OBJECT_CLASS (gst_proxy_control_binding_parent_class)->finalize (object);
}

static gboolean
gst_proxy_control_binding_sync_values (GstControlBinding * binding,
    GstObject * object, GstClockTime timestamp, GstClockTime last_sync)
{
  GstProxyControlBinding *self = (GstProxyControlBinding *)
      binding;
  gboolean ret = TRUE;
  GstObject *ref_object;

  ref_object = g_weak_ref_get (&self->ref_object);
  if (ref_object) {
    GstControlBinding *ref_binding =
        gst_object_get_control_binding (ref_object, self->property_name);
    if (ref_binding) {
      ret = gst_control_binding_sync_values (ref_binding, ref_object,
          timestamp, last_sync);
      gst_object_unref (ref_binding);
    }
    gst_object_unref (ref_object);
  }

  return ret;
}

static GValue *
gst_proxy_control_binding_get_value (GstControlBinding * binding,
    GstClockTime timestamp)
{
  GstProxyControlBinding *self = (GstProxyControlBinding *)
      binding;
  GValue *ret = NULL;
  GstObject *ref_object;

  ref_object = g_weak_ref_get (&self->ref_object);
  if (ref_object) {
    GstControlBinding *ref_binding =
        gst_object_get_control_binding (ref_object, self->property_name);
    if (ref_binding) {
      ret = gst_control_binding_get_value (ref_binding, timestamp);
      gst_object_unref (ref_binding);
    }
    gst_object_unref (ref_object);
  }

  return ret;
}

static gboolean
gst_proxy_control_binding_get_value_array (GstControlBinding * binding,
    GstClockTime timestamp, GstClockTime interval, guint n_values,
    gpointer values)
{
  GstProxyControlBinding *self = (GstProxyControlBinding *)
      binding;
  gboolean ret = FALSE;
  GstObject *ref_object;

  ref_object = g_weak_ref_get (&self->ref_object);
  if (ref_object) {
    GstControlBinding *ref_binding =
        gst_object_get_control_binding (ref_object, self->property_name);
    if (ref_binding) {
      ret = gst_control_binding_get_value_array (ref_binding, timestamp,
          interval, n_values, values);
      gst_object_unref (ref_binding);
    }
    gst_object_unref (ref_object);
  }

  return ret;
}

static gboolean
gst_proxy_control_binding_get_g_value_array (GstControlBinding *
    binding, GstClockTime timestamp, GstClockTime interval, guint n_values,
    GValue * values)
{
  GstProxyControlBinding *self = (GstProxyControlBinding *) binding;
  gboolean ret = FALSE;
  GstObject *ref_object;

  ref_object = g_weak_ref_get (&self->ref_object);
  if (ref_object) {
    GstControlBinding *ref_binding =
        gst_object_get_control_binding (ref_object, self->property_name);
    if (ref_binding) {
      ret = gst_control_binding_get_g_value_array (ref_binding, timestamp,
          interval, n_values, values);
      gst_object_unref (ref_binding);
    }
    gst_object_unref (ref_object);
  }

  return ret;
}

static void
gst_proxy_control_binding_class_init (GstProxyControlBindingClass * klass)
{
  GstControlBindingClass *cb_class = GST_CONTROL_BINDING_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  cb_class->sync_values = gst_proxy_control_binding_sync_values;
  cb_class->get_value = gst_proxy_control_binding_get_value;
  cb_class->get_value_array = gst_proxy_control_binding_get_value_array;
  cb_class->get_g_value_array = gst_proxy_control_binding_get_g_value_array;

  gobject_class->finalize = gst_proxy_control_binding_finalize;
}

/**
 * gst_proxy_control_binding_new:
 * @object: (transfer none): a #GstObject
 * @property_name: the property name in @object to control
 * @ref_object: (transfer none): a #GstObject to forward all
 *              #GstControlBinding requests to
 * @ref_property_name: the property_name in @ref_object to control
 *
 * #GstProxyControlBinding forwards all access to data or sync_values()
 * requests from @property_name on @object to the control binding at
 * @ref_property_name on @ref_object.
 *
 * Returns: a new #GstControlBinding that proxies the control interface between
 * properties on different #GstObject's
 *
 * Since: 1.12
 */
GstControlBinding *
gst_proxy_control_binding_new (GstObject * object, const gchar * property_name,
    GstObject * ref_object, const gchar * ref_property_name)
{
  GstProxyControlBinding *cb;

  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);
  g_return_val_if_fail (property_name != NULL, NULL);
  g_return_val_if_fail (GST_IS_OBJECT (ref_object), NULL);
  g_return_val_if_fail (ref_property_name != NULL, NULL);

  cb = g_object_new (GST_TYPE_PROXY_CONTROL_BINDING, "object", object,
      "name", property_name, NULL);

  g_weak_ref_set (&cb->ref_object, ref_object);
  cb->property_name = g_strdup (ref_property_name);

  return (GstControlBinding *) cb;
}
