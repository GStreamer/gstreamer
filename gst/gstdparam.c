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
	dparam->lock = g_mutex_new ();
}

/**
 * gst_dparam_new:
 *
 * Returns: a new instance of GstDParam
 */
GstDParam* 
gst_dparam_new ()
{
	GstDParam *dparam;

	dparam = g_object_new (gst_dparam_get_type (), NULL);
	dparam->do_update_func = gst_dparam_do_update_realtime;
	dparam->get_point_func = gst_dparam_get_point_realtime;
	
	dparam->point = gst_dparam_new_value_array(G_TYPE_NONE, 0);	
	
	return dparam;
}

/**
 * gst_dparam_set_parent
 * @dparam: GstDParam instance
 * @parent: the GstDParamManager that this dparam belongs to
 *
 */
void
gst_dparam_set_parent (GstDParam *dparam, GstObject *parent)
{
	g_return_if_fail (dparam != NULL);
	g_return_if_fail (GST_IS_DPARAM (dparam));
	g_return_if_fail (GST_DPARAM_PARENT (dparam) == NULL);
	g_return_if_fail (parent != NULL);
	g_return_if_fail (G_IS_OBJECT (parent));
	g_return_if_fail ((gpointer)dparam != (gpointer)parent);

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

	va_start (var_args, type);
	while (type){
		values_length++;
		type = va_arg (var_args, GType);
	}
	va_end (var_args);
	
	point = g_new0(GValue*,values_length + 1);

	va_start (var_args, type);
	for (x=0 ; x < values_length ; x++){
		value = g_new0(GValue,1);
		if (type != G_TYPE_NONE){
			g_value_init(value, type);
		}
		point[x] = value;
		type = va_arg (var_args, GType);
	}
	point[values_length] = NULL;
	va_end (var_args);
	
	GST_DEBUG(GST_CAT_PARAMS, "array with %d values created\n", values_length);

	return point;
}

static void
gst_dparam_do_update_realtime (GstDParam *dparam, gint64 timestamp)
{
	GST_DEBUG(GST_CAT_PARAMS, "updating point for %s(%p)\n",GST_DPARAM_NAME (dparam),dparam);
	
	GST_DPARAM_LOCK(dparam);
	GST_DPARAM_READY_FOR_UPDATE(dparam) = FALSE;
	g_value_copy(dparam->point[0], GST_DPARAM_VALUE(dparam));
	GST_DPARAM_UNLOCK(dparam);
}

static GValue** 
gst_dparam_get_point_realtime (GstDParam *dparam, gint64 timestamp)
{
	GST_DEBUG(GST_CAT_PARAMS, "getting point for %s(%p)\n",GST_DPARAM_NAME (dparam),dparam);
	return dparam->point;
}


