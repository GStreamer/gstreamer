/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdparam_smooth.c: Realtime smoothed dynamic parameter
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

#include "dparam_smooth.h"
#include "dparammanager.h"

GST_DEBUG_CATEGORY_EXTERN (_gst_control_debug);
#define GST_CAT_DEFAULT _gst_control_debug

static void gst_dpsmooth_class_init (GstDParamSmoothClass * klass);
static void gst_dpsmooth_init (GstDParamSmooth * dparam);
static void gst_dpsmooth_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dpsmooth_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_dpsmooth_do_update_float (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info);
static void gst_dpsmooth_value_changed_float (GstDParam * dparam);
static void gst_dpsmooth_do_update_double (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info);
static void gst_dpsmooth_value_changed_double (GstDParam * dparam);

enum
{
  ARG_0,
  ARG_UPDATE_PERIOD,
  ARG_SLOPE_TIME,
  ARG_SLOPE_DELTA_FLOAT,
  ARG_SLOPE_DELTA_DOUBLE,
  ARG_SLOPE_DELTA_INT,
  ARG_SLOPE_DELTA_INT64,
};

GType
gst_dpsmooth_get_type (void)
{
  static GType dpsmooth_type = 0;

  if (!dpsmooth_type) {
    static const GTypeInfo dpsmooth_info = {
      sizeof (GstDParamSmoothClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_dpsmooth_class_init,
      NULL,
      NULL,
      sizeof (GstDParamSmooth),
      0,
      (GInstanceInitFunc) gst_dpsmooth_init,
    };

    dpsmooth_type =
        g_type_register_static (GST_TYPE_DPARAM, "GstDParamSmooth",
        &dpsmooth_info, 0);
  }
  return dpsmooth_type;
}

static void
gst_dpsmooth_class_init (GstDParamSmoothClass * klass)
{
  GObjectClass *gobject_class;
  GstDParamSmoothClass *dpsmooth_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  dpsmooth_class = (GstDParamSmoothClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  gobject_class->get_property = gst_dpsmooth_get_property;
  gobject_class->set_property = gst_dpsmooth_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_UPDATE_PERIOD,
      g_param_spec_int64 ("update_period",
          "Update Period (nanoseconds)",
          "Number of nanoseconds between updates",
          0LL, G_MAXINT64, 2000000LL, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SLOPE_TIME,
      g_param_spec_int64 ("slope_time",
          "Slope Time (nanoseconds)",
          "The time period to define slope_delta by",
          0LL, G_MAXINT64, 10000000LL, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_SLOPE_DELTA_FLOAT,
      g_param_spec_float ("slope_delta_float", "Slope Delta float",
          "The amount a float value can change for a given slope_time",
          0.0F, G_MAXFLOAT, 0.2F, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_SLOPE_DELTA_DOUBLE,
      g_param_spec_double ("slope_delta_double", "Slope Delta double",
          "The amount a double value can change for a given slope_time",
          0.0, G_MAXDOUBLE, 0.2, G_PARAM_READWRITE));

  /*gstobject_class->save_thyself = gst_dparam_save_thyself; */

}

static void
gst_dpsmooth_init (GstDParamSmooth * dpsmooth)
{
  g_return_if_fail (dpsmooth != NULL);
}

/**
 * gst_dpsmooth_new:
 * @type: the type that this dparam will store
 *
 * Create a new dynamic parameter controller which smoothes control changes.
 *
 * Returns: a new instance of GstDParam
 */
GstDParam *
gst_dpsmooth_new (GType type)
{
  GstDParam *dparam;
  GstDParamSmooth *dpsmooth;

  dpsmooth = g_object_new (gst_dpsmooth_get_type (), NULL);
  dparam = GST_DPARAM (dpsmooth);

  GST_DPARAM_TYPE (dparam) = type;

  switch (type) {
    case G_TYPE_FLOAT:{
      dparam->do_update_func = gst_dpsmooth_do_update_float;
      g_signal_connect (G_OBJECT (dpsmooth), "value_changed",
          G_CALLBACK (gst_dpsmooth_value_changed_float), NULL);
      break;
    }
    case G_TYPE_DOUBLE:{
      dparam->do_update_func = gst_dpsmooth_do_update_double;
      g_signal_connect (G_OBJECT (dpsmooth), "value_changed",
          G_CALLBACK (gst_dpsmooth_value_changed_double), NULL);
      break;
    }
    default:
      /* we don't support this type here */
      dparam->do_update_func = gst_dparam_do_update_default;
      break;
  }
  return dparam;
}

static void
gst_dpsmooth_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDParam *dparam;
  GstDParamSmooth *dpsmooth;

  g_return_if_fail (GST_IS_DPSMOOTH (object));

  dpsmooth = GST_DPSMOOTH (object);
  dparam = GST_DPARAM (object);

  GST_DPARAM_LOCK (dparam);

  switch (prop_id) {
    case ARG_UPDATE_PERIOD:
      dpsmooth->update_period = g_value_get_int64 (value);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    case ARG_SLOPE_TIME:
      dpsmooth->slope_time = g_value_get_int64 (value);
      GST_DEBUG ("dpsmooth->slope_time:%"
          G_GINT64_FORMAT, dpsmooth->slope_time);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    case ARG_SLOPE_DELTA_FLOAT:
      dpsmooth->slope_delta_float = g_value_get_float (value);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    case ARG_SLOPE_DELTA_DOUBLE:
      dpsmooth->slope_delta_double = g_value_get_double (value);
      GST_DPARAM_READY_FOR_UPDATE (dparam) = TRUE;
      break;

    default:
      break;
  }
  GST_DPARAM_UNLOCK (dparam);
}

static void
gst_dpsmooth_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDParam *dparam;
  GstDParamSmooth *dpsmooth;

  g_return_if_fail (GST_IS_DPSMOOTH (object));

  dpsmooth = GST_DPSMOOTH (object);
  dparam = GST_DPARAM (object);

  switch (prop_id) {
    case ARG_UPDATE_PERIOD:
      g_value_set_int64 (value, dpsmooth->update_period);
      break;
    case ARG_SLOPE_TIME:
      g_value_set_int64 (value, dpsmooth->slope_time);
      break;
    case ARG_SLOPE_DELTA_FLOAT:
      g_value_set_float (value, dpsmooth->slope_delta_float);
      break;
    case ARG_SLOPE_DELTA_DOUBLE:
      g_value_set_double (value, dpsmooth->slope_delta_double);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dpsmooth_value_changed_float (GstDParam * dparam)
{
  GstDParamSmooth *dpsmooth;
  gfloat time_ratio;

  g_return_if_fail (GST_IS_DPSMOOTH (dparam));
  dpsmooth = GST_DPSMOOTH (dparam);

  if (GST_DPARAM_IS_LOG (dparam)) {
    dparam->value_float = log (dparam->value_float);
  }
  dpsmooth->start_float = dpsmooth->current_float;
  dpsmooth->diff_float = dparam->value_float - dpsmooth->start_float;

  time_ratio = ABS (dpsmooth->diff_float) / dpsmooth->slope_delta_float;
  dpsmooth->duration_interp =
      (gint64) (time_ratio * (gfloat) dpsmooth->slope_time);

  dpsmooth->need_interp_times = TRUE;

  GST_DEBUG ("%f to %f ratio:%f duration:%"
      G_GINT64_FORMAT "\n",
      dpsmooth->start_float, dparam->value_float, time_ratio,
      dpsmooth->duration_interp);
}

static void
gst_dpsmooth_do_update_float (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info)
{
  gfloat time_ratio;
  GstDParamSmooth *dpsmooth = GST_DPSMOOTH (dparam);

  GST_DPARAM_LOCK (dparam);

  if (dpsmooth->need_interp_times) {
    dpsmooth->start_interp = timestamp;
    dpsmooth->end_interp = timestamp + dpsmooth->duration_interp;
    dpsmooth->need_interp_times = FALSE;
  }

  if ((update_info == GST_DPARAM_UPDATE_FIRST)
      || (timestamp >= dpsmooth->end_interp)) {
    if (GST_DPARAM_IS_LOG (dparam)) {
      g_value_set_float (value, exp (dparam->value_float));
    } else {
      g_value_set_float (value, dparam->value_float);
    }
    dpsmooth->current_float = dparam->value_float;

    GST_DEBUG ("interp finished at %" G_GINT64_FORMAT, timestamp);

    GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;
    GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) = timestamp;

    GST_DPARAM_READY_FOR_UPDATE (dparam) = FALSE;
    GST_DPARAM_UNLOCK (dparam);
    return;
  }

  if (timestamp <= dpsmooth->start_interp) {
    if (GST_DPARAM_IS_LOG (dparam)) {
      g_value_set_float (value, exp (dpsmooth->start_float));
    } else {
      g_value_set_float (value, dpsmooth->start_float);
    }
    GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;
    GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
        dpsmooth->start_interp + dpsmooth->update_period;

    GST_DEBUG ("interp started at %" G_GINT64_FORMAT, timestamp);

    GST_DPARAM_UNLOCK (dparam);
    return;

  }

  time_ratio =
      (gfloat) (timestamp -
      dpsmooth->start_interp) / (gfloat) dpsmooth->duration_interp;

  GST_DEBUG ("start:%" G_GINT64_FORMAT " current:%" G_GINT64_FORMAT " end:%"
      G_GINT64_FORMAT " ratio%f", dpsmooth->start_interp, timestamp,
      dpsmooth->end_interp, time_ratio);
  GST_DEBUG ("pre  start:%f current:%f target:%f", dpsmooth->start_float,
      dpsmooth->current_float, dparam->value_float);

  dpsmooth->current_float =
      dpsmooth->start_float + (dpsmooth->diff_float * time_ratio);

  GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
      timestamp + dpsmooth->update_period;
  if (GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) > dpsmooth->end_interp) {
    GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) = dpsmooth->end_interp;
  }

  GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;

  if (GST_DPARAM_IS_LOG (dparam)) {
    g_value_set_float (value, exp (dpsmooth->current_float));
  } else {
    g_value_set_float (value, dpsmooth->current_float);
  }

  GST_DEBUG ("post start:%f current:%f target:%f", dpsmooth->start_float,
      dpsmooth->current_float, dparam->value_float);

  GST_DPARAM_UNLOCK (dparam);
}

static void
gst_dpsmooth_value_changed_double (GstDParam * dparam)
{
  GstDParamSmooth *dpsmooth;
  gdouble time_ratio;

  g_return_if_fail (GST_IS_DPSMOOTH (dparam));
  dpsmooth = GST_DPSMOOTH (dparam);

  if (GST_DPARAM_IS_LOG (dparam)) {
    dparam->value_double = log (dparam->value_double);
  }
  dpsmooth->start_double = dpsmooth->current_double;
  dpsmooth->diff_double = dparam->value_double - dpsmooth->start_double;

  time_ratio = ABS (dpsmooth->diff_double) / dpsmooth->slope_delta_double;
  dpsmooth->duration_interp =
      (gint64) (time_ratio * (gdouble) dpsmooth->slope_time);

  dpsmooth->need_interp_times = TRUE;

  GST_DEBUG ("%f to %f ratio:%f duration:%"
      G_GINT64_FORMAT "\n",
      dpsmooth->start_double, dparam->value_double, time_ratio,
      dpsmooth->duration_interp);
}

static void
gst_dpsmooth_do_update_double (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info)
{
  gdouble time_ratio;
  GstDParamSmooth *dpsmooth = GST_DPSMOOTH (dparam);

  GST_DPARAM_LOCK (dparam);

  if (dpsmooth->need_interp_times) {
    dpsmooth->start_interp = timestamp;
    dpsmooth->end_interp = timestamp + dpsmooth->duration_interp;
    dpsmooth->need_interp_times = FALSE;
  }

  if ((update_info == GST_DPARAM_UPDATE_FIRST)
      || (timestamp >= dpsmooth->end_interp)) {
    if (GST_DPARAM_IS_LOG (dparam)) {
      g_value_set_double (value, exp (dparam->value_double));
    } else {
      g_value_set_double (value, dparam->value_double);
    }
    dpsmooth->current_double = dparam->value_double;

    GST_DEBUG ("interp finished at %" G_GINT64_FORMAT, timestamp);

    GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;
    GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) = timestamp;

    GST_DPARAM_READY_FOR_UPDATE (dparam) = FALSE;
    GST_DPARAM_UNLOCK (dparam);
    return;
  }

  if (timestamp <= dpsmooth->start_interp) {
    if (GST_DPARAM_IS_LOG (dparam)) {
      g_value_set_double (value, exp (dpsmooth->start_double));
    } else {
      g_value_set_double (value, dpsmooth->start_double);
    }
    GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;
    GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
        dpsmooth->start_interp + dpsmooth->update_period;

    GST_DEBUG ("interp started at %" G_GINT64_FORMAT, timestamp);

    GST_DPARAM_UNLOCK (dparam);
    return;

  }

  time_ratio =
      (gdouble) (timestamp -
      dpsmooth->start_interp) / (gdouble) dpsmooth->duration_interp;

  GST_DEBUG ("start:%" G_GINT64_FORMAT " current:%" G_GINT64_FORMAT " end:%"
      G_GINT64_FORMAT " ratio%f", dpsmooth->start_interp, timestamp,
      dpsmooth->end_interp, time_ratio);
  GST_DEBUG ("pre  start:%f current:%f target:%f", dpsmooth->start_double,
      dpsmooth->current_double, dparam->value_double);

  dpsmooth->current_double =
      dpsmooth->start_double + (dpsmooth->diff_double * time_ratio);

  GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) =
      timestamp + dpsmooth->update_period;
  if (GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) > dpsmooth->end_interp) {
    GST_DPARAM_NEXT_UPDATE_TIMESTAMP (dparam) = dpsmooth->end_interp;
  }

  GST_DPARAM_LAST_UPDATE_TIMESTAMP (dparam) = timestamp;

  if (GST_DPARAM_IS_LOG (dparam)) {
    g_value_set_double (value, exp (dpsmooth->current_double));
  } else {
    g_value_set_double (value, dpsmooth->current_double);
  }

  GST_DEBUG ("post start:%f current:%f target:%f", dpsmooth->start_double,
      dpsmooth->current_double, dparam->value_double);

  GST_DPARAM_UNLOCK (dparam);
}
