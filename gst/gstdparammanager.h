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
#include <gst/gstdparam.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_DPMAN			(gst_dpman_get_type ())
#define GST_DPMAN(obj)			(G_TYPE_CHECK_INSTANCE_CAST	((obj), GST_TYPE_DPMAN,GstDparamManager))
#define GST_DPMAN_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST	((klass), GST_TYPE_DPMAN,GstDparamManager))
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

typedef struct _GstDparamManager GstDparamManager;
typedef struct _GstDparamManagerClass GstDparamManagerClass;
typedef struct _GstDpmMode GstDpmMode;
typedef struct _GstDparamWrapper GstDparamWrapper;

typedef guint (*GstDpmModeFirstCountdownFunction) (GstDparamManager *dpman, guint frames, gint64 timestamp);
typedef guint (*GstDpmModeCountdownFunction) (GstDparamManager *dpman, guint frame_count);
typedef void (*GstDpmModeSetupFunction) (GstDparamManager *dpman);
typedef void (*GstDpmModeTeardownFunction) (GstDparamManager *dpman);

typedef void (*GstDpmUpdateFunction) (GValue *value, gpointer data);

struct _GstDparamManager {
	GstObject		object;

	GHashTable *dparams;
	GSList *dparams_list;
	
	gchar *mode_name;
	GstDpmMode* mode;
	gpointer mode_data;
	
	gint64 timestamp;
	guint rate;
};

struct _GstDparamManagerClass {
	GstObjectClass parent_class;
	
	GHashTable *modes;
	/* signal callbacks */
};

struct _GstDpmMode {
	GstDpmModeFirstCountdownFunction firstcountdownfunc;
	GstDpmModeCountdownFunction countdownfunc;
	GstDpmModeSetupFunction setupfunc;
	GstDpmModeTeardownFunction teardownfunc;
};

struct _GstDparamWrapper {
	gchar *dparam_name;
	GValue *value;
	GstDparam *dparam;
	GstDpmUpdateFunction update_func;
	gpointer update_data;
};

#define GST_DPMAN_FIRST_COUNTDOWNFUNC(dpman)		(((dpman)->mode)->firstcountdownfunc)
#define GST_DPMAN_COUNTDOWNFUNC(dpman)		(((dpman)->mode)->countdownfunc)
#define GST_DPMAN_SETUPFUNC(dpman)		(((dpman)->mode)->setupfunc)
#define GST_DPMAN_TEARDOWNFUNC(dpman)		(((dpman)->mode)->teardownfunc)

#define GST_DPMAN_FIRST_COUNTDOWN(dpman, buffer_size, timestamp) \
				(GST_DPMAN_FIRST_COUNTDOWNFUNC(dpman)(dpman, buffer_size, timestamp))

#define GST_DPMAN_COUNTDOWN(dpman, frame_countdown, frame_count) \
				(frame_countdown-- || \
				(frame_countdown = GST_DPMAN_COUNTDOWNFUNC(dpman)(dpman, frame_count)))

#define GST_DPMAN_DO_UPDATE(dpwrap) ((dpwrap->update_func)(dpwrap->value, dpwrap->update_data))

GType gst_dpman_get_type (void);
GstDparamManager* gst_dpman_new (gchar *name, GstElement *parent);
void gst_dpman_set_parent (GstDparamManager *dpman, GstElement *parent);

gboolean gst_dpman_add_required_dparam (GstDparamManager *dpman, 
                                        gchar *dparam_name, 
                                        GType type, 
                                        GstDpmUpdateFunction update_func, 
                                        gpointer update_data);
void gst_dpman_remove_required_dparam (GstDparamManager *dpman, gchar *dparam_name);
gboolean gst_dpman_attach_dparam (GstDparamManager *dpman, gchar *dparam_name, GstDparam *dparam);
void gst_dpman_dettach_dparam (GstDparamManager *dpman, gchar *dparam_name);                         
GstDparam* gst_dpman_get_dparam(GstDparamManager *dpman, gchar *name);

void gst_dpman_set_rate_change_pad(GstDparamManager *dpman, GstPad *pad);

gboolean gst_dpman_set_mode(GstDparamManager *dpman, gchar *modename);
void gst_dpman_register_mode (GstDparamManagerClass *klass,
                           gchar *modename, 
                           GstDpmModeFirstCountdownFunction firstcountdownfunc,
                           GstDpmModeCountdownFunction countdownfunc,
                           GstDpmModeSetupFunction setupfunc,
                           GstDpmModeTeardownFunction teardownfunc);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_DPMAN_H__ */
