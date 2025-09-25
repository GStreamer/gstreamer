/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-element-monitor.h - Validate ElementMonitor class
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

#ifndef __GST_VALIDATE_ELEMENT_MONITOR_H__
#define __GST_VALIDATE_ELEMENT_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>

#include <gst/validate/gst-validate-monitor.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_ELEMENT_MONITOR			(gst_validate_element_monitor_get_type ())
#define GST_IS_VALIDATE_ELEMENT_MONITOR(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_ELEMENT_MONITOR))
#define GST_IS_VALIDATE_ELEMENT_MONITOR_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_ELEMENT_MONITOR))
#define GST_VALIDATE_ELEMENT_MONITOR_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_ELEMENT_MONITOR, GstValidateElementMonitorClass))
#define GST_VALIDATE_ELEMENT_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_ELEMENT_MONITOR, GstValidateElementMonitor))
#define GST_VALIDATE_ELEMENT_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_ELEMENT_MONITOR, GstValidateElementMonitorClass))
#define GST_VALIDATE_ELEMENT_MONITOR_CAST(obj)                ((GstValidateElementMonitor*)(obj))
#define GST_VALIDATE_ELEMENT_MONITOR_CLASS_CAST(klass)        ((GstValidateElementMonitorClass*)(klass))
#endif

#define GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_DECODER(m) (GST_VALIDATE_ELEMENT_MONITOR_CAST (m)->is_decoder)
#define GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_ENCODER(m) (GST_VALIDATE_ELEMENT_MONITOR_CAST (m)->is_encoder)
#define GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_DEMUXER(m) (GST_VALIDATE_ELEMENT_MONITOR_CAST (m)->is_demuxer)
#define GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_CONVERTER(m) (GST_VALIDATE_ELEMENT_MONITOR_CAST (m)->is_converter)
#define GST_VALIDATE_ELEMENT_MONITOR_ELEMENT_IS_SINK(m) (GST_VALIDATE_ELEMENT_MONITOR_CAST (m)->is_sink)

typedef struct _GstValidateElementMonitor GstValidateElementMonitor;
typedef struct _GstValidateElementMonitorClass GstValidateElementMonitorClass;

/**
 * GstValidateElementMonitor:
 *
 * GStreamer Validate ElementMonitor class.
 *
 * Class that wraps a #GstElement for Validate checks
 */
struct _GstValidateElementMonitor {
  GstValidateMonitor 	 parent;

  /*< private >*/
  gulong         pad_added_id;
  GList         *pad_monitors;

  gboolean       is_decoder;
  gboolean       is_encoder;
  gboolean       is_demuxer;
  gboolean       is_converter;
  gboolean       is_sink;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstValidateElementMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate ElementMonitor object class.
 */
struct _GstValidateElementMonitorClass {
  GstValidateMonitorClass	parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* normal GObject stuff */
GST_VALIDATE_API
GType		gst_validate_element_monitor_get_type		(void);

GST_VALIDATE_API
GstValidateElementMonitor *   gst_validate_element_monitor_new      (GstElement * element, GstValidateRunner * runner, GstValidateMonitor * parent) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* __GST_VALIDATE_ELEMENT_MONITOR_H__ */

