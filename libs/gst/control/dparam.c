/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdparam.c: Dynamic Parameter functionality
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
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

#include <math.h>
#include <string.h>
#include <gst/gstinfo.h>

#include "dparam.h"
#include "dparammanager.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_EXTERN (_gst_control_debug);

static void gst_dparam_class_init (GstDParamClass * klass);
static void gst_dparam_init (GstDParam * dparam);
static void gst_dparam_dispose (GObject * object);
static void gst_dparam_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_dparam_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

enum
{
  ARG_0,
  ARG_VALUE_FLOAT,
  ARG_VALUE_DOUBLE,
  ARG_VALUE_INT,
  ARG_VALUE_INT64,
};

enum
{
  VALUE_CHANGED,
  LAST_SIGNAL
};

static guint gst_dparam_signals[LAST_SIGNAL] = { 0 };

GType
gst_dparam_get_type (void)
{
  static GType dparam_type = 0;

  if (!dparam_type) {
    static const GTypeInfo dparam_info = {
      sizeof (GstDParamClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_dparam_class_init,
      NULL,
      NULL,
      sizeof (GstDParam),
      0,
      (GInstanceInitFunc) gst_dparam_init,
    };

    dparam_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstDParam", &dparam_info, 0);
  }
  return dparam_type;
}

static void
gst_dparam_class_init (GstDParamClass * klass)
{
  GObjectClass *gobject_class;
  GstDParamClass *dparam_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  dparam_class = (GstDParamClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  gobject_class->get_property = gst_dparam_get_property;
  gobject_class->set_property = gst_dparam_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALUE_FLOAT,
      g_param_spec_float ("value_float", "Float Value",
          "The value that should be changed if gfloat is the type",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0F, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALUE_DOUBLE,
      g_param_spec_double ("value_double", "Double Value",
          "The value that should be changed if gdouble is the type",
          -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALUE_INT,
      g_param_spec_int ("value_int", "Integer Value",
          "The value that should be changed if gint is the type",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VALUE_INT64,
      g_param_spec_int64 ("value_int64", "64 bit Integer Value",
          "The value that should be changed if gint64 is the type",
          G_MININT64, G_MAXINT64, 0, G_PARAM_READWRITE));

  gobject_class->dispose = gst_dparam_dispose;

  gst_dparam_signals[VALUE_CHANGED] =
      g_signal_new ("value-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDParamClass, value_changed), NULL,
      NULL, gst_marshal_VOID__VOID, G_TYPE_NONE, 0);
  /*gstobject_class->save_thyself = gst_dparam_save_thyself; */

}

static void
gst_dparam_init (GstDParam * dparam)
{
  g_return_if_fail (dparam != NULL);
  GST_DPARAM_TYPE (dparam) = 0;
  GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) = 0LL;
  GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = 0LL;
  GST_DPARAM_READY_FOR_UPDATE (dparam) = FALSE;
  dparam->lock = g_mutex_new ();
}

/**
 * gst_dparam_new:
 * @type: the type that this dparam will store
 *
 * Returns: a new instance of GstDParam
 */
GstDParam *
gst_dparam_new (GType type)
{
  GstDParam *dparam;

  dparam = g_object_new (gst_dparam_get_type (), NULL);
  dparam->do_update_func = gst_dparam_do_update_default;

  GST_DPARAM_TYPE (dparam) = type;

  return dparam;
}


static void
gst_dparam_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDParam *dparam;

  g_return_if_fail (GST_IS_DPARAM (object));
  dparam = GST_DPARAM (object);

  switch (prop_id) {
    case ARG_VALUE_FLOAT:
      g_value_set_float (value, dparam->value_float);
      break;

    case ARG_VALUE_DOUBLE:
      g_value_set_double (value, dparam->value_double);
      break;

    case ARG_VALUE_INT:
      g_value_set_int (value, dparam->value_int);
      break;

    case ARG_VALUE_INT64:
      g_value_set_int64 (value, dparam->value_int64);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dparam_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstDParam *dparam;

  g_return_if_fail (GST_IS_DPARAM (object));
  dparam = GST_DPARAM (object);
  GST_DPARAM_LOCK (dparam);

  switch (prop_id) {
    case ARG_VALUE_FLOAT:
      GST_DEBUG ("setting value from %g to %g", dparam->value_float,
          g_value_get_float (value));
      dparam->value_float = g_value_get_float (value);
      GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
          GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    case ARG_VALUE_DOUBLE:
      GST_DEBUG ("setting value from %g to %g",
          dparam->value_double, g_value_get_double (value));
      dparam->value_double = g_value_get_double (value);
      GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
          GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    case ARG_VALUE_INT:
      GST_DEBUG ("setting value from %d to %d", dparam->value_int,
          g_value_get_int (value));
      dparam->value_int = g_value_get_int (value);
      GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
          GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    case ARG_VALUE_INT64:
      GST_DEBUG ("setting value from %"
          G_GINT64_FORMAT " to %"
          G_GINT64_FORMAT, dparam->value_int64, g_value_get_int64 (value));
      dparam->value_int64 = g_value_get_int64 (value);
      GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
          GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    default:
      break;
  }

  /* note that this signal is sent while we still have the lock. */
  g_signal_emit (G_OBJECT (dparam), gst_dparam_signals[VALUE_CHANGED], 0);
  GST_DPARAM_UNLOCK (dparam);
}

void
gst_dparam_do_update_default (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info)
{
  GST_DPARAM_LOCK (dparam);

  g_return_if_fail (G_VALUE_TYPE (value) == GST_DPARAM_TYPE (dparam));
  GST_DEBUG ("updating value for %s(%p)", GST_DPARAM_NAME (dparam), dparam);

  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_FLOAT:
      g_value_set_float (value, dparam->value_float);
      break;

    case G_TYPE_DOUBLE:
      g_value_set_double (value, dparam->value_double);
      break;

    case G_TYPE_INT:
      g_value_set_int (value, dparam->value_int);
      break;

    case G_TYPE_INT64:
      g_value_set_int64 (value, dparam->value_int64);
      break;

    default:
      break;
  }

  GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;
  GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) = timestamp;
  GST_DPARAM_READY_FOR_UPDATE (dparam) = FALSE;

  GST_DPARAM_UNLOCK (dparam);
}

static void
gst_dparam_dispose (GObject * object)
{
  GstDParam *dparam = GST_DPARAM (object);
  gchar *dparam_name = g_strdup (GST_DPARAM_NAME (dparam));

  GST_DEBUG ("disposing of %s", dparam_name);
  if (GST_DPARAM_MANAGER (dparam)) {
    gst_dpman_detach_dparam (GST_DPARAM_MANAGER (dparam), dparam_name);
  }
  g_free (dparam_name);
}

/**
 * gst_dparam_attach
 * @dparam: GstDParam instance
 * @manager: the GstDParamManager that this dparam belongs to
 *
 */
void
gst_dparam_attach (GstDParam * dparam, GstDParamManager * manager,
    GParamSpec * param_spec, gchar * unit_name)
{

  g_return_if_fail (dparam != NULL);
  g_return_if_fail (GST_IS_DPARAM (dparam));
  g_return_if_fail (manager != NULL);
  g_return_if_fail (GST_IS_DPMAN (manager));
  g_return_if_fail (param_spec != NULL);
  g_return_if_fail (unit_name != NULL);
  g_return_if_fail (G_IS_PARAM_SPEC (param_spec));
  g_return_if_fail (G_PARAM_SPEC_VALUE_TYPE (param_spec) ==
      GST_DPARAM_TYPE (dparam));

  GST_DPARAM_NAME (dparam) = g_param_spec_get_name (param_spec);
  GST_DPARAM_PARAM_SPEC (dparam) = param_spec;
  GST_DPARAM_MANAGER (dparam) = manager;
  GST_DPARAM_UNIT_NAME (dparam) = unit_name;
  GST_DPARAM_IS_LOG (dparam) = gst_unitconv_unit_is_logarithmic (unit_name);
  GST_DEBUG ("attaching %s to dparam %p", GST_DPARAM_NAME (dparam), dparam);

}

/**
 * gst_dparam_detach
 * @dparam: GstDParam instance
 * @manager: the GstDParamManager that this dparam belongs to
 *
 */
void
gst_dparam_detach (GstDParam * dparam)
{

  g_return_if_fail (dparam != NULL);
  g_return_if_fail (GST_IS_DPARAM (dparam));

  GST_DEBUG ("detaching %s from dparam %p", GST_DPARAM_NAME (dparam), dparam);

  GST_DPARAM_NAME (dparam) = NULL;
  GST_DPARAM_PARAM_SPEC (dparam) = NULL;
  GST_DPARAM_MANAGER (dparam) = NULL;
}
