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
#include <gst/control/dparamcommon.h>
#include <gst/control/dparam.h>
#include <gst/control/unitconvert.h>

G_BEGIN_DECLS
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
#define GST_DPMAN_RATE(dpman)				 ((dpman)->rate)
#define GST_DPMAN_FRAMES_TO_PROCESS(dpman)		 ((dpman)->frames_to_process)
#define GST_DPMAN_NEXT_UPDATE_FRAME(dpman)		 ((dpman)->next_update_frame)
    typedef enum
{
  GST_DPMAN_CALLBACK,
  GST_DPMAN_DIRECT,
  GST_DPMAN_ARRAY,
} GstDPMUpdateMethod;

typedef struct _GstDParamManagerClass GstDParamManagerClass;
typedef struct _GstDPMMode GstDPMMode;
typedef struct _GstDParamWrapper GstDParamWrapper;
typedef struct _GstDParamAsyncToUpdate GstDParamAsyncToUpdate;

typedef gboolean (*GstDPMModePreProcessFunction) (GstDParamManager * dpman,
    guint frames, gint64 timestamp);
typedef gboolean (*GstDPMModeProcessFunction) (GstDParamManager * dpman,
    guint frame_count);
typedef void (*GstDPMModeSetupFunction) (GstDParamManager * dpman);
typedef void (*GstDPMModeTeardownFunction) (GstDParamManager * dpman);

typedef void (*GstDPMUpdateFunction) (const GValue * value, gpointer data);

struct _GstDParamManager
{
  GstObject object;

  GHashTable *dparams;
  GList *dparams_list;

  /* mode state */
  GstDPMMode *mode;
  gchar *mode_name;

  guint frames_to_process;	/* the number of frames in the current buffer */
  guint next_update_frame;	/* the frame when the next update is required */

  /* the following data is only used for async mode */
  guint rate;			/* the frame/sample rate - */
  guint rate_ratio;		/* number used to convert between samples and time */
  guint num_frames;		/* the number of frames in the current buffer */

  gint64 time_buffer_ends;
  gint64 time_buffer_starts;
};

struct _GstDParamManagerClass
{
  GstObjectClass parent_class;
  GHashTable *modes;
  /* signal callbacks */
  void (*new_required_dparam) (GstDParamManager * dpman, gchar * dparam_name);
};

struct _GstDPMMode
{
  GstDPMModePreProcessFunction preprocessfunc;
  GstDPMModeProcessFunction processfunc;
  GstDPMModeSetupFunction setupfunc;
  GstDPMModeTeardownFunction teardownfunc;
};

struct _GstDParamWrapper
{
  GParamSpec *param_spec;
  GValue *value;
  GstDParam *dparam;

  guint next_update_frame;

  GstDPMUpdateMethod update_method;
  gpointer update_data;
  GstDPMUpdateFunction update_func;

  gchar *unit_name;
  GstDParamUpdateInfo update_info;
};

struct _GstDParamAsyncToUpdate
{
  guint frame;
  GValue *value;
  GstDParamWrapper *dpwrap;
};

#define GST_DPMAN_PREPROCESSFUNC(dpman)		(((dpman)->mode)->preprocessfunc)
#define GST_DPMAN_PROCESSFUNC(dpman)		(((dpman)->mode)->processfunc)
#define GST_DPMAN_SETUPFUNC(dpman)		(((dpman)->mode)->setupfunc)
#define GST_DPMAN_TEARDOWNFUNC(dpman)		(((dpman)->mode)->teardownfunc)

#define GST_DPMAN_PREPROCESS(dpman, buffer_size, timestamp) \
			(GST_DPMAN_PREPROCESSFUNC(dpman)(dpman, buffer_size, timestamp))

#define GST_DPMAN_PROCESS(dpman, frame_count) \
                         (frame_count < dpman->next_update_frame || \
                         (dpman->next_update_frame < dpman->num_frames && (GST_DPMAN_PROCESSFUNC(dpman)(dpman, frame_count))))

#define GST_DPMAN_CALLBACK_UPDATE(dpwrap, value) ((dpwrap->update_func)(value, dpwrap->update_data))

void _gst_dpman_initialize (void);
GType gst_dpman_get_type (void);
GstDParamManager *gst_dpman_new (gchar * name, GstElement * parent);
void gst_dpman_set_parent (GstDParamManager * dpman, GstElement * parent);
GstDParamManager *gst_dpman_get_manager (GstElement * parent);

gboolean gst_dpman_add_required_dparam_callback (GstDParamManager * dpman,
    GParamSpec * param_spec,
    gchar * unit_name, GstDPMUpdateFunction update_func, gpointer update_data);
gboolean gst_dpman_add_required_dparam_direct (GstDParamManager * dpman,
    GParamSpec * param_spec, gchar * unit_name, gpointer update_data);

gboolean gst_dpman_add_required_dparam_array (GstDParamManager * dpman,
    GParamSpec * param_spec, gchar * unit_name, gpointer update_data);

void gst_dpman_remove_required_dparam (GstDParamManager * dpman,
    gchar * dparam_name);
gboolean gst_dpman_attach_dparam (GstDParamManager * dpman, gchar * dparam_name,
    GstDParam * dparam);
void gst_dpman_detach_dparam (GstDParamManager * dpman, gchar * dparam_name);
GstDParam *gst_dpman_get_dparam (GstDParamManager * dpman, gchar * name);
GType gst_dpman_get_dparam_type (GstDParamManager * dpman, gchar * name);

GParamSpec **gst_dpman_list_dparam_specs (GstDParamManager * dpman);
GParamSpec *gst_dpman_get_param_spec (GstDParamManager * dpman,
    gchar * dparam_name);
void gst_dpman_dparam_spec_has_changed (GstDParamManager * dpman,
    gchar * dparam_name);

void gst_dpman_set_rate (GstDParamManager * dpman, gint rate);
void gst_dpman_bypass_dparam (GstDParamManager * dpman, gchar * dparam_name);

gboolean gst_dpman_set_mode (GstDParamManager * dpman, gchar * modename);
void gst_dpman_register_mode (GstDParamManagerClass * klass,
    gchar * modename,
    GstDPMModePreProcessFunction preprocessfunc,
    GstDPMModeProcessFunction processfunc,
    GstDPMModeSetupFunction setupfunc, GstDPMModeTeardownFunction teardownfunc);

G_END_DECLS
#endif /* __GST_DPMAN_H__ */
