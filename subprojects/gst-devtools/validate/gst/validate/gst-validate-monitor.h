/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor.h - Validate Monitor abstract base class
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

#ifndef __GST_VALIDATE_MONITOR_H__
#define __GST_VALIDATE_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>

typedef struct _GstValidateMonitor GstValidateMonitor;
typedef struct _GstValidateMonitorClass GstValidateMonitorClass;

#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-reporter.h>
#include <gst/validate/gst-validate-runner.h>
#include <gst/validate/gst-validate-override.h>
#include <gst/validate/media-descriptor-parser.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_MONITOR			(gst_validate_monitor_get_type ())
#define GST_IS_VALIDATE_MONITOR(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_MONITOR))
#define GST_IS_VALIDATE_MONITOR_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_MONITOR))
#define GST_VALIDATE_MONITOR_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_MONITOR, GstValidateMonitorClass))
#define GST_VALIDATE_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_MONITOR, GstValidateMonitor))
#define GST_VALIDATE_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_MONITOR, GstValidateMonitorClass))
#define GST_VALIDATE_MONITOR_CAST(obj)                ((GstValidateMonitor*)(obj))
#define GST_VALIDATE_MONITOR_CLASS_CAST(klass)        ((GstValidateMonitorClass*)(klass))
#endif

#define GST_VALIDATE_MONITOR_GET_PARENT(m) (GST_VALIDATE_MONITOR_CAST (m)->parent)

#define GST_VALIDATE_MONITOR_LOCK(m)			\
  G_STMT_START {					\
  GST_LOG_OBJECT (m, "About to lock %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  (g_mutex_lock (&GST_VALIDATE_MONITOR_CAST(m)->mutex));		\
  GST_LOG_OBJECT (m, "Acquired lock %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  } G_STMT_END

#define GST_VALIDATE_MONITOR_UNLOCK(m)				\
  G_STMT_START {						\
  GST_LOG_OBJECT (m, "About to unlock %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  (g_mutex_unlock (&GST_VALIDATE_MONITOR_CAST(m)->mutex));		\
  GST_LOG_OBJECT (m, "unlocked %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  } G_STMT_END

#define GST_VALIDATE_MONITOR_OVERRIDES_LOCK(m) g_mutex_lock (&GST_VALIDATE_MONITOR_CAST (m)->overrides_mutex)
#define GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK(m) g_mutex_unlock (&GST_VALIDATE_MONITOR_CAST (m)->overrides_mutex)
#define GST_VALIDATE_MONITOR_OVERRIDES(m) (GST_VALIDATE_MONITOR_CAST (m)->overrides)

/* #else TODO Implemen no variadic macros, use inline,
 * Problem being:
 *     GST_VALIDATE_REPORT_LEVEL_ ## status
 *     GST_VALIDATE_AREA_ ## area ## _ ## subarea
 */

/**
 * GstValidateMonitor:
 *
 * GStreamer Validate Monitor class.
 *
 * Class that wraps a #GObject for Validate checks
 */
struct _GstValidateMonitor {
  GstObject 	 object;

  GWeakRef       target;
  GWeakRef       pipeline;
  GMutex         mutex;
  gchar         *target_name;

  GstValidateMonitor  *parent;

  GMutex        overrides_mutex;
  GQueue        overrides;
  GstValidateMediaDescriptor *media_descriptor;

  GstValidateReportingDetails level;

  /*< private >*/
  GHashTable *reports;

  GstValidateVerbosityFlags verbosity;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstValidateMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate Monitor object class.
 */
struct _GstValidateMonitorClass {
  GstObjectClass	parent_class;

  gboolean (* setup) (GstValidateMonitor * monitor);
  GstElement *(* get_element) (GstValidateMonitor * monitor);
  void (*set_media_descriptor) (GstValidateMonitor * monitor,
          GstValidateMediaDescriptor * media_descriptor);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* normal GObject stuff */
GST_VALIDATE_API
GType		gst_validate_monitor_get_type		(void);

GST_VALIDATE_API
void            gst_validate_monitor_attach_override  (GstValidateMonitor * monitor,
                                                 GstValidateOverride * override);

GST_VALIDATE_API
GstElement *    gst_validate_monitor_get_element (GstValidateMonitor * monitor);
GST_VALIDATE_API
gchar *   gst_validate_monitor_get_element_name (GstValidateMonitor * monitor);
GST_VALIDATE_API
void gst_validate_monitor_set_media_descriptor (GstValidateMonitor * monitor,
                                                GstValidateMediaDescriptor *media_descriptor);
GST_VALIDATE_API
GstPipeline * gst_validate_monitor_get_pipeline (GstValidateMonitor * monitor);
GST_VALIDATE_API
GstObject * gst_validate_monitor_get_target (GstValidateMonitor * monitor);
G_END_DECLS

#endif /* __GST_VALIDATE_MONITOR_H__ */

