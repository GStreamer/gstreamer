/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdparammanager.h: Dynamic Parameter group functionality
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

#ifndef __GST_DPMAN_H__
#define __GST_DPMAN_H__

#include <gst/gstobject.h>
#include <gst/gstprops.h>
#include <gst/control/dparamcommon.h>
#include <gst/control/dparam.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_DPMAN			(gst_dpman_get_type ())
#define GST_DPMAN(obj)			(G_TYPE_CHECK_INSTANCE_CAST	((obj), GST_TYPE_DPMAN,GstDParamManager))
#define GST_DPMAN_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST	((klass), GST_TYPE_DPMAN,GstDParamManager))
#define GST_IS_DPMAN(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DPMAN))
#define GST_IS_DPMAN_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DPMAN))

#define GST_DPMAN_NAME(dpman)			(GST_OBJECT_NAME(dpman))
#define GST_DPMAN_PARENT(dpman)		(GST_OBJECT_PARENT(dpman))
#define GST_DPMAN_DPARAMS(dpman)		((dpman)->dparams)
#define GST_DPMAN_DPARAMS_LIST(dpman)		((dpman)->dparams_list)

#define GST_DPMAN_MODE_NAME(dpman)				 ((dpman)->mode_name)
#define GST_DPMAN_MODE(dpman)				 ((dpman)->mode)
#define GST_DPMAN_MODE_DATA(dpman)				 ((dpman)->mode_data)
#define GST_DPMAN_RATE(dpman)				 ((dpman)->rate)

typedef enum {
  GST_DPMAN_CALLBACK,
  GST_DPMAN_DIRECT,
  GST_DPMAN_ARRAY,
} GstDPMUpdateMethod;

typedef struct _GstDParamManagerClass GstDParamManagerClass;
typedef struct _GstDPMMode GstDPMMode;
typedef struct _GstDParamWrapper GstDParamWrapper;

typedef guint (*GstDPMModePreProcessFunction) (GstDParamManager *dpman, guint frames, gint64 timestamp);
typedef guint (*GstDPMModeProcessFunction) (GstDParamManager *dpman, guint frame_count);
typedef void (*GstDPMModeSetupFunction) (GstDParamManager *dpman);
typedef void (*GstDPMModeTeardownFunction) (GstDParamManager *dpman);

typedef void (*GstDPMUpdateFunction) (GValue *value, gpointer data);

struct _GstDParamManager {
	GstObject		object;

	GHashTable *dparams;
	GSList *dparams_list;
	
	gchar *mode_name;
	GstDPMMode* mode;
	gpointer mode_data;
	
	gint64 timestamp;
	guint rate;
};

struct _GstDParamManagerClass {
	GstObjectClass parent_class;
	
	GHashTable *modes;
	/* signal callbacks */
};

struct _GstDPMMode {
	GstDPMModePreProcessFunction preprocessfunc;
	GstDPMModeProcessFunction processfunc;
	GstDPMModeSetupFunction setupfunc;
	GstDPMModeTeardownFunction teardownfunc;
};

struct _GstDParamWrapper {
	GParamSpec* param_spec;
	GValue *value;
	GstDParam *dparam;
	GstDPMUpdateMethod update_method;
	gpointer update_data;
	GstDPMUpdateFunction update_func;
	gboolean is_log;
	gboolean is_rate;
};

#define GST_DPMAN_PREPROCESSFUNC(dpman)		(((dpman)->mode)->preprocessfunc)
#define GST_DPMAN_PROCESSFUNC(dpman)		(((dpman)->mode)->processfunc)
#define GST_DPMAN_SETUPFUNC(dpman)		(((dpman)->mode)->setupfunc)
#define GST_DPMAN_TEARDOWNFUNC(dpman)		(((dpman)->mode)->teardownfunc)

#define GST_DPMAN_PREPROCESS(dpman, buffer_size, timestamp) \
				(GST_DPMAN_PREPROCESSFUNC(dpman)(dpman, buffer_size, timestamp))

#define GST_DPMAN_PROCESS(dpman, frame_count) \
				(GST_DPMAN_PROCESSFUNC(dpman)(dpman, frame_count))

#define GST_DPMAN_PROCESS_COUNTDOWN(dpman, frame_countdown, frame_count) \
				(frame_countdown-- || \
				(frame_countdown = GST_DPMAN_PROCESS(dpman, frame_count)))
				
#define GST_DPMAN_DO_UPDATE(dpwrap) ((dpwrap->update_func)(dpwrap->value, dpwrap->update_data))

void _gst_dpman_initialize();
GType gst_dpman_get_type (void);
GstDParamManager* gst_dpman_new (gchar *name, GstElement *parent);
void gst_dpman_set_parent (GstDParamManager *dpman, GstElement *parent);
GstDParamManager* gst_dpman_get_manager (GstElement *parent);

gboolean gst_dpman_add_required_dparam_callback (GstDParamManager *dpman, 
                                                 GParamSpec *param_spec,
                                                 gboolean is_log,
                                                 gboolean is_rate,
                                                 GstDPMUpdateFunction update_func, 
                                                 gpointer update_data);
gboolean gst_dpman_add_required_dparam_direct (GstDParamManager *dpman, 
                                               GParamSpec *param_spec,
                                               gboolean is_log,
                                               gboolean is_rate,
                                               gpointer update_data);
                                                                              
gboolean gst_dpman_add_required_dparam_array (GstDParamManager *dpman, 
                                              GParamSpec *param_spec,
                                              gboolean is_log,
                                              gboolean is_rate,
                                              gpointer update_data);
                                     
void gst_dpman_remove_required_dparam (GstDParamManager *dpman, gchar *dparam_name);
gboolean gst_dpman_attach_dparam (GstDParamManager *dpman, gchar *dparam_name, GstDParam *dparam);
void gst_dpman_detach_dparam (GstDParamManager *dpman, gchar *dparam_name);                         
GstDParam* gst_dpman_get_dparam(GstDParamManager *dpman, gchar *name);
GType gst_dpman_get_dparam_type (GstDParamManager *dpman, gchar *name);

GParamSpec** gst_dpman_list_param_specs(GstDParamManager *dpman);
GParamSpec* gst_dpman_get_param_spec (GstDParamManager *dpman, gchar *dparam_name);
void gst_dpman_dparam_spec_has_changed (GstDParamManager *dpman, gchar *dparam_name);

void gst_dpman_set_rate_change_pad(GstDParamManager *dpman, GstPad *pad);

gboolean gst_dpman_set_mode(GstDParamManager *dpman, gchar *modename);
void gst_dpman_register_mode (GstDParamManagerClass *klass,
                           gchar *modename, 
                           GstDPMModePreProcessFunction preprocessfunc,
                           GstDPMModeProcessFunction processfunc,
                           GstDPMModeSetupFunction setupfunc,
                           GstDPMModeTeardownFunction teardownfunc);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_DPMAN_H__ */
