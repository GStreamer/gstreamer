/* GStreamer Raspberry Pi Camera Source Device Provider
 * Copyright (C) 2014 Tim-Philipp Müller <tim@centricular.com>
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

#ifndef __GST_RPICAMSRC_DEVICE_PROVIDER_H__
#define __GST_RPICAMSRC_DEVICE_PROVIDER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstRpiCamSrcDeviceProvider GstRpiCamSrcDeviceProvider;
typedef struct _GstRpiCamSrcDeviceProviderClass GstRpiCamSrcDeviceProviderClass;

#define GST_TYPE_RPICAMSRC_DEVICE_PROVIDER                 (gst_rpi_cam_src_device_provider_get_type())
#define GST_IS_RPICAMSRC_DEVICE_PROVIDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RPICAMSRC_DEVICE_PROVIDER))
#define GST_IS_RPICAMSRC_DEVICE_PROVIDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RPICAMSRC_DEVICE_PROVIDER))
#define GST_RPICAMSRC_DEVICE_PROVIDER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RPICAMSRC_DEVICE_PROVIDER, GstRpiCamSrcDeviceProviderClass))
#define GST_RPICAMSRC_DEVICE_PROVIDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RPICAMSRC_DEVICE_PROVIDER, GstRpiCamSrcDeviceProvider))
#define GST_RPICAMSRC_DEVICE_PROVIDER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE_PROVIDER, GstRpiCamSrcDeviceProviderClass))
#define GST_RPICAMSRC_DEVICE_PROVIDER_CAST(obj)            ((GstRpiCamSrcDeviceProvider *)(obj))

struct _GstRpiCamSrcDeviceProvider {
  GstDeviceProvider  parent;
};

struct _GstRpiCamSrcDeviceProviderClass {
  GstDeviceProviderClass  parent_class;
};

GType  gst_rpi_cam_src_device_provider_get_type (void);

typedef struct _GstRpiCamSrcDevice GstRpiCamSrcDevice;
typedef struct _GstRpiCamSrcDeviceClass GstRpiCamSrcDeviceClass;

#define GST_TYPE_RPICAMSRC_DEVICE                 (gst_rpi_cam_src_device_get_type())
#define GST_IS_RPICAMSRC_DEVICE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RPICAMSRC_DEVICE))
#define GST_IS_RPICAMSRC_DEVICE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RPICAMSRC_DEVICE))
#define GST_RPICAMSRC_DEVICE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RPICAMSRC_DEVICE, GstRpiCamSrcDeviceClass))
#define GST_RPICAMSRC_DEVICE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RPICAMSRC_DEVICE, GstRpiCamSrcDevice))
#define GST_RPICAMSRC_DEVICE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE, GstRpiCamSrcDeviceClass))
#define GST_RPICAMSRC_DEVICE_CAST(obj)            ((GstRpiCamSrcDevice *)(obj))

struct _GstRpiCamSrcDevice {
  GstDevice  parent;
};

struct _GstRpiCamSrcDeviceClass {
  GstDeviceClass  parent_class;
};

GType  gst_rpi_cam_src_device_get_type (void);

G_END_DECLS

#endif /* __GST_RPICAMSRC_DEVICE_PROVIDER_H__ */
