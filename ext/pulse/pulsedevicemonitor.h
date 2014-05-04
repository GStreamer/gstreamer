/* GStreamer
 * Copyright (C) 2012 Olivier Crete <olivier.crete@collabora.com>
 *
 * pulsedevicemonitor.h: Device probing and monitoring
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


#ifndef __GST_PULSE_DEVICE_MONITOR_H__
#define __GST_PULSE_DEVICE_MONITOR_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstPulseDeviceMonitor GstPulseDeviceMonitor;
typedef struct _GstPulseDeviceMonitorPrivate GstPulseDeviceMonitorPrivate;
typedef struct _GstPulseDeviceMonitorClass GstPulseDeviceMonitorClass;

#define GST_TYPE_PULSE_DEVICE_MONITOR                 (gst_pulse_device_monitor_get_type())
#define GST_IS_PULSE_DEVICE_MONITOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PULSE_DEVICE_MONITOR))
#define GST_IS_PULSE_DEVICE_MONITOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PULSE_DEVICE_MONITOR))
#define GST_PULSE_DEVICE_MONITOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PULSE_DEVICE_MONITOR, GstPulseDeviceMonitorClass))
#define GST_PULSE_DEVICE_MONITOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PULSE_DEVICE_MONITOR, GstPulseDeviceMonitor))
#define GST_PULSE_DEVICE_MONITOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE_MONITOR, GstPulseDeviceMonitorClass))
#define GST_PULSE_DEVICE_MONITOR_CAST(obj)            ((GstPulseDeviceMonitor *)(obj))

struct _GstPulseDeviceMonitor {
  GstDeviceMonitor         parent;

  gchar *server;
  gchar *client_name;

  pa_threaded_mainloop *mainloop;
  pa_context *context;
};

typedef enum {
  GST_PULSE_DEVICE_TYPE_SOURCE,
  GST_PULSE_DEVICE_TYPE_SINK
} GstPulseDeviceType;

struct _GstPulseDeviceMonitorClass {
  GstDeviceMonitorClass    parent_class;
};

GType        gst_pulse_device_monitor_get_type (void);


typedef struct _GstPulseDevice GstPulseDevice;
typedef struct _GstPulseDevicePrivate GstPulseDevicePrivate;
typedef struct _GstPulseDeviceClass GstPulseDeviceClass;

#define GST_TYPE_PULSE_DEVICE                 (gst_pulse_device_get_type())
#define GST_IS_PULSE_DEVICE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PULSE_DEVICE))
#define GST_IS_PULSE_DEVICE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PULSE_DEVICE))
#define GST_PULSE_DEVICE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PULSE_DEVICE, GstPulseDeviceClass))
#define GST_PULSE_DEVICE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PULSE_DEVICE, GstPulseDevice))
#define GST_PULSE_DEVICE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE, GstPulseDeviceClass))
#define GST_PULSE_DEVICE_CAST(obj)            ((GstPulseDevice *)(obj))

struct _GstPulseDevice {
  GstDevice         parent;

  GstPulseDeviceType type;
  guint             device_index;
  gchar            *internal_name;
  const gchar      *element;
};

struct _GstPulseDeviceClass {
  GstDeviceClass    parent_class;
};

GType        gst_pulse_device_get_type (void);

#endif /* __GST_PULSE_DEVICE_MONITOR_H__ */
