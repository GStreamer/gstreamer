/* GStreamer
 * Copyright (C) 2013 Olivier Crete <olivier.crete@collabora.com>
 *
 * gstglobaldevicemonitor.c: Global device monitor
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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


#ifndef __GST_GLOBAL_DEVICE_MONITOR_H__
#define __GST_GLOBAL_DEVICE_MONITOR_H__

#include <gst/gstobject.h>
#include <gst/gstdevice.h>
#include <gst/gstdevicemonitor.h>
#include <gst/gstdevicemonitorfactory.h>

G_BEGIN_DECLS

typedef struct _GstGlobalDeviceMonitor GstGlobalDeviceMonitor;
typedef struct _GstGlobalDeviceMonitorPrivate GstGlobalDeviceMonitorPrivate;
typedef struct _GstGlobalDeviceMonitorClass GstGlobalDeviceMonitorClass;

#define GST_TYPE_GLOBAL_DEVICE_MONITOR                 (gst_global_device_monitor_get_type())
#define GST_IS_GLOBAL_DEVICE_MONITOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GLOBAL_DEVICE_MONITOR))
#define GST_IS_GLOBAL_DEVICE_MONITOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GLOBAL_DEVICE_MONITOR))
#define GST_GLOBAL_DEVICE_MONITOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_GLOBAL_DEVICE_MONITOR, GstGlobalDeviceMonitorClass))
#define GST_GLOBAL_DEVICE_MONITOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GLOBAL_DEVICE_MONITOR, GstGlobalDeviceMonitor))
#define GST_GLOBAL_DEVICE_MONITOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GLOBAL_DEVICE_MONITOR, GstGlobalDeviceMonitorClass))
#define GST_GLOBAL_DEVICE_MONITOR_CAST(obj)            ((GstGlobalDeviceMonitor *)(obj))


struct _GstGlobalDeviceMonitor {
  GstObject                parent;

  /*< private >*/

  GstGlobalDeviceMonitorPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstGlobalDeviceMonitorClass {
  GstObjectClass           parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType       gst_global_device_monitor_get_type (void);

GstGlobalDeviceMonitor * gst_global_device_monitor_new  (void);

GList *     gst_global_device_monitor_get_devices (GstGlobalDeviceMonitor * monitor);

gboolean    gst_global_device_monitor_start (GstGlobalDeviceMonitor * monitor);
void        gst_global_device_monitor_stop  (GstGlobalDeviceMonitor * monitor);


void        gst_global_device_monitor_set_type_filter (
  GstGlobalDeviceMonitor * monitor,
  GstDeviceMonitorFactoryListType type);

GstDeviceMonitorFactoryListType gst_global_device_monitor_get_type_filter (
  GstGlobalDeviceMonitor * monitor);


void        gst_global_device_monitor_set_caps_filter (
  GstGlobalDeviceMonitor * monitor,
  GstCaps *                caps);

GstCaps *   gst_global_device_monitor_get_caps_filter (
  GstGlobalDeviceMonitor * monitor);

GstBus *    gst_global_device_monitor_get_bus (
  GstGlobalDeviceMonitor * monitor);

G_END_DECLS

#endif /* __GST_GLOBAL_DEVICE_MONITOR_H__ */
