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

#include "gst_private.h"

#include "gstdparam.h"
#include "gstdparammanager.h"

static void gst_dparam_class_init (GstDParamClass *klass);
static void gst_dparam_base_class_init (GstDParamClass *klass);
static void gst_dparam_init (GstDParam *dparam);

static void gst_dparam_do_update_realtime (GstDParam *dparam, gint64 timestamp);
static GValue** gst_dparam_get_point_realtime (GstDParam *dparam, gint64 timestamp);

GType 
gst_dparam_get_type(void) {
	static GType dparam_type = 0;

	if (!dparam_type) {
		static const GTypeInfo dparam_info = {
			sizeof(GstDParamClass),
			(GBaseInitFunc)gst_dparam_base_class_init,
			NULL,
			(GClassInitFunc)gst_dparam_class_init,
			NULL,
			NULL,
			sizeof(GstDParam),
			0,
			(GInstanceInitFunc)gst_dparam_init,
		};
		dparam_type = g_type_register_static(GST_TYPE_OBJECT, "GstDParam", &dparam_info, 0);
	}
	return dparam_type;
}

static void
gst_dparam_base_class_init (GstDParamClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = (GObjectClass*) klass;

}

static void
gst_dparam_class_init (GstDParamClass *klass)
{
	GObjectClass *gobject_class;
	GstDParamClass *dparam_class;
	GstObjectClass *gstobject_class;

	gobject_class = (GObjectClass*)klass;
	dparam_class = (GstDParamClass*)klass;
	gstobject_class = (GstObjectClass*) klass;

	//gstobject_class->save_thyself = gst_dparam_save_thyself;

}

static void
gst_dparam_init (GstDParam *dparam)
{
	g_return_if_fail (dparam != NULL);
	GST_DPARAM_VALUE(dparam) = NULL;
	GST_DPARAM_TYPE(dparam) = 0;
	GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam)=0LL;
	GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam)=0LL;
	GST_DPARAM_READY_FOR_UPDATE(dparam)=FALSE;
	dparam->lock = g_mutex_new ();
}

/**
 * gst_dparam_new:
 * @type: the type that this dparam will store
 *
 * Returns: a new instance of GstDParam
 */
GstDParam* 
gst_dparam_new (GType type)
{
	GstDParam *dparam;

	dparam = g_object_new (gst_dparam_get_type (), NULL);
	dparam->do_update_func = gst_dparam_do_update_realtime;
	dparam->get_point_func = gst_dparam_get_point_realtime;
	
	dparam->point = gst_dparam_new_value_array(type, 0);	
	GST_DPARAM_TYPE(dparam) = type;
	
	return dparam;
}

/**
 * gst_dparam_attach
 * @dparam: GstDParam instance
 * @parent: the GstDParamManager that this dparam belongs to
 *
 */
