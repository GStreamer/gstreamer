/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdparam.h: Dynamic Parameter functionality
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

#ifndef __GST_DPARAM_H__
#define __GST_DPARAM_H__

#include <gst/gstobject.h>
#include <gst/gstprops.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_DPARAM			(gst_dparam_get_type ())
#define GST_DPARAM(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DPARAM,GstDParam))
#define GST_DPARAM_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DPARAM,GstDParam))
#define GST_IS_DPARAM(obj)			(G_TYPE_CHECK_INSTANCE_TYPE	((obj), GST_TYPE_DPARAM))
#define GST_IS_DPARAM_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DPARAM))

#define GST_DPARAM_NAME(dparam)				 (GST_OBJECT_NAME(dparam))
#define GST_DPARAM_PARENT(dparam)			 (GST_OBJECT_PARENT(dparam))
#define GST_DPARAM_VALUE(dparam)				 ((dparam)->value)
#define GST_DPARAM_SPEC(dparam)				 ((dparam)->spec)
#define GST_DPARAM_TYPE(dparam)				 ((dparam)->type)

#define GST_DPARAM_LOCK(dparam)		(g_mutex_lock((dparam)->lock))
#define GST_DPARAM_UNLOCK(dparam)		(g_mutex_unlock((dparam)->lock))

#define GST_DPARAM_READY_FOR_UPDATE(dparam)		((dparam)->ready_for_update)
#define GST_DPARAM_DEFAULT_UPDATE_PERIOD(dparam)	((dparam)->default_update_period)
#define GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam)	((dparam)->next_update_timestamp)
#define GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam)	((dparam)->last_update_timestamp)

#define GST_DPARAM_GET_POINT(dparam, timestamp) \
	((dparam->get_point_func)(dparam, timestamp))

#define GST_DPARAM_FIND_POINT(dparam, timestamp, search_flag) \
	((dparam->find_point_func)(dparam, data, search_flag))

#define GST_DPARAM_DO_UPDATE(dparam, timestamp) \
	((dparam->do_update_func)(dparam, timestamp))
		
#define GST_DPARAM_INSERT_POINT(dparam, timestamp) \
	((dparam->insert_point_func)(dparam, timestamp))

#define GST_DPARAM_REMOVE_POINT(dparam, data) \
	((dparam->remove_point_func)(dparam, data))
	
typedef enum {
  GST_DPARAM_CLOSEST,
  GST_DPARAM_CLOSEST_AFTER,
  GST_DPARAM_CLOSEST_BEFORE,
  GST_DPARAM_EXACT,
} GstDParamSearchFlag;

typedef enum {
  GST_DPARAM_NOT_FOUND = 0,
  GST_DPARAM_FOUND_EXACT,
  GST_DPARAM_FOUND_CLOSEST,
} GstDParamSearchResult;

typedef struct _GstDParam GstDParam;
typedef struct _GstDParamClass GstDParamClass;
typedef struct _GstDParamSpec GstDParamSpec;

typedef GValue** (*GstDParamInsertPointFunction) (GstDParam *dparam, guint64 timestamp);
typedef void (*GstDParamRemovePointFunction) (GstDParam *dparam, GValue** point);
typedef GValue** (*GstDParamGetPointFunction) (GstDParam *dparam, gint64 timestamp);
typedef GstDParamSearchResult (*GstDParamFindPointFunction) (GstDParam *dparam, gint64 *timestamp, GstDParamSearchFlag search_flag);

typedef void (*GstDParamDoUpdateFunction) (GstDParam *dparam, gint64 timestamp);

struct _GstDParam {
	GstObject		object;

	GstDParamGetPointFunction get_point_func;
	GstDParamFindPointFunction find_point_func;

	GstDParamDoUpdateFunction do_update_func;
	
	GstDParamInsertPointFunction insert_point_func;
	GstDParamRemovePointFunction remove_point_func;	
	
	GMutex *lock;
	GValue *value;
	GstDParamSpec *spec;
	GValue **point;
	GType type;
	gint64 last_update_timestamp;
	gint64 next_update_timestamp;
	gint64 default_update_period;
	gboolean ready_for_update;
};

struct _GstDParamClass {
	GstObjectClass parent_class;

	/* signal callbacks */
};

struct _GstDParamSpec {
	gchar *dparam_name;
	gchar *unit_name;
	GValue *min_val;
	GValue *max_val;
	GValue *default_val;
	gboolean is_log;
	gboolean is_rate;
};

GType gst_dparam_get_type (void);
GstDParam* gst_dparam_new (GType type);
void gst_dparam_attach (GstDParam *dparam, GstObject *parent, GValue *value, GstDParamSpec *spec);
GValue** gst_dparam_new_value_array(GType type, ...);
void gst_dparam_set_value_from_string(GValue *value, const gchar *value_str);

/**********************
 * GstDParamSmooth
 **********************/

GstDParam* gst_dparam_smooth_new (GType type);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_DPARAM_H__ */
