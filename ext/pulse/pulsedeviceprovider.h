/* GStreamer
 * Copyright (C) 2012 Olivier Crete <olivier.crete@collabora.com>
 *
 * pulsedeviceprovider.h: Device probing and monitoring
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


#ifndef __GST_PULSE_DEVICE_PROVIDER_H__
#define __GST_PULSE_DEVICE_PROVIDER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pulse/pulseaudio.h>
#include <pulse/thread-mainloop.h>

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_PULSE_DEVICE_PROVIDER (gst_pulse_device_provider_get_type())
G_DECLARE_FINAL_TYPE (GstPulseDeviceProvider, gst_pulse_device_provider,
    GST, PULSE_DEVICE_PROVIDER, GstDeviceProvider)
#define GST_PULSE_DEVICE_PROVIDER_CAST(obj) ((GstPulseDeviceProvider *)(obj))

struct _GstPulseDeviceProvider {
  GstDeviceProvider         parent;

  gchar *server;
  gchar *client_name;
  gchar *default_source_name;
  gchar *default_sink_name;

  pa_threaded_mainloop *mainloop;
  pa_context *context;
};

typedef enum {
  GST_PULSE_DEVICE_TYPE_SOURCE,
  GST_PULSE_DEVICE_TYPE_SINK
} GstPulseDeviceType;


#define GST_TYPE_PULSE_DEVICE (gst_pulse_device_get_type())
G_DECLARE_FINAL_TYPE (GstPulseDevice, gst_pulse_device, GST, PULSE_DEVICE,
    GstDevice)
#define GST_PULSE_DEVICE_CAST(obj) ((GstPulseDevice *)(obj))

struct _GstPulseDevice {
  GstDevice         parent;

  GstPulseDeviceType type;
  guint             device_index;
  gchar            *internal_name;
  gboolean         is_default;
  const gchar      *element;
};

G_END_DECLS

#endif /* __GST_PULSE_DEVICE_PROVIDER_H__ */
