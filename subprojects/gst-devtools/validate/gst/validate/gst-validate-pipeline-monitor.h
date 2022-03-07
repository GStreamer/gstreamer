/* GStreamer
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
 *
 * gst-validate-pipeline-monitor.h - Validate PipelineMonitor class
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VALIDATE_PIPELINE_MONITOR_H__
#define __GST_VALIDATE_PIPELINE_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/validate/gst-validate-bin-monitor.h>
#include <gst/validate/gst-validate-runner.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_PIPELINE_MONITOR			(gst_validate_pipeline_monitor_get_type ())
#define GST_IS_VALIDATE_PIPELINE_MONITOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_PIPELINE_MONITOR))
#define GST_IS_VALIDATE_PIPELINE_MONITOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_PIPELINE_MONITOR))
#define GST_VALIDATE_PIPELINE_MONITOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_PIPELINE_MONITOR, GstValidatePipelineMonitorClass))
#define GST_VALIDATE_PIPELINE_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_PIPELINE_MONITOR, GstValidatePipelineMonitor))
#define GST_VALIDATE_PIPELINE_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_PIPELINE_MONITOR, GstValidatePipelineMonitorClass))
#define GST_VALIDATE_PIPELINE_MONITOR_CAST(obj)            ((GstValidatePipelineMonitor*)(obj))
#define GST_VALIDATE_PIPELINE_MONITOR_CLASS_CAST(klass)    ((GstValidatePipelineMonitorClass*)(klass))
#endif

#define GST_VALIDATE_PIPELINE_MONITOR_GET_PIPELINE(m) (GST_PIPELINE_CAST (GST_VALIDATE_ELEMENT_MONITOR_GET_ELEMENT (m)))

typedef struct _GstValidatePipelineMonitor GstValidatePipelineMonitor;
typedef struct _GstValidatePipelineMonitorClass GstValidatePipelineMonitorClass;

/**
 * GstValidatePipelineMonitor:
 *
 * GStreamer Validate PipelineMonitor class.
 *
 * Class that wraps a #GstPipeline for Validate checks
 */
struct _GstValidatePipelineMonitor {
  GstValidateBinMonitor parent;

  /*< private >*/
  gulong element_added_id;
  guint print_pos_srcid;
  gboolean buffering;
  gboolean got_error;

  /* TRUE if monitoring a playbin2 pipeline */
  gboolean is_playbin;
  /* TRUE if monitoring a playbin3 pipeline */
  gboolean is_playbin3;

  /* Latest collection received from GST_MESSAGE_STREAM_COLLECTION */
  GstStreamCollection *stream_collection;
  /* Latest GstStream received from GST_MESSAGE_STREAMS_SELECTED */
  GList *streams_selected;

  gulong deep_notify_id;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstValidatePipelineMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate PipelineMonitor object class.
 */
struct _GstValidatePipelineMonitorClass {
  GstValidateBinMonitorClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* normal GObject stuff */
GST_VALIDATE_API
GType		gst_validate_pipeline_monitor_get_type		(void);

GST_VALIDATE_API
GstValidatePipelineMonitor *   gst_validate_pipeline_monitor_new      (GstPipeline * pipeline,
    GstValidateRunner * runner, GstValidateMonitor * parent);

G_END_DECLS

#endif /* __GST_VALIDATE_PIPELINE_MONITOR_H__ */