void
gst_dparam_attach (GstDParam *dparam, GstObject *parent, gchar *dparam_name, GValue *value)
{
	
	g_return_if_fail (dparam != NULL);
	g_return_if_fail (GST_IS_DPARAM (dparam));
	g_return_if_fail (GST_DPARAM_PARENT (dparam) == NULL);
	g_return_if_fail (parent != NULL);
	g_return_if_fail (G_IS_OBJECT (parent));
	g_return_if_fail (GST_IS_DPMAN (parent));
	g_return_if_fail ((gpointer)dparam != (gpointer)parent);
	g_return_if_fail (dparam_name != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (GST_DPARAM_TYPE(dparam) == G_VALUE_TYPE(value));
	
	GST_DPARAM_NAME(dparam) = dparam_name;
	GST_DPARAM_VALUE(dparam) = value;
	gst_object_set_parent (GST_OBJECT (dparam), parent);
}

/**
 * gst_dparam_new_value_array
 * @type: the type of the first GValue in the array
 * @...: the type of other GValues in the array
 *
 * The list of types should be terminated with a 0.
 * If the type of a value is not yet known then use G_TYPE_NONE .
 *
 * Returns: an newly created array of GValues
 */
GValue**
gst_dparam_new_value_array(GType type, ...)
{
	GValue **point;
	GValue *value;
	guint x;
	gint values_length = 0;
	va_list var_args;
	GType each_type;

	va_start (var_args, type);
	each_type = type;
	while (each_type){
		values_length++;
		each_type = va_arg (var_args, GType);
	}
	va_end (var_args);
	
	point = g_new0(GValue*,values_length + 1);

	va_start (var_args, type);
	each_type = type;
	for (x=0 ; x < values_length ; x++){
		value = g_new0(GValue,1);
		if (each_type != G_TYPE_NONE){
			g_value_init(value, each_type);
		}
		point[x] = value;
		each_type = va_arg (var_args, GType);
	}
	point[values_length] = NULL;
	va_end (var_args);
	
	GST_DEBUG(GST_CAT_PARAMS, "array with %d values created\n", values_length);

	return point;
}

void
gst_dparam_set_value_from_string(GValue *value, const gchar *value_str)
{

	g_return_if_fail(value != NULL);
	g_return_if_fail(value_str != NULL);
	
	GST_DEBUG(GST_CAT_PARAMS, "parsing '%s' to type %s\n", value_str, g_type_name(G_VALUE_TYPE(value)));

	switch (G_VALUE_TYPE(value)) {
		case G_TYPE_STRING:
			g_value_set_string(value, g_strdup(value_str));
			break;
		case G_TYPE_ENUM: 
		case G_TYPE_INT: {
			gint i;
			sscanf (value_str, "%d", &i);
			g_value_set_int(value, i);
			break;
		}
		case G_TYPE_UINT: {
			guint i;
			sscanf (value_str, "%u", &i);
			g_value_set_uint(value, i);
			break;
		}
		case G_TYPE_LONG: {
			glong i;
			sscanf (value_str, "%ld", &i);
			g_value_set_long(value, i);
			break;
		}
		case G_TYPE_ULONG: {
			gulong i;
			sscanf (value_str, "%lu", &i);
			g_value_set_ulong(value, i);
			break;
		}
		case G_TYPE_BOOLEAN: {
			gboolean i = FALSE;
			if (!strncmp ("true", value_str, 4)) i = TRUE;
			g_value_set_boolean(value, i);
			break;
		}
		case G_TYPE_CHAR: {
			gchar i;
			sscanf (value_str, "%c", &i);
			g_value_set_char(value, i);
			break;
		}
		case G_TYPE_UCHAR: {
			guchar i;
			sscanf (value_str, "%c", &i);
			g_value_set_uchar(value, i);
			break;
		}
		case G_TYPE_FLOAT: {
			gfloat i;
			sscanf (value_str, "%f", &i);
			g_value_set_float(value, i);
			break;
		}
		case G_TYPE_DOUBLE: {
			gfloat i;
			sscanf (value_str, "%g", &i);
			g_value_set_double(value, (gdouble)i);
			break;
		}
		default:
	  		break;
	}
}

static void
gst_dparam_do_update_realtime (GstDParam *dparam, gint64 timestamp)
{
	GST_DPARAM_LOCK(dparam);
	GST_DPARAM_READY_FOR_UPDATE(dparam) = FALSE;
	GST_DEBUG(GST_CAT_PARAMS, "updating value for %s(%p)\n",GST_DPARAM_NAME (dparam),dparam);
	g_value_copy(dparam->point[0], GST_DPARAM_VALUE(dparam));
	GST_DPARAM_UNLOCK(dparam);
}

static GValue** 
gst_dparam_get_point_realtime (GstDParam *dparam, gint64 timestamp)
{
	GST_DEBUG(GST_CAT_PARAMS, "getting point for %s(%p)\n",GST_DPARAM_NAME (dparam),dparam);
	return dparam->point;
}

/**********************
 * GstDParamSmooth
 **********************/

static void gst_dparam_do_update_smooth (GstDParam *dparam, gint64 timestamp);
static GValue** gst_dparam_get_point_smooth (GstDParam *dparam, gint64 timestamp);

/**
 * gst_dparam_smooth_new:
 * @type: the type that this dparam will store
 *
 * Returns: a new instance of GstDParamSmooth
 */
GstDParam* 
gst_dparam_smooth_new (GType type)
{
	GstDParam *dparam;

	dparam = g_object_new (gst_dparam_get_type (), NULL);
	
	dparam->do_update_func = gst_dparam_do_update_smooth;
	dparam->get_point_func = gst_dparam_get_point_smooth;
	
	dparam->point = gst_dparam_new_value_array(type, type, G_TYPE_FLOAT, 0);	
	GST_DPARAM_TYPE(dparam) = type;
	
	return dparam;
}

static void
gst_dparam_do_update_smooth (GstDParam *dparam, gint64 timestamp)
{
	gint64 time_diff;
	gfloat time_ratio;
	
	time_diff = MIN(GST_DPARAM_DEFAULT_UPDATE_PERIOD(dparam), 
	                timestamp - GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam));
	                
	time_ratio = (gfloat)time_diff / g_value_get_float(dparam->point[2]);

	GST_DPARAM_LOCK(dparam);

	GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam) = GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam);  
	while(GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) <= timestamp){
		GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) += GST_DPARAM_DEFAULT_UPDATE_PERIOD(dparam);
	}
	GST_DEBUG(GST_CAT_PARAMS, "last:%lld current:%lld next:%lld\n",
	                           GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam), timestamp, GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam));

	
	switch (G_VALUE_TYPE(GST_DPARAM_VALUE(dparam))){
		case G_TYPE_FLOAT: {
			gfloat target = g_value_get_float(dparam->point[0]);
			gfloat current = g_value_get_float(GST_DPARAM_VALUE(dparam));
			gfloat max_change = time_ratio * g_value_get_float(dparam->point[1]);
			
			GST_DEBUG(GST_CAT_PARAMS, "target:%f current:%f max_change:%f \n", 
			          target, current, max_change);

			if (ABS (current - target) < max_change){
				GST_DPARAM_READY_FOR_UPDATE(dparam) = FALSE;
				current = target;
			}
			else {
				current += (target < current) ? -max_change : max_change;				
			}
			g_value_set_float(GST_DPARAM_VALUE(dparam), current);

		}
		default:
			break;
	}

		                           
	//GST_DEBUG(GST_CAT_PARAMS, "smooth update for %s(%p): %f\n",
	//                           GST_DPARAM_NAME (dparam),dparam, g_value_get_float(GST_DPARAM_VALUE(dparam)));

	GST_DPARAM_UNLOCK(dparam);
}

static GValue** 
gst_dparam_get_point_smooth (GstDParam *dparam, gint64 timestamp)
{
	GST_DEBUG(GST_CAT_PARAMS, "getting point for %s(%p)\n",GST_DPARAM_NAME (dparam),dparam);
	return dparam->point;
}
