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

#include <math.h>
#include <string.h>
#include <gst/gstinfo.h>

#include "dparam_smooth.h"
#include "dparammanager.h"

static void gst_dpsmooth_class_init (GstDParamSmoothClass *klass);
static void gst_dpsmooth_init (GstDParamSmooth *dparam);
static void gst_dpsmooth_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_dpsmooth_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_dpsmooth_do_update_float (GstDParam *dparam, gint64 timestamp, GValue *value, GstDParamUpdateInfo update_info);

enum {
	ARG_0,
	ARG_UPDATE_PERIOD,
	ARG_SLOPE_TIME,
	ARG_SLOPE_DELTA_FLOAT,
	ARG_SLOPE_DELTA_INT,
	ARG_SLOPE_DELTA_INT64,
};

GType 
gst_dpsmooth_get_type(void) {
	static GType dpsmooth_type = 0;

	if (!dpsmooth_type) {
		static const GTypeInfo dpsmooth_info = {
			sizeof(GstDParamSmoothClass),
			NULL,
			NULL,
			(GClassInitFunc)gst_dpsmooth_class_init,
			NULL,
			NULL,
			sizeof(GstDParamSmooth),
			0,
			(GInstanceInitFunc)gst_dpsmooth_init,
		};
		dpsmooth_type = g_type_register_static(GST_TYPE_DPARAM, "GstDParamSmooth", &dpsmooth_info, 0);
	}
	return dpsmooth_type;
}

static void
gst_dpsmooth_class_init (GstDParamSmoothClass *klass)
{
	GObjectClass *gobject_class;
	GstDParamSmoothClass *dpsmooth_class;
	GstObjectClass *gstobject_class;

	gobject_class = (GObjectClass*)klass;
	dpsmooth_class = (GstDParamSmoothClass*)klass;
	gstobject_class = (GstObjectClass*) klass;
	
	gobject_class->get_property = gst_dpsmooth_get_property;
	gobject_class->set_property = gst_dpsmooth_set_property;
	
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_UPDATE_PERIOD,
		g_param_spec_int64("update_period", 
		                   "Update Period (nanoseconds)", 
		                   "Number of nanoseconds between updates",
		                   0LL, G_MAXINT64, 2000000LL, G_PARAM_READWRITE));
		                   
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SLOPE_TIME,
		g_param_spec_int64("slope_time", 
		                   "Slope Time (nanoseconds)", 
		                   "The time period to define slope_delta by",
		                   0LL, G_MAXINT64, 10000000LL, G_PARAM_READWRITE));
		                   
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SLOPE_DELTA_FLOAT,
		g_param_spec_float("slope_delta_float", 
		                   "Slope Delta float", 
		                   "The amount a float value can change for a given slope_time",
		                   0.0F, G_MAXFLOAT, 0.2F, G_PARAM_READWRITE));
	
	/*gstobject_class->save_thyself = gst_dparam_save_thyself; */

}

static void
gst_dpsmooth_init (GstDParamSmooth *dpsmooth)
{
	g_return_if_fail (dpsmooth != NULL);
}

/**
 * gst_dpsmooth_new:
 * @type: the type that this dparam will store
 *
 * Returns: a new instance of GstDParam
 */
