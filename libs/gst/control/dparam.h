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
#include "dparamcommon.h"

G_BEGIN_DECLS
#define GST_TYPE_DPARAM			(gst_dparam_get_type ())
#define GST_DPARAM(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DPARAM,GstDParam))
#define GST_DPARAM_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DPARAM,GstDParam))
#define GST_IS_DPARAM(obj)			(G_TYPE_CHECK_INSTANCE_TYPE	((obj), GST_TYPE_DPARAM))
#define GST_IS_DPARAM_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DPARAM))
#define GST_DPARAM_NAME(dparam)				(GST_OBJECT_NAME(dparam))
#define GST_DPARAM_PARENT(dparam)			(GST_OBJECT_PARENT(dparam))
#define GST_DPARAM_CHANGE_VALUE(dparam)		((dparam)->change_value)
#define GST_DPARAM_PARAM_SPEC(dparam)		((dparam)->param_spec)
#define GST_DPARAM_MANAGER(dparam)			((dparam)->manager)
#define GST_DPARAM_TYPE(dparam)				((dparam)->type)
#define GST_DPARAM_UNIT_NAME(dparam)		((dparam)->unit_name)
#define GST_DPARAM_IS_LOG(dparam)			((dparam)->is_log)
#define GST_DPARAM_META_VALUES(dparam)		((dparam)->meta_values)
#define GST_DPARAM_META_PARAM_SPECS(dparam)	((dparam)->meta_param_specs)
#define GST_DPARAM_LOCK(dparam)				(g_mutex_lock((dparam)->lock))
#define GST_DPARAM_UNLOCK(dparam)			(g_mutex_unlock((dparam)->lock))
#define GST_DPARAM_READY_FOR_UPDATE(dparam)	((dparam)->ready_for_update)
#define GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam)	((dparam)->next_update_timestamp)
#define GST_DPARAM_LAST_UPDATE_TIMESTAMP(dparam)	((dparam)->last_update_timestamp)
#define GST_DPARAM_DO_UPDATE(dparam, timestamp, value, update_info) \
	((dparam->do_update_func)(dparam, timestamp, value, update_info))
typedef struct _GstDParamClass GstDParamClass;


typedef enum
{
  GST_DPARAM_UPDATE_FIRST,
  GST_DPARAM_UPDATE_NORMAL,
} GstDParamUpdateInfo;

typedef void (*GstDParamDoUpdateFunction) (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info);

struct _GstDParam
{
  GstObject object;

  GstDParamDoUpdateFunction do_update_func;

  GMutex *lock;

  gfloat value_float;
  gdouble value_double;
  gint value_int;
  gint64 value_int64;

  GstDParamManager *manager;
  GParamSpec *param_spec;
  GType type;
  gboolean ready_for_update;

  gint64 next_update_timestamp;
  gint64 last_update_timestamp;
  gchar *unit_name;
  gboolean is_log;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstDParamClass
{
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*value_changed) (GstDParam * dparam);

  gpointer _gst_reserved[GST_PADDING];
};


GType gst_dparam_get_type (void);
GstDParam *gst_dparam_new (GType type);
void gst_dparam_attach (GstDParam * dparam, GstDParamManager * manager,
    GParamSpec * param_spec, gchar * unit_name);
void gst_dparam_detach (GstDParam * dparam);
void gst_dparam_do_update_default (GstDParam * dparam, gint64 timestamp,
    GValue * value, GstDParamUpdateInfo update_info);

G_END_DECLS
#endif /* __GST_DPARAM_H__ */
