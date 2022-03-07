/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-bin-monitor.h - Validate BinMonitor class
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

#ifndef __GST_VALIDATE_BIN_MONITOR_H__
#define __GST_VALIDATE_BIN_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/validate/gst-validate-element-monitor.h>
#include <gst/validate/gst-validate-runner.h>
#include <gst/validate/gst-validate-scenario.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_BIN_MONITOR			(gst_validate_bin_monitor_get_type ())
#define GST_IS_VALIDATE_BIN_MONITOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_BIN_MONITOR))
#define GST_IS_VALIDATE_BIN_MONITOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_BIN_MONITOR))
#define GST_VALIDATE_BIN_MONITOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_BIN_MONITOR, GstValidateBinMonitorClass))
#define GST_VALIDATE_BIN_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_BIN_MONITOR, GstValidateBinMonitor))
#define GST_VALIDATE_BIN_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_BIN_MONITOR, GstValidateBinMonitorClass))
#define GST_VALIDATE_BIN_MONITOR_CAST(obj)            ((GstValidateBinMonitor*)(obj))
#define GST_VALIDATE_BIN_MONITOR_CLASS_CAST(klass)    ((GstValidateBinMonitorClass*)(klass))
#endif

typedef struct _GstValidateBinMonitor GstValidateBinMonitor;
typedef struct _GstValidateBinMonitorClass GstValidateBinMonitorClass;

/**
 * GstValidateBinMonitor:
 *
 * GStreamer Validate BinMonitor class.
 *
 * Class that wraps a #GstBin for Validate checks
 */
struct _GstValidateBinMonitor {
  GstValidateElementMonitor parent;

  GList *element_monitors;

  GstValidateScenario *scenario;

  /*< private >*/
  gulong element_added_id;
  gulong element_removed_id;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstValidateBinMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate BinMonitor object class.
 */
struct _GstValidateBinMonitorClass {
  GstValidateElementMonitorClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* normal GObject stuff */
GST_VALIDATE_API
GType		gst_validate_bin_monitor_get_type		(void);

GST_VALIDATE_API GstValidateBinMonitor *
gst_validate_bin_monitor_new                (GstBin * bin,
                                             GstValidateRunner * runner,
                                             GstValidateMonitor * parent);

GST_VALIDATE_API GstValidateScenario *
gst_validate_bin_monitor_get_scenario       (GstValidateBinMonitor * monitor);

G_END_DECLS

#endif /* __GST_VALIDATE_BIN_MONITOR_H__ */

