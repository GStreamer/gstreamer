/* GStreamer
 * Copyright (C) 2012 Olivier Crete <olivier.crete@collabora.com>
 *
 * gstdevicemonitor.h: Device probing and monitoring
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

#include <gst/gstdevicemonitorfactory.h>


#ifndef __GST_DEVICE_MONITOR_H__
#define __GST_DEVICE_MONITOR_H__

#include <gst/gstelement.h>

G_BEGIN_DECLS

typedef struct _GstDeviceMonitor GstDeviceMonitor;
typedef struct _GstDeviceMonitorClass GstDeviceMonitorClass;
typedef struct _GstDeviceMonitorPrivate GstDeviceMonitorPrivate;

#define GST_TYPE_DEVICE_MONITOR                 (gst_device_monitor_get_type())
#define GST_IS_DEVICE_MONITOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEVICE_MONITOR))
#define GST_IS_DEVICE_MONITOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEVICE_MONITOR))
#define GST_DEVICE_MONITOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEVICE_MONITOR, GstDeviceMonitorClass))
#define GST_DEVICE_MONITOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEVICE_MONITOR, GstDeviceMonitor))
#define GST_DEVICE_MONITOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE_MONITOR, GstDeviceMonitorClass))
#define GST_DEVICE_MONITOR_CAST(obj)            ((GstDeviceMonitor *)(obj))


struct _GstDeviceMonitor {
  GstObject         parent;

  /*< private >*/

  /* Protected by the Object lock */
  GList *devices;

  GstDeviceMonitorPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstDeviceMonitorClass:
 * @factory: a pointer to the #GstDeviceMonitorFactory that creates this
 *  monitor
 * @get_devices: Returns a list of devices that are currently available.
 *  This should never block.
 * @start: Starts monitoring for new devices.
 * @stop: Stops monitoring for new devices
 *
 * The structure of the base #GstDeviceMonitorClass
 *
 * Since: 1.4
 */

struct _GstDeviceMonitorClass {
  GstObjectClass    parent_class;

  GstDeviceMonitorFactory     *factory;

  GList*      (*probe) (GstDeviceMonitor * monitor);

  gboolean    (*start) (GstDeviceMonitor * monitor);
  void        (*stop)  (GstDeviceMonitor * monitor);


  gpointer metadata;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType       gst_device_monitor_get_type (void);


GList *     gst_device_monitor_get_devices    (GstDeviceMonitor * monitor);

gboolean    gst_device_monitor_start          (GstDeviceMonitor * monitor);
void        gst_device_monitor_stop           (GstDeviceMonitor * monitor);

gboolean    gst_device_monitor_can_monitor    (GstDeviceMonitor * monitor);

GstBus *    gst_device_monitor_get_bus        (GstDeviceMonitor * monitor);

void        gst_device_monitor_device_add     (GstDeviceMonitor * monitor,
                                               GstDevice * device);
void        gst_device_monitor_device_remove  (GstDeviceMonitor * monitor,
                                               GstDevice * device);


/* device monitor class meta data */
void        gst_device_monitor_class_set_metadata          (GstDeviceMonitorClass *klass,
                                                            const gchar     *longname,
                                                            const gchar     *classification,
                                                            const gchar     *description,
                                                            const gchar     *author);
void        gst_device_monitor_class_set_static_metadata   (GstDeviceMonitorClass *klass,
                                                            const gchar     *longname,
                                                            const gchar     *classification,
                                                            const gchar     *description,
                                                            const gchar     *author);
void        gst_device_monitor_class_add_metadata          (GstDeviceMonitorClass * klass,
                                                            const gchar * key, const gchar * value);
void        gst_device_monitor_class_add_static_metadata   (GstDeviceMonitorClass * klass,
                                                            const gchar * key, const gchar * value);
const gchar * gst_device_monitor_class_get_metadata        (GstDeviceMonitorClass * klass,
                                                              const gchar * key);

/* factory management */
GstDeviceMonitorFactory * gst_device_monitor_get_factory   (GstDeviceMonitor * monitor);

G_END_DECLS

#endif /* __GST_DEVICE_MONITOR_H__ */
