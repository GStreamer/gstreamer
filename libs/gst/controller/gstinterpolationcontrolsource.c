/* GStreamer
 *
 * Copyright (C) 2007,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * gstinterpolationcontrolsource.c: Control source that provides several
 *                                  interpolation methods
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
 * SECTION:gstinterpolationcontrolsource
 * @short_description: interpolation control source
 *
 * #GstInterpolationControlSource is a #GstControlSource, that interpolates values between user-given
 * control points. It supports several interpolation modes and property types.
 *
 * To use #GstInterpolationControlSource get a new instance by calling
 * gst_interpolation_control_source_new(), bind it to a #GParamSpec and set some
 * control points by calling gst_timed_value_control_source_set().
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

enum
{
  PROP_MODE = 1
};

GType
gst_interpolation_mode_get_type (void)
{
  static gsize gtype = 0;
  static const GEnumValue values[] = {
    {GST_INTERPOLATION_MODE_NONE, "GST_INTERPOLATION_MODE_NONE", "none"},
    {GST_INTERPOLATION_MODE_LINEAR, "GST_INTERPOLATION_MODE_LINEAR", "linear"},
    {GST_INTERPOLATION_MODE_CUBIC, "GST_INTERPOLATION_MODE_CUBIC", "cubic"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&gtype)) {
    GType tmp = g_enum_register_static ("GstLFOWaveform", values);
    g_once_init_leave (&gtype, tmp);
  }

  return (GType) gtype;
}


#define _do_init \
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "interpolation control source", 0, \
    "timeline value interpolating control source")

G_DEFINE_TYPE_WITH_CODE (GstInterpolationControlSource,
    gst_interpolation_control_source, GST_TYPE_TIMED_VALUE_CONTROL_SOURCE,
    _do_init);

struct _GstInterpolationControlSourcePrivate
{
  GstInterpolationMode interpolation_mode;
};

/**
 * gst_interpolation_control_source_new:
 *
 * This returns a new, unbound #GstInterpolationControlSource.
 *
 * Returns: a new, unbound #GstInterpolationControlSource.
 */
GstInterpolationControlSource *
gst_interpolation_control_source_new (void)
{
  return g_object_newv (GST_TYPE_INTERPOLATION_CONTROL_SOURCE, 0, NULL);
}

static gboolean
    gst_interpolation_control_source_set_interpolation_mode
    (GstInterpolationControlSource * self, GstInterpolationMode mode)
{
  gboolean ret = TRUE;
  GstControlSource *csource = GST_CONTROL_SOURCE (self);

  if (mode >= priv_gst_num_interpolation_methods
      || priv_gst_interpolation_methods[mode] == NULL) {
    GST_WARNING ("interpolation mode %d invalid or not implemented yet", mode);
    return FALSE;
  }

  GST_TIMED_VALUE_CONTROL_SOURCE_LOCK (self);
  switch (gst_timed_value_control_source_get_base_value_type (
          (GstTimedValueControlSource *) self)) {
    case G_TYPE_INT:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_int;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_int_value_array;
      break;
    case G_TYPE_UINT:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_uint;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_uint_value_array;
      break;
    case G_TYPE_LONG:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_long;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_long_value_array;
      break;
    case G_TYPE_ULONG:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_ulong;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_ulong_value_array;
      break;
    case G_TYPE_INT64:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_int64;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_int64_value_array;
      break;
    case G_TYPE_UINT64:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_uint64;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_uint64_value_array;
      break;
    case G_TYPE_FLOAT:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_float;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_float_value_array;
      break;
    case G_TYPE_DOUBLE:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_double;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_double_value_array;
      break;
    case G_TYPE_BOOLEAN:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_boolean;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_boolean_value_array;
      break;
    case G_TYPE_ENUM:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_enum;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_enum_value_array;
      break;
    case G_TYPE_STRING:
      csource->get_value = priv_gst_interpolation_methods[mode]->get_string;
      csource->get_value_array =
          priv_gst_interpolation_methods[mode]->get_string_value_array;
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
  self->priv->interpolation_mode = mode;

  GST_TIMED_VALUE_CONTROL_SOURCE_UNLOCK (self);

  return ret;
}

static gboolean
gst_interpolation_control_source_bind (GstControlSource * csource,
    GParamSpec * pspec)
{
  if (GST_CONTROL_SOURCE_CLASS
      (gst_interpolation_control_source_parent_class)->bind (csource, pspec)) {
    GstInterpolationControlSource *self =
        GST_INTERPOLATION_CONTROL_SOURCE (csource);

    if (gst_interpolation_control_source_set_interpolation_mode (self,
            self->priv->interpolation_mode))
      return TRUE;
  }
  return FALSE;
}

static void
gst_interpolation_control_source_init (GstInterpolationControlSource * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_INTERPOLATION_CONTROL_SOURCE,
      GstInterpolationControlSourcePrivate);
  self->priv->interpolation_mode = GST_INTERPOLATION_MODE_NONE;
}

static void
gst_interpolation_control_source_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInterpolationControlSource *self =
      GST_INTERPOLATION_CONTROL_SOURCE (object);

  switch (prop_id) {
    case PROP_MODE:
      gst_interpolation_control_source_set_interpolation_mode (self,
          (GstInterpolationMode) g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_interpolation_control_source_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstInterpolationControlSource *self =
      GST_INTERPOLATION_CONTROL_SOURCE (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, self->priv->interpolation_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_interpolation_control_source_class_init (GstInterpolationControlSourceClass
    * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstControlSourceClass *csource_class = GST_CONTROL_SOURCE_CLASS (klass);

  g_type_class_add_private (klass,
      sizeof (GstInterpolationControlSourcePrivate));

  gobject_class->set_property = gst_interpolation_control_source_set_property;
  gobject_class->get_property = gst_interpolation_control_source_get_property;

  csource_class->bind = gst_interpolation_control_source_bind;

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Interpolation mode",
          GST_TYPE_INTERPOLATION_MODE, GST_INTERPOLATION_MODE_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