GstDParam* 
gst_dpsmooth_new (GType type)
{
	GstDParam *dparam;
	GstDParamSmooth *dpsmooth;
	
	dpsmooth =g_object_new (gst_dpsmooth_get_type (), NULL);
	dparam = GST_DPARAM(dpsmooth);
	
	GST_DPARAM_TYPE(dparam) = type;

	switch (type){
		case G_TYPE_FLOAT: {
			dparam->do_update_func = gst_dpsmooth_do_update_float;
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
gst_dpsmooth_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
	GstDParam *dparam;
	GstDParamSmooth *dpsmooth;

	g_return_if_fail(GST_IS_DPSMOOTH(object));
	
	dpsmooth = GST_DPSMOOTH(object);
	dparam = GST_DPARAM(object);
	
	GST_DPARAM_LOCK(dparam);

	switch (prop_id) {
		case ARG_UPDATE_PERIOD:
			dpsmooth->update_period = g_value_get_int64 (value);
			GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
			break;

		case ARG_SLOPE_TIME:
			dpsmooth->slope_time = g_value_get_int64 (value);
			GST_DEBUG(GST_CAT_PARAMS, "dpsmooth->slope_time:%lld",dpsmooth->slope_time);
			GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
			break;

		case ARG_SLOPE_DELTA_FLOAT:
			dpsmooth->slope_delta_float = g_value_get_float (value);
			GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
			break;
			
		default:
			break;
	}
	GST_DPARAM_UNLOCK(dparam);
}

static void 
gst_dpsmooth_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
	GstDParam *dparam;
	GstDParamSmooth *dpsmooth;

	g_return_if_fail(GST_IS_DPSMOOTH(object));
	
	dpsmooth = GST_DPSMOOTH(object);
	dparam = GST_DPARAM(object);
	
	switch (prop_id) {
		case ARG_UPDATE_PERIOD:
			g_value_set_int64(value, dpsmooth->update_period);
			break;
		case ARG_SLOPE_TIME:
			g_value_set_int64(value, dpsmooth->slope_time);
			break;
		case ARG_SLOPE_DELTA_FLOAT:
			g_value_set_float (value, dpsmooth->slope_delta_float);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_dpsmooth_do_update_float (GstDParam *dparam, gint64 timestamp, GValue *value, GstDParamUpdateInfo update_info)
{
	gint64 time_diff;
	gfloat time_ratio;
	gfloat current, target, max_change, final_val;
	gfloat current_diff = 0;
	
	GstDParamSmooth *dpsmooth = GST_DPSMOOTH(dparam);

	GST_DPARAM_LOCK(dparam);

	if (update_info == GST_DPARAM_UPDATE_FIRST){
		/*this is the first update since the pipeline started.
		* the value won't be smoothed, it will be updated immediately
		*/
		g_value_set_float(value, dparam->value_float); 
		GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam) = timestamp;  
		GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) = timestamp;
		
		GST_DPARAM_READY_FOR_UPDATE(dparam) = FALSE;
		GST_DPARAM_UNLOCK(dparam);
		return;
	}
	
	time_diff = timestamp - GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam);
	
	target = dparam->value_float;
	current = g_value_get_float(value);

	time_ratio = (gfloat)time_diff / (gfloat)dpsmooth->slope_time;

	max_change = time_ratio * dpsmooth->slope_delta_float;

	GST_DEBUG(GST_CAT_PARAMS, "target:%f current:%f max_change:%f ", 
	                           target, current, max_change);
	                           
	if (GST_DPARAM_IS_LOG(dparam)){
		if (current == 0.0F){
			/* this shouldn't happen, so forget about smoothing and just set the value */
			final_val = target;
		}
		else {
			gfloat current_log;
			current_log = log(current);
			current_diff = ABS(current_log - log(target));
			
			GST_DEBUG(GST_CAT_PARAMS, "current_log:%f",current_log);
			GST_DEBUG(GST_CAT_PARAMS, "current_diff:%f",current_diff);
	
			if (current_diff > max_change){
				final_val = (target < current) ? exp(current_log-max_change) : exp(current_log+max_change);
			}
			else {
				final_val = target;
			}
		}
	} 
	else {
		current_diff = ABS (current - target);
		if (current_diff > max_change){
			final_val = (target < current) ? current-max_change : current+max_change;
		}
		else {
			final_val = target;									
		}
	}

	GST_DPARAM_READY_FOR_UPDATE(dparam) = (final_val != target);
	if (GST_DPARAM_READY_FOR_UPDATE(dparam)){
		GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) = timestamp + dpsmooth->update_period; 
	}
	GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam) = timestamp;

	g_value_set_float(value, final_val);
		                           
 	GST_DEBUG(GST_CAT_PARAMS, "target:%f current:%f final:%f actual:%f", target, current, final_val, g_value_get_float(value));

	GST_DPARAM_UNLOCK(dparam);
}

